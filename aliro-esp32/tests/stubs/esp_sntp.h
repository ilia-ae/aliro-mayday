#pragma once
#include <cstdint>
constexpr uint8_t SNTP_OPMODE_POLL=0;
bool esp_sntp_enabled();
void esp_sntp_setoperatingmode(uint8_t);
void esp_sntp_setservername(uint8_t,const char*);
void esp_sntp_init();
namespace test_clock {void reset(bool running=false);unsigned starts();}
