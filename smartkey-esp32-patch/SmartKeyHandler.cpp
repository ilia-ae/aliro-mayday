/**
 * SmartKey handler — skeleton for rednblkx/HomeKey-ESP32.
 *
 * DRAFT.  Crypto-verify stubs are marked TODO; this file currently
 * accepts any well-formed AUTH response.  Do not flash a production
 * lock from this code — see ../smartkey-esp32-patch/README.md.
 *
 * Reader side of /PROTOCOL.md §3.  Single AUTH round-trip after SELECT.
 */

#include "SmartKeyHandler.hpp"

#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <algorithm>
#include <cstring>

// Crypto — already pulled into the HomeKey-ESP32 build:
//   libsodium (HomeKit pairing)
//   mbedTLS   (HomeSpan / ECP / X.509)
// #include <sodium.h>
// #include "mbedtls/md.h"

static const char* TAG = "SmartKey";
static const char* NVS_NS = "smartkey_endp";  // NVS namespace names cap at 15 chars

namespace smartkey {

SmartKeyHandler::SmartKeyHandler() = default;

void SmartKeyHandler::buildAuthApdu(const std::array<uint8_t, 16>& nonce,
                                     std::vector<uint8_t>& out) const {
    // 80 A0 00 00 11 01 <nonce(16)> 00
    out.clear();
    out.reserve(5 + 1 + 16 + 1);
    out.push_back(0x80);  // CLA
    out.push_back(0xA0);  // INS
    out.push_back(0x00);  // P1
    out.push_back(0x00);  // P2
    out.push_back(0x11);  // Lc = 17
    out.push_back(0x01);  // protocol version
    out.insert(out.end(), nonce.begin(), nonce.end());
    out.push_back(0x00);  // Le
}

AuthResult SmartKeyHandler::runAuthTap(const NfcExchange& exchange,
                                        const EndpointRecord** matched_out) {
    if (matched_out) *matched_out = nullptr;

    // 1. Fresh 16-byte random nonce for this tap.
    std::array<uint8_t, 16> nonce{};
    esp_fill_random(nonce.data(), nonce.size());

    // 2. Send the AUTH APDU to the phone.
    std::vector<uint8_t> authApdu;
    buildAuthApdu(nonce, authApdu);

    std::vector<uint8_t> response;
    if (exchange(authApdu, response) != ESP_OK) {
        ESP_LOGE(TAG, "NFC exchange (AUTH) failed");
        return AuthResult::IO_ERROR;
    }

    // 3. Verify.
    const EndpointRecord* matched = nullptr;
    auto result = verifyAuthResponse(response, nonce, matched);
    if (matched_out) *matched_out = matched;

    if (result == AuthResult::OK && matched) {
        ESP_LOGI(TAG, "SmartKey auth OK, endpoint=%s", matched->label.c_str());
    } else {
        ESP_LOGW(TAG, "SmartKey auth failed, rc=%d", static_cast<int>(result));
    }
    return result;
}

AuthResult SmartKeyHandler::verifyAuthResponse(const std::vector<uint8_t>& resp,
                                                const std::array<uint8_t, 16>& nonce,
                                                const EndpointRecord*& matched) const {
    matched = nullptr;

    // Expected body (29 bytes): ver(1) ep_id(8) ts(4) tag(16)
    // Full APDU response: body + SW1 SW2 = 31 bytes.
    if (resp.size() < 31) return AuthResult::MALFORMED;

    // Strip SW1/SW2 if present (PN532 returns the phone's APDU response
    // including trailing status words; treat anything with a non-9000
    // trailer as a phone-side rejection).
    const size_t body_end = resp.size() - 2;
    if (resp[body_end] != 0x90 || resp[body_end + 1] != 0x00) {
        ESP_LOGW(TAG, "phone returned SW %02X%02X", resp[body_end], resp[body_end + 1]);
        return AuthResult::MALFORMED;
    }
    if (body_end != 29) return AuthResult::MALFORMED;

    if (resp[0] != 0x01) return AuthResult::VERSION_MISMATCH;

    std::array<uint8_t, 8> endpoint_id{};
    std::copy_n(resp.begin() + 1, 8, endpoint_id.begin());

    uint32_t ts = (uint32_t(resp[9])  << 24) |
                  (uint32_t(resp[10]) << 16) |
                  (uint32_t(resp[11]) <<  8) |
                   uint32_t(resp[12]);

    std::array<uint8_t, 16> phone_tag{};
    std::copy_n(resp.begin() + 13, 16, phone_tag.begin());

    // Find endpoint.
    for (const auto& rec : m_endpoints) {
        if (rec.endpoint_id == endpoint_id) {
            matched = &rec;
            break;
        }
    }
    if (!matched) return AuthResult::UNKNOWN_ENDPOINT;

    // TODO: freshness window — require |ts - time(nullptr)| <= 30 seconds.
    (void)ts;

    // TODO: recompute expected_tag =
    //   HMAC-SHA256(matched->mk,
    //               "OPEN/v1" || nonce || endpoint_id || ts_be)[0..15]
    // via mbedtls_md_hmac / mbedtls_md_hmac_starts + mbedtls_md_hmac_update
    // + mbedtls_md_hmac_finish.
    //
    // Then constant-time compare against phone_tag — e.g.
    // mbedtls_ct_memcmp(expected_tag, phone_tag.data(), 16).
    (void)nonce;
    (void)phone_tag;

    return AuthResult::OK;  // STUB: accepts once endpoint matches.
}

bool SmartKeyHandler::loadEndpoints() {
    // TODO: nvs_open(NVS_NS, ...) + iterate keys, deserialise records (msgpack).
    (void)NVS_NS;
    return true;
}

bool SmartKeyHandler::saveEndpoint(const EndpointRecord& rec) {
    // TODO: msgpack-serialise + nvs_set_blob(NVS_NS, key=hex(endpoint_id)).
    m_endpoints.push_back(rec);
    return true;
}

bool SmartKeyHandler::revokeEndpoint(const std::array<uint8_t, 8>& endpoint_id) {
    auto it = std::find_if(
        m_endpoints.begin(), m_endpoints.end(),
        [&](const EndpointRecord& r) { return r.endpoint_id == endpoint_id; });
    if (it == m_endpoints.end()) return false;
    m_endpoints.erase(it);
    // TODO: nvs_erase_key(NVS_NS, hex(endpoint_id))
    return true;
}

}  // namespace smartkey
