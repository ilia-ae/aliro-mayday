// OledPanel — M5Stack Unit OLED 1.3" SH1107 (128×64) on PORT.A I2C.
// Shows live status of the lock: WiFi / Matter / Aliro / lock state /
// last NFC tap / uptime.
//
// Optional, gated by CONFIG_ALIRO_OLED_PANEL. Init is non-fatal — if
// the unit is absent or NACKs at boot we just keep running headless.

#pragma once

#include <esp_err.h>

namespace aliro {

class OledPanel {
public:
    static OledPanel & instance();
    esp_err_t init();
    bool isPresent() const;

    // Surface raw events from the rest of the firmware so the OLED can
    // show "last action" lines. Safe to call from any task.
    static void notifyTap(const char * uid_hex, const char * label);
    static void notifyMatterUnlock(const char * source);
    static void notifyMatterLock  (const char * source);

    // Mark the start of a tap pipeline (called from
    // lockind::nfc_reading_start). Pairs with notifyTap() to give
    // total tap-to-unlock latency on the panel. Cost: 2 * read of
    // esp_timer (single 64-bit register), no syscalls.
    static void notifyTapStart();

    // Per-stage timing breakdown — T0 = tap start (above), T1 = AID
    // selected, T2 = DDK auth started, T3 = DDK auth complete. Final
    // stage T4 = lock state changed = notifyTap(). Each stage costs
    // a single esp_timer_get_time() call (~10ns).
    static void notifyAidSelected();
    static void notifyAuthStart();
    static void notifyAuthDone();

    // Counters surfaced on the OLED.
    static void notifyAuthFail();          // increments fail counter
    static void notifyCacheHit();          // precompute cache hit
    static void notifyCacheMiss();         // precompute cache miss

    // Boot-phase signal — NfcManager calls this after the PN532 firmware
    // version has been read successfully. Other phases (WiFi, NTP, Matter,
    // Aliro) are polled by the render task itself, but the NFC stack
    // doesn't have a polling-friendly "is ready" hook.
    static void notifyNfcReady();

    // OTA progress surface. When `bytes_total` > 0, the render task
    // switches to a full-screen progress bar with "OTA <pct>% — <KB>K
    // / <KB>K" until OTA finishes (set with bytes_total=0 to clear).
    // Called from WebServerManager otaTask on every chunk + on enter/exit.
    static void notifyOtaProgress(size_t bytes_received, size_t bytes_total);

    // True once all five boot phases (NFC reader / WiFi / NTP / Matter /
    // Aliro provisioning) have been observed ready at least once, OR
    // the 30 s force-ready timeout has fired. App-main uses this to
    // gate NfcManager polling so a tap during boot doesn't auth before
    // Matter has loaded fabrics. Safe to call from any task; reads a
    // single atomic.
    static bool isAllReady();

    // Snapshot of last-tap timing + cumulative counters, for telemetry
    // sinks (admin panel WebSocket /ws metrics frame). All fields use
    // -1 / 0 sentinels when no measurement is available yet. Reads are
    // lock-free atomics, safe from any task.
    struct TapMetrics {
        int32_t  total_ms;     // T0..T4 — full tap latency, -1 if unknown
        int32_t  detect_ms;    // T0..T1 — PN532 detect + AID select
        int32_t  prep_ms;      // T1..T2 — auth setup
        int32_t  auth_ms;      // T2..T3 — DDK FastAuth crypto
        int32_t  lock_ms;      // T3..T4 — SetLockState dispatch
        uint32_t tap_count;    // total successful taps since boot
        uint32_t tap_fail;     // total failed/unrecognised taps
        uint32_t cache_hit;    // DDK precompute cache hits
        uint32_t cache_miss;   // DDK precompute cache misses
    };
    static TapMetrics getTapMetrics();

private:
    OledPanel() = default;
};

}  // namespace aliro
