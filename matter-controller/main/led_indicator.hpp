// led_indicator.hpp — WS2812 RGB LED state machine for matter-controller.
#pragma once

#include <esp_err.h>

namespace led {

enum class State {
    kBoot,                  // dim blue
    kWifiConnecting,        // blink blue
    kWifiConnected,         // cyan
    kPairing,               // blink yellow
    kSubscribed,            // green
    kLocked,                // orange (last reported LockState=Locked)
    kUnlocked,              // pulse green (LockState=Unlocked)
    kSubscriptionLost,      // blink red
    kError,                 // fast blink red
};

esp_err_t init();
void      set_state(State s);
State     get_state();

} // namespace led
