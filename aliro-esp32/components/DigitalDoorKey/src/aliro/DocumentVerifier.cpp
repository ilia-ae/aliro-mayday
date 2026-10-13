#include "ddk/aliro/DocumentVerifier.h"
#include "aliro/DocumentCbor.h"
#include "CoseSign1.h"
#include "CommonCryptoUtils.h"
#include "document_cbor/document_cbor.h"
#include <cbor.h>
#include <mbedtls/md.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace ddk::aliro {
namespace {
using namespace cbor_tools;
std::optional<std::vector<uint8_t>> device_key(Value mso) {
    auto info=field(mso,"4");
    if (!info || !cbor_value_is_map(&*info) || field(*info,"keyAuthorizations")) return {};
    auto key=field(*info,"1");
    if (!key || !cbor_value_is_map(&*key)) return {};
    auto kty=field(*key,"1"), crv=field(*key,"-1"), xv=field(*key,"-2"), yv=field(*key,"-3");
    if (!kty || !crv || !xv || !yv || number(*kty)!=2 || number(*crv)!=1) return {};
    auto x=bytes(*xv), y=bytes(*yv);
    if (!x || !y || x->size()!=32 || y->size()!=32) return {};
    std::vector<uint8_t> out{4};
    out.insert(out.end(),x->begin(),x->end()); out.insert(out.end(),y->begin(),y->end());
    CommonCryptoUtils::EcpGroupGuard group;
    CommonCryptoUtils::EcpPointGuard point;
    if (mbedtls_ecp_group_load(group,MBEDTLS_ECP_DP_SECP256R1)!=0 ||
        mbedtls_ecp_point_read_binary(group,point,out.data(),out.size())!=0 ||
        mbedtls_ecp_check_pubkey(group,point)!=0) return {};
    return out;
}
} // namespace

std::vector<uint8_t> issuer_key_identifier(span<const uint8_t> public_key) {
    if (public_key.size()!=65 || public_key[0]!=4) return {};
    constexpr char prefix[]="key-identifier";
    std::vector<uint8_t> input(prefix,prefix+sizeof(prefix)-1);
    input.insert(input.end(),public_key.begin(),public_key.end());
    std::array<uint8_t,32> hash;
    if (!sha256(input,hash)) return {};
    return {hash.begin(),hash.begin()+8};
}

static std::optional<VerifiedDocument> verify_document(
    span<const uint8_t> document, span<const uint8_t> trusted_key,
    const DocumentPolicy& policy, const std::vector<std::string>& requested, bool revocation) {
    const char* doc_type=revocation ? "aliro-r" : "aliro-a";
    CborParser parser; Value doc;
    if (!parse(document,parser,doc) || !is_text_field(doc,"5",doc_type)) return {};
    auto signed_part=field(doc,"1");
    if (!signed_part) return {};
    auto auth=field(*signed_part,"2"), namespaces=field(*signed_part,"1");
    if (!auth || !namespaces || !cbor_value_is_array(&*auth) || !cbor_value_is_map(&*namespaces)) return {};
    size_t count=0;
    if (cbor_value_get_array_length(&*auth,&count)!=CborNoError || count!=4) return {};
    auto cose=CoseSign1::parse_from_iterator(&*auth);
    if (!cose || !valid(cose->protected_headers_encoded) || cose->signature.size()!=64) return {};
    // This reader supports provisioned issuer keys. An unvalidated certificate
    // must never introduce a new issuer or take precedence over that trust store.
    Value elem;
    if (cbor_value_enter_container(&*auth,&elem)!=CborNoError || cbor_value_advance(&elem)!=CborNoError ||
        field(elem,"33") || field(elem,"1") || field(elem,"2")) return {};
    CborParser ph_parser; Value ph;
    if (!parse(cose->protected_headers_encoded,ph_parser,ph) || field(ph,"2") || field(ph,"33")) return {};
    auto kid=field(ph,"4");
    std::optional<std::vector<uint8_t>> kid_bytes=kid?bytes(*kid):cose->issuer_id;
    if (kid && cose->issuer_id) return {};
    if (!kid_bytes || *kid_bytes!=issuer_key_identifier(trusted_key) ||
        !CoseSign1::verify(*cose,CoseAlgorithm::ES256,trusted_key)) return {};
    CborParser payload_parser; Value payload;
    if (!parse(cose->payload,payload_parser,payload)) return {};
    auto mso_bytes=tagged(payload);
    CborParser mso_parser; Value mso;
    if (!mso_bytes || !parse(*mso_bytes,mso_parser,mso) || !is_text_field(mso,"1","1.0") ||
        !is_text_field(mso,"2","SHA-256") || !is_text_field(mso,"5",doc_type)) return {};
    std::optional<std::vector<uint8_t>> key;
    if (revocation) {
        if (field(mso,"4")) return {}; // revocation is independent of the presenting device
    } else { key=device_key(mso); if (!key) return {}; }
    auto validity=field(mso,"6"), tv=field(mso,"7"), digests=field(mso,"3");
    if (!validity || !tv || !digests || !cbor_value_is_boolean(&*tv) ||
        !cbor_value_is_map(&*digests)) return {};
    bool time_required=false;
    if (cbor_value_get_boolean(&*tv,&time_required)!=CborNoError) return {};
    auto sv=field(*validity,"1"), fromv=field(*validity,"2"), untilv=field(*validity,"3");
    if (!sv || !fromv || !untilv) return {};
    auto signed_time=date(*sv), from=date(*fromv), until=date(*untilv);
    if (!signed_time || !from || !until || *from>*until) return {};
    if (policy.current_time) {
        if (*policy.current_time<*from || *policy.current_time>*until) return {};
    } else if (time_required || !policy.allow_time_optional_without_clock) return {};
    VerifiedDocument out; if (key) out.device_public_key=*key;
    out.time_verification_required=time_required;
    out.revocation=revocation;
    if (auto iv=field(*validity,"5")) {
        auto iteration=number(*iv);
        if (!iteration || (revocation ? *iteration < policy.revocation_iteration :
            (*iteration<policy.access_iteration && policy.access_iteration-*iteration>=8))) return {};
        out.iteration=*iteration;
    }
    Value ns;
    if (cbor_value_enter_container(&*namespaces,&ns)!=CborNoError) return {};
    size_t item_count=0;
    while (!cbor_value_at_end(&ns)) {
        auto name=text(ns);
        if (!name || !advance(ns) || !cbor_value_is_array(&ns)) return {};
        auto expected_ns=field(*digests,*name);
        if (!expected_ns || !cbor_value_is_map(&*expected_ns)) return {};
        Value item;
        if (cbor_value_enter_container(&ns,&item)!=CborNoError) return {};
        std::vector<uint64_t> seen;
        while (!cbor_value_at_end(&item)) {
            if (++item_count>16) return {};
            const uint8_t* start=item.source.ptr;
            auto content=tagged(item);
            Value next=item;
            if (!content || !advance(next)) return {};
            const uint8_t* end=next.source.ptr;
            CborParser ip; Value map;
            if (!parse(*content,ip,map)) return {};
            auto idv=field(map,"1"), randomv=field(map,"2"), namev=field(map,"3"), value=field(map,"4");
            if (!idv || !randomv || !namev || !value || !bytes(*randomv)) return {};
            auto id=number(*idv);
            auto element=text(*namev);
            if (!id || !element || std::find(seen.begin(),seen.end(),*id)!=seen.end()) return {};
            seen.push_back(*id);
            auto expected=field(*expected_ns,std::to_string(*id));
            auto hash=expected?bytes(*expected):std::optional<std::vector<uint8_t>>{};
            std::array<uint8_t,32> actual;
            if (!hash || hash->size()!=32 || !sha256({start,static_cast<size_t>(end-start)},actual) ||
                !CommonCryptoUtils::constant_time_compare(hash->data(),actual.data(),actual.size())) return {};
            if (*name==doc_type && (revocation ||
                    std::find(requested.begin(),requested.end(),*element)!=requested.end())) {
                out.disclosed_elements.push_back(*element);
                Value value_end=*value;
                if (!advance(value_end)) return {};
                out.elements.push_back({*element,std::vector<uint8_t>(value->source.ptr,value_end.source.ptr)});
            }
            item=next;
        }
        if (!advance(ns)) return {};
    }
    if (out.disclosed_elements.empty()) return {};
    return out;
}
std::optional<VerifiedDocument> verify_access_document(span<const uint8_t> doc,
    span<const uint8_t> issuer,const DocumentPolicy& policy,const std::vector<std::string>& requested) {
    return verify_document(doc,issuer,policy,requested,false);
}
std::optional<VerifiedDocument> verify_revocation_document(span<const uint8_t> doc,
    span<const uint8_t> issuer,const DocumentPolicy& policy) {
    return verify_document(doc,issuer,policy,{},true);
}
std::vector<uint8_t> credential_key_hash(span<const uint8_t> key) {
    if (key.size()!=65 || key[0]!=4) return {};
    std::array<uint8_t,32> hash;
    if (!cbor_tools::sha256(key,hash)) return {};
    return {hash.begin(),hash.end()};
}
} // namespace ddk::aliro
