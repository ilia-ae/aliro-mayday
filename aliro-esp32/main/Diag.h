#pragma once
//
// Diag — single-place diagnostics for aliro-esp32.
//
// Purpose: every layer of the firmware (Matter / WiFi / mDNS / NFC / NVS)
// reports its health through one common namespace, so you can debug
// pairing-vs-tap-vs-network issues by just reading the log.
//
// What this module produces:
//
//   * boot_snapshot() — once at startup, dumps a clear "this is the
//     starting state" block. Chip / IDF / app versions, boot reason,
//     reset counter, free heap, partition layout, compile time.
//
//   * start_heartbeat(interval_sec) — spawns a background task that
//     re-prints a compact one-screen status every N seconds: uptime,
//     heap, WiFi RSSI/IP, Matter fabric count, NFC last-activity. Drop
//     `interval_sec=0` to disable.
//
//   * note_nfc_event(uid, len, info) — call when the NFC stack sees
//     anything; updates an internal timestamp + counter so the heartbeat
//     can show "last tap N seconds ago, total taps M".
//
// All entries log under the `diag` tag with a consistent `[diag]` prefix
// so the output is grep-friendly. They are also routed through the same
// loggable / WebSocket sink that the rest of the firmware uses, so they
// show up in the device WebUI live log too.

#include <stddef.h>
#include <stdint.h>

namespace diag {

// One-shot startup dump. Call once after nvs_flash_init().
void boot_snapshot();

// Periodic status. interval_sec=0 disables. Safe to call once at startup.
void start_heartbeat(uint32_t interval_sec);

// Subscribes to WIFI_EVENT and IP_EVENT and logs every transition with the
// disconnect reason code. Call once during startup, BEFORE esp_matter::start
// (so we don't miss the first connection event).
void install_wifi_event_logger();

// Spawns a task that pings the configured default gateway every
// ping_interval_sec, measuring RTT and recording rolling stats.
// 0 disables.
void start_gateway_ping(uint32_t ping_interval_sec);

// Spawns a task that pings a specific peer (e.g. the controller iPhone)
// every interval_sec, with its own rolling stats reported under HB-C.
// Useful for verifying L3 reachability between the accessory and the
// controller — Apple Home "no connection" usually traces to multicast /
// mDNS / VLAN issues that a unicast ping still survives.
void start_peer_ping(uint8_t a, uint8_t b, uint8_t c, uint8_t d,
                     const char *label, uint32_t interval_sec);

// NFC layer hooks — wire these from NfcManager / Aliro reader once §7.3 lands.
//   tag_uid     : pointer to UID bytes (may be NULL)
//   tag_uid_len : length in bytes (4, 7, or 10 typically)
//   info        : free-form annotation, e.g. "ECP-Aliro" or "Mifare"
void note_nfc_event(const uint8_t *tag_uid, size_t tag_uid_len, const char *info);

// Aliro-flow hooks. These let app_main / future Aliro state machine record
// progress even when the actual transaction code lives elsewhere.
void note_aliro_phase(const char *phase, const char *detail);

}  // namespace diag
