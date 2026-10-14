#include "AliroDelegate.h"
#include "ReaderConfigStore.hpp"
#include "ReaderDataManager.hpp"
#include "DoorLockStorageLimits.hpp"
#include "DDKAuthContext.h"
#include "SystemClock.hpp"
#include <esp_sntp.h>
#include "aliro_test_endpoint.hpp"
#include <app/clusters/door-lock-server/door-lock-server.h>
#include <catch2/catch_test_macros.hpp>
#include <nvs.h>
#include <algorithm>

using namespace aliro_test;
using namespace chip::app::Clusters::DoorLock;
void doorlock_stubs_load_from_nvs();
void replay_aliro_credentials_to_aliro_delegate();
bool aliro_user_access_allowed(const readerData_t&,const std::vector<uint8_t>&,const std::vector<uint8_t>&);

namespace {
constexpr auto issuerType=CredentialTypeEnum::kAliroCredentialIssuerKey;
constexpr auto endpointType=CredentialTypeEnum::kAliroNonEvictableEndpointKey;
void blob(const char* name,const void* bytes,size_t size) {
    nvs_handle_t h; REQUIRE(nvs_open("aliro_cfg",NVS_READWRITE,&h)==ESP_OK);
    REQUIRE(nvs_set_blob(h,name,bytes,size)==ESP_OK);
    REQUIRE(nvs_commit(h)==ESP_OK); nvs_close(h);
}
template<class T> chip::ByteSpan bytes(const T& v) {return {v.data(),v.size()};}
aliro::reader_config::Config config(const FlowRig& rig) {
    aliro::reader_config::Config result;result.configured=true;
    const auto& id=rig.store.identity;
    std::copy(id.private_key.begin(),id.private_key.end(),result.signing.begin());
    std::copy(id.public_key.begin(),id.public_key.end(),result.verification.begin());
    std::copy(id.group_identifier.begin(),id.group_identifier.end(),result.group.begin());
    return result;
}
struct Rig {
    FlowRig device{true}; ReaderDataManager reader;
    Rig() {
        test_nvs::reset();
        const auto& sub=device.store.identity.sub_identifier;
        REQUIRE(sub.size()==16); blob("subId",sub.data(),sub.size());
        aliro::instance()=aliro::AliroDelegate{};
        doorlock_stubs_load_from_nvs();
        REQUIRE(reader.begin());
        aliro::instance().setReaderDataSink(&reader);
    }
    ~Rig(){aliro::instance().setReaderDataSink(nullptr);}
    CHIP_ERROR provision() {
        const auto c=config(device);
        return aliro::instance().SetAliroReaderConfig(bytes(c.signing),bytes(c.verification),bytes(c.group),{});
    }
    bool put(CredentialTypeEnum type,uint16_t index,const std::vector<uint8_t>& key,
             DlCredentialStatus status=DlCredentialStatus::kOccupied) {
        return emberAfPluginDoorLockSetCredential(1,index,1,1,status,type,bytes(key));
    }
    void provisionDevice(uint16_t index=16) {
        REQUIRE(provision()==CHIP_NO_ERROR);
        REQUIRE(put(issuerType,1,device.store.issuer_list[0].public_key));
        REQUIRE(put(endpointType,index,device.store.issuer_list[0].endpoints[0].public_key));
    }
    bool user(UserStatusEnum status,uint16_t index=16) {
        CredentialStruct credential{endpointType,index};
        return emberAfPluginDoorLockSetUser(1,1,1,1,chip::CharSpan("owner",5),1,
            status,UserTypeEnum::kUnrestrictedUser,CredentialRuleEnum::kSingle,&credential,1);
    }
};
auto authenticate(ReaderDataManager& reader,AliroTestEndpoint& endpoint) {
    auto snapshot=reader.getSnapshot();auto data=snapshot.data;
    auto transport=endpoint.callback();
    DDKAuthenticationContext::Nfc nfc=[transport](auto& command,auto& response,bool) {
        return transport(command,response);
    };
    DDKAuthenticationContext::Save save=[&](const readerData_t& next) {
        uint64_t revision=0;return reader.commitAuthentication(next,snapshot.revision,revision);
    };
    DDKAuthenticationContext auth(kAliro,nfc,data,save);
    std::vector<uint8_t> select{0x00,0xA4,0x04,0x00,0x09,0xA0,0,0,0x09,0x09,0xAC,0xCE,0x55,0x01},reply;
    REQUIRE(nfc(select,reply,false));auth.setSelectResponse(reply);
    return auth.authenticate(kFlowFAST);
}
}

TEST_CASE("All announced Aliro slots can be enumerated before provisioning", "[firmware][provisioning]") {
    Rig rig;
    for(auto type:{CredentialTypeEnum::kAliroEvictableEndpointKey,endpointType})
        for(uint16_t i=1;i<=aliro::instance().GetNumberOfAliroEndpointKeysSupported();++i) {
            EmberAfPluginDoorLockCredentialInfo credential;
            REQUIRE(emberAfPluginDoorLockGetCredential(1,i,type,credential));
            REQUIRE(credential.status==DlCredentialStatus::kAvailable);
        }
    EmberAfPluginDoorLockCredentialInfo credential;
    REQUIRE_FALSE(emberAfPluginDoorLockGetCredential(1,17,endpointType,credential));
    REQUIRE_FALSE(emberAfPluginDoorLockGetCredential(1,0,endpointType,credential));
}

TEST_CASE("Matter slot sixteen is persisted, replayed and cleared by its actual callbacks", "[firmware][provisioning]") {
    Rig rig;rig.provisionDevice();
    EmberAfPluginDoorLockCredentialInfo credential;
    REQUIRE(emberAfPluginDoorLockGetCredential(1,16,endpointType,credential));
    REQUIRE(credential.credentialData.data_equal(bytes(rig.device.store.issuer_list[0].endpoints[0].public_key)));
    doorlock_stubs_load_from_nvs(); replay_aliro_credentials_to_aliro_delegate();
    const auto saved=rig.reader.getReaderDataCopy();
    REQUIRE(saved.issuers.size()==1);
    REQUIRE(saved.issuers[0].endpoints.size()==1);
    REQUIRE(saved.issuers[0].endpoints[0].credential_index==16);
    REQUIRE(rig.put(endpointType,16,{},DlCredentialStatus::kAvailable));
    REQUIRE(rig.reader.getReaderDataCopy().issuers[0].endpoints.empty());
    doorlock_stubs_load_from_nvs(); replay_aliro_credentials_to_aliro_delegate();
    REQUIRE(rig.reader.getReaderDataCopy().issuers[0].endpoints.empty());
}

TEST_CASE("Failed SetUser grant does not appear in RAM or after reload", "[firmware][provisioning][storage]") {
    Rig rig;test_nvs::failNextCommit();
    REQUIRE_FALSE(rig.user(UserStatusEnum::kOccupiedEnabled));
    EmberAfPluginDoorLockUserInfo user;
    REQUIRE(emberAfPluginDoorLockGetUser(1,1,user));
    REQUIRE(user.userStatus==UserStatusEnum::kAvailable);
    REQUIRE(user.credentials.empty());
    doorlock_stubs_load_from_nvs();
    REQUIRE(emberAfPluginDoorLockGetUser(1,1,user));
    REQUIRE(user.userStatus==UserStatusEnum::kAvailable);
}

TEST_CASE("Failed re-enable keeps a previously disabled key denied", "[firmware][provisioning][storage]") {
    Rig rig;rig.provisionDevice();
    REQUIRE(rig.user(UserStatusEnum::kOccupiedDisabled));
    test_nvs::failNextCommit(); REQUIRE_FALSE(rig.user(UserStatusEnum::kOccupiedEnabled));
    const auto data=rig.reader.getReaderDataCopy();
    REQUIRE_FALSE(aliro_user_access_allowed(data,data.issuers[0].issuer_id,data.issuers[0].endpoints[0].endpoint_id));
}

TEST_CASE("Failed disable keeps existing assignments denied in RAM", "[firmware][provisioning][storage]") {
    Rig rig;rig.provisionDevice(); REQUIRE(rig.user(UserStatusEnum::kOccupiedEnabled));
    test_nvs::failNextCommit(); REQUIRE_FALSE(rig.user(UserStatusEnum::kOccupiedDisabled));
    const auto data=rig.reader.getReaderDataCopy();
    REQUIRE_FALSE(aliro_user_access_allowed(data,data.issuers[0].issuer_id,data.issuers[0].endpoints[0].endpoint_id));
}

TEST_CASE("Oversized user requests fail without silently truncating credentials", "[firmware][provisioning]") {
    Rig rig; CredentialStruct credentials[4]{};
    REQUIRE_FALSE(emberAfPluginDoorLockSetUser(1,1,1,1,chip::CharSpan("owner",5),1,
        UserStatusEnum::kOccupiedEnabled,UserTypeEnum::kUnrestrictedUser,CredentialRuleEnum::kSingle,credentials,4));
    REQUIRE_FALSE(emberAfPluginDoorLockSetUser(1,1,1,1,chip::CharSpan("owner",5),1,
        UserStatusEnum::kOccupiedEnabled,UserTypeEnum::kUnrestrictedUser,CredentialRuleEnum::kSingle,nullptr,1));
    REQUIRE_FALSE(emberAfPluginDoorLockSetUser(1,1,1,1,chip::CharSpan(nullptr,5),1,
        UserStatusEnum::kOccupiedEnabled,UserTypeEnum::kUnrestrictedUser,CredentialRuleEnum::kSingle,credentials,1));
}

TEST_CASE("Reader config commit failure returns failure without publishing new identity", "[firmware][provisioning][storage]") {
    Rig rig;test_nvs::failNextCommit();
    REQUIRE(rig.provision()!=CHIP_NO_ERROR);
    REQUIRE_FALSE(aliro::instance().isProvisioned());
    uint8_t buffer[65];chip::MutableByteSpan value(buffer);
    REQUIRE(aliro::instance().GetAliroReaderVerificationKey(value)==CHIP_NO_ERROR);
    REQUIRE(value.empty());
    aliro::reader_config::Config loaded;
    REQUIRE(aliro::reader_config::load(loaded)==aliro::reader_config::Load::Empty);
    REQUIRE(rig.provision()==CHIP_NO_ERROR);
}

TEST_CASE("Reader config round trip retains the stable sub-ID", "[firmware][provisioning]") {
    Rig rig; REQUIRE(rig.provision()==CHIP_NO_ERROR);
    uint8_t before[16],after[16];chip::MutableByteSpan b(before),a(after);
    REQUIRE(aliro::instance().GetAliroReaderGroupSubIdentifier(b)==CHIP_NO_ERROR);
    aliro::AliroDelegate rebooted;
    REQUIRE(rebooted.isProvisioned());
    REQUIRE(rebooted.GetAliroReaderGroupSubIdentifier(a)==CHIP_NO_ERROR);
    REQUIRE(std::equal(b.begin(),b.end(),a.begin()));
    uint8_t publicKey[65];chip::MutableByteSpan publicSpan(publicKey);
    REQUIRE(rebooted.GetAliroReaderVerificationKey(publicSpan)==CHIP_NO_ERROR);
    REQUIRE(std::equal(publicSpan.begin(),publicSpan.end(),rig.device.store.identity.public_key.begin()));
}

TEST_CASE("Malformed reader keys cannot be provisioned", "[firmware][provisioning][deny]") {
    Rig rig;auto c=config(rig.device);
    SECTION("zero scalar") {c.signing.fill(0);}
    SECTION("point off curve") {c.verification.fill(0);c.verification[0]=4;}
    SECTION("different valid public key") {c.verification=generate_p256_key().pub;}
    REQUIRE(aliro::instance().SetAliroReaderConfig(bytes(c.signing),bytes(c.verification),bytes(c.group),{})==CHIP_ERROR_INVALID_ARGUMENT);
    REQUIRE_FALSE(aliro::instance().isProvisioned());
}

TEST_CASE("NFC-only provisioning rejects a BLE resolving key", "[firmware][provisioning][deny]") {
    Rig rig;auto c=config(rig.device);
    chip::Optional<chip::ByteSpan> resolving(bytes(c.resolving));
    REQUIRE(aliro::instance().SetAliroReaderConfig(bytes(c.signing),bytes(c.verification),bytes(c.group),resolving)==CHIP_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("Legacy reader fields load without identity or boot-time migration", "[firmware][provisioning][legacy]") {
    Rig rig;const auto c=config(rig.device);
    blob("signKey",c.signing.data(),c.signing.size());
    blob("verKey",c.verification.data(),c.verification.size());
    blob("grpId",c.group.data(),c.group.size());
    aliro::AliroDelegate legacy;
    REQUIRE(legacy.isProvisioned());
    uint8_t sub[16];chip::MutableByteSpan span(sub);
    REQUIRE(legacy.GetAliroReaderGroupSubIdentifier(span)==CHIP_NO_ERROR);
    REQUIRE(std::equal(span.begin(),span.end(),rig.device.store.identity.sub_identifier.begin()));
    nvs_handle_t h; REQUIRE(nvs_open("aliro_cfg",NVS_READWRITE,&h)==ESP_OK);
    size_t size=0;REQUIRE(nvs_get_blob(h,aliro::reader_config::record_key,nullptr,&size)==ESP_ERR_NVS_NOT_FOUND);nvs_close(h);
}

TEST_CASE("Durable clear marker prevents fallback to old legacy reader keys", "[firmware][provisioning][legacy]") {
    Rig rig;const auto c=config(rig.device);
    blob("signKey",c.signing.data(),c.signing.size());blob("verKey",c.verification.data(),c.verification.size());blob("grpId",c.group.data(),c.group.size());
    REQUIRE(rig.provision()==CHIP_NO_ERROR);
    REQUIRE(aliro::instance().ClearAliroReaderConfig()==CHIP_NO_ERROR);
    aliro::AliroDelegate rebooted;REQUIRE_FALSE(rebooted.isProvisioned());
    uint8_t publicKey[65];chip::MutableByteSpan span(publicKey);
    REQUIRE(rebooted.GetAliroReaderVerificationKey(span)==CHIP_NO_ERROR);REQUIRE(span.empty());
    uint8_t sub[16];chip::MutableByteSpan subSpan(sub);
    REQUIRE(rebooted.GetAliroReaderGroupSubIdentifier(subSpan)==CHIP_NO_ERROR);
    REQUIRE(std::equal(subSpan.begin(),subSpan.end(),rig.device.store.identity.sub_identifier.begin()));
    nvs_handle_t h;REQUIRE(nvs_open("aliro_cfg",NVS_READWRITE,&h)==ESP_OK);
    size_t size=0;REQUIRE(nvs_get_blob(h,"signKey",nullptr,&size)==ESP_ERR_NVS_NOT_FOUND);nvs_close(h);
}

TEST_CASE("Corrupt new config never falls back to an apparently valid legacy key", "[firmware][provisioning][deny]") {
    Rig rig;const auto c=config(rig.device);
    blob("signKey",c.signing.data(),c.signing.size());blob("verKey",c.verification.data(),c.verification.size());blob("grpId",c.group.data(),c.group.size());
    uint8_t invalid=0;blob(aliro::reader_config::record_key,&invalid,1);
    aliro::AliroDelegate corrupt;REQUIRE_FALSE(corrupt.isProvisioned());
    uint8_t publicKey[65];chip::MutableByteSpan span(publicKey);
    REQUIRE(corrupt.GetAliroReaderVerificationKey(span)!=CHIP_NO_ERROR);
}

TEST_CASE("Invalid saved sub-ID is never replaced with a fresh identity", "[firmware][provisioning][legacy]") {
    Rig rig;uint8_t invalid=0x42;blob("subId",&invalid,1);
    aliro::AliroDelegate bad;
    uint8_t sub[16];chip::MutableByteSpan span(sub);
    REQUIRE(bad.GetAliroReaderGroupSubIdentifier(span)!=CHIP_NO_ERROR);
    nvs_handle_t h; REQUIRE(nvs_open("aliro_cfg",NVS_READWRITE,&h)==ESP_OK);
    size_t size=0;REQUIRE(nvs_get_blob(h,"subId",nullptr,&size)==ESP_OK);REQUIRE(size==1);nvs_close(h);
}

TEST_CASE("Invalid endpoint point fails without leaving a Matter credential", "[firmware][provisioning][deny]") {
    Rig rig;REQUIRE(rig.provision()==CHIP_NO_ERROR);
    std::vector<uint8_t> invalid(65);invalid[0]=4;
    REQUIRE_FALSE(rig.put(endpointType,16,invalid));
    EmberAfPluginDoorLockCredentialInfo credential;
    REQUIRE(emberAfPluginDoorLockGetCredential(1,16,endpointType,credential));
    REQUIRE(credential.status==DlCredentialStatus::kAvailable);
}

TEST_CASE("Reader storage open or write failure cannot report provisioning success", "[firmware][provisioning][storage]") {
    Rig rig;
    SECTION("open failed") {test_nvs::failNextOpen();}
    SECTION("blob write failed") {test_nvs::failNextSet();}
    REQUIRE(rig.provision()!=CHIP_NO_ERROR);
    REQUIRE_FALSE(aliro::instance().isProvisioned());
    REQUIRE(rig.reader.getReaderDataCopy().reader_sk.empty());
}

TEST_CASE("Failed NFC mirror rolls reader config back so commissioning can retry", "[firmware][provisioning][storage]") {
    Rig rig;test_nvs::failNextCommitIn("SAVED_DATA");
    REQUIRE(rig.provision()!=CHIP_NO_ERROR);
    REQUIRE_FALSE(aliro::instance().isProvisioned());
    uint8_t buffer[65];chip::MutableByteSpan span(buffer);
    REQUIRE(aliro::instance().GetAliroReaderVerificationKey(span)==CHIP_NO_ERROR);
    REQUIRE(span.empty());
    aliro::reader_config::Config persisted;
    REQUIRE(aliro::reader_config::load(persisted)==aliro::reader_config::Load::Empty);
    REQUIRE(rig.provision()==CHIP_NO_ERROR);
    REQUIRE(aliro::instance().isProvisioned());
}

TEST_CASE("Failed reader clear keeps NFC denied and reports storage failure", "[firmware][provisioning][storage]") {
    Rig rig;rig.provisionDevice();test_nvs::failNextCommit();
    REQUIRE(aliro::instance().ClearAliroReaderConfig()!=CHIP_NO_ERROR);
    REQUIRE_FALSE(aliro::instance().isProvisioned());
    REQUIRE(rig.reader.getReaderDataCopy().reader_sk.empty());
}

TEST_CASE("Unpersisted new sub-ID is not advertised as a stable reader identity", "[firmware][provisioning][storage]") {
    test_nvs::reset();test_nvs::failNextCommit();aliro::AliroDelegate failed;
    uint8_t sub[16];chip::MutableByteSpan span(sub);
    REQUIRE(failed.GetAliroReaderGroupSubIdentifier(span)!=CHIP_NO_ERROR);
    REQUIRE_FALSE(failed.isProvisioned());
}

TEST_CASE("Document time sync starts without a display and preserves an existing service", "[firmware][provisioning][clock]") {
    SECTION("starts once") {
        test_clock::reset();aliro::startSystemClock();aliro::startSystemClock();
        REQUIRE(test_clock::starts()==1);
    }
    SECTION("existing service") {
        test_clock::reset(true);aliro::startSystemClock();REQUIRE(test_clock::starts()==0);
    }
}

TEST_CASE("Failed NFC credential mirror restores the Matter slot for a retry", "[firmware][provisioning][storage]") {
    Rig rig;REQUIRE(rig.provision()==CHIP_NO_ERROR);
    REQUIRE(rig.put(issuerType,1,rig.device.store.issuer_list[0].public_key));
    const auto key=rig.device.store.issuer_list[0].endpoints[0].public_key;
    test_nvs::failNextCommitIn("SAVED_DATA");
    REQUIRE_FALSE(rig.put(endpointType,16,key));
    EmberAfPluginDoorLockCredentialInfo credential;
    REQUIRE(emberAfPluginDoorLockGetCredential(1,16,endpointType,credential));
    REQUIRE(credential.status==DlCredentialStatus::kAvailable);
    doorlock_stubs_load_from_nvs();
    REQUIRE(emberAfPluginDoorLockGetCredential(1,16,endpointType,credential));
    REQUIRE(credential.status==DlCredentialStatus::kAvailable);
    REQUIRE(rig.put(endpointType,16,key));
}

TEST_CASE("Matter provisioning reaches Standard, Fast, reboot and revocation without changing reader identity", "[firmware][provisioning][auth]") {
    Rig rig;rig.provisionDevice();REQUIRE(rig.user(UserStatusEnum::kOccupiedEnabled));
    rig.device.endpoint.scenario.serve_cryptogram=false;
    REQUIRE(std::get<2>(authenticate(rig.reader,rig.device.endpoint))==kFlowSTANDARD);
    rig.device.endpoint.retain_standard_persistent_key();
    const auto before=rig.reader.getReaderDataCopy();
    REQUIRE_FALSE(before.issuers[0].endpoints[0].endpoint_prst_k.empty());
    REQUIRE(std::equal(before.issuers[0].endpoints[0].endpoint_prst_k.begin(),
        before.issuers[0].endpoints[0].endpoint_prst_k.end(),rig.device.endpoint.persistent_key.begin()));
    // An idempotent reader sync must preserve a working iPhone's cached key.
    REQUIRE(rig.provision()==CHIP_NO_ERROR);
    REQUIRE(rig.reader.getReaderDataCopy().issuers[0].endpoints[0].endpoint_prst_k==before.issuers[0].endpoints[0].endpoint_prst_k);
    rig.device.endpoint.scenario.serve_cryptogram=true;
    REQUIRE(std::get<2>(authenticate(rig.reader,rig.device.endpoint))==kFlowFAST);
    ReaderDataManager rebooted;REQUIRE(rebooted.begin());
    aliro::instance()=aliro::AliroDelegate{};
    aliro::instance().setReaderDataSink(&rebooted);
    doorlock_stubs_load_from_nvs();replay_aliro_credentials_to_aliro_delegate();
    const auto restored=rebooted.getReaderDataCopy();
    REQUIRE(restored.reader_id==before.reader_id);
    REQUIRE(restored.issuers[0].endpoints[0].endpoint_id==before.issuers[0].endpoints[0].endpoint_id);
    REQUIRE(restored.issuers[0].endpoints[0].endpoint_prst_k==before.issuers[0].endpoints[0].endpoint_prst_k);
    REQUIRE(std::get<2>(authenticate(rebooted,rig.device.endpoint))==kFlowFAST);
    REQUIRE(rig.user(UserStatusEnum::kOccupiedDisabled));
    const auto disabled=rebooted.getReaderDataCopy();
    REQUIRE_FALSE(aliro_user_access_allowed(disabled,disabled.issuers[0].issuer_id,disabled.issuers[0].endpoints[0].endpoint_id));
    REQUIRE(rig.put(endpointType,16,{},DlCredentialStatus::kAvailable));
    REQUIRE(std::get<2>(authenticate(rebooted,rig.device.endpoint))==kFlowFailed);
    aliro::instance().setReaderDataSink(nullptr);
}
