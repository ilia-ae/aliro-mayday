#pragma once
#include "DDKReaderData.h"
#include <algorithm>

namespace aliro::credentials {
constexpr uint8_t kIssuer = 6;
constexpr uint8_t kEvictable = 7;
constexpr uint8_t kNonEvictable = 8;
struct Provisioned { uint8_t type; uint16_t index; std::vector<uint8_t> key; };

inline bool revoked(const readerData_t& data, const std::vector<uint8_t>& key) {
    return std::find(data.revoked_endpoint_keys.begin(), data.revoked_endpoint_keys.end(), key)
        != data.revoked_endpoint_keys.end();
}

inline void allowExplicitKey(readerData_t& data, const std::vector<uint8_t>& key) {
    std::erase(data.revoked_endpoint_keys, key);
}

inline void revoke(readerData_t& data, uint8_t type, uint16_t index,
                   const std::vector<uint8_t>& previousKey) {
    if (type == kIssuer) {
        std::erase_if(data.issuers, [&](const hkIssuer_t& issuer) {
            return (index != 0 && issuer.credential_index == index) ||
                (!previousKey.empty() && issuer.issuer_pk == previousKey);
        });
        return;
    }
    if (type != kEvictable && type != kNonEvictable) return;
    if (!previousKey.empty() && !revoked(data, previousKey))
        data.revoked_endpoint_keys.push_back(previousKey);
    for (auto& issuer : data.issuers) {
        std::erase_if(issuer.endpoints, [&](const hkEndpoint_t& endpoint) {
            const bool match = (index != 0 && endpoint.credential_index == index &&
                                endpoint.credential_type == type) ||
                (!previousKey.empty() && endpoint.endpoint_pk == previousKey);
            if (match && !endpoint.endpoint_pk.empty() && !revoked(data, endpoint.endpoint_pk))
                data.revoked_endpoint_keys.push_back(endpoint.endpoint_pk);
            return match;
        });
    }
}

inline const hkEndpoint_t* find(const readerData_t& data, const std::vector<uint8_t>& issuerId,
                               const std::vector<uint8_t>& endpointId) {
    for (const auto& issuer : data.issuers) {
        if (issuer.issuer_id != issuerId) continue;
        for (const auto& endpoint : issuer.endpoints)
            if (endpoint.endpoint_id == endpointId && !revoked(data, endpoint.endpoint_pk))
                return &endpoint;
    }
    return nullptr;
}
inline void reconcile(readerData_t& data, const std::vector<Provisioned>& live) {
    const auto present = [&](uint8_t type, uint16_t index, const std::vector<uint8_t>& key) {
        return std::any_of(live.begin(), live.end(), [&](const Provisioned& credential) {
            return credential.type == type && (index == 0 || credential.index == index) &&
                credential.key == key;
        });
    };
    std::erase_if(data.issuers, [&](const hkIssuer_t& issuer) {
        return !issuer.issuer_pk.empty() &&
            !present(kIssuer, issuer.credential_index, issuer.issuer_pk);
    });
    for (auto& issuer : data.issuers) {
        std::erase_if(issuer.endpoints, [&](const hkEndpoint_t& endpoint) {
            if ((endpoint.credential_type != kEvictable && endpoint.credential_type != kNonEvictable) ||
                present(endpoint.credential_type, endpoint.credential_index, endpoint.endpoint_pk)) return false;
            if (!revoked(data, endpoint.endpoint_pk)) data.revoked_endpoint_keys.push_back(endpoint.endpoint_pk);
            return true;
        });
    }
}
} // namespace aliro::credentials
