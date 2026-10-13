// ByteButtonPanel — M5Stack Unit ByteButton (U192) on I2C @ 0x47.
// 8 buttons (read reg 0x00, bit per button) + 9 WS2812 RGB LEDs
// (write 4 bytes per LED at 0x20+N*4 — RGB888 packed 0x00RRGGBB).
//
// Wired to the lock at GPIO8 (SDA), GPIO9 (SCL), 5V, GND.
//
// Phase 1 (this file): button-0 = NFC-style toggle Lock/Unlock,
// LED-0 mirrors LockState. Other buttons/LEDs are scaffolded for
// expansion (commissioning window, factoryreset, status pixels).

#pragma once

#include <esp_err.h>

namespace aliro {

class ByteButtonPanel {
public:
    static ByteButtonPanel & instance();
    esp_err_t init();      // probes I2C, spawns poll + render tasks. No-op on dup-init.
    bool isPresent() const;

    // Visual ping — flashes LED 7 blue for ~300ms over the white-idle
    // background. Called from any Matter hook (attribute change, command
    // callback, fabric event). Safe to call from any task.
    static void notifyMatterActivity();

    // NFC pipeline indicators on LEDs 0 + 1:
    //   LED 0: cyan blink while a card is being read (reading start →
    //          either tap result or timeout), then briefly mirrors result
    //          colour, then returns to lock-state mirror.
    //   LED 1: holds the result colour for ~2s post-tap, then mirrors
    //          lock state.
    static void notifyNfcReading();           // PN532 detected a tag
    static void notifyNfcResult(bool ok);     // auth pass / fail

    // Commissioning window state — drives LEDs 2 & 3 (blue pulse while
    // open, white when idle). Hooked to chip device events
    // kCommissioningWindowOpened / kCommissioningWindowClosed in
    // app_main.cpp so Apple Home's "Add Accessory" sequence is visible
    // on the panel even when the panel buttons weren't pressed.
    // duration_seconds = how long the window will remain open (default
    // 180s for both basic and our btn1 helper).
    static void notifyCommissioningOpened(uint32_t duration_seconds);
    static void notifyCommissioningClosed();

private:
    ByteButtonPanel() = default;
};

}  // namespace aliro
