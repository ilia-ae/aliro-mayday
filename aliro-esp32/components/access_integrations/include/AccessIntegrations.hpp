// Copyright (c) 2026 Mayday / Ilia Arestov. See /LICENSE.
#pragma once
#include "AccessPolicy.hpp"
#include <cJSON.h>
#include <cstdint>

namespace aliro::access {
void begin();
// Reserve additional pins used by the board profile before begin().
void reservePin(int pin);
void lockChanged(bool locked, unsigned source);
void accessDenied();
// Returned JSON is owned by the caller. Secrets are never returned.
cJSON *status();
bool configure(const cJSON *json, std::string &error);
bool testNotification(std::string &error);
} // namespace aliro::access
