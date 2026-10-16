// Copyright (c) 2026 Mayday / Ilia Arestov. See /LICENSE.
#pragma once
#include <cstdint>
#include <string>

namespace aliro::access {
struct Config {
    bool relayEnabled = false;
    int relayPin = -1;
    bool activeHigh = true;
    bool pulse = true;
    uint32_t pulseMs = 1000;
    bool telegramEnabled = false;
    bool notifyDenied = false;
    std::string token, chatId;
};
// Deliberately conservative ESP32-S3 DevKitC header allowlist. Excludes
// PN532 SPI, I2C panels, USB, strapping, flash/PSRAM and the status LED.
inline bool safeRelayPin(int pin) {
    return (pin >= 4 && pin <= 7) || (pin >= 14 && pin <= 18) ||
           pin == 21 || pin == 38 || pin == 47;
}
inline bool numericId(const std::string &id, bool positiveOnly = false) {
    size_t n = (!id.empty() && id[0] == '-' && !positiveOnly) ? 1 : 0;
    if (n == id.size() || id.size() > 16 || id[n] == '0') return false;
    for (; n < id.size(); ++n) if (id[n] < '0' || id[n] > '9') return false;
    return true;
}
inline bool validToken(const std::string &token) {
    auto colon = token.find(':');
    if (colon == std::string::npos || colon < 1 || colon > 16 || token.size() > 128 || token.size() - colon < 21) return false;
    for (size_t i = 0; i < token.size(); ++i) {
        auto c = token[i];
        if (i < colon ? (c < '0' || c > '9') :
            i != colon && !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return true;
}
inline const char *validate(const Config &c) {
    if (c.relayPin != -1 && !safeRelayPin(c.relayPin)) return "Choose an available relay GPIO.";
    if (c.relayEnabled && !safeRelayPin(c.relayPin)) return "Choose an available relay GPIO.";
    if (c.pulseMs < 100 || c.pulseMs > 30000) return "Pulse duration must be 100–30000 ms.";
    if (!c.token.empty() && !validToken(c.token)) return "Invalid bot token format.";
    if (!c.chatId.empty() && !numericId(c.chatId)) return "Use a numeric Telegram chat ID.";
    if (c.telegramEnabled && (c.token.empty() || c.chatId.empty())) return "Bot token and chat ID are required.";
    return nullptr;
}
// Monotonic deadline, recomputed on every new event: an old timeout cannot
// cut off a newer pulse. Boot/configuration changes always start inactive.
struct Relay {
    bool active = false;
    uint64_t until = 0;
    void reset() { active = false; until = 0; }
    void state(const Config &c, bool locked, uint64_t now) {
        active = c.relayEnabled && !locked;
        until = active && c.pulse ? now + c.pulseMs : 0;
    }
    bool tick(uint64_t now) {
        if (!until || now < until) return false;
        reset(); return true;
    }
};
} // namespace aliro::access
