#pragma once
/**
 * SmartKey (custom Android-companion protocol v1) handler for homekey-esp32.
 *
 * Reader side of /PROTOCOL.md §3.  Flow:
 *
 *   1. NfcManager detects a 14A tag in polling.
 *   2. NfcManager tries Apple HomeKey AID SELECT first.
 *   3. If that's 6A82, it tries our SELECT (00 A4 04 00 08 <AID> 00).
 *   4. On 9000, NfcManager hands off to SmartKeyHandler::runAuthTap(),
 *      which generates a random nonce, sends the AUTH APDU, parses
 *      the phone's response, verifies the HMAC tag, and returns.
 */

#include "esp_err.h"
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace smartkey {

/** Our registered AID — `F0SMRTKEY`. */
inline constexpr std::array<uint8_t, 8> AID = {
    0xF0, 0x53, 0x4D, 0x52, 0x54, 0x4B, 0x45, 0x59
};

/** ECP2 polling-loop annotation, emitted alongside Apple HomeKey's. */
inline constexpr std::array<uint8_t, 11> ECP2_ANNOTATION = {
    0x6A, 0x02, 0xC3, 0x02, 0x04, 0xFE, 0xED, 0xFA, 0xCE, 0x00, 0x01
};

/** Callback to exchange one APDU pair with the phone via PN532. */
using NfcExchange =
    std::function<esp_err_t(const std::vector<uint8_t>& req,
                             std::vector<uint8_t>& resp)>;

/** One paired Android phone — persisted in NVS namespace "smartkey_endp". */
struct EndpointRecord {
    std::array<uint8_t, 8>  endpoint_id;
    std::array<uint8_t, 33> phone_pub;   // compressed SEC1 P-256 (phone's static pub)
    std::array<uint8_t, 32> mk;          // HKDF(ECDH(phone_pub, reader_priv), ...)
    std::string             label;
    uint64_t                created_at_ms = 0;
};

enum class AuthResult {
    OK,
    UNKNOWN_ENDPOINT,
    BAD_MAC,
    TS_OUT_OF_WINDOW,
    MALFORMED,
    VERSION_MISMATCH,
    IO_ERROR,
};

class SmartKeyHandler {
public:
    SmartKeyHandler();

    /**
     * Drive one tap transaction after NfcManager has SELECTed our AID.
     *
     * Generates nonce, sends AUTH APDU, reads response, verifies.
     * On OK, `matched_out` (if non-null) is set to the endpoint that
     * tapped.  Caller is responsible for actuating the lock.
     */
    AuthResult runAuthTap(const NfcExchange& exchange,
                          const EndpointRecord** matched_out = nullptr);

    // Storage ops — NVS namespace "smartkey_endp".
    bool loadEndpoints();
    bool saveEndpoint(const EndpointRecord& rec);
    bool revokeEndpoint(const std::array<uint8_t, 8>& endpoint_id);

    const std::vector<EndpointRecord>& endpoints() const { return m_endpoints; }

    /** Long-lived reader keypair used at pairing time (not per-tap). */
    const std::array<uint8_t, 33>& readerPublicKey() const { return m_reader_pub; }
    const std::array<uint8_t, 8>&  readerId() const { return m_reader_id; }

private:
    std::array<uint8_t, 8>  m_reader_id{};
    std::array<uint8_t, 32> m_reader_priv{};
    std::array<uint8_t, 33> m_reader_pub{};
    std::vector<EndpointRecord> m_endpoints;

    void buildAuthApdu(const std::array<uint8_t, 16>& nonce,
                       std::vector<uint8_t>& out) const;

    AuthResult verifyAuthResponse(const std::vector<uint8_t>& resp,
                                   const std::array<uint8_t, 16>& nonce,
                                   const EndpointRecord*& matched) const;
};

}  // namespace smartkey
