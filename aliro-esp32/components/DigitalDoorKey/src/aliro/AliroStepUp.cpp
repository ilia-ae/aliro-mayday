#include "aliro/AliroStepUp.h"
#include "aliro/AliroSecureContext.h"
#include "CoseSign1.h"
#include "ddk/aliro/DocumentVerifier.h"
#include "document_cbor/document_cbor.h"
#include "BerTlv.h"
#include "CommonCryptoUtils.h"
#include "DDKLogging.h"
#include "ddk/store/CredentialStore.h"
#include <cbor.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include "ddk/store/Issuer.h"

namespace {
constexpr const char* TAG = "AliroStepUp";
constexpr const char* kAccessDocType = "aliro-a";
constexpr const char* kRevocationDocType = "aliro-r";
constexpr uint8_t kStepUpAid[] = {0xA0,0x00,0x00,0x09,0x09,0xAC,0xCE,0x55,0x02};

bool text_string_equals(const CborValue& v, const char* s)
{
    if (!cbor_value_is_text_string(&v)) return false;
    size_t len = 0;
    if (cbor_value_get_string_length(&v, &len) != CborNoError) return false;
    if (len != std::strlen(s)) return false;
    char buf[32];
    if (len >= sizeof(buf)) return false;
    size_t buf_len = sizeof(buf);
    if (cbor_value_copy_text_string(&v, buf, &buf_len, nullptr) != CborNoError) return false;
    return std::memcmp(buf, s, len) == 0;
}

bool text_value(const CborValue& v, std::string& out)
{
    if (!cbor_value_is_text_string(&v)) return false;
    size_t len = 0;
    if (cbor_value_get_string_length(&v, &len) != CborNoError) return false;
    out.resize(len);
    if (len > 0 &&
        cbor_value_copy_text_string(&v, out.data(), &len, nullptr) != CborNoError)
        return false;
    return true;
}

bool find_map_field(const CborValue* map, int64_t key, CborValue* out)
{
    if (!cbor_value_is_map(map)) return false;
    char text_key[24];
    std::snprintf(text_key, sizeof(text_key), "%lld", static_cast<long long>(key));

    CborValue it;
    if (cbor_value_enter_container(map, &it) != CborNoError) return false;
    while (!cbor_value_at_end(&it)) {
        bool match = false;
        if (cbor_value_is_integer(&it)) {
            int64_t k = 0;
            match = cbor_value_get_int64(&it, &k) == CborNoError && k == key;
        } else if (cbor_value_is_text_string(&it)) {
            match = text_string_equals(it, text_key);
        }
        if (cbor_value_advance(&it) != CborNoError) return false;  // now at value
        CborValue value = it;
        if (cbor_value_skip_tag(&it) != CborNoError ||
            cbor_value_advance(&it) != CborNoError) return false;  // next key / end
        if (match) { *out = value; return true; }
    }
    return false;
}
} // namespace

AliroStepUp::AliroStepUp(ddk::Session& session, AliroSecureContext& ctx,
                         size_t max_command_data_size)
    : session_(session), ctx_(ctx), max_command_data_size_(max_command_data_size) {}

void AliroStepUp::selectStepUpAid()
{
    // SELECT: CLA=0x00, INS=0xA4, P1=0x04, P2=0x00, AID
    std::vector<uint8_t> apdu{0x00, 0xA4, 0x04, 0x00,
                               static_cast<uint8_t>(sizeof(kStepUpAid))};
    apdu.insert(apdu.end(), std::begin(kStepUpAid), std::end(kStepUpAid));
    apdu.push_back(0x00);  // Le
    auto resp = session_.apdu().transceive(apdu);
    if(!resp.ok())  LOG(D, "Step-up AID SELECT failed");
}

std::vector<uint8_t> AliroStepUp::buildDeviceRequest(
    const std::vector<std::string>& doc_types,
    const std::map<std::string, bool>& scopes)
{
    // {"1": version, "2": [ {"1": tag24( {"1": {docType: scopes}, "5": docType} )} ]}
    uint8_t buf[1024];
    CborEncoder root, rootMap, docArray;

    cbor_encoder_init(&root, buf, sizeof(buf), 0);
    cbor_encoder_create_map(&root, &rootMap, 2);
    cbor_encode_text_stringz(&rootMap, "1");          // version
    cbor_encode_text_stringz(&rootMap, "1.0");
    cbor_encode_text_stringz(&rootMap, "2");          // docRequests
    cbor_encoder_create_array(&rootMap, &docArray, doc_types.size());

    for (const auto& doc_type : doc_types) {
        // Inner ItemsRequest: {"1": {docType: scopes}, "5": docType}
        // Nested containers must be closed before encoding the next imap
        // pair — tinycbor encoders share one buffer, so interleaving
        // overwrites bytes.
        uint8_t inner[512];
        CborEncoder ienc, imap, nsMap, scopeMap;
        cbor_encoder_init(&ienc, inner, sizeof(inner), 0);
        cbor_encoder_create_map(&ienc, &imap, 2);
        cbor_encode_text_stringz(&imap, "1");         // nameSpaces
        cbor_encoder_create_map(&imap, &nsMap, 1);
        cbor_encode_text_string(&nsMap, doc_type.c_str(), doc_type.size());
        cbor_encoder_create_map(&nsMap, &scopeMap, scopes.size());
        for (const auto& [element, retain] : scopes) {
            cbor_encode_text_string(&scopeMap, element.c_str(), element.size());
            cbor_encode_boolean(&scopeMap, retain);
        }
        cbor_encoder_close_container(&nsMap, &scopeMap);
        cbor_encoder_close_container(&imap, &nsMap);
        cbor_encode_text_stringz(&imap, "5");         // docType
        cbor_encode_text_string(&imap, doc_type.c_str(), doc_type.size());
        cbor_encoder_close_container(&ienc, &imap);
        size_t inner_len = cbor_encoder_get_buffer_size(&ienc, inner);

        // DocRequest: {"1": tag24(inner)}
        CborEncoder docReq;
        cbor_encoder_create_map(&docArray, &docReq, 1);
        cbor_encode_text_stringz(&docReq, "1");       // itemsRequest
        cbor_encode_tag(&docReq, CborEncodedCborTag);
        cbor_encode_byte_string(&docReq, inner, inner_len);
        cbor_encoder_close_container(&docArray, &docReq);
    }
    cbor_encoder_close_container(&rootMap, &docArray);
    if (cbor_encoder_close_container(&root, &rootMap) != CborNoError) return {};

    size_t len = cbor_encoder_get_buffer_size(&root, buf);
    return std::vector<uint8_t>(buf, buf + len);
}

AliroStepUpResult AliroStepUp::parseDeviceResponse(ddk::span<const uint8_t> cbor,
                                                  const std::map<std::string,bool>& scopes)
{
    AliroStepUpResult result;
    CborParser parser;
    CborValue root;

    if (!aliro_document_cbor_valid(cbor.data(), cbor.size()) ||
        cbor_parser_init(cbor.data(), cbor.size(), 0, &parser, &root)
        != CborNoError || !cbor_value_is_map(&root))
        return result;

    CborValue version;
    if (!find_map_field(&root, 1, &version) || !text_string_equals(version, "1.0")) return result;
    // DeviceResponse: {1: version, 2: [documents], 3: status}
    CborValue status;
    int64_t st = -1;
    if (!find_map_field(&root, 3, &status) || !cbor_value_is_integer(&status) ||
        cbor_value_get_int64(&status, &st) != CborNoError || st != 0)
        return result;

    CborValue documents;
    if (!find_map_field(&root, 2, &documents) || !cbor_value_is_array(&documents)) {
        LOG(E, "DeviceResponse has no documents array (key 2)");
        return result;
    }

    CborValue doc;
    if (cbor_value_enter_container(&documents, &doc) != CborNoError)
        return result;

    while (!cbor_value_at_end(&doc)) {
        if (!cbor_value_is_map(&doc)) break;

        // Document: {1: issuerSigned, 5: docType}
        const uint8_t* doc_begin = doc.source.ptr;
        CborValue doc_item = doc;
        cbor_value_advance(&doc);
        const uint8_t* doc_end = doc.source.ptr;
        if (doc_end < doc_begin || doc_end > cbor.data() + cbor.size()) break;
        std::vector<uint8_t> document(doc_begin, doc_end);

        std::string doc_type;
        CborValue v;
        // A signed revocation document is never an enrollment grant.
        if (!find_map_field(&doc_item, 5, &v) || !text_value(v, doc_type) ||
            doc_type != kAccessDocType) continue;

        std::vector<std::string> requested;
        for (const auto& scope : scopes) requested.push_back(scope.first);
        for (auto& issuer : session_.store().issuers()) {
            ddk::aliro::DocumentPolicy policy;
            policy.current_time = session_.config().step_up_current_time;
            policy.access_iteration = issuer.access_iteration;
            auto verified = ddk::aliro::verify_access_document(document, issuer.public_key,
                                                              policy, requested);
            if (!verified) continue;
            if (!result.success) {
                result.access_document_cbor = document;
                result.documents.push_back(document);
                result.issuer = &issuer;
                result.endpoint_public_key = std::move(verified->device_public_key);
                result.access_iteration = verified->iteration;
                result.success = true;
                break;
            }
        }
    }
    return result;
}

AliroStepUpResult AliroStepUp::run(
    ddk::aliro::SignalingBitmask signaling_bitmap,
    const std::map<std::string, bool>& scopes)
{
    std::vector<std::string> doc_types;
    if (has(signaling_bitmap, ddk::aliro::SignalingBitmask::AccessDocumentRetrievable))
        doc_types.push_back(kAccessDocType);
    if (has(signaling_bitmap, ddk::aliro::SignalingBitmask::RevocationDocumentRetrievable))
        doc_types.push_back(kRevocationDocType);
    if (doc_types.empty()) {
        LOG(W, "Step-up: signaling bitmap indicates no retrievable documents");
        return {};
    }
    if (scopes.empty() || scopes.size() > 8 ||
        std::any_of(scopes.begin(), scopes.end(), [](const auto& entry) {
            return entry.first.empty() || entry.first.size() > 32;
        })) {
        LOG(E, "Step-up: empty scope map");
        return {};
    }

    if (has(signaling_bitmap, ddk::aliro::SignalingBitmask::StepUpSelectRequired))
        selectStepUpAid();

    auto request = buildDeviceRequest(doc_types, scopes);
    if (request.empty()) return {};

    // Encrypt + SessionData wrap + 0x53 TLV + ENVELOPE + decrypt —
    auto plaintext = ctx_.envelope(session_, request, max_command_data_size_);
    if (!plaintext || plaintext->empty()) {
        LOG(E, "Step-up: ENVELOPE response decrypt failed");
        return {};
    }

    LOG_HEX(V, "StepUp DeviceResponse", *plaintext);
    return parseDeviceResponse(*plaintext, scopes);
}
