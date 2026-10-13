#pragma once
#include "DDKReaderData.h"
#include <functional>
#include <memory>
#include <tuple>

// Firmware adapter to pinned upstream DDK sessions. Preserves the existing
// on-device readerData_t schema and the NFC precompute pool API.
class DDKAuthenticationContext {
public:
    using Nfc = std::function<bool(std::vector<uint8_t>&, std::vector<uint8_t>&, bool)>;
    using Save = std::function<bool(const readerData_t&)>;
    DDKAuthenticationContext(DigitalKeyType, const Nfc&, readerData_t&, const Save&);
    ~DDKAuthenticationContext();
    void setSelectResponse(const std::vector<uint8_t>& response);
    std::tuple<std::vector<uint8_t>, std::vector<uint8_t>, KeyFlow> authenticate(KeyFlow);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
