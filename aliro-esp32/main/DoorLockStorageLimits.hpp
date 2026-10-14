#pragma once
#include <cstddef>

namespace aliro::storage {
// Keep the persisted per-user record at three credentials. Expanding the
// number of credential slots does not change an individual NVS blob's layout.
inline constexpr size_t users = 5;
inline constexpr size_t credentials_per_user = 3;
inline constexpr size_t issuer_keys = 5;
inline constexpr size_t endpoint_keys = 16;
inline constexpr size_t credential_slots_per_type = endpoint_keys;
}
