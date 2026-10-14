#pragma once
#include "P256Validation.hpp"
#include <array>
#include <algorithm>
#include <nvs.h>

namespace aliro::reader_config {
struct Config {
    bool configured = false;
    bool has_resolving_key = false;
    std::array<uint8_t, 32> signing{};
    std::array<uint8_t, 65> verification{};
    std::array<uint8_t, 16> group{};
    std::array<uint8_t, 16> resolving{};
};
enum class Load { Valid, Empty, Corrupt, StorageError };
inline constexpr const char* name_space = "aliro_cfg";
inline constexpr const char* record_key = "readerCfg1";

inline bool valid(const Config& c) {
    return c.configured && keys::matchingKeyPair(c.signing.data(), c.signing.size(),
                                               c.verification.data(), c.verification.size());
}

template<size_t N>
inline esp_err_t read(nvs_handle_t h, const char* key, std::array<uint8_t, N>& data) {
    size_t size = N;
    const auto result = nvs_get_blob(h, key, data.data(), &size);
    return result == ESP_OK && size != N ? ESP_ERR_NVS_INVALID_LENGTH : result;
}

// A single NVS blob is authoritative for new writes, including a clear marker.
// Read legacy individual fields only when that blob has never been written.
// This preserves the old reader/sub-ID without a boot-time migration and avoids
// combining a new signing key with an old verification key after a partial write.
inline Load load(Config& out) {
    nvs_handle_t h;
    if (nvs_open(name_space, NVS_READWRITE, &h) != ESP_OK) return Load::StorageError;
    std::array<uint8_t, 135> record{};
    size_t size = record.size();
    const auto result = nvs_get_blob(h, record_key, record.data(), &size);
    Config c;
    if (result == ESP_OK) {
        nvs_close(h);
        if (size != record.size() || record[0] != 'A' || record[1] != 'C' ||
            record[2] != 'F' || record[3] != 'G' || record[4] != 1 || (record[5] & ~3u))
            return Load::Corrupt;
        c.configured = record[5] & 1u;
        c.has_resolving_key = record[5] & 2u;
        if (!c.configured) {
            if (c.has_resolving_key || std::any_of(record.begin()+6, record.end(),
                    [](uint8_t b) { return b != 0; })) return Load::Corrupt;
            out = {};
            return Load::Empty;
        }
        std::copy_n(record.begin()+6, 32, c.signing.begin());
        std::copy_n(record.begin()+38, 65, c.verification.begin());
        std::copy_n(record.begin()+103, 16, c.group.begin());
        std::copy_n(record.begin()+119, 16, c.resolving.begin());
        if (!valid(c) || (!c.has_resolving_key && std::any_of(c.resolving.begin(),
                c.resolving.end(), [](uint8_t b) { return b != 0; }))) return Load::Corrupt;
        out = c;
        return Load::Valid;
    }
    if (result != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(h);
        return result == ESP_ERR_NVS_INVALID_LENGTH ? Load::Corrupt : Load::StorageError;
    }
    const auto sk = read(h, "signKey", c.signing);
    const auto pk = read(h, "verKey", c.verification);
    const auto group = read(h, "grpId", c.group);
    const auto resolving = read(h, "grpRsKey", c.resolving);
    nvs_close(h);
    if (sk == ESP_ERR_NVS_NOT_FOUND && pk == ESP_ERR_NVS_NOT_FOUND && group == ESP_ERR_NVS_NOT_FOUND) {
        out = {};
        return Load::Empty;
    }
    if (sk != ESP_OK || pk != ESP_OK || group != ESP_OK ||
        (resolving != ESP_OK && resolving != ESP_ERR_NVS_NOT_FOUND)) return Load::Corrupt;
    c.configured = true;
    c.has_resolving_key = resolving == ESP_OK;
    if (!valid(c)) return Load::Corrupt;
    out = c;
    return Load::Valid;
}

inline bool save(const Config& c) {
    if (c.configured && !valid(c)) return false;
    std::array<uint8_t, 135> record{};
    record[0]='A'; record[1]='C'; record[2]='F'; record[3]='G'; record[4]=1;
    if (c.configured) {
        record[5] = 1u | (c.has_resolving_key ? 2u : 0u);
        std::copy(c.signing.begin(), c.signing.end(), record.begin()+6);
        std::copy(c.verification.begin(), c.verification.end(), record.begin()+38);
        std::copy(c.group.begin(), c.group.end(), record.begin()+103);
        if (c.has_resolving_key) std::copy(c.resolving.begin(), c.resolving.end(), record.begin()+119);
    }
    nvs_handle_t h;
    if (nvs_open(name_space, NVS_READWRITE, &h) != ESP_OK) return false;
    auto result = nvs_set_blob(h, record_key, record.data(), record.size());
    if (result == ESP_OK) result = nvs_commit(h);
    nvs_close(h);
    return result == ESP_OK;
}

inline bool eraseLegacy() {
    nvs_handle_t h;
    if (nvs_open(name_space,NVS_READWRITE,&h)!=ESP_OK) return false;
    esp_err_t result=ESP_OK;
    for (const char* key:{"signKey","verKey","grpId","grpRsKey"}) {
        const auto erased=nvs_erase_key(h,key);
        if (erased!=ESP_OK && erased!=ESP_ERR_NVS_NOT_FOUND) {result=erased;break;}
    }
    if (result==ESP_OK) result=nvs_commit(h);
    nvs_close(h);
    return result==ESP_OK;
}
}
