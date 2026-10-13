#include "aliro_test_endpoint.hpp"
#include "ddk/aliro/DocumentVerifier.h"
#include "../src/aliro/document_cbor/document_cbor.h"
#include <cbor.h>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>

using namespace aliro_test;
using namespace ddk::aliro;
namespace {
void replace(std::vector<uint8_t>& data, const std::vector<uint8_t>& old,
             const std::vector<uint8_t>& replacement) {
    auto start=std::search(data.begin(),data.end(),old.begin(),old.end());
    REQUIRE(start!=data.end());
    auto offset=start-data.begin();
    data.erase(start,start+old.size()); data.insert(data.begin()+offset,replacement.begin(),replacement.end());
}
struct Fixture {
    P256KeyPair issuer=generate_p256_key(), device=generate_p256_key();
    StepUpMaterial material;
    Fixture() {
        std::array<uint8_t,32> y;
        std::copy_n(device.pub.begin()+33,32,y.begin());
        material=make_step_up_material(device.pub_x,y,issuer_key_identifier(issuer.pub),true);
    }
    std::vector<uint8_t> mso() {
        CborParser p; CborValue v;
        REQUIRE(cbor_parser_init(material.payload.data(),material.payload.size(),0,&p,&v)==CborNoError);
        REQUIRE(cbor_value_advance(&v)==CborNoError);
        size_t len=0; REQUIRE(cbor_value_get_string_length(&v,&len)==CborNoError);
        std::vector<uint8_t> out(len);
        REQUIRE(cbor_value_copy_byte_string(&v,out.data(),&len,nullptr)==CborNoError);
        return out;
    }
    void set_mso(const std::vector<uint8_t>& raw) {
        uint8_t buf[1024]; CborEncoder e;
        cbor_encoder_init(&e,buf,sizeof(buf),0); cbor_encode_tag(&e,24);
        REQUIRE(cbor_encode_byte_string(&e,raw.data(),raw.size())==CborNoError);
        material.payload.assign(buf,buf+cbor_encoder_get_buffer_size(&e,buf));
    }
    std::vector<uint8_t> document() {
        material.sig=sign_cose_es256(issuer,material.protected_headers,material.payload);
        std::vector<uint8_t> doc;
        build_step_up_device_response(material,true,&doc);
        return doc;
    }
    std::optional<VerifiedDocument> verify(DocumentPolicy p={}) {
        auto doc=document(); return verify_access_document(doc,issuer.pub,p,{"matter1"});
    }
};
}

TEST_CASE("Complete access document binds the requested item and provisioned issuer", "[aliro][document]") {
    Fixture f; auto doc=f.document();
    auto result=verify_access_document(doc,f.issuer.pub,{}, {"matter1"});
    REQUIRE(result); REQUIRE(result->device_public_key==std::vector<uint8_t>(f.device.pub.begin(),f.device.pub.end()));
    REQUIRE(result->disclosed_elements==std::vector<std::string>{"matter1"});
    REQUIRE_FALSE(verify_access_document(doc,generate_p256_key().pub,{}, {"matter1"}));
    REQUIRE_FALSE(verify_access_document(doc,f.issuer.pub,{}, {"unrequested"}));
    // A legacy local ID must not be mistaken for the standard COSE kid.
    f.material.issuer_id.assign(8,0xAA); REQUIRE_FALSE(f.verify());
}

TEST_CASE("Every disclosed item must match its signed digest and namespace", "[aliro][document]") {
    Fixture f;
    SECTION("unsigned element-value substitution") { f.material.item.back()^=1; REQUIRE_FALSE(f.verify()); }
    SECTION("signed digest alteration still fails item binding") {
        auto raw=f.mso();
        // Locate the first SHA-256 digest through a stable encoded prefix.
        const std::vector<uint8_t> prefix={0x61,0x33,0xA1,0x67,'a','l','i','r','o','-','a',0xA1,0,0x58,0x20};
        auto at=std::search(raw.begin(),raw.end(),prefix.begin(),prefix.end()); REQUIRE(at!=raw.end());
        *(at+prefix.size())^=1; f.set_mso(raw); REQUIRE_FALSE(f.verify());
    }
    SECTION("namespace is part of digest lookup") {
        auto doc=f.document();
        replace(doc,{'a','l','i','r','o','-','a'},{'a','l','i','r','o','-','z'});
        REQUIRE_FALSE(verify_access_document(doc,f.issuer.pub,{}, {"matter1"}));
    }
}

TEST_CASE("Document clock policy rejects missing clock and invalid validity periods", "[aliro][document]") {
    Fixture f; DocumentPolicy p;
    SECTION("clock is checked even when optional") {
        p.current_time=2000000000; REQUIRE_FALSE(f.verify(p));
        p.current_time=1700000000; REQUIRE_FALSE(f.verify(p));
        p.current_time=1800000000; REQUIRE(f.verify(p));
        p.current_time=1735689600; REQUIRE(f.verify(p));
        p.current_time=1893456000; REQUIRE(f.verify(p));
    }
    SECTION("required clock cannot be supplied by document timestamps") {
        auto raw=f.mso(); raw.back()=0xF5; f.set_mso(raw);
        REQUIRE_FALSE(f.verify()); p.current_time=1800000000; REQUIRE(f.verify(p));
    }
    SECTION("missing mandatory time policy") {
        auto raw=f.mso(); REQUIRE(raw.front()==0xA7); raw.front()=0xA6;
        raw.resize(raw.size()-3); f.set_mso(raw); REQUIRE_FALSE(f.verify());
    }
    SECTION("invalid date cannot normalize into another month") {
        auto raw=f.mso(); replace(raw,{'2','0','2','5','-','0','1','-','0','1'},
                                     {'2','0','2','5','-','0','2','-','3','1'});
        f.set_mso(raw); REQUIRE_FALSE(f.verify());
    }
    SECTION("no-clock optional acceptance is configurable") {
        p.allow_time_optional_without_clock=false; REQUIRE_FALSE(f.verify(p));
    }
}

TEST_CASE("Validity iteration uses stored issuer state at the eight-iteration boundary", "[aliro][document]") {
    Fixture f; auto raw=f.mso();
    replace(raw,{0x61,0x36,0xA3},{0x61,0x36,0xA4});
    replace(raw,{'Z',0x61,0x37,0xF4},{'Z',0x61,0x35,0x09,0x61,0x37,0xF4});
    f.set_mso(raw); DocumentPolicy p;
    p.access_iteration=16; REQUIRE(f.verify(p));
    p.access_iteration=17; REQUIRE_FALSE(f.verify(p));
    p.access_iteration=8; auto result=f.verify(p); REQUIRE(result); REQUIRE(result->iteration==9);
}

TEST_CASE("Self-supplied certificates never create issuer trust", "[aliro][document]") {
    Fixture f; auto doc=f.document();
    const std::vector<uint8_t> prefix={0xA1,0x04,0x48};
    auto at=std::search(doc.begin(),doc.end(),prefix.begin(),prefix.end()); REQUIRE(at!=doc.end());
    auto offset=at-doc.begin(); doc[offset]=0xA2;
    doc.insert(doc.begin()+offset+11,{0x18,0x21,0x41,0x00});
    REQUIRE_FALSE(verify_access_document(doc,f.issuer.pub,{}, {"matter1"}));
}

TEST_CASE("CBOR guard rejects ambiguity, trailing data, excessive depth and every truncation", "[aliro][document]") {
    Fixture f; auto doc=f.document(); REQUIRE(aliro_document_cbor_valid(doc.data(),doc.size()));
    for (size_t n=0;n<doc.size();n++) REQUIRE_FALSE(aliro_document_cbor_valid(doc.data(),n));
    doc.push_back(0); REQUIRE_FALSE(aliro_document_cbor_valid(doc.data(),doc.size()));
    const std::vector<std::vector<uint8_t>> invalid={
        {0x18,0}, {0xA2,1,0,1,1}, {0xA2,2,0,1,0}, {0xA2,1,0,0x61,'1',0},
        {0x9F,0xFF}, {0xFA,0,0,0,0}, {0x61,0xFF}, {0x62,0xC0,0x80},
        {0x63,0xED,0xA0,0x80}, std::vector<uint8_t>(4097,0)};
    for (auto& b:invalid) REQUIRE_FALSE(aliro_document_cbor_valid(b.data(),b.size()));
    std::vector<uint8_t> nested(13,0x81); nested.push_back(0);
    REQUIRE_FALSE(aliro_document_cbor_valid(nested.data(),nested.size()));
}

TEST_CASE("Unknown enrollment is denied without an application AccessData policy", "[aliro][document][flow]") {
    FlowRig rig(false); rig.endpoint.scenario.serve_step_up=true;
    rig.endpoint.scenario.unknown_endpoint_key=true;
    ddk::SessionConfig cfg; cfg.target_flow=ddk::Flow::StepUp;
    REQUIRE_FALSE(rig.run(cfg)); REQUIRE(rig.store.save_count==0);
    REQUIRE(rig.store.issuer_list[0].endpoints.size()==1);
}

TEST_CASE("Stale document iteration cannot authenticate using FastAuth", "[aliro][document][flow]") {
    FlowRig rig(true); rig.endpoint.scenario.serve_cryptogram=true;
    rig.store.issuer_list[0].access_iteration=8;
    rig.store.issuer_list[0].endpoints[0].access_iteration=0;
    // Explicitly provisioned key remains usable through authenticated Standard.
    REQUIRE(rig.run(ddk::SessionConfig{})); REQUIRE(rig.endpoint.saw_auth1);
    REQUIRE(rig.store_endpoint().aliro.last_flow==ddk::kFlowSTANDARD);
}

TEST_CASE("Issuer kid matches an independent SHA-256 control value", "[aliro][document]") {
    const uint8_t generator[]={
        0x04,0x6b,0x17,0xd1,0xf2,0xe1,0x2c,0x42,0x47,0xf8,0xbc,0xe6,0xe5,0x63,0xa4,0x40,0xf2,
        0x77,0x03,0x7d,0x81,0x2d,0xeb,0x33,0xa0,0xf4,0xa1,0x39,0x45,0xd8,0x98,0xc2,0x96,
        0x4f,0xe3,0x42,0xe2,0xfe,0x1a,0x7f,0x9b,0x8e,0xe7,0xeb,0x4a,0x7c,0x0f,0x9e,0x16,
        0x2b,0xce,0x33,0x57,0x6b,0x31,0x5e,0xce,0xcb,0xb6,0x40,0x68,0x37,0xbf,0x51,0xf5};
    REQUIRE(issuer_key_identifier(generator)==std::vector<uint8_t>{0xe0,0x9a,0x8f,0xcc,0xe5,0x5a,0x5e,0x94});
}
