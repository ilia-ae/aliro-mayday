#pragma once
#include "ddk/Span.h"
#include "../../src/aliro/document_cbor/document_cbor.h"
#include <cbor.h>
#include <mbedtls/md.h>
#include <array>
#include <optional>
#include <string>
#include <vector>
namespace ddk::aliro::cbor_tools {
using Value = CborValue;
inline bool advance(Value& v) {
    return cbor_value_skip_tag(&v)==CborNoError && cbor_value_advance(&v)==CborNoError;
}
inline bool valid(span<const uint8_t> bytes) {
    return aliro_document_cbor_valid(bytes.data(), bytes.size());
}
inline bool parse(span<const uint8_t> bytes, CborParser& parser, Value& value) {
    return valid(bytes) && cbor_parser_init(bytes.data(), bytes.size(), 0, &parser, &value) == CborNoError;
}
inline std::optional<std::string> text(Value v) {
    if (!cbor_value_is_text_string(&v)) return {};
    size_t n = 0;
    if (cbor_value_get_string_length(&v, &n) != CborNoError || n > 256) return {};
    std::string s(n, '\0');
    if (cbor_value_copy_text_string(&v, s.data(), &n, nullptr) != CborNoError) return {};
    return s;
}
inline std::optional<std::vector<uint8_t>> bytes(Value v) {
    if (!cbor_value_is_byte_string(&v)) return {};
    size_t n = 0;
    if (cbor_value_get_string_length(&v, &n) != CborNoError || n > 4096) return {};
    std::vector<uint8_t> b(n);
    if (cbor_value_copy_byte_string(&v, b.data(), &n, nullptr) != CborNoError) return {};
    return b;
}
inline std::optional<uint64_t> number(Value v) {
    uint64_t n;
    if (!cbor_value_is_unsigned_integer(&v) || cbor_value_get_uint64(&v, &n) != CborNoError) return {};
    return n;
}
// Accept legacy integer aliases, but never two spellings of the same field.
inline std::optional<Value> field(Value map, const std::string& key) {
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
inline bool is_text_field(Value v, const char* key, const char* expected) {
    auto f = field(v, key);
    return f && text(*f) == std::optional<std::string>(expected);
}
inline bool sha256(span<const uint8_t> data, std::array<uint8_t,32>& out) {
    return mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), data.data(), data.size(), out.data()) == 0;
}
inline std::optional<std::vector<uint8_t>> tagged(Value v) {
    CborTag tag;
    if (!cbor_value_is_tag(&v) || cbor_value_get_tag(&v, &tag) != CborNoError || tag != 24 ||
        cbor_value_advance(&v) != CborNoError) return {};
    return bytes(v);
}
// Supported subset of tdate: RFC 3339 UTC, second precision. Reject malformed
// calendar dates even when the issuer permits operation without a clock.
inline std::optional<int64_t> date(Value v) {
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

}
