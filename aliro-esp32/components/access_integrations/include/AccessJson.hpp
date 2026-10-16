// Copyright (c) 2026 Mayday / Ilia Arestov. See /LICENSE.
#pragma once
#include "AccessPolicy.hpp"
#include <cJSON.h>
#include <cmath>
#include <cstring>
#include <cstdio>
namespace aliro::access {
inline cJSON *configJson(const Config &c, bool secret) {
    auto *j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "relayEnabled", c.relayEnabled);
    cJSON_AddNumberToObject(j, "relayPin", c.relayPin);
    cJSON_AddBoolToObject(j, "activeHigh", c.activeHigh);
    cJSON_AddBoolToObject(j, "pulse", c.pulse);
    cJSON_AddNumberToObject(j, "pulseMs", c.pulseMs);
    cJSON_AddBoolToObject(j, "telegramEnabled", c.telegramEnabled);
    cJSON_AddBoolToObject(j, "notifyDenied", c.notifyDenied);
    cJSON_AddStringToObject(j, "chatId", c.chatId.c_str());
    if (secret) cJSON_AddStringToObject(j, "token", c.token.c_str());
    else cJSON_AddBoolToObject(j, "tokenConfigured", !c.token.empty());
    return j;
}
inline bool parse(const cJSON *j, Config &c, std::string &error) {
    if (!cJSON_IsObject(j)) { error = "Invalid integration settings."; return false; }
    // Full configuration with a write-only token. Blank/missing token preserves it.
    const char *bools[] = {"relayEnabled", "activeHigh", "pulse", "telegramEnabled", "notifyDenied"};
    bool *values[] = {&c.relayEnabled, &c.activeHigh, &c.pulse, &c.telegramEnabled, &c.notifyDenied};
    for (size_t i = 0; i < 5; ++i) {
        auto *v = cJSON_GetObjectItemCaseSensitive(j, bools[i]);
        if (v && !cJSON_IsBool(v)) { error = "Invalid integration settings."; return false; }
        if (v) *values[i] = cJSON_IsTrue(v);
    }
    for (auto name : {"relayPin", "pulseMs"}) {
        auto *v = cJSON_GetObjectItemCaseSensitive(j, name);
        if (!v) continue;
        if (!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble) || std::floor(v->valuedouble) != v->valuedouble || v->valuedouble < -1 || v->valuedouble > 30000) {
            error = "Invalid integration settings."; return false;
        }
        if (name[0] == 'r') c.relayPin = v->valueint;
        else c.pulseMs = static_cast<uint32_t>(v->valueint);
    }
    for (auto name : {"token", "chatId"}) {
        auto *v = cJSON_GetObjectItemCaseSensitive(j, name);
        if (!v) continue;
        if (!cJSON_IsString(v) || std::strlen(v->valuestring) > 128) { error = "Invalid integration settings."; return false; }
        if (name[0] == 't') { if (*v->valuestring) c.token = v->valuestring; }
        else if (name[0] == 'c') c.chatId = v->valuestring;
    }
    auto *clear = cJSON_GetObjectItemCaseSensitive(j, "clearToken");
    if (clear && !cJSON_IsBool(clear)) { error = "Invalid integration settings."; return false; }
    if (cJSON_IsTrue(clear)) c.token.clear();
    if (const char *why = validate(c)) { error = why; return false; }
    return true;
}
} // namespace aliro::access
