#pragma once
#include "aliro_test_endpoint.hpp"
#include "ddk/aliro/DocumentAccess.h"
#include <cbor.h>
#include <algorithm>
#include <stdexcept>

namespace document_test {
using Bytes=std::vector<uint8_t>;
template<class F> Bytes scalar(F encode) {
    uint8_t buffer[4096]; CborEncoder e; cbor_encoder_init(&e,buffer,sizeof(buffer),0);
    if (encode(e)!=CborNoError) throw std::runtime_error("fixture encoding overflow");
    return {buffer,buffer+cbor_encoder_get_buffer_size(&e,buffer)};
}
inline Bytes integer(uint64_t n) { return scalar([&](auto& e){return cbor_encode_uint(&e,n);}); }
inline Bytes bstr(const Bytes& b) { return scalar([&](auto& e){return cbor_encode_byte_string(&e,b.data(),b.size());}); }
inline Bytes array(const std::vector<Bytes>& items) {
    uint8_t buffer[16]; CborEncoder e,a; cbor_encoder_init(&e,buffer,sizeof(buffer),0);
    cbor_encoder_create_array(&e,&a,items.size());
    Bytes out(buffer,buffer+cbor_encoder_get_buffer_size(&a,buffer));
    for (const auto& item:items) out.insert(out.end(),item.begin(),item.end());
    return out;
}
inline Bytes map(std::vector<std::pair<uint64_t,Bytes>> items) {
    std::sort(items.begin(),items.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    uint8_t buffer[16]; CborEncoder e,m; cbor_encoder_init(&e,buffer,sizeof(buffer),0);
    cbor_encoder_create_map(&e,&m,items.size());
    Bytes out(buffer,buffer+cbor_encoder_get_buffer_size(&m,buffer));
    for (const auto& item:items) {
        auto key=integer(item.first); out.insert(out.end(),key.begin(),key.end());
        out.insert(out.end(),item.second.begin(),item.second.end());
    }
    return out;
}
inline Bytes access_rule(uint16_t capability) { return map({{0,integer(capability)}}); }
inline Bytes access(const Bytes& id={},std::optional<Bytes> rules={}) {
    std::vector<std::pair<uint64_t,Bytes>> items{{0,integer(1)}};
    if (!id.empty()) items.push_back({1,bstr(id)});
    if (rules) items.push_back({2,*rules});
    return map(items);
}
inline Bytes entry(const ddk::aliro::RevocationEntry& e) {
    std::vector<std::pair<uint64_t,Bytes>> items;
    if (!e.public_key_hash.empty()) items.push_back({0,bstr(e.public_key_hash)});
    if (!e.id.empty()) items.push_back({1,bstr(e.id)});
    if (e.expiry) items.push_back({2,integer(*e.expiry)});
    return map(items);
}
inline Bytes revocation(uint64_t mode,const std::vector<ddk::aliro::RevocationEntry>& add,
                        std::optional<std::vector<ddk::aliro::RevocationEntry>> remove={}) {
    std::vector<Bytes> added,removed;
    for (const auto& e:add) added.push_back(entry(e));
    std::vector<std::pair<uint64_t,Bytes>> fields{{0,integer(1)},{1,integer(mode)}};
    if (!added.empty() || mode==0 || !remove) fields.push_back({2,array(added)});
    if (remove) {
        for (const auto& e:*remove) removed.push_back(entry(e));
        fields.push_back({3,array(removed)});
    }
    return map(fields);
}
inline Bytes document(const aliro_test::P256KeyPair& issuer,const aliro_test::P256KeyPair& device,
                      aliro_test::DocumentFixtureSpec spec={}) {
    std::array<uint8_t,32> y; std::copy_n(device.pub.begin()+33,32,y.begin());
    auto m=aliro_test::make_step_up_material(device.pub_x,y,
        ddk::aliro::issuer_key_identifier(issuer.pub),true,false,spec);
    m.sig=aliro_test::sign_cose_es256(issuer,m.protected_headers,m.payload);
    Bytes doc; aliro_test::build_step_up_device_response(m,true,&doc); return doc;
}
inline Bytes response(const std::vector<Bytes>& docs) {
    Bytes out{0xA3,0x61,'1',0x63,'1','.','0',0x61,'2'};
    auto all=array(docs); out.insert(out.end(),all.begin(),all.end());
    out.insert(out.end(),{0x61,'3',0}); return out;
}
struct Fixture {
    aliro_test::P256KeyPair issuer=aliro_test::generate_p256_key(), device=aliro_test::generate_p256_key();
    std::optional<ddk::aliro::VerifiedDocument> access_doc(const Bytes& value) {
        aliro_test::DocumentFixtureSpec s; s.element_value=value;
        return ddk::aliro::verify_access_document(document(issuer,device,s),issuer.pub,{}, {"matter1"});
    }
    std::optional<ddk::aliro::VerifiedDocument> revoke_doc(const Bytes& value,uint64_t iteration) {
        aliro_test::DocumentFixtureSpec s; s.element_value=value; s.doc_type="aliro-r";
        s.include_device_key=false; s.iteration=iteration;
        return ddk::aliro::verify_revocation_document(document(issuer,device,s),issuer.pub,{});
    }
};
} // namespace document_test
