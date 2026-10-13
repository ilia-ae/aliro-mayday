#include "ddk/aliro/DocumentVerifier.h"
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
using Value = CborValue;
bool advance(Value& v) {
    return cbor_value_skip_tag(&v)==CborNoError && cbor_value_advance(&v)==CborNoError;
}
bool valid(span<const uint8_t> bytes) {
    return aliro_document_cbor_valid(bytes.data(), bytes.size());
}
bool parse(span<const uint8_t> bytes, CborParser& parser, Value& value) {
    return valid(bytes) && cbor_parser_init(bytes.data(), bytes.size(), 0, &parser, &value) == CborNoError;
}
std::optional<std::string> text(Value v) {
    if (!cbor_value_is_text_string(&v)) return {};
    size_t n = 0;
    if (cbor_value_get_string_length(&v, &n) != CborNoError || n > 256) return {};
    std::string s(n, '\0');
    if (cbor_value_copy_text_string(&v, s.data(), &n, nullptr) != CborNoError) return {};
    return s;
}
std::optional<std::vector<uint8_t>> bytes(Value v) {
    if (!cbor_value_is_byte_string(&v)) return {};
    size_t n = 0;
    if (cbor_value_get_string_length(&v, &n) != CborNoError || n > 4096) return {};
    std::vector<uint8_t> b(n);
    if (cbor_value_copy_byte_string(&v, b.data(), &n, nullptr) != CborNoError) return {};
    return b;
}
std::optional<uint64_t> number(Value v) {
    uint64_t n;
    if (!cbor_value_is_unsigned_integer(&v) || cbor_value_get_uint64(&v, &n) != CborNoError) return {};
    return n;
}
// Accept legacy integer aliases, but never two spellings of the same field.
std::optional<Value> field(Value map, const std::string& key) {
    if (!cbor_value_is_map(&map)) return {};
    Value it;
    if (cbor_value_enter_container(&map, &it) != CborNoError) return {};
    std::optional<Value> found;
    while (!cbor_value_at_end(&it)) {
        bool match = text(it) == std::optional<std::string>(key);
        if (cbor_value_is_unsigned_integer(&it)) {
            auto n=number(it);
            if (!n) return {};
            match=std::to_string(*n)==key;
        } else if (cbor_value_is_integer(&it)) {
            int64_t n;
            if (cbor_value_get_int64(&it, &n) != CborNoError) return {};
            match = std::to_string(n) == key;
        }
        if (!advance(it)) return {};
        if (match) {
            if (found) return {}; // ambiguous alias
            found = it;
        }
        if (!advance(it)) return {};
    }
    return found;
}
bool is_text_field(Value v, const char* key, const char* expected) {
    auto f = field(v, key);
    return f && text(*f) == std::optional<std::string>(expected);
}
bool sha256(span<const uint8_t> data, std::array<uint8_t,32>& out) {
    return mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), data.data(), data.size(), out.data()) == 0;
}
std::optional<std::vector<uint8_t>> tagged(Value v) {
    CborTag tag;
    if (!cbor_value_is_tag(&v) || cbor_value_get_tag(&v, &tag) != CborNoError || tag != 24 ||
        cbor_value_advance(&v) != CborNoError) return {};
    return bytes(v);
}
// Supported subset of tdate: RFC 3339 UTC, second precision. Reject malformed
// calendar dates even when the issuer permits operation without a clock.
std::optional<int64_t> date(Value v) {
    CborTag tag;
    if (!cbor_value_is_tag(&v) || cbor_value_get_tag(&v, &tag) != CborNoError || tag != 0 ||
        cbor_value_advance(&v) != CborNoError) return {};
    auto s = text(v);
    if (!s || s->size() != 20 || (*s)[4]!='-' || (*s)[7]!='-' || (*s)[10]!='T' ||
        (*s)[13]!=':' || (*s)[16]!=':' || (*s)[19]!='Z') return {};
    auto digits = [&](size_t pos, size_t count)->int {
        int n=0;
        for (size_t i=pos;i<pos+count;i++) {
            if ((*s)[i]<'0' || (*s)[i]>'9') return -1;
            n=n*10+(*s)[i]-'0';
        }
        return n;
    };
    int y=digits(0,4), m=digits(5,2), d=digits(8,2), h=digits(11,2), min=digits(14,2), sec=digits(17,2);
    const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    bool leap = y%4==0 && (y%100!=0 || y%400==0);
    if (y<1 || m<1 || m>12 || d<1 || d>days[m-1]+(m==2 && leap) ||
        h<0 || h>23 || min<0 || min>59 || sec<0 || sec>59) return {};
    int64_t year=y-(m<=2), era=year/400, yoe=year-era*400;
    int64_t doy=(153*(m+(m>2?-3:9))+2)/5+d-1;
    int64_t day=era*146097+yoe*365+yoe/4-yoe/100+doy-719468;
    return day*86400+h*3600+min*60+sec;
}
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

std::optional<VerifiedDocument> verify_access_document(
    span<const uint8_t> document, span<const uint8_t> trusted_key,
    const DocumentPolicy& policy, const std::vector<std::string>& requested) {
    CborParser parser; Value doc;
    if (!parse(document,parser,doc) || !is_text_field(doc,"5","aliro-a")) return {};
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
        !is_text_field(mso,"2","SHA-256") || !is_text_field(mso,"5","aliro-a")) return {};
    auto key=device_key(mso);
    auto validity=field(mso,"6"), tv=field(mso,"7"), digests=field(mso,"3");
    if (!key || !validity || !tv || !digests || !cbor_value_is_boolean(&*tv) ||
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
    VerifiedDocument out; out.device_public_key=*key;
    if (auto iv=field(*validity,"5")) {
        auto iteration=number(*iv);
        if (!iteration || (*iteration<policy.access_iteration && policy.access_iteration-*iteration>=8)) return {};
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
            if (*name=="aliro-a" && std::find(requested.begin(),requested.end(),*element)!=requested.end())
                out.disclosed_elements.push_back(*element);
            item=next;
        }
        if (!advance(ns)) return {};
    }
    if (out.disclosed_elements.empty()) return {};
    return out;
}
} // namespace ddk::aliro
