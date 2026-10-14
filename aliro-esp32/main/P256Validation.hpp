#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <psa/crypto.h>

namespace aliro::keys {
inline bool validPublicKey(const uint8_t* data, size_t size) {
    if (!data || size != 65 || data[0] != 0x04 || psa_crypto_init() != PSA_SUCCESS) return false;
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attr, 256);
    psa_key_id_t key = 0;
    const auto result = psa_import_key(&attr, data, size, &key);
    psa_reset_key_attributes(&attr);
    if (result == PSA_SUCCESS) psa_destroy_key(key);
    return result == PSA_SUCCESS;
}

inline bool matchingKeyPair(const uint8_t* secret, size_t secretSize,
                            const uint8_t* publicKey, size_t publicSize) {
    if (!secret || secretSize != 32 || !validPublicKey(publicKey, publicSize)) return false;
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attr, 256);
    psa_key_id_t key = 0;
    auto result = psa_import_key(&attr, secret, secretSize, &key);
    psa_reset_key_attributes(&attr);
    if (result != PSA_SUCCESS) return false;
    uint8_t derived[65]{};
    size_t size = 0;
    result = psa_export_public_key(key, derived, sizeof(derived), &size);
    psa_destroy_key(key);
    return result == PSA_SUCCESS && size == publicSize && std::memcmp(derived, publicKey, size) == 0;
}
}
