// RemoteLog — see header for contract.
//
// Storage layout: one growable string of \n-separated lines + a parallel
// array of (seq, byte_offset) markers per line. We always hold the
// LATEST kRingBytes worth of data; oldest lines fall off when a push
// would overflow. Each marker also tracks the line's payload length so
// fetch() can iterate without reparsing.
//
// Why not a fixed-size circular char buffer? Because lookup by seq
// becomes O(N scan) with arbitrary line lengths, and partial-line
// fragments at the wraparound point complicate fetch(). A vector of
// (seq, offset, len) is O(log N) lookup with binary search and
// trivial fetch. Total RAM: kRingBytes payload + ~6 KB of markers
// for ~200 lines — fits comfortably in PSRAM (we move the whole
// thing to PSRAM via heap_caps_malloc).

#include "RemoteLog.hpp"

#include <cstring>
#include <atomic>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace aliro {

namespace {

constexpr size_t kMaxLines = 512;       // ring of marker entries

struct Marker {
    uint64_t seq;
    size_t   offset;          // byte offset in s_buf
    size_t   len;             // payload length (no NUL, no \n)
};

char *      s_buf       = nullptr;      // ring of payload bytes (PSRAM)
size_t      s_buf_used  = 0;            // current bytes occupied
Marker *    s_markers   = nullptr;      // ring of (seq, offset, len)
size_t      s_marker_head = 0;          // index of oldest marker (== seq of head line)
size_t      s_marker_count = 0;         // number of valid markers (≤ kMaxLines)
uint64_t    s_total_pushed = 0;         // lifetime count
uint64_t    s_dropped      = 0;         // lines evicted from head

SemaphoreHandle_t s_mtx = nullptr;
std::atomic<bool> s_inited{false};

void ensure_init() {
    if (s_inited.load(std::memory_order_acquire)) return;
    // First call wins. Allocations happen exactly once.
    static portMUX_TYPE init_mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&init_mux);
    if (!s_inited.load(std::memory_order_relaxed)) {
        s_buf = (char *)heap_caps_malloc(RemoteLog::kRingBytes,
                                         MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
        if (!s_buf) {
            // Fall back to internal RAM if PSRAM disabled. Smaller is fine.
            s_buf = (char *)heap_caps_malloc(RemoteLog::kRingBytes,
                                             MALLOC_CAP_8BIT);
        }
        s_markers = (Marker *)heap_caps_malloc(sizeof(Marker) * kMaxLines,
                                               MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
        if (!s_markers) {
            s_markers = (Marker *)heap_caps_malloc(sizeof(Marker) * kMaxLines,
                                                   MALLOC_CAP_8BIT);
        }
        s_mtx = xSemaphoreCreateMutex();
        s_inited.store(true, std::memory_order_release);
    }
    portEXIT_CRITICAL(&init_mux);
}

// Drop the oldest line, freeing its bytes from the front of s_buf and
// shifting everything down. Caller holds s_mtx.
void drop_head_locked() {
    if (s_marker_count == 0) return;
    size_t head_idx = s_marker_head % kMaxLines;
    size_t evict_len = s_markers[head_idx].len + 1;   // +1 for the \n we wrote
    if (evict_len > s_buf_used) evict_len = s_buf_used;
    memmove(s_buf, s_buf + evict_len, s_buf_used - evict_len);
    s_buf_used -= evict_len;
    // All remaining markers' offsets shift down by evict_len.
    for (size_t i = 1; i < s_marker_count; ++i) {
        size_t idx = (s_marker_head + i) % kMaxLines;
        s_markers[idx].offset -= evict_len;
    }
    ++s_marker_head;
    --s_marker_count;
    ++s_dropped;
}

}  // namespace

RemoteLog & RemoteLog::instance() {
    static RemoteLog inst;
    return inst;
}

void RemoteLog::push(const char * line, size_t len) {
    if (!line || len == 0) return;
    ensure_init();
    if (!s_buf || !s_markers || !s_mtx) return;

    // Trim a trailing newline — we add our own when fetching, so the
    // marker.len matches the actual payload bytes stored.
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) --len;
    if (len == 0) return;
    if (len > kMaxLineLen) len = kMaxLineLen;

    if (xSemaphoreTake(s_mtx, pdMS_TO_TICKS(20)) != pdTRUE) return;

    // Make room: evict head lines until len + 1 (for \n) fits AND there's a
    // free marker slot.
    while (s_buf_used + len + 1 > kRingBytes && s_marker_count > 0) {
        drop_head_locked();
    }
    while (s_marker_count >= kMaxLines && s_marker_count > 0) {
        drop_head_locked();
    }

    // Append payload + \n (for fetch convenience).
    memcpy(s_buf + s_buf_used, line, len);
    s_buf[s_buf_used + len] = '\n';

    // Append marker.
    size_t tail_idx = (s_marker_head + s_marker_count) % kMaxLines;
    s_markers[tail_idx] = Marker{
        .seq    = ++s_total_pushed,
        .offset = s_buf_used,
        .len    = len,
    };
    s_buf_used += len + 1;
    ++s_marker_count;

    xSemaphoreGive(s_mtx);
}

size_t RemoteLog::fetch(uint64_t since, char * out, size_t out_size,
                        uint64_t * next, uint64_t * dropped) {
    if (next)    *next = since;
    if (dropped) *dropped = 0;
    if (!out || out_size == 0) return 0;
    ensure_init();
    if (!s_buf || !s_markers || !s_mtx) return 0;

    if (xSemaphoreTake(s_mtx, pdMS_TO_TICKS(20)) != pdTRUE) return 0;

    if (s_marker_count == 0) {
        if (next) *next = s_total_pushed;
        xSemaphoreGive(s_mtx);
        return 0;
    }

    uint64_t oldest_seq = s_markers[s_marker_head % kMaxLines].seq;
    uint64_t newest_seq = s_markers[(s_marker_head + s_marker_count - 1) % kMaxLines].seq;

    // Caller fell off the back of the ring — report dropped + start from
    // the current head.
    if (since + 1 < oldest_seq) {
        if (dropped) *dropped = oldest_seq - since - 1;
        since = oldest_seq - 1;
    }

    if (since >= newest_seq) {
        if (next) *next = newest_seq;
        xSemaphoreGive(s_mtx);
        return 0;
    }

    // Find the first marker with seq > since. Markers are ordered by
    // seq, so we can scan forward from head — kMaxLines is bounded so
    // worst case is fine.
    size_t written = 0;
    uint64_t last_seq = since;
    for (size_t i = 0; i < s_marker_count; ++i) {
        size_t idx = (s_marker_head + i) % kMaxLines;
        if (s_markers[idx].seq <= since) continue;
        size_t need = s_markers[idx].len + 1;     // +\n
        if (written + need > out_size) break;
        memcpy(out + written, s_buf + s_markers[idx].offset, s_markers[idx].len);
        out[written + s_markers[idx].len] = '\n';
        written += need;
        last_seq = s_markers[idx].seq;
    }

    if (next) *next = last_seq;
    xSemaphoreGive(s_mtx);
    return written;
}

RemoteLog::Stats RemoteLog::stats() const {
    Stats s{};
    if (!s_inited.load()) return s;
    if (s_mtx && xSemaphoreTake(s_mtx, pdMS_TO_TICKS(20)) == pdTRUE) {
        s.total_pushed   = s_total_pushed;
        s.bytes_used     = s_buf_used;
        s.bytes_capacity = kRingBytes;
        if (s_marker_count > 0) {
            s.head_seq = s_markers[s_marker_head % kMaxLines].seq;
            s.tail_seq = s_markers[(s_marker_head + s_marker_count - 1) % kMaxLines].seq;
        }
        xSemaphoreGive(s_mtx);
    }
    return s;
}

}  // namespace aliro
