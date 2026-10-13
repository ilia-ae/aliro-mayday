#pragma once
//
// AliroDelegate — implements `chip::app::Clusters::DoorLock::Delegate` so the
// DoorLock cluster can serve Aliro provisioning reads/writes. Without this,
// connectedhomeip's door-lock-server returns `Status::Failure` for every
// Aliro attribute read, which Apple Home surfaces as
// "Specified action or command indicated is not supported".
//
// Storage:
//   - reader_signing_key (32 B, secret) — kept in RAM only for now,
//     §7.6 (security audit) will move to encrypted NVS namespace `aliro_cfg`.
//   - reader_verification_key (65 B, public) — RAM, persisted in NVS later.
//   - reader_group_identifier (16 B) — RAM.
//   - reader_group_sub_identifier (16 B) — random on first boot, persisted.

#include <app/clusters/door-lock-server/door-lock-delegate.h>
#include "ReaderCredentialOps.hpp"

class ReaderDataManager;  // forward — vendored from rednblkx/HomeKey-ESP32
class NfcManager;         // forward — vendored from rednblkx/HomeKey-ESP32

namespace aliro {

class AliroDelegate : public chip::app::Clusters::DoorLock::Delegate {
public:
    AliroDelegate();

    // ---- ALIRO provisioning attribute reads -----------------------------
    CHIP_ERROR GetAliroReaderVerificationKey(chip::MutableByteSpan & out) override;
    CHIP_ERROR GetAliroReaderGroupIdentifier(chip::MutableByteSpan & out) override;
    CHIP_ERROR GetAliroReaderGroupSubIdentifier(chip::MutableByteSpan & out) override;
    CHIP_ERROR GetAliroExpeditedTransactionSupportedProtocolVersionAtIndex(
        size_t index, chip::MutableByteSpan & out) override;
    CHIP_ERROR GetAliroGroupResolvingKey(chip::MutableByteSpan & out) override;
    CHIP_ERROR GetAliroSupportedBLEUWBProtocolVersionAtIndex(
        size_t index, chip::MutableByteSpan & out) override;
    uint8_t GetAliroBLEAdvertisingVersion() override;
    uint16_t GetNumberOfAliroCredentialIssuerKeysSupported() override;
    uint16_t GetNumberOfAliroEndpointKeysSupported() override;

    // ---- ALIRO provisioning commands -------------------------------------
    // SetAliroReaderConfig is the meat — Apple Home calls this to push the
    // reader keypair + group identifier into the lock during Set Up Home Key.
    CHIP_ERROR SetAliroReaderConfig(
        const chip::ByteSpan & signingKey,
        const chip::ByteSpan & verificationKey,
        const chip::ByteSpan & groupIdentifier,
        const chip::Optional<chip::ByteSpan> & groupResolvingKey) override;
    CHIP_ERROR ClearAliroReaderConfig() override;

    // True iff a reader signing/verification keypair has been provisioned.
    // Used by app_main / LED indicator: paired-without-keys = violet,
    // paired-with-keys = orange/green.
    bool isProvisioned() const { return reader_verification_key_len_ != 0; }

    // Get the 16-byte Aliro reader_group_identifier as hex string.
    // Returns 0 length if no GID is set.
    size_t getReaderGroupIdHex(char * out, size_t out_size) const {
        if (!reader_group_id_set_ || out_size < 33) return 0;
        for (int i = 0; i < 16; ++i) {
            snprintf(out + i * 2, out_size - i * 2, "%02X",
                     reader_group_identifier_[i]);
        }
        return 32;
    }

    // Wire the rednblkx ReaderDataManager so SetAliroReaderConfig also
    // populates the readerData_t consumed by NfcManager's DDK auth flow.
    // Called from app_main after both AliroDelegate::instance() and the
    // ReaderDataManager have been initialized. Triggers an immediate sync
    // if reader keys are already provisioned in NVS at boot.
    void setReaderDataSink(ReaderDataManager * rdm);

    // Wire the NfcManager so that whenever reader keys are (re)provisioned
    // we immediately refresh ECP polling data — Apple Wallet's ECP wakeup
    // pattern includes the reader GID, and a lock with stale (or zeroed)
    // GID just won't be recognised by an iPhone tap.
    void setNfcManager(NfcManager * nfc);

    // Mirror an Aliro credential into the DDK readerData_t. Called from
    // emberAfPluginDoorLockSetCredential (and replayed at boot for keys
    // already in NVS) so DDK kAliro FastAuth / StandardAuth has the
    // public keys it needs to verify a tapping phone's signature.
    //
    //   syncCredentialIssuerKey   — adds an entry to readerData.issuers[]
    //                               with issuer_id = SHA-256(pk)[0:8]
    //   syncCredentialEndpointKey — appends to issuers[0].endpoints[]
    //                               (single-issuer assumption — see body)
    bool syncCredentialIssuerKey(uint16_t idx, chip::ByteSpan publicKey);
    bool syncCredentialEndpointKey(uint16_t idx, chip::ByteSpan publicKey, bool evictable);
    bool clearCredential(uint16_t idx, uint8_t credentialType, chip::ByteSpan previousKey);
    bool reconcileCredentials(const std::vector<credentials::Provisioned>& live);

private:
    // Push current reader keys into the connected ReaderDataManager (if any).
    // Builds a readerData_t from in-memory fields and calls updateReaderData.
    void syncToReaderDataManager();

    ReaderDataManager * reader_data_sink_ = nullptr;
    NfcManager *        nfc_manager_      = nullptr;

    // Reader keypair + identifiers — populated via SetAliroReaderConfig().
    uint8_t  reader_signing_key_[32]      = {};
    uint8_t  reader_verification_key_[65] = {};
    size_t   reader_verification_key_len_ = 0;
    uint8_t  reader_group_identifier_[16] = {};
    bool     reader_group_id_set_         = false;

    // Sub-identifier — random, generated once on first construction.
    uint8_t  reader_group_sub_identifier_[16] = {};

    // Group resolving key (optional in SetAliroReaderConfig command).
    uint8_t  group_resolving_key_[16]     = {};
    bool     group_resolving_key_set_     = false;
};

// Singleton instance — lifetime is the whole app_main, so a global is fine.
AliroDelegate & instance();

}  // namespace aliro
