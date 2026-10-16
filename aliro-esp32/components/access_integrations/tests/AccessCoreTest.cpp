// Copyright (c) 2026 Mayday / Ilia Arestov. See /LICENSE.
#include "AccessJson.hpp"
#include <cassert>
#include <iostream>
using namespace aliro::access;
int main() {
    Config c;
    assert(!c.relayEnabled && !c.telegramEnabled && !validate(c));
    for (int pin : {0, 3, 8, 9, 10, 11, 12, 13, 19, 20, 26, 33, 35, 36, 37, 45, 46, 48, 255}) {
        c.relayEnabled = true; c.relayPin = pin; assert(validate(c));
    }
    c.relayPin = 4; assert(!validate(c));
    Relay relay;
    relay.state(c, false, 1000); assert(relay.active && relay.until == 2000);
    assert(!relay.tick(1999));
    relay.state(c, false, 1900); assert(!relay.tick(2000) && relay.active);
    assert(relay.tick(2900) && !relay.active);
    relay.state(c, false, 3000); relay.state(c, true, 3001); assert(!relay.active && !relay.tick(5000));
    c.pulse = false; relay.state(c, false, 6000); assert(relay.active && !relay.tick(UINT64_MAX));
    relay.reset(); assert(!relay.active);
    c.relayEnabled = false; relay.state(c, false, 7000); assert(!relay.active);
    std::string error;
    auto parseText = [&](const char *text) { auto *j = cJSON_Parse(text); bool ok = parse(j, c, error); cJSON_Delete(j); return ok; };
    assert(!parseText("{\"relayEnabled\":1}"));
    assert(!parseText("{\"pulseMs\":0}")); c.pulseMs = 1000;
    assert(!parseText("{\"relayPin\":4.5}"));
    assert(!parseText("{\"pulseMs\":30001}")); c.pulseMs = 1000;
    assert(!parseText("{\"telegramEnabled\":true}")); c.telegramEnabled = false;
    c.token = std::string("123456") + ":" + std::string(35, 'X'); c.chatId = "424242";
    assert(!validate(c));
    assert(parseText("{\"token\":\"\"}")); assert(!c.token.empty());
    auto *publicJson = configJson(c, false);
    assert(!cJSON_GetObjectItemCaseSensitive(publicJson, "token"));
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(publicJson, "tokenConfigured"))); cJSON_Delete(publicJson);
    c.telegramEnabled = true;
    assert(!parseText("{\"clearToken\":true}")); // Cannot leave an enabled client without a token.
    c.telegramEnabled = false;
    assert(parseText("{\"clearToken\":true}")); assert(c.token.empty());
    std::cout << "Relay timing, GPIO safety, redaction, strict configuration, notification configuration checks passed\n";
}
