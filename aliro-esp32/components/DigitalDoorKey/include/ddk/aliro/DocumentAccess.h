#pragma once
#include "ddk/aliro/DocumentVerifier.h"
#include <functional>
#include <map>

namespace ddk::aliro {
struct RevocationEntry {
    std::vector<uint8_t> public_key_hash;
    std::vector<uint8_t> id;
    std::optional<uint32_t> expiry;
    uint64_t iteration=0;
    bool operator==(const RevocationEntry& o) const {
        return public_key_hash==o.public_key_hash && id==o.id && expiry==o.expiry && iteration==o.iteration;
    }
};
struct RevocationState {
    uint64_t iteration=0;
    std::vector<RevocationEntry> entries;
    bool operator==(const RevocationState& o) const { return iteration==o.iteration && entries==o.entries; }
};
struct AccessRule {
    std::optional<uint16_t> capabilities;
    std::optional<uint8_t> allow_schedules, deny_schedules;
};
struct AccessPolicy {
    uint16_t intended_capability=1u<<1; // Unsecure / Matter UnlockDoor
    std::map<uint16_t,std::vector<AccessRule>> reader_rules;
    // Schedules are an optional Aliro feature. This implementation has no
    // schedule interpreter and rejects every rule referencing a schedule.
};
struct AccessDecision {
    bool allowed=false;
    std::vector<std::vector<uint8_t>> ids;
};
bool credential_revoked(const RevocationState&, span<const uint8_t> key,
    span<const uint8_t> id, std::optional<int64_t> current_time);
AccessDecision evaluate_access_document(const VerifiedDocument&,const AccessPolicy&,
    const RevocationState&,std::optional<int64_t> current_time);
// Atomic on success; malformed elements or state overflow leave state unchanged.
bool apply_revocation_document(const VerifiedDocument&,RevocationState&,
    std::optional<int64_t> current_time,bool require_full_snapshot=false);
} // namespace ddk::aliro
