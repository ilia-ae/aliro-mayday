#pragma once
#include "ddk/Span.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ddk::aliro {
// Cryptographic validation does not grant access. The application must separately
// interpret AccessData and consult its current user/revocation policy.
struct DocumentPolicy {
    std::optional<int64_t> current_time;
    uint64_t access_iteration = 0;
    bool allow_time_optional_without_clock = true;
};
struct VerifiedDocument {
    std::vector<uint8_t> device_public_key;
    std::optional<uint64_t> iteration;
    std::vector<std::string> disclosed_elements;
};
std::vector<uint8_t> issuer_key_identifier(span<const uint8_t> public_key);
std::optional<VerifiedDocument> verify_access_document(
    span<const uint8_t> document, span<const uint8_t> trusted_issuer_public_key,
    const DocumentPolicy& policy, const std::vector<std::string>& requested_elements);
}  // namespace ddk::aliro
