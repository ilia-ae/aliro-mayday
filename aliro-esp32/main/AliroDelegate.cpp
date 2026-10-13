#include "AliroDelegate.h"
#include "Diag.h"
#include "ReaderDataManager.hpp"
#include "NfcManager.hpp"
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
constexpr const char * kKeySign  = "signKey";      // 32 B blob
constexpr const char * kKeyVer   = "verKey";       // up to 65 B blob (length implicit)
constexpr const char * kKeyGrpId = "grpId";        // 16 B blob (set iff present)
constexpr const char * kKeyGrpRs = "grpRsKey";     // 16 B blob (set iff present)

bool nvs_open_rw(nvs_handle_t & h)
{
    esp_err_t err = nvs_open(kNs, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(%s) failed: 0x%x", kNs, err);
        return false;
    }
    return true;
}

bool nvs_load_blob(nvs_handle_t h, const char * key, void * dst, size_t & inout_len)
{
    size_t len = inout_len;
    esp_err_t err = nvs_get_blob(h, key, dst, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        inout_len = 0;
        return false;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_get_blob(%s) err 0x%x", key, err);
        inout_len = 0;
        return false;
    }
    inout_len = len;
    return true;
}

}  // namespace

AliroDelegate::AliroDelegate()
{
    nvs_handle_t h;
    bool nvs_ok = nvs_open_rw(h);

    // 1) Reader Group Sub-Identifier — 16 bytes, generated once at first
    //    boot and persisted forever. Spec mandates a stable value; iOS 26
    //    reads it back to fingerprint the reader and rejects mismatches.
    if (nvs_ok) {
        size_t sub_len = sizeof(reader_group_sub_identifier_);
        if (!nvs_load_blob(h, kKeySubId, reader_group_sub_identifier_, sub_len) ||
            sub_len != sizeof(reader_group_sub_identifier_)) {
            esp_fill_random(reader_group_sub_identifier_,
                            sizeof(reader_group_sub_identifier_));
            esp_err_t err = nvs_set_blob(h, kKeySubId, reader_group_sub_identifier_,
                                         sizeof(reader_group_sub_identifier_));
            if (err == ESP_OK) nvs_commit(h);
            ESP_LOGI(TAG, "[delegate] sub-id generated fresh, persisted (err=0x%x)", err);
        } else {
            ESP_LOGI(TAG, "[delegate] sub-id loaded from NVS");
        }
    } else {
        // NVS unavailable — fall back to per-boot random (matches old behaviour).
        esp_fill_random(reader_group_sub_identifier_,
                        sizeof(reader_group_sub_identifier_));
        ESP_LOGW(TAG, "[delegate] NVS unavailable; sub-id is per-boot only");
    }

    // 2) Reader signing key (secret, 32B) + verification key (public, up to 65B).
    if (nvs_ok) {
        size_t sk_len = sizeof(reader_signing_key_);
        bool   sk_ok  = nvs_load_blob(h, kKeySign, reader_signing_key_, sk_len) &&
                        sk_len == sizeof(reader_signing_key_);
        size_t vk_len = sizeof(reader_verification_key_);
        bool   vk_ok  = nvs_load_blob(h, kKeyVer, reader_verification_key_, vk_len) &&
                        vk_len > 0 && vk_len <= sizeof(reader_verification_key_);
        if (sk_ok && vk_ok) {
            reader_verification_key_len_ = vk_len;
            ESP_LOGI(TAG, "[delegate] reader keys loaded from NVS (verKey=%uB)",
                     (unsigned)vk_len);
        } else {
            // Either missing or partial — leave zero, accessory not provisioned.
            memset(reader_signing_key_, 0, sizeof(reader_signing_key_));
            memset(reader_verification_key_, 0, sizeof(reader_verification_key_));
            reader_verification_key_len_ = 0;
            ESP_LOGI(TAG, "[delegate] no reader keys in NVS (sk_ok=%d vk_ok=%d)",
                     sk_ok, vk_ok);
        }
    }

    // 3) Reader group identifier (16B).
    if (nvs_ok) {
        size_t gid_len = sizeof(reader_group_identifier_);
        if (nvs_load_blob(h, kKeyGrpId, reader_group_identifier_, gid_len) &&
            gid_len == sizeof(reader_group_identifier_)) {
            reader_group_id_set_ = true;
            ESP_LOGI(TAG, "[delegate] reader group-id loaded from NVS");
        }
    }

    // 4) Group resolving key (16B, optional).
    if (nvs_ok) {
        size_t grk_len = sizeof(group_resolving_key_);
        if (nvs_load_blob(h, kKeyGrpRs, group_resolving_key_, grk_len) &&
            grk_len == sizeof(group_resolving_key_)) {
            group_resolving_key_set_ = true;
            ESP_LOGI(TAG, "[delegate] group-resolving-key loaded from NVS");
        }
    }

    if (nvs_ok) nvs_close(h);
}

CHIP_ERROR AliroDelegate::GetAliroReaderVerificationKey(chip::MutableByteSpan & out)
{
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
    return 5;
}

uint16_t AliroDelegate::GetNumberOfAliroEndpointKeysSupported()
{
    // Endpoint keys = per-device keys. 16 covers a small home (~5 people
    // with 2-3 Apple devices each). Spec only mandates >= 1.
    return 16;
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

    if (signingKey.size() != sizeof(reader_signing_key_)) {
        ESP_LOGE(TAG, "[delegate] signingKey wrong size: %u (want %u)",
                 (unsigned)signingKey.size(),
                 (unsigned)sizeof(reader_signing_key_));
        return CHIP_ERROR_INVALID_ARGUMENT;
    }
    if (verificationKey.size() == 0 ||
        verificationKey.size() > sizeof(reader_verification_key_)) {
        ESP_LOGE(TAG, "[delegate] verificationKey wrong size: %u",
                 (unsigned)verificationKey.size());
        return CHIP_ERROR_INVALID_ARGUMENT;
    }
    if (groupIdentifier.size() != sizeof(reader_group_identifier_)) {
        ESP_LOGE(TAG, "[delegate] groupIdentifier wrong size: %u (want 16)",
                 (unsigned)groupIdentifier.size());
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    memcpy(reader_signing_key_, signingKey.data(), sizeof(reader_signing_key_));
    memcpy(reader_verification_key_, verificationKey.data(), verificationKey.size());
    reader_verification_key_len_ = verificationKey.size();
    memcpy(reader_group_identifier_, groupIdentifier.data(),
           sizeof(reader_group_identifier_));
    reader_group_id_set_ = true;

    if (groupResolvingKey.HasValue() &&
        groupResolvingKey.Value().size() == sizeof(group_resolving_key_)) {
        memcpy(group_resolving_key_, groupResolvingKey.Value().data(),
               sizeof(group_resolving_key_));
        group_resolving_key_set_ = true;
    } else {
        group_resolving_key_set_ = false;
        memset(group_resolving_key_, 0, sizeof(group_resolving_key_));
    }

    // Persist immediately. Apple iOS 26 will read these back on next CASE
    // session and any inconsistency stalls "Access Codes" / "Set Up Home Key".
    nvs_handle_t h;
    if (nvs_open_rw(h)) {
        nvs_set_blob(h, kKeySign, reader_signing_key_, sizeof(reader_signing_key_));
        nvs_set_blob(h, kKeyVer,  reader_verification_key_, reader_verification_key_len_);
        nvs_set_blob(h, kKeyGrpId, reader_group_identifier_,
                     sizeof(reader_group_identifier_));
        if (group_resolving_key_set_) {
            nvs_set_blob(h, kKeyGrpRs, group_resolving_key_,
                         sizeof(group_resolving_key_));
        } else {
            nvs_erase_key(h, kKeyGrpRs);
        }
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "[delegate] reader config persisted to NVS");
    } else {
        ESP_LOGE(TAG, "[delegate] FAILED to persist reader config — accessory will lose pairing on reboot");
    }

    diag::note_aliro_phase("SetReaderConfig",
        groupResolvingKey.HasValue() ? "with-grpResolv" : "no-grpResolv");
    ESP_LOGI(TAG, "[delegate] SetAliroReaderConfig OK — reader provisioned");
    // Reader keys provisioned → LED leaves the violet "paired-no-keys"
    // state for solid orange ("locked"). LockDoor/UnlockDoor will toggle
    // between orange and green.
    lockind::set_locked();

    // Mirror the keys into the rednblkx ReaderDataManager so NfcManager's
    // DDK auth precompute can build an authentication context. Without
    // this NfcManager keeps logging "reader not provisioned yet, retrying…".
    syncToReaderDataManager();

    // ECP polling data and auth-cache must both be refreshed after every
    // key change. Upstream HomeKey-ESP32 wires this through HK_BUS_TOPIC
    // which we don't run; do it directly via the NfcManager pointer.
    if (nfc_manager_ != nullptr) {
        nfc_manager_->updateEcpData();
        nfc_manager_->invalidateAuthCacheExternal();
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

    // Wipe NVS too. Sub-id deliberately stays — spec wants it to live for
    // the lifetime of the lock so Apple Home re-recognises us across
    // re-pairings.
    nvs_handle_t h;
    if (nvs_open_rw(h)) {
        nvs_erase_key(h, kKeySign);
        nvs_erase_key(h, kKeyVer);
        nvs_erase_key(h, kKeyGrpId);
        nvs_erase_key(h, kKeyGrpRs);
        nvs_commit(h);
        nvs_close(h);
    }
    diag::note_aliro_phase("ClearReaderConfig", "");

    // Wipe the readerData_t mirror too — NfcManager auth precompute will
    // then go idle and stop holding stale ECC public keys in RAM.
    if (reader_data_sink_ != nullptr) {
        reader_data_sink_->deleteAllReaderData();
    }
    if (nfc_manager_ != nullptr) {
        nfc_manager_->updateEcpData();
        nfc_manager_->invalidateAuthCacheExternal();
    }
    return CHIP_NO_ERROR;
}

void AliroDelegate::setReaderDataSink(ReaderDataManager * rdm)
{
    reader_data_sink_ = rdm;
    if (rdm != nullptr && isProvisioned()) {
        ESP_LOGI(TAG, "[delegate] reader data sink wired, initial sync");
        syncToReaderDataManager();
    }
}

void AliroDelegate::setNfcManager(NfcManager * nfc)
{
    nfc_manager_ = nfc;
    if (nfc != nullptr && isProvisioned()) {
        ESP_LOGI(TAG, "[delegate] NfcManager wired, refreshing ECP polling data");
        nfc->updateEcpData();
    }
}

void AliroDelegate::syncToReaderDataManager()
{
    if (reader_data_sink_ == nullptr) {
        ESP_LOGW(TAG, "[delegate] readerDataSink not wired yet — skipping sync");
        return;
    }
    if (!isProvisioned()) {
        ESP_LOGW(TAG, "[delegate] not provisioned — skipping sync");
        return;
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
    } else {
        ESP_LOGE(TAG, "[delegate] ReaderDataManager::updateReaderData failed");
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
    return pk.size() == 65 && pk.data()[0] == 0x04;
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
    if (nfc_manager_) nfc_manager_->invalidateAuthCacheExternal();
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
    if (nfc_manager_) nfc_manager_->invalidateAuthCacheExternal();
    return saved;
}

bool AliroDelegate::clearCredential(uint16_t idx, uint8_t type, chip::ByteSpan previousKey)
{
    if (!reader_data_sink_) return false;
    const std::vector<uint8_t> key(previousKey.begin(), previousKey.end());
    const bool saved = reader_data_sink_->mutateReaderData([&](readerData_t& data) {
        credentials::revoke(data, type, idx, key);
    });
    if (nfc_manager_) nfc_manager_->invalidateAuthCacheExternal();
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
    if (nfc_manager_) nfc_manager_->invalidateAuthCacheExternal();
    return saved;
}

}  // namespace aliro
