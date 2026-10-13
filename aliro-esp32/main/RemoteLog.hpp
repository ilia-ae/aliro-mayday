// RemoteLog — in-memory ring buffer for ESP_LOG output, drained over
// HTTP by the admin panel so we can read serial-equivalent logs without
// a USB cable. Cursor-based: each line gets a monotonic 64-bit seq, the
// caller polls `/api/logs?since=<seq>` and gets every line written
// after that. If the buffer wrapped past their cursor, `dropped` tells
// them how many lines they missed.
//
// Concurrency: push() is called from any task (esp_log_set_vprintf
// hooks every ESP_LOGx call). fetch() is called from the httpd worker.
// A single FreeRTOS mutex guards both — contention is microseconds, no
// I/O happens under the lock.

#pragma once

#include <cstddef>
#include <cstdint>

namespace aliro {

class RemoteLog {
public:
    // Singleton. Lazily inits the mutex + ring buffer on first call.
    static RemoteLog & instance();

    // Append one fully-formatted log line (newline-terminated or not —
    // we strip a trailing \n, then re-add one when fetching). Lines
    // longer than kMaxLineLen are truncated. Safe from any task incl.
    // ISR-context callbacks (uses portMUX, not mutex, on those paths
    // — but we don't expect ESP_LOG from ISR so plain mutex is fine).
    void push(const char * line, size_t len);

    // Fill `out` with NUL-separated lines whose seq is > since. Returns
    // the number of *bytes* written into out. Each line ends with \n.
    // Sets *next to the seq of the last line written (or `since` if no
    // new lines), and *dropped to the number of lines lost since the
    // ring head moved past `since` (0 if caller is keeping up).
    size_t fetch(uint64_t since, char * out, size_t out_size,
                 uint64_t * next, uint64_t * dropped);

    // Snapshot of current state for diagnostics (admin panel sysinfo).
    struct Stats {
        uint64_t total_pushed;     // lifetime line count
        uint64_t head_seq;         // seq of oldest in-buffer line
        uint64_t tail_seq;         // seq of newest in-buffer line
        size_t   bytes_used;       // current ring fill
        size_t   bytes_capacity;   // == kRingBytes
    };
    Stats stats() const;

    // Bumped 16 K → 64 K. Boot snapshot (chip info, reset reason, fabric
    // load, DoorLock cluster commands, mDNS init) needs ~300+ lines to
    // remain reachable over /api/logs after a crash. At ~80 B/line the
    // previous 16 K cap held only the last ~200 lines, so the reset
    // reason scrolled off before we could pull it over WiFi. 64 K fits
    // ~800 lines, lives in PSRAM (fallback to internal RAM is rare).
    static constexpr size_t kRingBytes   = 64 * 1024;
    static constexpr size_t kMaxLineLen  = 256;

private:
    RemoteLog() = default;
};

}  // namespace aliro
