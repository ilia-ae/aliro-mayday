#include "AliroDelegate.h"
#include "Diag.h"
#include "ReaderDataManager.hpp"
#include "NfcManagerBridge.hpp"
#include "DoorLockStorageLimits.hpp"
#include "ReaderConfigStore.hpp"
#include "DDKReaderData.h"
#include "ReaderCredentialOps.hpp"
#include "ReaderIdentityOps.hpp"

#include <app/reporting/reporting.h>
#include <clusters/DoorLock/AttributeIds.h>
#include <clusters/DoorLock/ClusterId.h>
#include <esp_log.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <string.h>

namespace lockind { void set_locked(); }

static const char *TAG = "aliro";

namespace aliro {

namespace {

// Aliro Expedited Transaction protocol versions we support.
// Spec §7 declares version 0x0100 as the only Aliro 1.0 protocol version.
// Apple Home reads this list before sending SetAliroReaderConfig — if it
// is empty, Apple gives up.
constexpr uint8_t kSupportedExpeditedVersions[][2] = {
    { 0x01, 0x00 },   // Aliro v1.0 expedited transaction
};

constexpr size_t kSupportedExpeditedVersionCount =
    sizeof(kSupportedExpeditedVersions) / sizeof(kSupportedExpeditedVersions[0]);

// NVS namespace + keys. Apple Home iOS 26 reads back every Aliro attribute
// after CommissioningComplete *and* after every accessory reconnection;
// any mismatch between the value Apple saved at provisioning and what we
// return now stalls the Home app on "Access Codes" / "Setting up Home Key".
// Therefore *every* piece of Aliro reader state must be persistent.
constexpr const char * kNs       = "aliro_cfg";
constexpr const char * kKeySubId = "subId";        // 16 B blob

bool nvs_open_rw(nvs_handle_t & h)
{
    esp_err_t err = nvs_open(kNs, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(%s) failed: 0x%x", kNs, err);
        return false;
    }
    return true;
}


}  // namespace

AliroDelegate::AliroDelegate()
{
    nvs_handle_t h;
    bool nvs_ok = nvs_open_rw(h);

    // Retain a valid lifetime sub-ID exactly. An unreadable existing value
    // must not be replaced with a new random identity during an update.
    if (nvs_ok) {
        size_t length = sizeof(reader_group_sub_identifier_);
        auto result = nvs_get_blob(h, kKeySubId, reader_group_sub_identifier_, &length);
        if (result == ESP_OK && length == sizeof(reader_group_sub_identifier_)) {
            sub_identifier_persisted_ = true;
        } else if (result == ESP_ERR_NVS_NOT_FOUND) {
            esp_fill_random(reader_group_sub_identifier_, sizeof(reader_group_sub_identifier_));
            result = nvs_set_blob(h, kKeySubId, reader_group_sub_identifier_, sizeof(reader_group_sub_identifier_));
            if (result == ESP_OK) result = nvs_commit(h);
            sub_identifier_persisted_ = result == ESP_OK;
        }
        nvs_close(h);
    }
    reader_config::Config config;
    const auto loaded = reader_config::load(config);
    reader_config_read_ok_ = loaded == reader_config::Load::Valid || loaded == reader_config::Load::Empty;
    reader_config_valid_ = loaded == reader_config::Load::Valid;
    if (reader_config_valid_) {
        std::copy(config.signing.begin(), config.signing.end(), reader_signing_key_);
        std::copy(config.verification.begin(), config.verification.end(), reader_verification_key_);
        reader_verification_key_len_ = sizeof(reader_verification_key_);
        std::copy(config.group.begin(), config.group.end(), reader_group_identifier_);
        reader_group_id_set_ = true;
        std::copy(config.resolving.begin(), config.resolving.end(), group_resolving_key_);
        group_resolving_key_set_ = config.has_resolving_key;
    }
    ESP_LOGI(TAG, "[delegate] reader config valid=%d stable-sub-id=%d storage-ok=%d",
        reader_config_valid_, sub_identifier_persisted_, reader_config_read_ok_);
}

CHIP_ERROR AliroDelegate::GetAliroReaderVerificationKey(chip::MutableByteSpan & out)
{
    if (!reader_config_read_ok_) return CHIP_ERROR_INTERNAL;
    if (reader_verification_key_len_ == 0) {
        // Not provisioned — return empty span so Apple sees "no reader yet".
        out.reduce_size(0);
        return CHIP_NO_ERROR;
    }
    if (out.size() < reader_verification_key_len_) {
        return CHIP_ERROR_BUFFER_TOO_SMALL;
    }
    memcpy(out.data(), reader_verification_key_, reader_verification_key_len_);
    out.reduce_size(reader_verification_key_len_);
    return CHIP_NO_ERROR;
}

CHIP_ERROR AliroDelegate::GetAliroReaderGroupIdentifier(chip::MutableByteSpan & out)
{
    if (!reader_group_id_set_) {
        out.reduce_size(0);
        return CHIP_NO_ERROR;
    }
    if (out.size() < sizeof(reader_group_identifier_)) {
        return CHIP_ERROR_BUFFER_TOO_SMALL;
    }
    memcpy(out.data(), reader_group_identifier_, sizeof(reader_group_identifier_));
    out.reduce_size(sizeof(reader_group_identifier_));
    return CHIP_NO_ERROR;
}

CHIP_ERROR AliroDelegate::GetAliroReaderGroupSubIdentifier(chip::MutableByteSpan & out)
{
    if (!sub_identifier_persisted_) return CHIP_ERROR_INTERNAL;
    if (out.size() < sizeof(reader_group_sub_identifier_)) {
        return CHIP_ERROR_BUFFER_TOO_SMALL;
    }
    memcpy(out.data(), reader_group_sub_identifier_,
           sizeof(reader_group_sub_identifier_));
    out.reduce_size(sizeof(reader_group_sub_identifier_));
    return CHIP_NO_ERROR;
}

CHIP_ERROR AliroDelegate::GetAliroExpeditedTransactionSupportedProtocolVersionAtIndex(
    size_t index, chip::MutableByteSpan & out)
{
    if (index >= kSupportedExpeditedVersionCount) {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
    if (out.size() < 2) {
        return CHIP_ERROR_BUFFER_TOO_SMALL;
    }
    memcpy(out.data(), kSupportedExpeditedVersions[index], 2);
    out.reduce_size(2);
    return CHIP_NO_ERROR;
}

CHIP_ERROR AliroDelegate::GetAliroGroupResolvingKey(chip::MutableByteSpan & out)
{
    // Only meaningful when the ALBU (BLE+UWB) feature is on — we don't enable
    // ALBU. Returning an empty span is the spec-compliant "not provisioned".
    if (!group_resolving_key_set_) {
        out.reduce_size(0);
        return CHIP_NO_ERROR;
    }
    if (out.size() < sizeof(group_resolving_key_)) {
        return CHIP_ERROR_BUFFER_TOO_SMALL;
    }
    memcpy(out.data(), group_resolving_key_, sizeof(group_resolving_key_));
    out.reduce_size(sizeof(group_resolving_key_));
    return CHIP_NO_ERROR;
}

CHIP_ERROR AliroDelegate::GetAliroSupportedBLEUWBProtocolVersionAtIndex(
    size_t /*index*/, chip::MutableByteSpan & /*out*/)
{
    // ALBU not enabled — list is empty.
    return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
}

uint8_t AliroDelegate::GetAliroBLEAdvertisingVersion()
{
    // ALBU not enabled — return zero.
    return 0;
}

uint16_t AliroDelegate::GetNumberOfAliroCredentialIssuerKeysSupported()
{
    // Apple Home appears to want at least one slot. We support several so
    // multiple HomeKit Home users can each hold their own credential issuer.
    return storage::issuer_keys;
}

uint16_t AliroDelegate::GetNumberOfAliroEndpointKeysSupported()
{
    // Endpoint keys = per-device keys. 16 covers a small home (~5 people
    // with 2-3 Apple devices each). Spec only mandates >= 1.
    return storage::endpoint_keys;
}

CHIP_ERROR AliroDelegate::SetAliroReaderConfig(
    const chip::ByteSpan & signingKey,
    const chip::ByteSpan & verificationKey,
    const chip::ByteSpan & groupIdentifier,
    const chip::Optional<chip::ByteSpan> & groupResolvingKey)
{
    ESP_LOGI(TAG, "[delegate] SetAliroReaderConfig: signKey=%uB verifyKey=%uB groupId=%uB grpResolv=%s",
             (unsigned)signingKey.size(),
             (unsigned)verificationKey.size(),
             (unsigned)groupIdentifier.size(),
             groupResolvingKey.HasValue() ? "yes" : "no");

    if (!sub_identifier_persisted_) return CHIP_ERROR_INTERNAL;
    // NFC-only reader: the server rejects a GroupResolvingKey without ALBU.
    // Validate the delegate boundary as well as the incoming Matter command.
    if (signingKey.size() != 32 || verificationKey.size() != 65 || groupIdentifier.size() != 16 || !groupIdentifier.data() ||
        groupResolvingKey.HasValue() || !keys::matchingKeyPair(signingKey.data(), signingKey.size(),
                                                            verificationKey.data(), verificationKey.size()))
        return CHIP_ERROR_INVALID_ARGUMENT;
    reader_config::Config previous;
    previous.configured = reader_config_valid_;
    previous.has_resolving_key = group_resolving_key_set_;
    std::copy_n(reader_signing_key_,32,previous.signing.begin());
    std::copy_n(reader_verification_key_,65,previous.verification.begin());
    std::copy_n(reader_group_identifier_,16,previous.group.begin());
    std::copy_n(group_resolving_key_,16,previous.resolving.begin());
    const bool previously_synced = reader_data_synced_;
    reader_config::Config config;
    config.configured = true;
    std::copy(signingKey.begin(), signingKey.end(), config.signing.begin());
    std::copy(verificationKey.begin(), verificationKey.end(), config.verification.begin());
    std::copy(groupIdentifier.begin(), groupIdentifier.end(), config.group.begin());
    if (!reader_config::save(config)) return CHIP_ERROR_INTERNAL;
    std::copy(config.signing.begin(), config.signing.end(), reader_signing_key_);
    std::copy(config.verification.begin(), config.verification.end(), reader_verification_key_);
    reader_verification_key_len_ = 65;
    std::copy(config.group.begin(), config.group.end(), reader_group_identifier_);
    reader_group_id_set_ = true;
    group_resolving_key_set_ = false;
    memset(group_resolving_key_, 0, sizeof(group_resolving_key_));
    reader_config_valid_ = true;
    reader_config_read_ok_ = true;

    // Mirror the keys into the rednblkx ReaderDataManager so NfcManager's
    // DDK auth precompute can build an authentication context. Without
    // this NfcManager keeps logging "reader not provisioned yet, retrying…".
    reader_data_synced_ = syncToReaderDataManager();
    if (!reader_data_synced_) {
        // The NFC store rejected the update. Restore the previous reader
        // record so an unconfigured commissioner can retry after the error.
        const bool rolled_back = reader_config::save(previous);
        std::copy(previous.signing.begin(),previous.signing.end(),reader_signing_key_);
        std::copy(previous.verification.begin(),previous.verification.end(),reader_verification_key_);
        reader_verification_key_len_ = previous.configured ? 65 : 0;
        std::copy(previous.group.begin(),previous.group.end(),reader_group_identifier_);
        reader_group_id_set_ = previous.configured;
        std::copy(previous.resolving.begin(),previous.resolving.end(),group_resolving_key_);
        group_resolving_key_set_ = previous.has_resolving_key;
        reader_config_valid_ = previous.configured;
        reader_config_read_ok_ = rolled_back;
        reader_data_synced_ = rolled_back && previously_synced;
        nfc::invalidateReader(nfc_manager_);
        return CHIP_ERROR_INTERNAL;
    }
    diag::note_aliro_phase("SetReaderConfig",
        groupResolvingKey.HasValue() ? "with-grpResolv" : "no-grpResolv");
    ESP_LOGI(TAG, "[delegate] SetAliroReaderConfig OK — reader provisioned");
    // Reader keys provisioned → LED leaves the violet "paired-no-keys"
    // state for solid orange ("locked"). LockDoor/UnlockDoor will toggle
    // between orange and green.
    lockind::set_locked();


    // ECP polling data and auth-cache must both be refreshed after every
    // key change. Upstream HomeKey-ESP32 wires this through HK_BUS_TOPIC
    // which we don't run; do it directly via the NfcManager pointer.
    if (nfc_manager_ != nullptr) {
        nfc::refreshReader(nfc_manager_);
        ESP_LOGI(TAG, "[delegate] NfcManager kicked: ECP data + auth cache refreshed");
    }

    // Wake the iOS subscriber so it sees the new reader config without
    // waiting for its min-interval. The DoorLock endpoint is fixed at 1.
    {
        namespace DL = chip::app::Clusters::DoorLock;
        MatterReportingAttributeChangeCallback(1, DL::Id,
            DL::Attributes::AliroReaderVerificationKey::Id);
        MatterReportingAttributeChangeCallback(1, DL::Id,
            DL::Attributes::AliroReaderGroupIdentifier::Id);
        MatterReportingAttributeChangeCallback(1, DL::Id,
            DL::Attributes::AliroReaderGroupSubIdentifier::Id);
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR AliroDelegate::ClearAliroReaderConfig()
{
    ESP_LOGW(TAG, "[delegate] ClearAliroReaderConfig — wiping reader keys");
    memset(reader_signing_key_, 0, sizeof(reader_signing_key_));
    memset(reader_verification_key_, 0, sizeof(reader_verification_key_));
    reader_verification_key_len_ = 0;
    memset(reader_group_identifier_, 0, sizeof(reader_group_identifier_));
    reader_group_id_set_ = false;
    memset(group_resolving_key_, 0, sizeof(group_resolving_key_));
    group_resolving_key_set_ = false;

    reader_config_valid_ = false;
    reader_data_synced_ = false;
    // A durable clear marker takes precedence over retained legacy fields.
    // The stable lifetime sub-ID is intentionally outside this record.
    const bool saved = reader_config::save({});
    reader_config_read_ok_ = saved;
    // On an explicitly requested clear, remove obsolete fields too so an
    // older firmware cannot rediscover a supposedly cleared reader config.
    const bool legacy_cleared = saved && reader_config::eraseLegacy();
    diag::note_aliro_phase("ClearReaderConfig", "");

    // Wipe the readerData_t mirror too — NfcManager auth precompute will
    // then go idle and stop holding stale ECC public keys in RAM.
    const bool mirror_cleared = reader_data_sink_ == nullptr || reader_data_sink_->deleteAllReaderData();
    if (nfc_manager_ != nullptr) {
        nfc::refreshReader(nfc_manager_);
    }
    return saved && mirror_cleared && legacy_cleared ? CHIP_NO_ERROR : CHIP_ERROR_INTERNAL;
}

void AliroDelegate::setReaderDataSink(ReaderDataManager * rdm)
{
    reader_data_sink_ = rdm;
    if (rdm != nullptr && reader_config_valid_ && sub_identifier_persisted_) {
        ESP_LOGI(TAG, "[delegate] reader data sink wired, initial sync");
        reader_data_synced_ = syncToReaderDataManager();
    }
}

void AliroDelegate::setNfcManager(NfcManager * nfc)
{
    nfc_manager_ = nfc;
    if (nfc != nullptr && isProvisioned()) {
        ESP_LOGI(TAG, "[delegate] NfcManager wired, refreshing ECP polling data");
        nfc::refreshReader(nfc);
    }
}

bool AliroDelegate::syncToReaderDataManager()
{
    if (reader_data_sink_ == nullptr) {
        ESP_LOGW(TAG, "[delegate] readerDataSink not wired yet — skipping sync");
        return true;
    }
    if (!reader_config_valid_ || !sub_identifier_persisted_) {
        ESP_LOGW(TAG, "[delegate] not provisioned — skipping sync");
        return false;
    }

    // Map AliroDelegate fields → DDK readerData_t. Field semantics (DDK
    // commit e592f5f3 introduced kAliro=0x00, kHomeKey=0x01 and threads
    // the type through every auth path):
    //   reader_sk    — 32 B  signing key (private)             ← AliroDelegate
    //   reader_pk    — 65 B  uncompressed P-256 public key     ← AliroDelegate
    //   reader_pk_x  — 32 B  X-coordinate (offset 1..33 of pk) — derived
    //   reader_gid   — 16 B  group identifier (Aliro)           ← AliroDelegate
    //                  HomeKey upstream uses 8 B; the kAliro path in
    //                  DDK FastAuth/StandardAuth handles 16 B correctly.
    //   reader_id    — 8 B   stable reader id                   ← derived
    //                  from first 8 B of the 16-byte sub-id; sub-id is
    //                  random-on-first-boot and persisted forever.
    readerData_t rd;
    rd.reader_sk.assign(reader_signing_key_,
                        reader_signing_key_ + sizeof(reader_signing_key_));
    rd.reader_pk.assign(reader_verification_key_,
                        reader_verification_key_ + reader_verification_key_len_);
    if (reader_verification_key_len_ == 65 && reader_verification_key_[0] == 0x04) {
        rd.reader_pk_x.assign(reader_verification_key_ + 1,
                              reader_verification_key_ + 33);
    }
    rd.reader_gid.assign(reader_group_identifier_,
                         reader_group_identifier_ + sizeof(reader_group_identifier_));
    // Aliro spec §8.3.3.2.1 Table 8-4 — AUTH0 tag 0x4D `reader_identifier`
    // is **32 bytes**: `reader_group_identifier (16 B) ||
    // reader_group_sub_identifier (16 B)`. AuthContext.cpp:92-94 builds
    // this by concatenating `reader_gid` and `reader_id`, so we MUST
    // pass the full 16-byte sub-identifier here. Previously we passed
    // 8 bytes (HomeKey style), which produced a 24-byte 0x4D and Apple
    // Wallet's Aliro applet returns `6984` ("reference data not found")
    // because its Access Credential lookup keys on the spec'd 16-byte
    // sub_id portion.
    rd.reader_id.assign(reader_group_sub_identifier_,
                        reader_group_sub_identifier_ + sizeof(reader_group_sub_identifier_));

    if (reader_data_sink_->mutateReaderData([&](readerData_t& existing) {
        credentials::mergeIdentity(existing, rd);
        // In particular, boot-time sync must not erase cached iPhone
        // endpoints or revocation tombstones loaded from SAVED_DATA.
    })) {
        ESP_LOGI(TAG, "[delegate] readerData_t mirrored to ReaderDataManager "
                      "(sk=%uB, pk=%uB, gid=%uB)",
                 (unsigned)rd.reader_sk.size(),
                 (unsigned)rd.reader_pk.size(),
                 (unsigned)rd.reader_gid.size());
        return true;
    } else {
        ESP_LOGE(TAG, "[delegate] ReaderDataManager::updateReaderData failed");
        return false;
    }
}

// ---------------------------------------------------------------------------
// Helpers for credential → readerData_t mirroring
// ---------------------------------------------------------------------------

namespace {

// 8-byte deterministic identifier derived from an uncompressed P-256
// public key. Matches what Apple Wallet's HomeKey applet computes when
// it sends an authenticated payload — without this, DDK can't look up
// the right issuer / endpoint to verify the signature against.
std::vector<uint8_t> derive_id_from_pk(const chip::ByteSpan & pk)
{
    uint8_t digest[32] = {};
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, /*is224=*/0);
    mbedtls_sha256_update(&ctx, pk.data(), pk.size());
    mbedtls_sha256_finish(&ctx, digest);
    mbedtls_sha256_free(&ctx);
    return std::vector<uint8_t>(digest, digest + 8);
}

// Validate a 65-byte uncompressed P-256 public key (`0x04 ‖ X(32) ‖ Y(32)`).
bool valid_uncompressed_pk(const chip::ByteSpan & pk)
{
    return keys::validPublicKey(pk.data(), pk.size());
}

}  // namespace

bool AliroDelegate::syncCredentialIssuerKey(uint16_t idx, chip::ByteSpan publicKey)
{
    if (!reader_data_sink_ || !valid_uncompressed_pk(publicKey)) return false;
    const std::vector<uint8_t> key(publicKey.begin(), publicKey.end());
    const auto id = derive_id_from_pk(publicKey);
    const bool saved = reader_data_sink_->mutateReaderData([&](readerData_t& data) {
        std::erase_if(data.issuers, [&](const hkIssuer_t& issuer) {
            return issuer.credential_index == idx && issuer.issuer_pk != key;
        });
        auto found = std::find_if(data.issuers.begin(), data.issuers.end(),
            [&](const hkIssuer_t& issuer) { return issuer.issuer_pk == key; });
        if (found == data.issuers.end()) {
            hkIssuer_t issuer;
            issuer.issuer_id = id;
            issuer.issuer_pk = key;
            issuer.issuer_pk_x.assign(key.begin() + 1, key.begin() + 33);
            data.issuers.push_back(std::move(issuer));
            found = std::prev(data.issuers.end());
        }
        found->credential_index = idx;
    });
    if (!saved) ESP_LOGE(TAG, "Issuer credential persistence failed");
    nfc::invalidateReader(nfc_manager_);
    return saved;
}

bool AliroDelegate::syncCredentialEndpointKey(uint16_t idx, chip::ByteSpan publicKey, bool evictable)
{
    if (!reader_data_sink_ || !valid_uncompressed_pk(publicKey)) return false;
    const std::vector<uint8_t> key(publicKey.begin(), publicKey.end());
    const auto id = derive_id_from_pk(publicKey);
    const uint8_t type = evictable ? credentials::kEvictable : credentials::kNonEvictable;
    const bool saved = reader_data_sink_->mutateReaderData([&](readerData_t& data) {
        for (auto& issuer : data.issuers) {
            std::erase_if(issuer.endpoints, [&](const hkEndpoint_t& endpoint) {
                if (endpoint.credential_index != idx || endpoint.credential_type != type ||
                    endpoint.endpoint_pk == key) return false;
                if (!credentials::revoked(data, endpoint.endpoint_pk))
                    data.revoked_endpoint_keys.push_back(endpoint.endpoint_pk);
                return true;
            });
        }
        credentials::allowExplicitKey(data, key);
        for (auto& issuer : data.issuers) {
            for (auto& endpoint : issuer.endpoints) {
                if (endpoint.endpoint_pk != key) continue;
                endpoint.credential_index = idx;
                endpoint.credential_type = type;
                endpoint.key_type = evictable ? 1 : 0;
                return; // preserve identity and FastAuth key of an existing iPhone
            }
        }
        if (data.issuers.empty()) {
            hkIssuer_t issuer;
            issuer.issuer_id.assign(8, 0);
            data.issuers.push_back(std::move(issuer));
        }
        hkEndpoint_t endpoint;
        endpoint.endpoint_id = id;
        endpoint.endpoint_pk = key;
        endpoint.endpoint_pk_x.assign(key.begin() + 1, key.begin() + 33);
        endpoint.credential_index = idx;
        endpoint.credential_type = type;
        endpoint.key_type = evictable ? 1 : 0;
        data.issuers.front().endpoints.push_back(std::move(endpoint));
    });
    if (!saved) ESP_LOGE(TAG, "Endpoint credential persistence failed");
    nfc::invalidateReader(nfc_manager_);
    return saved;
}

bool AliroDelegate::clearCredential(uint16_t idx, uint8_t type, chip::ByteSpan previousKey)
{
    if (!reader_data_sink_) return false;
    const std::vector<uint8_t> key(previousKey.begin(), previousKey.end());
    const bool saved = reader_data_sink_->mutateReaderData([&](readerData_t& data) {
        credentials::revoke(data, type, idx, key);
    });
    nfc::invalidateReader(nfc_manager_);
    return saved;
}

AliroDelegate & instance()
{
    static AliroDelegate g_instance;
    return g_instance;
}

bool AliroDelegate::reconcileCredentials(const std::vector<credentials::Provisioned>& live)
{
    if (!reader_data_sink_) return false;
    const bool saved = reader_data_sink_->mutateReaderData([&](readerData_t& data) {
        credentials::reconcile(data, live);
    });
    nfc::invalidateReader(nfc_manager_);
    return saved;
}

}  // namespace aliro
