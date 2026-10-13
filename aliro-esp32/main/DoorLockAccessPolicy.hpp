#pragma once
#include "ReaderDataManager.hpp"
// Called on the Matter task, which serializes user/credential changes.
bool aliro_user_access_allowed(const readerData_t& data,
    const std::vector<uint8_t>& issuerId, const std::vector<uint8_t>& endpointId);
