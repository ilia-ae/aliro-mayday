#include "DDKAuthContext.h"
#include "ReaderDataManager.hpp"
#include "ReaderCredentialOps.hpp"
#include "ReaderIdentityOps.hpp"
#include "UserAccessPolicy.hpp"
#include "aliro_test_endpoint.hpp"
#include "aliro/AliroKeySchedule.h"
#include "TLV8.hpp"
#include <catch2/catch_test_macros.hpp>
#include <nvs.h>
#include <msgpack.h>
#include <cstring>
#include <numeric>

using namespace aliro_test;
namespace {
readerData_t legacyData(const FlowRig& rig) {
    readerData_t data;
    const auto& identity = rig.store.identity;
    data.reader_sk = identity.private_key; data.reader_pk = identity.public_key;
    data.reader_pk_x = identity.public_key_x; data.reader_gid = identity.group_identifier;
    data.reader_id = identity.sub_identifier;
    for (const auto& issuer : rig.store.issuer_list) {
        hkIssuer_t old; old.issuer_id = issuer.id; old.issuer_pk = issuer.public_key;
        old.issuer_pk_x = issuer.public_key_x; old.credential_index = 1;
        for (const auto& endpoint : issuer.endpoints) {
            hkEndpoint_t ep; ep.endpoint_id = endpoint.id; ep.endpoint_pk = endpoint.public_key;
            ep.endpoint_pk_x = endpoint.public_key_x; ep.endpoint_prst_k = endpoint.persistent_key;
            ep.credential_type = aliro::credentials::kNonEvictable; ep.credential_index = 1;
            old.endpoints.push_back(ep);
        }
        data.issuers.push_back(old);
    }
    return data;
}
auto run(readerData_t& data, AliroTestEndpoint& endpoint, int& saves, KeyFlow flow = kFlowFAST) {
    auto callback = endpoint.callback();
    DDKAuthenticationContext::Nfc nfc = [callback](auto& command, auto& response, bool) {
        return callback(command, response);
    };
    DDKAuthenticationContext::Save save = [&saves](const readerData_t&) { ++saves; return true; };
    DDKAuthenticationContext context(kAliro, nfc, data, save);
    std::vector<uint8_t> select{0x00,0xA4,0x04,0x00,0x09,0xA0,0,0,0x09,0x09,0xAC,0xCE,0x55,0x01};
    std::vector<uint8_t> response; REQUIRE(nfc(select, response, false));
    context.setSelectResponse(response);
    return context.authenticate(flow);
}
std::vector<uint8_t> publicKey(uint8_t value) {
    std::vector<uint8_t> key(65, value); key[0] = 0x04; return key;
}
std::vector<char> legacyBlob(const readerData_t& data) {
    msgpack_sbuffer buffer; msgpack_sbuffer_init(&buffer);
    msgpack_packer pack; msgpack_packer_init(&pack, &buffer, msgpack_sbuffer_write);
    const auto text = [&](const char* key) { msgpack_pack_str_with_body(&pack, key, std::strlen(key)); };
    const auto bytes = [&](const char* key, const std::vector<uint8_t>& value) {
        text(key); msgpack_pack_array(&pack, value.size());
        for (uint8_t b : value) msgpack_pack_uint8(&pack, b);
    };
    msgpack_pack_map(&pack, 6);
    bytes("reader_private_key", data.reader_sk); bytes("reader_public_key", data.reader_pk);
    bytes("reader_key_x", data.reader_pk_x); bytes("group_identifier", data.reader_gid);
    bytes("unique_identifier", data.reader_id); text("issuers"); msgpack_pack_array(&pack, data.issuers.size());
    for (const auto& issuer : data.issuers) {
        msgpack_pack_map(&pack, 4); bytes("issuerId", issuer.issuer_id);
        bytes("publicKey", issuer.issuer_pk); bytes("issuer_key_x", issuer.issuer_pk_x);
        text("endpoints"); msgpack_pack_array(&pack, issuer.endpoints.size());
        for (const auto& endpoint : issuer.endpoints) {
            msgpack_pack_map(&pack, 7); bytes("endpointId", endpoint.endpoint_id);
            text("last_used_at"); msgpack_pack_uint32(&pack, endpoint.last_used_at);
            text("counter"); msgpack_pack_uint8(&pack, endpoint.counter);
            text("key_type"); msgpack_pack_uint8(&pack, endpoint.key_type);
            bytes("publicKey", endpoint.endpoint_pk); bytes("endpoint_key_x", endpoint.endpoint_pk_x);
            bytes("persistent_key", endpoint.endpoint_prst_k);
        }
    }
    std::vector<char> result(buffer.data, buffer.data + buffer.size);
    msgpack_sbuffer_destroy(&buffer); return result;
}
}

TEST_CASE("Adapter preserves known iPhone keys in Standard and Fast flows", "[firmware][auth]") {
    for (bool fast : {false, true}) {
        FlowRig rig(fast); rig.endpoint.scenario.serve_cryptogram = fast;
        auto data = legacyData(rig); const auto before = data; int saves = 0;
        auto result = run(data, rig.endpoint, saves);
        REQUIRE(std::get<2>(result) == (fast ? kFlowFAST : kFlowSTANDARD));
        REQUIRE(std::get<0>(result) == before.issuers[0].issuer_id);
        REQUIRE(std::get<1>(result) == before.issuers[0].endpoints[0].endpoint_id);
        REQUIRE(data.reader_sk == before.reader_sk);
        REQUIRE(data.reader_id == before.reader_id);
        REQUIRE(data.issuers[0].endpoints[0].credential_index == 1);
        REQUIRE(saves == (fast ? 0 : 1));
    }
}

TEST_CASE("Apple v9 keeps the old NFC APDU framing and completion", "[firmware][legacy]") {
    for (bool fast : {false,true}) {
        FlowRig rig(fast); rig.endpoint.scenario.serve_cryptogram = fast;
        // Captured SELECT structure of the working Apple profile; no user keys.
        rig.endpoint.select_fci_override = {0x6F,0x20,0x84,0x09,0xA0,0,0,0x09,0x09,0xAC,0xCE,0x55,0x01,
            0xA5,0x13,0x80,0x02,0,0,0x5C,0x02,0,0x09,0x7F,0x66,0x08,0x02,0x02,0x0E,0,0x02,0x02,0x0E,0};
        auto data = legacyData(rig); int saves = 0;
        REQUIRE(std::get<2>(run(data, rig.endpoint, saves)) == (fast ? kFlowFAST : kFlowSTANDARD));
        REQUIRE(rig.endpoint.control_flow_status == std::make_optional(std::make_pair(uint8_t(1),uint8_t(1))));
        for (const auto& command : rig.endpoint.raw_commands) {
            if (command[1] == 0x80 || command[1] == 0x81)
                REQUIRE(command.size() == 5 + command[4]); // old case-3, no Le
        }
    }
}

TEST_CASE("Unknown or incorrectly signed credentials never save", "[firmware][deny]") {
    for (bool unknown : {false,true}) {
        FlowRig rig(false);
        rig.endpoint.scenario.unknown_endpoint_key = unknown;
        rig.endpoint.scenario.corrupt_device_signature = !unknown;
        auto data = legacyData(rig); const auto before = data; int saves = 0;
        REQUIRE(std::get<2>(run(data, rig.endpoint, saves)) == kFlowFailed);
        REQUIRE(saves == 0);
        REQUIRE(data.issuers[0].endpoints.size() == before.issuers[0].endpoints.size());
        REQUIRE(data.issuers[0].endpoints[0].endpoint_prst_k == before.issuers[0].endpoints[0].endpoint_prst_k);
    }
}

TEST_CASE("Step-up does not grant an unknown device access even with a trusted signature", "[firmware][stepup]") {
    for (int attack : {0,1,2,3}) {
        FlowRig rig(false);
        rig.endpoint.scenario.unknown_endpoint_key = true;
        rig.endpoint.scenario.serve_step_up = true;
        rig.endpoint.scenario.corrupt_step_up_signature = attack == 1;
        rig.endpoint.scenario.corrupt_device_signature = attack == 2;
        if (attack == 3) rig.endpoint.document_key_override = generate_p256_key();
        auto data = legacyData(rig); int saves = 0;
        const auto result = run(data, rig.endpoint, saves);
        REQUIRE(std::get<2>(result) == kFlowFailed);
        REQUIRE(saves == 0);
        REQUIRE(data.issuers[0].endpoints.size() == 1);
    }
}

TEST_CASE("Revocation rejects an old in-flight authentication and survives reboot", "[firmware][revoke]") {
    test_nvs::reset(); FlowRig rig(true); auto data = legacyData(rig);
    ReaderDataManager store; REQUIRE(store.begin()); REQUIRE(store.updateReaderData(data));
    const auto stale = store.getSnapshot(); const auto key = data.issuers[0].endpoints[0].endpoint_pk;
    REQUIRE(store.mutateReaderData([&](auto& current) {
        aliro::credentials::revoke(current, aliro::credentials::kNonEvictable, 1, key);
    }));
    uint64_t revision = 0;
    REQUIRE_FALSE(store.commitAuthentication(stale.data, stale.revision, revision));
    REQUIRE_FALSE(store.authorizationCurrent(data.issuers[0].issuer_id,
        data.issuers[0].endpoints[0].endpoint_id, stale.revision));
    ReaderDataManager reboot; REQUIRE(reboot.begin());
    const auto restored = reboot.getReaderDataCopy();
    REQUIRE(restored.issuers[0].endpoints.empty());
    REQUIRE(aliro::credentials::revoked(restored, key));
    REQUIRE(restored.reader_sk == data.reader_sk);
    REQUIRE(restored.reader_id == data.reader_id);
}

TEST_CASE("Revoked key cannot enroll again with a valid issuer document", "[firmware][revoke]") {
    FlowRig rig(false); auto data = legacyData(rig);
    const auto key = data.issuers[0].endpoints[0].endpoint_pk;
    aliro::credentials::revoke(data, aliro::credentials::kNonEvictable, 1, key);
    rig.endpoint.scenario.serve_step_up = true; int saves = 0;
    REQUIRE(std::get<2>(run(data, rig.endpoint, saves)) == kFlowFailed);
    REQUIRE(saves == 0);
    REQUIRE(data.issuers[0].endpoints.empty());
}

TEST_CASE("Revocation distinguishes evictable and non-evictable slot numbers", "[firmware][revoke]") {
    readerData_t data; hkIssuer_t issuer;
    for (uint8_t type : {aliro::credentials::kEvictable, aliro::credentials::kNonEvictable}) {
        hkEndpoint_t ep; ep.credential_index = 1; ep.credential_type = type;
        ep.endpoint_pk = publicKey(type); issuer.endpoints.push_back(ep);
    }
    data.issuers.push_back(issuer);
    aliro::credentials::revoke(data, aliro::credentials::kEvictable, 1, {});
    REQUIRE(data.issuers[0].endpoints.size() == 1);
    REQUIRE(data.issuers[0].endpoints[0].credential_type == aliro::credentials::kNonEvictable);
    REQUIRE(aliro::credentials::revoked(data, publicKey(aliro::credentials::kEvictable)));
}

TEST_CASE("Issuer revocation removes all of its cached endpoint keys", "[firmware][revoke]") {
    FlowRig rig(true); auto data = legacyData(rig);
    aliro::credentials::revoke(data, aliro::credentials::kIssuer, 1, data.issuers[0].issuer_pk);
    REQUIRE(data.issuers.empty());
}

TEST_CASE("A failed NVS commit cannot report successful authentication", "[firmware][storage]") {
    test_nvs::reset(); FlowRig rig(true); ReaderDataManager store; REQUIRE(store.begin());
    REQUIRE(store.updateReaderData(legacyData(rig)));
    auto snapshot = store.getSnapshot(); test_nvs::failNextCommit(); uint64_t committed = 0;
    auto enrollment = snapshot.data;
    enrollment.issuers[0].endpoints[0].endpoint_prst_k.assign(32, 0xAB);
    hkEndpoint_t extra; extra.endpoint_pk = publicKey(0xEE);
    enrollment.issuers[0].endpoints.push_back(extra);
    REQUIRE_FALSE(store.commitAuthentication(enrollment, snapshot.revision, committed));
    const auto restored = store.getReaderDataCopy();
    REQUIRE(restored.issuers[0].endpoints.size() == 1);
    REQUIRE(restored.issuers[0].endpoints[0].endpoint_prst_k ==
        snapshot.data.issuers[0].endpoints[0].endpoint_prst_k);
}

TEST_CASE("Existing seven-field endpoint NVS blobs still unlock through FastAuth", "[firmware][migration]") {
    test_nvs::reset(); FlowRig rig(true); rig.endpoint.scenario.serve_cryptogram = true;
    auto before = legacyData(rig); const auto blob = legacyBlob(before);
    nvs_handle_t handle; REQUIRE(nvs_open("SAVED_DATA", NVS_READWRITE, &handle) == ESP_OK);
    REQUIRE(nvs_set_blob(handle, "READERDATA", blob.data(), blob.size()) == ESP_OK);
    REQUIRE(nvs_commit(handle) == ESP_OK); nvs_close(handle);
    ReaderDataManager store; REQUIRE(store.begin()); auto loaded = store.getReaderDataCopy();
    REQUIRE(loaded.reader_sk == before.reader_sk);
    REQUIRE(loaded.reader_gid == before.reader_gid);
    REQUIRE(loaded.reader_id == before.reader_id);
    REQUIRE(loaded.issuers[0].endpoints[0].endpoint_prst_k == before.issuers[0].endpoints[0].endpoint_prst_k);
    REQUIRE(loaded.issuers[0].endpoints[0].credential_index == 0);
    int saves = 0; REQUIRE(std::get<2>(run(loaded, rig.endpoint, saves)) == kFlowFAST);
    REQUIRE(saves == 0);
}

TEST_CASE("Authoritative Matter slots remove a stale persisted NFC key at boot", "[firmware][revoke]") {
    FlowRig rig(true); auto data = legacyData(rig);
    const auto key = data.issuers[0].endpoints[0].endpoint_pk;
    std::vector<aliro::credentials::Provisioned> live{{aliro::credentials::kIssuer, 1, data.issuers[0].issuer_pk}};
    aliro::credentials::reconcile(data, live);
    REQUIRE(data.issuers.size() == 1);
    REQUIRE(data.issuers[0].endpoints.empty());
    REQUIRE(aliro::credentials::revoked(data, key));
    live.clear(); aliro::credentials::reconcile(data, live);
    REQUIRE(data.issuers.empty());
}

TEST_CASE("Assigned disabled users are rejected without breaking unassigned legacy keys", "[firmware][access]") {
    using aliro::access::Assignment;
    Assignment disabled[]{{aliro::credentials::kIssuer, 1, false}};
    REQUIRE_FALSE(aliro::access::allowed(1, 0, 0, disabled, 1));
    disabled[0].enabled = true;
    REQUIRE(aliro::access::allowed(1, 0, 0, disabled, 1));
    REQUIRE(aliro::access::allowed(1, 0, 0, nullptr, 0));
    Assignment otherType[]{{aliro::credentials::kEvictable, 1, false}};
    REQUIRE(aliro::access::allowed(0, aliro::credentials::kNonEvictable, 1, otherType, 1));
    Assignment explicitDevice[]{{aliro::credentials::kIssuer, 1, true},
        {aliro::credentials::kNonEvictable, 1, false}};
    REQUIRE_FALSE(aliro::access::allowed(1, aliro::credentials::kNonEvictable, 1, explicitDevice, 2));
}

TEST_CASE("Boot identity synchronization preserves iPhone keys and revocation records", "[firmware][migration]") {
    FlowRig rig(true); auto data = legacyData(rig); const auto before = data;
    data.revoked_endpoint_keys.push_back(publicKey(0x99));
    aliro::credentials::mergeIdentity(data, before);
    REQUIRE(data.issuers[0].endpoints[0].endpoint_prst_k == before.issuers[0].endpoints[0].endpoint_prst_k);
    REQUIRE(data.issuers[0].endpoints[0].endpoint_id == before.issuers[0].endpoints[0].endpoint_id);
    REQUIRE(data.revoked_endpoint_keys.size() == 1);
    auto changed = before; changed.reader_id[0] ^= 1;
    aliro::credentials::mergeIdentity(data, changed);
    REQUIRE(data.issuers[0].endpoints.size() == 1);
    REQUIRE(data.issuers[0].endpoints[0].endpoint_prst_k.empty());
    REQUIRE(data.revoked_endpoint_keys.size() == 1);
}

TEST_CASE("Malformed or unsupported SELECT fails before AUTH0", "[firmware][deny]") {
    for (auto fci : std::vector<std::vector<uint8_t>>{
        {0x6F,0}, {0x6F,4,0xA5,2,0x5C,0}, {0x6F,6,0xA5,4,0x5C,2,0,8},
        {0x6F,5,0xA5,3,0x5C,1,1}, {0x6F,0x81,0x20,0xA5}}) {
        FlowRig rig(true); rig.endpoint.select_fci_override = fci;
        auto data = legacyData(rig); int saves = 0;
        REQUIRE(std::get<2>(run(data, rig.endpoint, saves)) == kFlowFailed);
        REQUIRE(saves == 0);
        REQUIRE(rig.endpoint.raw_commands.size() == 1);
    }
}

TEST_CASE("Aliro 1.0 is preferred when Apple v9 is also advertised", "[firmware][legacy]") {
    FlowRig rig(true); rig.endpoint.scenario.serve_cryptogram = true;
    rig.endpoint.select_fci_override = {0x6F,0x22,0x84,0x09,0xA0,0,0,0x09,0x09,0xAC,0xCE,0x55,0x01,
        0xA5,0x15,0x80,0x02,0,0,0x5C,0x04,0,0x09,0x01,0,0x7F,0x66,0x08,0x02,0x02,0x0E,0,0x02,0x02,0x0E,0};
    auto data = legacyData(rig); int saves = 0;
    REQUIRE(std::get<2>(run(data, rig.endpoint, saves)) == kFlowFAST);
    for (const auto& command : rig.endpoint.raw_commands) {
        if (command[1] != 0x80) continue;
        REQUIRE(command.size() == 6 + command[4]);
        TLV8 parameters; parameters.parse(command.data() + 5, command[4]);
        REQUIRE(parameters.expect(0x5C) != nullptr);
        REQUIRE(parameters.expect(0x5C)->value == std::vector<uint8_t>{0x01,0x00});
    }
}

TEST_CASE("Apple v9 key schedule matches independent pre-update golden vectors", "[firmware][legacy]") {
    // Synthetic inputs; expected values computed independently with Python
    // hmac/hashlib from the previous DDK's salt layout (RFC 5869 SHA-256).
    const auto sequence = [](uint8_t first, size_t count) {
        std::vector<uint8_t> value(count); std::iota(value.begin(), value.end(), first); return value;
    };
    const auto hex = [](const char* text) {
        std::vector<uint8_t> value;
        const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
        REQUIRE(std::strlen(text) % 2 == 0);
        while (*text) { value.push_back((digit(text[0]) << 4) | digit(text[1])); text += 2; }
        return value;
    };
    auto reader = sequence(0,32), identifier = sequence(32,32), readerEph = sequence(64,32);
    auto deviceEph = sequence(96,32), endpoint = sequence(128,32), seed = sequence(160,32), tx = sequence(192,16);
    auto fci = hex("a513800200005c0200097f660802020e0002020e00");
    AliroKeySchedule::SessionInput input{reader, identifier, readerEph, deviceEph, tx,
        {0,9}, {1,1}, fci, 0x5E, {}};
    AliroKeySchedule schedule;
    auto fast = schedule.derive_fast(input, endpoint, seed);
    REQUIRE(std::vector<uint8_t>(fast.cryptogram_sk.begin(), fast.cryptogram_sk.end()) ==
        hex("3f308a9728cbe42170aedb3268d0340ce7d73d1429646ea7b9684a20596dc4af"));
    REQUIRE(std::vector<uint8_t>(fast.exchange_sk_device.begin(), fast.exchange_sk_device.end()) ==
        hex("52edbd302d528585008078fd87cee09b99f121749dcfea59d3b4ba929ebd68de"));
    auto key = ddk::span<const uint8_t,32>(seed.data(), seed.size());
    auto standard = schedule.derive_volatile(input, key);
    REQUIRE(std::vector<uint8_t>(standard.exchange_sk_reader.begin(), standard.exchange_sk_reader.end()) ==
        hex("fd17300667a0b02f269ab80c8311d5d1f3c26bdc221ba149d4aeaf1672137fdc"));
    auto persistent = schedule.derive_persistent(input, key, endpoint);
    REQUIRE(std::vector<uint8_t>(persistent.begin(), persistent.end()) ==
        hex("c1be34c05a9f89f83615d8f5013417a8c06c719fe0ff7dd771a30460cc14eaa6"));
}
