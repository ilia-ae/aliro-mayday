#include "DDKAuthContext.h"
#include "ReaderDataManager.hpp"
#include "ReaderCredentialOps.hpp"
#include "DocumentAccessOps.hpp"
#include "document_fixtures.hpp"
#include <catch2/catch_test_macros.hpp>
#include <nvs.h>
#include <string>
using namespace document_test;
using namespace aliro_test;
using namespace ddk::aliro;

namespace {
readerData_t initial(const FlowRig& rig) {
    readerData_t data; const auto& i=rig.store.identity;
    data.reader_sk=i.private_key; data.reader_pk=i.public_key; data.reader_pk_x=i.public_key_x;
    data.reader_gid=i.group_identifier; data.reader_id=i.sub_identifier;
    for (const auto& issuer:rig.store.issuer_list) {
        hkIssuer_t old; old.issuer_id=issuer.id; old.issuer_pk=issuer.public_key; old.credential_index=1;
        for (const auto& ep:issuer.endpoints) {
            hkEndpoint_t e; e.endpoint_id=ep.id; e.endpoint_pk=ep.public_key; e.endpoint_pk_x=ep.public_key_x;
            e.endpoint_prst_k=ep.persistent_key; e.credential_index=1; e.credential_type=8;
            old.endpoints.push_back(e);
        }
        data.issuers.push_back(old);
    }
    return data;
}
auto authenticate(ReaderDataManager& store,AliroTestEndpoint& endpoint,KeyFlow flow=kFlowFAST,
                  std::optional<int64_t> now=1800000000,size_t chunk=0) {
    auto snapshot=store.getSnapshot(); auto data=snapshot.data; auto revision=snapshot.revision;
    auto wire=endpoint.callback(); Bytes pending; size_t offset=0;
    DDKAuthenticationContext::Nfc nfc=[&](auto& command,auto& reply,bool) {
        if (!pending.empty() && command.size()>1 && command[1]==0xC0) {
            size_t count=std::min(chunk,pending.size()-offset);
            reply.assign(pending.begin()+offset,pending.begin()+offset+count); offset+=count;
            reply.push_back(offset==pending.size()?0x90:0x61);
            reply.push_back(offset==pending.size()?0:static_cast<uint8_t>(std::min(chunk,pending.size()-offset)));
            return true;
        }
        if (!wire(command,reply)) return false;
        if (chunk && command.size()>1 && command[1]==0xC3 && reply.size()>chunk+2) {
            pending.assign(reply.begin(),reply.end()-2); offset=chunk;
            reply.assign(pending.begin(),pending.begin()+chunk); reply.insert(reply.end(),{0x61,static_cast<uint8_t>(chunk)});
        }
        return true;
    };
    DDKAuthenticationContext::Save grant=[&](const auto& value){return store.commitAuthentication(value,revision,revision);};
    DDKAuthenticationContext::Save policy=[&](const auto& value){return store.commitDocumentPolicy(value,revision,revision);};
    DDKAuthenticationContext context(kAliro,nfc,data,grant,policy,[now]{return now;});
    Bytes select{0x00,0xA4,0x04,0,9,0xA0,0,0,9,9,0xAC,0xCE,0x55,1},response;
    REQUIRE(nfc(select,response,false)); context.setSelectResponse(response);
    return context.authenticate(flow);
}
void present(AliroTestEndpoint& endpoint,const std::vector<Bytes>& docs,bool revocation=false) {
    endpoint.scenario.serve_step_up=true; endpoint.scenario.advertise_revocation=revocation;
    endpoint.set_step_up_response(document_test::response(docs));
}
Bytes grant_doc(const FlowRig& rig,const P256KeyPair& device,const Bytes& value=access({4})) {
    DocumentFixtureSpec s; s.element_value=value;
    return document(rig.issuer_key,device,s);
}
Bytes rev_doc(const FlowRig& rig,const Bytes& value,uint64_t iteration=1) {
    DocumentFixtureSpec s; s.doc_type="aliro-r"; s.include_device_key=false;
    s.iteration=iteration; s.element_value=value;
    return document(rig.issuer_key,rig.endpoint_key,s);
}
}

TEST_CASE("Verified AccessData enrolls a document-bound key and survives reboot", "[firmware][rights]") {
    test_nvs::reset(); FlowRig rig(false); auto before=initial(rig);
    ReaderDataManager store; REQUIRE(store.begin()); REQUIRE(store.updateReaderData(before));
    rig.endpoint.scenario.unknown_endpoint_key=true; rig.endpoint.device_key=generate_p256_key();
    auto doc=grant_doc(rig,rig.endpoint.device_key); present(rig.endpoint,{doc});
    REQUIRE(std::get<2>(authenticate(store,rig.endpoint))==kFlowATTESTATION);
    auto after=store.getReaderDataCopy(); REQUIRE(after.issuers[0].endpoints.size()==2);
    const auto& learned=after.issuers[0].endpoints.back();
    REQUIRE(learned.document_bound); REQUIRE(learned.access_document==doc); REQUIRE(learned.endpoint_prst_k.size()==32);
    REQUIRE(after.reader_sk==before.reader_sk); REQUIRE(after.reader_id==before.reader_id);
    ReaderDataManager reboot; REQUIRE(reboot.begin()); auto restored=reboot.getSnapshot();
    REQUIRE(restored.data.issuers[0].endpoints.back().access_document==doc);
    REQUIRE(reboot.authorizationCurrent(after.issuers[0].issuer_id,learned.endpoint_id,restored.revision,1800000000));
    REQUIRE_FALSE(reboot.authorizationCurrent(after.issuers[0].issuer_id,learned.endpoint_id,restored.revision,2000000000));
}
TEST_CASE("AccessData restrictions cannot create a usable cached endpoint", "[firmware][rights]") {
    for (const auto& value:std::vector<Bytes>{access({},array({access_rule(1)})),
        access({},array({map({{0,integer(2)},{1,integer(1)}})})),map({{0,integer(1)},{4,array({integer(99)})}})}) {
        test_nvs::reset(); FlowRig rig(false); ReaderDataManager store; REQUIRE(store.begin());
        REQUIRE(store.updateReaderData(initial(rig))); rig.endpoint.scenario.unknown_endpoint_key=true;
        rig.endpoint.device_key=generate_p256_key(); present(rig.endpoint,{grant_doc(rig,rig.endpoint.device_key,value)});
        REQUIRE(std::get<2>(authenticate(store,rig.endpoint))==kFlowFailed);
        REQUIRE(store.getReaderDataCopy().issuers[0].endpoints.size()==1);
    }
}
TEST_CASE("Document-bound FastAuth checks rights and expiry without relying on a new document", "[firmware][rights]") {
    for (auto now:std::vector<int64_t>{1800000000,2000000000}) {
        test_nvs::reset(); FlowRig rig(true); auto data=initial(rig); auto& ep=data.issuers[0].endpoints[0];
        ep.document_bound=true; ep.access_document=grant_doc(rig,rig.endpoint_key);
        ReaderDataManager store; REQUIRE(store.begin()); REQUIRE(store.updateReaderData(data));
        rig.endpoint.scenario.serve_cryptogram=true;
        const auto result=authenticate(store,rig.endpoint,kFlowFAST,now);
        if (now==1800000000) REQUIRE(std::get<2>(result)==kFlowFAST);
        else REQUIRE(std::get<2>(result)==kFlowFailed);
    }
}
TEST_CASE("A signed restriction replaces a cached grant even when access is denied", "[firmware][rights]") {
    test_nvs::reset(); FlowRig rig(false); auto data=initial(rig); auto& ep=data.issuers[0].endpoints[0];
    ep.document_bound=true; ep.access_document=grant_doc(rig,rig.endpoint_key);
    ReaderDataManager store; REQUIRE(store.begin()); REQUIRE(store.updateReaderData(data));
    auto deny=grant_doc(rig,rig.endpoint_key,access({},array({access_rule(1)}))); present(rig.endpoint,{deny});
    REQUIRE(std::get<2>(authenticate(store,rig.endpoint,kFlowATTESTATION))==kFlowFailed);
    ReaderDataManager reboot; REQUIRE(reboot.begin()); auto state=reboot.getSnapshot();
    REQUIRE(state.data.issuers[0].endpoints[0].access_document==deny);
    REQUIRE_FALSE(reboot.authorizationCurrent(data.issuers[0].issuer_id,ep.endpoint_id,state.revision,1800000000));
}
TEST_CASE("Revocation is persisted independently of successful device authentication", "[firmware][revoke]") {
    test_nvs::reset(); FlowRig rig(true); auto data=initial(rig);
    ReaderDataManager store; REQUIRE(store.begin()); REQUIRE(store.updateReaderData(data));
    const auto stale=store.getSnapshot(); RevocationEntry e; e.public_key_hash=credential_key_hash(rig.endpoint_key.pub);
    rig.endpoint.scenario.unknown_endpoint_key=true; rig.endpoint.device_key=generate_p256_key();
    present(rig.endpoint,{rev_doc(rig,revocation(0,{e}))});
    REQUIRE(std::get<2>(authenticate(store,rig.endpoint,kFlowATTESTATION))==kFlowFailed);
    auto state=store.getSnapshot(); REQUIRE(state.data.issuers[0].revocations.entries.size()==1);
    REQUIRE(state.data.issuers[0].endpoints[0].endpoint_prst_k.empty());
    uint64_t revision=0; REQUIRE_FALSE(store.commitAuthentication(stale.data,stale.revision,revision));
    ReaderDataManager reboot; REQUIRE(reboot.begin()); auto restored=reboot.getSnapshot();
    REQUIRE(restored.data.issuers[0].revocations.iteration==1);
    REQUIRE_FALSE(reboot.authorizationCurrent(data.issuers[0].issuer_id,data.issuers[0].endpoints[0].endpoint_id,
        restored.revision,1800000000));
}
TEST_CASE("Revocation applies to access documents regardless of their order in the response", "[firmware][revoke]") {
    for (bool reverse:{false,true}) {
        test_nvs::reset(); FlowRig rig(false); ReaderDataManager store; REQUIRE(store.begin());
        REQUIRE(store.updateReaderData(initial(rig))); rig.endpoint.scenario.unknown_endpoint_key=true;
        rig.endpoint.device_key=generate_p256_key();
        RevocationEntry e; e.public_key_hash=credential_key_hash(rig.endpoint.device_key.pub);
        std::vector<Bytes> docs{grant_doc(rig,rig.endpoint.device_key),rev_doc(rig,revocation(0,{e}))};
        if (reverse) std::reverse(docs.begin(),docs.end()); present(rig.endpoint,docs,true);
        REQUIRE(std::get<2>(authenticate(store,rig.endpoint))==kFlowFailed);
        REQUIRE(store.getReaderDataCopy().issuers[0].endpoints.size()==1);
        REQUIRE(store.getReaderDataCopy().issuers[0].revocations.entries.size()==1);
    }
}
TEST_CASE("ID revocation is scoped to the issuer and surviving grants are evaluated separately", "[firmware][revoke]") {
    test_nvs::reset(); FlowRig rig(true); auto data=initial(rig); auto& ep=data.issuers[0].endpoints[0];
    ep.document_bound=true; ep.access_document=grant_doc(rig,rig.endpoint_key,access({4}));
    data.issuers[0].revocations.entries={{{},{4},{},0}};
    ReaderDataManager store; REQUIRE(store.begin()); REQUIRE(store.updateReaderData(data));
    rig.endpoint.scenario.serve_cryptogram=true;
    REQUIRE(std::get<2>(authenticate(store,rig.endpoint))==kFlowFailed);
    REQUIRE(store.mutateReaderData([](auto& d){d.issuers[0].revocations.entries[0].id={5};}));
    REQUIRE(std::get<2>(authenticate(store,rig.endpoint))==kFlowSTANDARD);
    const auto refreshed=store.getReaderDataCopy();
    std::copy_n(refreshed.issuers[0].endpoints[0].endpoint_prst_k.begin(),32,rig.endpoint.persistent_key.begin());
    REQUIRE(std::get<2>(authenticate(store,rig.endpoint))==kFlowFAST);
}
TEST_CASE("Failed revocation storage keeps denials in RAM and invalidates an in-flight grant", "[firmware][revoke]") {
    test_nvs::reset(); FlowRig rig(true); ReaderDataManager store; REQUIRE(store.begin());
    REQUIRE(store.updateReaderData(initial(rig))); const auto stale=store.getSnapshot();
    RevocationEntry e; e.public_key_hash=credential_key_hash(rig.endpoint_key.pub);
    present(rig.endpoint,{rev_doc(rig,revocation(0,{e}))},true); test_nvs::failNextCommit();
    REQUIRE(std::get<2>(authenticate(store,rig.endpoint))==kFlowFailed);
    const auto state=store.getSnapshot(); REQUIRE_FALSE(state.data.issuers[0].document_policy_valid);
    REQUIRE_FALSE(store.authorizationCurrent(stale.data.issuers[0].issuer_id,
        stale.data.issuers[0].endpoints[0].endpoint_id,state.revision,1800000000));
    uint64_t revision=0; REQUIRE_FALSE(store.commitAuthentication(stale.data,stale.revision,revision));
    REQUIRE(std::get<2>(authenticate(store,rig.endpoint,kFlowATTESTATION))==kFlowFailed); // applies retry; key is revoked
    REQUIRE(store.getReaderDataCopy().issuers[0].document_policy_valid);
    ReaderDataManager reboot; REQUIRE(reboot.begin()); REQUIRE(reboot.getReaderDataCopy().issuers[0].revocations.entries.size()==1);
}
TEST_CASE("Revocation removal cannot undo an explicit local credential deletion", "[firmware][revoke]") {
    test_nvs::reset(); FlowRig rig(false); auto data=initial(rig); auto key=data.issuers[0].endpoints[0].endpoint_pk;
    aliro::credentials::revoke(data,8,1,key); ReaderDataManager store; REQUIRE(store.begin()); REQUIRE(store.updateReaderData(data));
    RevocationEntry e; e.public_key_hash=credential_key_hash(key);
    present(rig.endpoint,{grant_doc(rig,rig.endpoint_key),rev_doc(rig,revocation(1,{},std::vector<RevocationEntry>{e}))},true);
    REQUIRE(std::get<2>(authenticate(store,rig.endpoint,kFlowATTESTATION))==kFlowFailed);
    REQUIRE(aliro::credentials::revoked(store.getReaderDataCopy(),key));
    REQUIRE(store.getReaderDataCopy().issuers[0].endpoints.empty());
}
TEST_CASE("Modern revocation refresh handles chained NFC responses and does not change Apple v9 FastAuth", "[firmware][revoke]") {
    test_nvs::reset(); FlowRig rig(true); ReaderDataManager store; REQUIRE(store.begin()); REQUIRE(store.updateReaderData(initial(rig)));
    rig.endpoint.scenario.serve_cryptogram=true; present(rig.endpoint,{rev_doc(rig,revocation(0,{}))},true);
    REQUIRE(std::get<2>(authenticate(store,rig.endpoint,kFlowFAST,1800000000,96))==kFlowSTANDARD);
    REQUIRE(rig.endpoint.saw_auth1); REQUIRE(store.getReaderDataCopy().issuers[0].revocations.iteration==1);
    FlowRig legacy(true); ReaderDataManager old; test_nvs::reset(); REQUIRE(old.begin()); REQUIRE(old.updateReaderData(initial(legacy)));
    legacy.endpoint.scenario.serve_cryptogram=true; legacy.endpoint.scenario.advertise_revocation=true;
    legacy.endpoint.select_fci_override={0x6F,0x06,0xA5,0x04,0x5C,0x02,0x00,0x09};
    REQUIRE(std::get<2>(authenticate(old,legacy.endpoint))==kFlowFAST); REQUIRE_FALSE(legacy.endpoint.saw_auth1);
}

TEST_CASE("Missing trusted time denies time-required documents without inventing a document clock", "[firmware][rights]") {
    test_nvs::reset(); FlowRig rig(false); ReaderDataManager store; REQUIRE(store.begin()); REQUIRE(store.updateReaderData(initial(rig)));
    rig.endpoint.scenario.unknown_endpoint_key=true; rig.endpoint.device_key=generate_p256_key();
    DocumentFixtureSpec s; s.time_required=true; s.element_value=access({4});
    present(rig.endpoint,{document(rig.issuer_key,rig.endpoint.device_key,s)});
    REQUIRE(std::get<2>(authenticate(store,rig.endpoint,kFlowFAST,{}))==kFlowFailed);
    REQUIRE(store.getReaderDataCopy().issuers[0].endpoints.size()==1);
    REQUIRE(std::get<2>(authenticate(store,rig.endpoint,kFlowFAST,1800000000))==kFlowATTESTATION);
    auto state=store.getSnapshot(); const auto& ep=state.data.issuers[0].endpoints.back();
    REQUIRE_FALSE(store.authorizationCurrent(state.data.issuers[0].issuer_id,ep.endpoint_id,state.revision,{}));
}
TEST_CASE("Forged revocation signatures never change persisted permissions", "[firmware][revoke]") {
    test_nvs::reset(); FlowRig rig(true); auto data=initial(rig); ReaderDataManager store; REQUIRE(store.begin()); REQUIRE(store.updateReaderData(data));
    RevocationEntry e; e.public_key_hash=credential_key_hash(rig.endpoint_key.pub);
    auto doc=rev_doc(rig,revocation(0,{e})); Bytes signature{0x58,0x40};
    auto at=std::search(doc.begin(),doc.end(),signature.begin(),signature.end()); REQUIRE(at!=doc.end()); *(at+2)^=1;
    present(rig.endpoint,{doc},true);
    REQUIRE(std::get<2>(authenticate(store,rig.endpoint))==kFlowFailed);
    REQUIRE(store.getReaderDataCopy().issuers[0].revocations.entries.empty());
    rig.endpoint.scenario.advertise_revocation=false; rig.endpoint.scenario.serve_cryptogram=true;
    REQUIRE(std::get<2>(authenticate(store,rig.endpoint))==kFlowFAST);
}
TEST_CASE("Another issuer's revocation cannot satisfy the current issuer refresh", "[firmware][revoke]") {
    test_nvs::reset(); FlowRig rig(true),foreign(false); auto data=initial(rig);
    hkIssuer_t second; second.issuer_pk.assign(foreign.issuer_key.pub.begin(),foreign.issuer_key.pub.end());
    second.issuer_id={99}; second.credential_index=2; data.issuers.push_back(second);
    ReaderDataManager store; REQUIRE(store.begin()); REQUIRE(store.updateReaderData(data));
    present(rig.endpoint,{rev_doc(foreign,revocation(0,{}))},true);
    REQUIRE(std::get<2>(authenticate(store,rig.endpoint))==kFlowFailed);
    auto result=store.getReaderDataCopy(); REQUIRE(result.issuers[0].revocations.iteration==0);
    REQUIRE(result.issuers[1].revocations.iteration==1); // valid independent update still stored
}
TEST_CASE("Legacy FastAuth resumes after Standard authenticates a new persistent-key epoch", "[firmware][migration]") {
    test_nvs::reset(); FlowRig rig(true); auto data=initial(rig);
    data.issuers[0].access_iteration=9; rig.endpoint.scenario.serve_cryptogram=true;
    ReaderDataManager store; REQUIRE(store.begin()); REQUIRE(store.updateReaderData(data));
    REQUIRE(std::get<2>(authenticate(store,rig.endpoint))==kFlowSTANDARD);
    data=store.getReaderDataCopy(); REQUIRE(data.issuers[0].endpoints[0].access_iteration==9);
    std::copy_n(data.issuers[0].endpoints[0].endpoint_prst_k.begin(),32,rig.endpoint.persistent_key.begin());
    REQUIRE(std::get<2>(authenticate(store,rig.endpoint))==kFlowFAST); REQUIRE_FALSE(rig.endpoint.saw_auth1);
}
TEST_CASE("Malformed optional NVS policy metadata denies rather than treating a key as legacy", "[firmware][storage]") {
    test_nvs::reset(); FlowRig rig(true); auto data=initial(rig); ReaderDataManager store; REQUIRE(store.begin()); REQUIRE(store.updateReaderData(data));
    nvs_handle_t handle; REQUIRE(nvs_open("SAVED_DATA",NVS_READWRITE,&handle)==ESP_OK);
    size_t size=0; REQUIRE(nvs_get_blob(handle,"READERDATA",nullptr,&size)==ESP_OK);
    Bytes blob(size); REQUIRE(nvs_get_blob(handle,"READERDATA",blob.data(),&size)==ESP_OK);
    const std::string name="document_policy_valid";
    auto at=std::search(blob.begin(),blob.end(),name.begin(),name.end()); REQUIRE(at!=blob.end());
    REQUIRE(*(at+name.size())==0xC3); *(at+name.size())=0xC0;
    REQUIRE(nvs_set_blob(handle,"READERDATA",blob.data(),blob.size())==ESP_OK); REQUIRE(nvs_commit(handle)==ESP_OK); nvs_close(handle);
    ReaderDataManager reboot; REQUIRE(reboot.begin()); auto loaded=reboot.getSnapshot();
    REQUIRE_FALSE(loaded.data.issuers[0].document_policy_valid);
    REQUIRE_FALSE(reboot.authorizationCurrent(data.issuers[0].issuer_id,data.issuers[0].endpoints[0].endpoint_id,loaded.revision,1800000000));
}

TEST_CASE("A legacy placeholder cannot hide a trusted issuer's public-key revocation", "[firmware][revoke]") {
    test_nvs::reset(); FlowRig rig(true),publisher(false); auto data=initial(rig);
    data.issuers[0].issuer_pk.clear();
    hkIssuer_t trusted; trusted.issuer_pk.assign(publisher.issuer_key.pub.begin(),publisher.issuer_key.pub.end());
    trusted.issuer_id={98}; trusted.credential_index=2;
    trusted.revocations.entries={{{credential_key_hash(rig.endpoint_key.pub)},{},{},1}};
    data.issuers.push_back(trusted);
    ReaderDataManager store; REQUIRE(store.begin()); REQUIRE(store.updateReaderData(data));
    rig.endpoint.scenario.serve_cryptogram=true;
    REQUIRE(std::get<2>(authenticate(store,rig.endpoint))==kFlowFailed);
    auto state=store.getSnapshot(); REQUIRE_FALSE(store.authorizationCurrent(data.issuers[0].issuer_id,
        data.issuers[0].endpoints[0].endpoint_id,state.revision,1800000000));
}
TEST_CASE("No clock blocks document-based grants while legacy iPhone FastAuth remains usable", "[firmware][rights]") {
    test_nvs::reset(); FlowRig rig(true); auto data=initial(rig); ReaderDataManager store; REQUIRE(store.begin()); REQUIRE(store.updateReaderData(data));
    rig.endpoint.scenario.serve_cryptogram=true;
    REQUIRE(std::get<2>(authenticate(store,rig.endpoint,kFlowFAST,{}))==kFlowFAST);
    data.issuers[0].endpoints[0].document_bound=true;
    data.issuers[0].endpoints[0].access_document=grant_doc(rig,rig.endpoint_key);
    REQUIRE(store.updateReaderData(data));
    REQUIRE(std::get<2>(authenticate(store,rig.endpoint,kFlowFAST,{}))==kFlowFailed);
}
TEST_CASE("Duplicate cached IDs cannot split the revocation scope of one issuer key", "[firmware][revoke]") {
    test_nvs::reset(); FlowRig rig(true); auto data=initial(rig);
    auto alias=data.issuers[0]; alias.issuer_id={88}; alias.credential_index=2;
    alias.endpoints[0].document_bound=true; alias.endpoints[0].access_document=grant_doc(rig,rig.endpoint_key);
    data.issuers[0].endpoints.clear(); data.issuers.push_back(alias);
    ReaderDataManager store; REQUIRE(store.begin()); REQUIRE(store.updateReaderData(data));
    RevocationEntry e; e.public_key_hash=credential_key_hash(rig.endpoint_key.pub);
    present(rig.endpoint,{rev_doc(rig,revocation(0,{e}))},true);
    REQUIRE(std::get<2>(authenticate(store,rig.endpoint))==kFlowFailed);
    auto state=store.getSnapshot(); REQUIRE(state.data.issuers[0].revocations.entries.size()==1);
    REQUIRE_FALSE(store.authorizationCurrent(alias.issuer_id,alias.endpoints[0].endpoint_id,state.revision,1800000000));
    REQUIRE(state.data.issuers[1].endpoints[0].endpoint_prst_k.empty());
}
TEST_CASE("Authentication preserves exact credential metadata on issuer aliases", "[firmware][migration]") {
    test_nvs::reset(); FlowRig rig(false); auto data=initial(rig);
    auto alias=data.issuers[0]; alias.issuer_id={88}; alias.credential_index=2;
    alias.endpoints[0].credential_index=7; data.issuers[0].endpoints.clear(); data.issuers.push_back(alias);
    ReaderDataManager store; REQUIRE(store.begin()); REQUIRE(store.updateReaderData(data));
    REQUIRE(std::get<2>(authenticate(store,rig.endpoint))==kFlowSTANDARD);
    auto after=store.getReaderDataCopy(); REQUIRE(after.issuers[0].credential_index==1);
    REQUIRE(after.issuers[1].credential_index==2); REQUIRE(after.issuers[1].issuer_id==alias.issuer_id);
    REQUIRE(after.issuers[1].endpoints[0].credential_index==7);
    REQUIRE(after.issuers[1].endpoints[0].endpoint_id==alias.endpoints[0].endpoint_id);
}
