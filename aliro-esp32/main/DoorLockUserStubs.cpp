// In-RAM (+ NVS-persistent) user/credential storage for the DoorLock cluster.
//
// Apple Home iOS 26.x reads back every stored user and credential after
// each accessory reconnection (CASE session re-establishment, especially
// after a device reboot). If a user/credential it provisioned during
// commissioning is missing on read-back, Apple considers the accessory
// "broken" and either tears down the fabric or stalls the Home UI on
// "Access Codes / Setting up Home Key" (the symptom we hit). Therefore
// users and credentials must persist across reboots.
//
// Storage layout: one NVS blob per slot, keyed `u<idx>` (1..5) and
// `c<type><idx>` (e.g. `c0601` for type=6 idx=1). Per-blob saves keep the
// per-write cost low; full-array load happens once at boot.

#include <app/clusters/door-lock-server/door-lock-server.h>
#include <app/reporting/reporting.h>
#include <clusters/DoorLock/AttributeIds.h>
#include <clusters/DoorLock/ClusterId.h>
#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <string.h>
#include <algorithm>
#include <stdio.h>

#include "AliroDelegate.h"
#include "DoorLockAccessPolicy.hpp"
#include "ReaderCredentialOps.hpp"
#include "UserAccessPolicy.hpp"
#include "DoorLockStorageLimits.hpp"
#include "P256Validation.hpp"

using chip::ByteSpan;
using chip::CharSpan;
using chip::EndpointId;
using chip::FabricIndex;
using chip::Span;
using chip::kUndefinedFabricIndex;
using chip::app::Clusters::DoorLock::CredentialRuleEnum;
using chip::app::Clusters::DoorLock::CredentialTypeEnum;
using chip::app::Clusters::DoorLock::UserStatusEnum;
using chip::app::Clusters::DoorLock::UserTypeEnum;

static const char *TAG = "doorlock-stubs";

namespace {

constexpr size_t kMaxUsers          = aliro::storage::users;
constexpr size_t kMaxCredsPerUser   = aliro::storage::credentials_per_user;
constexpr size_t kUserNameBuffer    = 32;
constexpr size_t kMaxCredTypes      = 10;     // covers PIN(1), RFID(2), Aliro 6/7/8
constexpr size_t kMaxCredsPerType   = aliro::storage::credential_slots_per_type;
constexpr size_t kCredDataBuffer    = 96;     // Aliro endpoint pubkey is 65 B
constexpr const char * kNs          = "dl_store";

struct StoredUser {
    bool                occupied = false;
    char                name[kUserNameBuffer]{};
    size_t              name_len = 0;
    uint32_t            uniqueId = 0;
    UserStatusEnum      userStatus = UserStatusEnum::kAvailable;
    UserTypeEnum        userType = UserTypeEnum::kUnrestrictedUser;
    CredentialRuleEnum  credentialRule = CredentialRuleEnum::kSingle;
    CredentialStruct    creds[kMaxCredsPerUser]{};
    size_t              cred_count = 0;
    FabricIndex         createdBy = kUndefinedFabricIndex;
    FabricIndex         lastModifiedBy = kUndefinedFabricIndex;
};

struct StoredCredential {
    bool                  occupied = false;
    CredentialTypeEnum    credentialType = CredentialTypeEnum::kProgrammingPIN;
    uint8_t               data[kCredDataBuffer]{};
    size_t                data_len = 0;
    FabricIndex           createdBy = kUndefinedFabricIndex;
    FabricIndex           lastModifiedBy = kUndefinedFabricIndex;
};

static StoredUser        g_users[kMaxUsers];
static StoredCredential  g_creds[kMaxCredTypes][kMaxCredsPerType];

bool nvs_open_rw(nvs_handle_t & h)
{
    esp_err_t err = nvs_open(kNs, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(%s) failed: 0x%x", kNs, err);
        return false;
    }
    return true;
}

void user_key(uint16_t idx, char * out, size_t cap)
{
    snprintf(out, cap, "u%u", (unsigned)idx);
}

void cred_key(CredentialTypeEnum t, uint16_t idx, char * out, size_t cap)
{
    snprintf(out, cap, "c%02u%02u", (unsigned)static_cast<uint8_t>(t), (unsigned)idx);
}

bool persist_user(uint16_t userIndex, const StoredUser & u)
{
    nvs_handle_t h;
    if (!nvs_open_rw(h)) return false;
    char k[12];
    user_key(userIndex, k, sizeof(k));
    esp_err_t err = u.occupied ? nvs_set_blob(h, k, &u, sizeof(u)) : nvs_erase_key(h, k);
    if (err == ESP_ERR_NVS_NOT_FOUND && !u.occupied) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

bool persist_cred(CredentialTypeEnum t, uint16_t idx, const StoredCredential & c)
{
    nvs_handle_t h;
    if (!nvs_open_rw(h)) return false;
    char k[12];
    cred_key(t, idx, k, sizeof(k));
    esp_err_t err = c.occupied ? nvs_set_blob(h, k, &c, sizeof(c)) : nvs_erase_key(h, k);
    if (err == ESP_ERR_NVS_NOT_FOUND && !c.occupied) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

// Index helpers. userIndex/credIndex are 1-based per spec, except
// Programming PIN credIndex which is 0.
size_t userSlot(uint16_t userIndex)
{
    return (userIndex >= 1 && userIndex <= kMaxUsers) ? (userIndex - 1) : kMaxUsers;
}

size_t credTypeIndex(CredentialTypeEnum t)
{
    auto v = static_cast<uint8_t>(t);
    return (v < kMaxCredTypes) ? v : kMaxCredTypes;
}

size_t credSlot(CredentialTypeEnum t, uint16_t credentialIndex)
{
    if (t == CredentialTypeEnum::kProgrammingPIN) {
        return (credentialIndex == 0) ? 0 : kMaxCredsPerType;
    }
    return (credentialIndex >= 1 && credentialIndex <= kMaxCredsPerType)
               ? (credentialIndex - 1)
               : kMaxCredsPerType;
}

// One-shot loader: walks every possible slot and pulls whatever is in NVS.
void load_all_from_nvs()
{
    nvs_handle_t h;
    if (!nvs_open_rw(h)) return;

    size_t loaded_users = 0, loaded_creds = 0;

    for (uint16_t i = 1; i <= kMaxUsers; ++i) {
        char k[12]; user_key(i, k, sizeof(k));
        size_t len = sizeof(StoredUser);
        if (nvs_get_blob(h, k, &g_users[i - 1], &len) == ESP_OK &&
            len == sizeof(StoredUser) && g_users[i - 1].occupied) {
            loaded_users++;
        } else {
            // Not present or stale-sized: zero-init the slot.
            g_users[i - 1] = StoredUser{};
        }
    }

    for (size_t t = 0; t < kMaxCredTypes; ++t) {
        for (size_t s = 0; s < kMaxCredsPerType; ++s) {
            // Use the natural credentialIndex for that type (1-based normally,
            // 0-based for ProgrammingPIN at type==0 / s==0).
            uint16_t idx = (t == 0 && s == 0) ? 0 : static_cast<uint16_t>(s + 1);
            char k[12]; cred_key(static_cast<CredentialTypeEnum>(t), idx, k, sizeof(k));
            size_t len = sizeof(StoredCredential);
            if (nvs_get_blob(h, k, &g_creds[t][s], &len) == ESP_OK &&
                len == sizeof(StoredCredential) && g_creds[t][s].occupied) {
                loaded_creds++;
            } else {
                g_creds[t][s] = StoredCredential{};
            }
        }
    }
    nvs_close(h);
    ESP_LOGI(TAG, "NVS load: %u users, %u credentials restored",
             (unsigned)loaded_users, (unsigned)loaded_creds);
}

// NOTE: the previous design used a static-init `LoaderInit` to call
// load_all_from_nvs() at C++ runtime startup. That ran BEFORE
// nvs_flash_init() in app_main, so nvs_open returned ESP_ERR_NVS_NOT_INITIALIZED
// and the load early-returned silently — leaving every credential unset
// across reboots even when SetCredential had previously persisted them
// (the symptom: `Aliro credentials replayed: 0 issuer key(s), 0 endpoint key(s)`
// at the start of every fresh boot, even after a successful provisioning
// cycle on the prior boot). Loader is now driven explicitly from app_main
// AFTER nvs_flash_init via the `doorlock_stubs_load_from_nvs()` symbol below.

}  // namespace

void doorlock_stubs_load_from_nvs()
{
    load_all_from_nvs();
}

// Replay every persisted Aliro credential into AliroDelegate's readerData_t
// mirror. Called from app_main *after* AliroDelegate.setReaderDataSink() so
// the in-RAM ReaderDataManager picks up issuer + endpoint keys provisioned
// in a previous session — without this, every reboot loses the readerData
// issuers[] (we only NVS the DoorLock-cluster blob, not DDK's view) and the
// next iPhone tap fails auth until Apple Home re-runs SetCredential.
void replay_aliro_credentials_to_aliro_delegate()
{
    using chip::app::Clusters::DoorLock::CredentialTypeEnum;
    auto issuerType  = static_cast<size_t>(CredentialTypeEnum::kAliroCredentialIssuerKey);
    auto evictType   = static_cast<size_t>(CredentialTypeEnum::kAliroEvictableEndpointKey);
    auto nonevType   = static_cast<size_t>(CredentialTypeEnum::kAliroNonEvictableEndpointKey);

    size_t replayed_issuers = 0, replayed_endpoints = 0;

    auto replay = [&](size_t typeIdx, void (*fn)(uint16_t, chip::ByteSpan)) {
        if (typeIdx >= kMaxCredTypes) return;
        for (size_t s = 0; s < kMaxCredsPerType; ++s) {
            auto & c = g_creds[typeIdx][s];
            if (!c.occupied || c.data_len == 0) continue;
            chip::ByteSpan span(c.data, c.data_len);
            uint16_t credentialIndex = static_cast<uint16_t>(s + 1);
            fn(credentialIndex, span);
        }
    };

    auto issuer_fn = [](uint16_t idx, chip::ByteSpan span) {
        aliro::instance().syncCredentialIssuerKey(idx, span);
    };
    auto evict_fn = [](uint16_t idx, chip::ByteSpan span) {
        aliro::instance().syncCredentialEndpointKey(idx, span, /*evictable=*/true);
    };
    auto nonev_fn = [](uint16_t idx, chip::ByteSpan span) {
        aliro::instance().syncCredentialEndpointKey(idx, span, /*evictable=*/false);
    };

    // Issuer keys first (so endpoint sync finds a non-empty issuers[0]).
    replay(issuerType,  static_cast<void(*)(uint16_t, chip::ByteSpan)>(issuer_fn));
    replay(evictType,   static_cast<void(*)(uint16_t, chip::ByteSpan)>(evict_fn));
    replay(nonevType,   static_cast<void(*)(uint16_t, chip::ByteSpan)>(nonev_fn));
    std::vector<aliro::credentials::Provisioned> live;
    for (uint8_t type : {aliro::credentials::kIssuer, aliro::credentials::kEvictable,
                         aliro::credentials::kNonEvictable}) {
        for (size_t slot = 0; slot < kMaxCredsPerType; ++slot) {
            const auto& credential = g_creds[type][slot];
            if (!credential.occupied || credential.data_len == 0) continue;
            live.push_back({type, static_cast<uint16_t>(slot + 1),
                {credential.data, credential.data + credential.data_len}});
        }
    }
    if (!aliro::instance().reconcileCredentials(live))
        ESP_LOGE(TAG, "Aliro credential reconciliation could not be persisted");

    // Count for log line.
    for (size_t s = 0; s < kMaxCredsPerType; ++s) {
        if (issuerType < kMaxCredTypes && g_creds[issuerType][s].occupied) replayed_issuers++;
        if (evictType  < kMaxCredTypes && g_creds[evictType][s].occupied)  replayed_endpoints++;
        if (nonevType  < kMaxCredTypes && g_creds[nonevType][s].occupied)  replayed_endpoints++;
    }
    ESP_LOGI(TAG, "Aliro credentials replayed to ReaderDataManager: "
                  "%u issuer key(s), %u endpoint key(s)",
             (unsigned)replayed_issuers, (unsigned)replayed_endpoints);
}

bool emberAfPluginDoorLockGetUser(EndpointId endpointId, uint16_t userIndex,
                                  EmberAfPluginDoorLockUserInfo & user)
{
    size_t s = userSlot(userIndex);
    if (s >= kMaxUsers) return false;
    StoredUser & u = g_users[s];

    user.userName           = CharSpan(u.name, u.name_len);
    user.credentials        = Span<const CredentialStruct>(u.creds, u.cred_count);
    user.userUniqueId       = u.uniqueId;
    user.userStatus         = u.userStatus;
    user.userType           = u.userType;
    user.credentialRule     = u.credentialRule;
    user.creationSource     = DlAssetSource::kMatterIM;
    user.createdBy          = u.createdBy;
    user.modificationSource = DlAssetSource::kMatterIM;
    user.lastModifiedBy     = u.lastModifiedBy;

    ESP_LOGI(TAG, "GetUser ep=%u idx=%u -> %s (creds=%u)", endpointId, userIndex,
             u.occupied ? "OCCUPIED" : "available", (unsigned)u.cred_count);
    return true;
}

bool emberAfPluginDoorLockSetUser(EndpointId endpointId, uint16_t userIndex,
                                  FabricIndex creator, FabricIndex modifier,
                                  const CharSpan & userName, uint32_t uniqueId,
                                  UserStatusEnum userStatus, UserTypeEnum userType,
                                  CredentialRuleEnum credentialRule,
                                  const CredentialStruct * credentials, size_t totalCredentials)
{
    size_t s = userSlot(userIndex);
    if (s >= kMaxUsers) {
        ESP_LOGE(TAG, "SetUser ep=%u idx=%u -> out-of-range", endpointId, userIndex);
        return false;
    }
    const StoredUser previous = g_users[s];
    StoredUser u = previous;
    if (userName.size() > kUserNameBuffer || (userName.size() && !userName.data()) ||
        totalCredentials > kMaxCredsPerUser ||
        (totalCredentials && credentials == nullptr)) return false;

    u.occupied   = (userStatus != UserStatusEnum::kAvailable);
    u.name_len   = std::min(userName.size(), kUserNameBuffer);
    if (u.name_len) memcpy(u.name, userName.data(), u.name_len);
    u.uniqueId       = uniqueId;
    u.userStatus     = userStatus;
    u.userType       = userType;
    u.credentialRule = credentialRule;
    u.cred_count     = std::min(totalCredentials, kMaxCredsPerUser);
    for (size_t i = 0; i < u.cred_count; ++i) u.creds[i] = credentials[i];
    if (u.createdBy == kUndefinedFabricIndex) u.createdBy = creator;
    u.lastModifiedBy = modifier;

    if (!persist_user(userIndex, u)) {
        // A failed grant must not publish new permissions in RAM. A requested
        // disable/clear still keeps the previous credential assignments denied.
        if (previous.occupied && userStatus != UserStatusEnum::kOccupiedEnabled) {
            g_users[s] = previous;
            g_users[s].userStatus = UserStatusEnum::kOccupiedDisabled;
        }
        return false;
    }
    g_users[s] = u;

    // Apple Home iOS 26 subscribes after CommissioningComplete; the UI on
    // "Access Codes" / "Setting up Home Key" advances only when it sees a
    // ReportData on a watched attribute. Without these explicit change
    // notifications iOS waits for its subscribe min-interval to fire and
    // can stall for minutes — long enough that the user gives up.
    {
        namespace DL = chip::app::Clusters::DoorLock;
        MatterReportingAttributeChangeCallback(endpointId, DL::Id,
            DL::Attributes::NumberOfTotalUsersSupported::Id);
    }

    ESP_LOGI(TAG, "SetUser ep=%u idx=%u status=%u type=%u creds=%u (stored+nvs)",
             endpointId, userIndex, (unsigned)userStatus, (unsigned)userType,
             (unsigned)u.cred_count);
    return true;
}

bool emberAfPluginDoorLockGetCredential(EndpointId endpointId, uint16_t credentialIndex,
                                        CredentialTypeEnum credentialType,
                                        EmberAfPluginDoorLockCredentialInfo & credential)
{
    size_t t = credTypeIndex(credentialType);
    size_t s = credSlot(credentialType, credentialIndex);
    if (t >= kMaxCredTypes || s >= kMaxCredsPerType) return false;
    StoredCredential & c = g_creds[t][s];

    credential.status             = c.occupied ? DlCredentialStatus::kOccupied : DlCredentialStatus::kAvailable;
    credential.credentialType     = credentialType;
    credential.credentialData     = ByteSpan(c.data, c.data_len);
    credential.creationSource     = DlAssetSource::kMatterIM;
    credential.createdBy          = c.createdBy;
    credential.modificationSource = DlAssetSource::kMatterIM;
    credential.lastModifiedBy     = c.lastModifiedBy;

    ESP_LOGI(TAG, "GetCredential ep=%u idx=%u type=%u -> %s (data=%uB)",
             endpointId, credentialIndex, (unsigned)credentialType,
             c.occupied ? "OCCUPIED" : "available", (unsigned)c.data_len);
    return true;
}

bool emberAfPluginDoorLockSetCredential(EndpointId endpointId, uint16_t credentialIndex,
                                        FabricIndex creator, FabricIndex modifier,
                                        DlCredentialStatus credentialStatus,
                                        CredentialTypeEnum credentialType,
                                        const ByteSpan & credentialData)
{
    size_t t = credTypeIndex(credentialType);
    size_t s = credSlot(credentialType, credentialIndex);
    if (t >= kMaxCredTypes || s >= kMaxCredsPerType) {
        ESP_LOGE(TAG, "SetCredential ep=%u idx=%u type=%u -> out-of-range",
                 endpointId, credentialIndex, (unsigned)credentialType);
        return false;
    }
    StoredCredential & c = g_creds[t][s];

    const StoredCredential previous = c;
    if (credentialData.size() > kCredDataBuffer || (credentialData.size() && !credentialData.data())) return false;
    const bool aliroKey = credentialType == CredentialTypeEnum::kAliroCredentialIssuerKey ||
        credentialType == CredentialTypeEnum::kAliroEvictableEndpointKey ||
        credentialType == CredentialTypeEnum::kAliroNonEvictableEndpointKey;
    if (aliroKey && credentialStatus == DlCredentialStatus::kOccupied &&
        !aliro::keys::validPublicKey(credentialData.data(),credentialData.size())) return false;
    c.occupied       = (credentialStatus == DlCredentialStatus::kOccupied);
    c.credentialType = credentialType;
    c.data_len       = std::min(credentialData.size(), kCredDataBuffer);
    if (c.data_len) memcpy(c.data, credentialData.data(), c.data_len);
    if (c.createdBy == kUndefinedFabricIndex) c.createdBy = creator;
    c.lastModifiedBy = modifier;

    bool persisted = persist_cred(credentialType, credentialIndex, c);
    if (!persisted && c.occupied) {
        c = previous;
        return false;
    }

    // Mirror Aliro public keys into the DDK readerData_t — without this
    // step DDK FastAuth/StandardAuth has no issuer or endpoint key to
    // verify a tapping phone's signature against, and every real iPhone
    // tap logs `HomeKey authentication failed` even though the credential
    // was successfully provisioned through Matter.
    if (c.occupied && c.data_len > 0) {
        chip::ByteSpan span(c.data, c.data_len);
        if (credentialType == CredentialTypeEnum::kAliroCredentialIssuerKey) {
            persisted = aliro::instance().syncCredentialIssuerKey(credentialIndex, span) && persisted;
        } else if (credentialType == CredentialTypeEnum::kAliroEvictableEndpointKey) {
            persisted = aliro::instance().syncCredentialEndpointKey(credentialIndex, span, /*evictable=*/true) && persisted;
        } else if (credentialType == CredentialTypeEnum::kAliroNonEvictableEndpointKey) {
            persisted = aliro::instance().syncCredentialEndpointKey(credentialIndex, span, /*evictable=*/false) && persisted;
        }
    } else {
        // Cleared credential — wipe the matching readerData entry too.
        if (credentialType == CredentialTypeEnum::kAliroCredentialIssuerKey ||
            credentialType == CredentialTypeEnum::kAliroEvictableEndpointKey ||
            credentialType == CredentialTypeEnum::kAliroNonEvictableEndpointKey) {
            const bool removed = aliro::instance().clearCredential(credentialIndex,
                static_cast<uint8_t>(credentialType), chip::ByteSpan(previous.data, previous.data_len));
            persisted = persisted && removed;
        }
    }

    if (!persisted && c.occupied) {
        // A failed NFC mirror must not leave a successfully enumerable grant
        // in the Matter table. Restore the old per-slot record where possible.
        c = previous;
        if (!persist_cred(credentialType,credentialIndex,c))
            ESP_LOGE(TAG,"Credential rollback could not be persisted");
        return false;
    }

    // Trigger ReportData to wake up the iOS subscription (see SetUser
    // comment above). For Aliro credential issuer/endpoint key changes,
    // poke the matching counter attribute; for PIN/RFID, the user-side
    // counter is the relevant one.
    namespace DL = chip::app::Clusters::DoorLock;
    if (credentialType == CredentialTypeEnum::kAliroCredentialIssuerKey) {
        MatterReportingAttributeChangeCallback(endpointId, DL::Id,
            DL::Attributes::NumberOfAliroCredentialIssuerKeysSupported::Id);
    } else if (credentialType == CredentialTypeEnum::kAliroEvictableEndpointKey ||
               credentialType == CredentialTypeEnum::kAliroNonEvictableEndpointKey) {
        MatterReportingAttributeChangeCallback(endpointId, DL::Id,
            DL::Attributes::NumberOfAliroEndpointKeysSupported::Id);
    } else {
        MatterReportingAttributeChangeCallback(endpointId, DL::Id,
            DL::Attributes::NumberOfTotalUsersSupported::Id);
    }

    ESP_LOGI(TAG, "SetCredential ep=%u idx=%u type=%u status=%u (stored+nvs %uB)",
             endpointId, credentialIndex, (unsigned)credentialType,
             (unsigned)credentialStatus, (unsigned)c.data_len);
    return persisted;
}

bool aliro_user_access_allowed(const readerData_t& data,
    const std::vector<uint8_t>& issuerId, const std::vector<uint8_t>& endpointId)
{
    const auto* endpoint = aliro::credentials::find(data, issuerId, endpointId);
    if (!endpoint) return false;
    uint16_t issuerIndex = 0;
    for (const auto& issuer : data.issuers)
        if (issuer.issuer_id == issuerId) issuerIndex = issuer.credential_index;
    aliro::access::Assignment assignments[kMaxUsers * kMaxCredsPerUser]{};
    size_t count = 0;
    for (const auto& user : g_users) {
        for (size_t i = 0; i < user.cred_count; ++i) {
            const auto& credential = user.creds[i];
            const auto type = static_cast<uint8_t>(credential.credentialType);
            const bool enabled = user.occupied && user.userStatus == UserStatusEnum::kOccupiedEnabled &&
                user.userType == UserTypeEnum::kUnrestrictedUser &&
                user.credentialRule == CredentialRuleEnum::kSingle;
            assignments[count++] = {type, credential.credentialIndex, enabled};
        }
    }
    // Existing iPhone credentials can be issuer-provisioned without a user
    // assignment. Preserve that path, while honoring explicit assignments.
    return aliro::access::allowed(issuerIndex, endpoint->credential_type,
        endpoint->credential_index, assignments, count);
}
