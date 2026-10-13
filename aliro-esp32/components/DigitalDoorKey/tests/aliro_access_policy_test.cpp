#include "document_fixtures.hpp"
#include <catch2/catch_test_macros.hpp>
using namespace document_test;
using namespace ddk::aliro;

TEST_CASE("Signed AccessData validates version, action and rule alternatives", "[aliro][rights]") {
    Fixture f;
    for (const auto& value:std::vector<Bytes>{access(),access({1,2}),
        access({},array({access_rule(1),access_rule(2)})),access({},array({access_rule(3)}))}) {
        auto doc=f.access_doc(value); REQUIRE(doc); REQUIRE(evaluate_access_document(*doc,{}, {},{}).allowed);
    }
    for (const auto& value:std::vector<Bytes>{map({{0,integer(2)}}),access({},array({access_rule(1)})),
        access({},array({access_rule(0)})),access({},array({map({})})),access({},array({})),
        map({{0,integer(1)},{7,integer(0)}}),map({{0,integer(1)},{1,bstr(Bytes(17,1))}}),
        map({{0,integer(1)},{1,bstr({})}})}) {
        auto doc=f.access_doc(value); REQUIRE(doc); REQUIRE_FALSE(evaluate_access_document(*doc,{}, {},{}).allowed);
    }
}
TEST_CASE("Unsupported schedules never lose their access restrictions", "[aliro][rights]") {
    Fixture f;
    for (auto label:{1,2}) {
        auto rule=map({{0,integer(2)},{static_cast<uint64_t>(label),integer(1)}});
        auto doc=f.access_doc(access({},array({rule}))); REQUIRE(doc);
        REQUIRE_FALSE(evaluate_access_document(*doc,{}, {},{}).allowed);
        doc=f.access_doc(access({},array({rule,access_rule(2)}))); REQUIRE(doc);
        REQUIRE(evaluate_access_document(*doc,{}, {},{}).allowed); // OR rule semantics
    }
}
TEST_CASE("Reader rules must all be interpretable and are independent restrictions", "[aliro][rights]") {
    Fixture f; AccessPolicy policy;
    policy.reader_rules[7]={{uint16_t(2),{}, {}}};
    policy.reader_rules[8]={{uint16_t(1),{}, {}}};
    auto known=f.access_doc(map({{0,integer(1)},{4,array({integer(7)})}})); REQUIRE(known);
    REQUIRE(evaluate_access_document(*known,policy, {},{}).allowed);
    REQUIRE_FALSE(evaluate_access_document(*known,{}, {},{}).allowed);
    auto unknown=f.access_doc(map({{0,integer(1)},{4,array({integer(7),integer(99)})}})); REQUIRE(unknown);
    REQUIRE_FALSE(evaluate_access_document(*unknown,policy, {},{}).allowed);
    auto deny=f.access_doc(map({{0,integer(1)},{2,array({access_rule(2)})},{4,array({integer(8)})}})); REQUIRE(deny);
    REQUIRE_FALSE(evaluate_access_document(*deny,policy, {},{}).allowed);
}
TEST_CASE("Unknown critical extensions deny while non-access data cannot grant rights", "[aliro][rights]") {
    Fixture f;
    for (uint64_t flags:{0,1,2}) {
        auto extensions=map({{0x123456,array({array({integer(flags),integer(1),integer(1),bstr({1})})})}});
        auto access_ext=f.access_doc(map({{0,integer(1)},{6,extensions}})); REQUIRE(access_ext);
        REQUIRE(evaluate_access_document(*access_ext,{}, {},{}).allowed==(flags==0));
        auto non_access=f.access_doc(map({{0,integer(1)},{2,array({access_rule(1)})},{5,extensions}})); REQUIRE(non_access);
        REQUIRE_FALSE(evaluate_access_document(*non_access,{}, {},{}).allowed);
    }
    auto missing_flag=f.access_doc(map({{0,integer(1)},{6,map({{1,array({array({integer(1),integer(1),bstr({1})})})}})}}));
    REQUIRE(missing_flag); REQUIRE_FALSE(evaluate_access_document(*missing_flag,{}, {},{}).allowed);
}
TEST_CASE("Revocation document has no device key and never acts as an access grant", "[aliro][revoke]") {
    Fixture f; RevocationEntry e; e.public_key_hash=credential_key_hash(f.device.pub);
    auto doc=f.revoke_doc(revocation(0,{e}),1); REQUIRE(doc);
    REQUIRE(doc->device_public_key.empty()); REQUIRE_FALSE(evaluate_access_document(*doc,{}, {},{}).allowed);
    aliro_test::DocumentFixtureSpec invalid; invalid.doc_type="aliro-r"; invalid.iteration=1;
    invalid.element_value=revocation(0,{e}); // deviceKeyInfo forbidden in aliro-r
    REQUIRE_FALSE(verify_revocation_document(document(f.issuer,f.device,invalid),f.issuer.pub,{}));
}
TEST_CASE("Signed Overwrite and Update obey replay and entry iteration semantics", "[aliro][revoke]") {
    Fixture f; RevocationState state;
    RevocationEntry first; first.id={1}; RevocationEntry future; future.id={2};
    auto overwrite=f.revoke_doc(revocation(0,{first}),1); REQUIRE(overwrite);
    REQUIRE(apply_revocation_document(*overwrite,state,{})); REQUIRE(state.iteration==1);
    auto update=f.revoke_doc(revocation(1,{future}),5); REQUIRE(update);
    REQUIRE(apply_revocation_document(*update,state,{})); REQUIRE(state.iteration==1);
    REQUIRE(state.entries.size()==2); REQUIRE(state.entries.back().iteration==5);
    auto same=f.revoke_doc(revocation(0,{}),1); REQUIRE(same);
    auto before=state; REQUIRE(apply_revocation_document(*same,state,{})); REQUIRE(state==before);
    auto replacement=f.revoke_doc(revocation(0,{{{}, {3}, {},0}}),2); REQUIRE(replacement);
    REQUIRE(apply_revocation_document(*replacement,state,{})); REQUIRE(state.entries.size()==2);
    REQUIRE(state.entries[0].id==Bytes{2}); // future Update survives lower Overwrite
    REQUIRE_FALSE(apply_revocation_document(*overwrite,state,{}));
    auto clear=f.revoke_doc(revocation(0,{}),6); REQUIRE(clear);
    REQUIRE(apply_revocation_document(*clear,state,{})); REQUIRE(state.entries.empty()); REQUIRE(state.iteration==6);
}
TEST_CASE("Update applies additions before removals and rejects expiry-bearing removals", "[aliro][revoke]") {
    Fixture f; RevocationState state; RevocationEntry e; e.id={7};
    auto both=f.revoke_doc(revocation(1,{e},std::vector<RevocationEntry>{e}),1); REQUIRE(both);
    REQUIRE(apply_revocation_document(*both,state,{})); REQUIRE(state.entries.empty());
    auto add=f.revoke_doc(revocation(1,{e}),1); REQUIRE(add); REQUIRE(apply_revocation_document(*add,state,{}));
    e.expiry=2000000000;
    auto bad_remove=f.revoke_doc(revocation(1,{},std::vector<RevocationEntry>{e}),1); REQUIRE(bad_remove);
    REQUIRE(apply_revocation_document(*bad_remove,state,1800000000)); REQUIRE(state.entries.size()==1);
    auto invalid=f.revoke_doc(revocation(0,{e},std::vector<RevocationEntry>{}),2); REQUIRE(invalid);
    auto before=state; REQUIRE_FALSE(apply_revocation_document(*invalid,state,{})); REQUIRE(state==before);
}
TEST_CASE("Identical entries retain their iteration and identity collisions replace the old entry", "[aliro][revoke]") {
    Fixture f; RevocationEntry entry; entry.id={1}; entry.public_key_hash=credential_key_hash(f.device.pub);
    RevocationState state; auto first=f.revoke_doc(revocation(1,{entry}),1); REQUIRE(first);
    REQUIRE(apply_revocation_document(*first,state,{}));
    auto repeat=f.revoke_doc(revocation(1,{entry}),5); REQUIRE(repeat); REQUIRE(apply_revocation_document(*repeat,state,{}));
    REQUIRE(state.entries[0].iteration==1);
    entry.id={2}; auto change=f.revoke_doc(revocation(1,{entry}),5); REQUIRE(change);
    REQUIRE(apply_revocation_document(*change,state,{})); REQUIRE(state.entries.size()==1);
    REQUIRE(state.entries[0].id==Bytes{2}); REQUIRE(state.entries[0].iteration==5);
    auto overwrite=f.revoke_doc(revocation(0,{}),2); REQUIRE(overwrite);
    auto before=state; REQUIRE_FALSE(apply_revocation_document(*repeat,state,{},true)); REQUIRE(state==before);
    REQUIRE(apply_revocation_document(*overwrite,state,{},true)); REQUIRE(state.entries.empty());
}
TEST_CASE("ID and public-key revocations expire only with a usable reader clock", "[aliro][revoke]") {
    Fixture f; RevocationEntry by_id; by_id.id={4}; by_id.expiry=1800000000;
    RevocationState state; auto doc=f.revoke_doc(revocation(1,{by_id}),1); REQUIRE(doc);
    REQUIRE(apply_revocation_document(*doc,state,{}));
    auto grant=f.access_doc(access({4})); REQUIRE(grant);
    REQUIRE_FALSE(evaluate_access_document(*grant,{},state,{}).allowed);
    REQUIRE_FALSE(evaluate_access_document(*grant,{},state,1799999999).allowed);
    REQUIRE(evaluate_access_document(*grant,{},state,1800000000).allowed);
    state={}; REQUIRE(apply_revocation_document(*doc,state,1800000000)); REQUIRE(state.entries.empty());
    RevocationEntry by_key; by_key.public_key_hash=credential_key_hash(f.device.pub);
    doc=f.revoke_doc(revocation(1,{by_key}),2); REQUIRE(doc); REQUIRE(apply_revocation_document(*doc,state,{}));
    grant=f.access_doc(access({5})); REQUIRE(grant); REQUIRE_FALSE(evaluate_access_document(*grant,{},state,{}).allowed);
}
TEST_CASE("Malformed revocation data and capacity overflow leave the previous state intact", "[aliro][revoke]") {
    Fixture f; RevocationState state; state.entries={{{},{1},{},0}};
    for (const auto& value:std::vector<Bytes>{map({{0,integer(2)},{1,integer(1)},{2,array({})}}),
        map({{0,integer(1)},{1,integer(2)},{2,array({})}}),map({{0,integer(1)},{1,integer(1)}}),
        revocation(1,{}),
        map({{0,integer(1)},{1,integer(1)},{2,array({map({{0,bstr(Bytes(31,1))}})})}})}) {
        auto doc=f.revoke_doc(value,1); REQUIRE(doc); auto before=state;
        REQUIRE_FALSE(apply_revocation_document(*doc,state,{})); REQUIRE(state==before);
    }
    state.entries.clear();
    for (uint8_t i=0;i<64;i++) state.entries.push_back({{}, {i}, {},0});
    auto doc=f.revoke_doc(revocation(1,{{{}, {99}, {},0}}),1); REQUIRE(doc); auto before=state;
    REQUIRE_FALSE(apply_revocation_document(*doc,state,{})); REQUIRE(state==before);
}
