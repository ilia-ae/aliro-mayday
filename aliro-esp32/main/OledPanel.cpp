#include "SystemClock.hpp"
// OledPanel — minimal SH1107 driver + status renderer.
//
// 128×64 OLED, I2C @ 0x3C. Layout: 8 horizontal pages × 128 columns.
// Each page is 1 byte = 8 vertical pixels. Total framebuffer = 1024 bytes.
//
// Why a hand-rolled driver and not lvgl/u8g2: this lock already pulls
// in esp-matter + arduino-esp32 + littlefs + admin_panel + libsodium.
// A graphics library would add 50–100 KB and a host of build-flag
// reshuffles. We need eight rows of text — 300 lines is enough.

#include "OledPanel.hpp"

#include <esp_attr.h>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <esp_sntp.h>
#include <esp_freertos_hooks.h>
#include <driver/temperature_sensor.h>
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/idf_additions.h>

#include <app/server/Server.h>
#include <credentials/FabricTable.h>
#include <esp_matter.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <app-common/zap-generated/attributes/Accessors.h>

#include "I2cBus.hpp"
#include "AliroDelegate.h"

namespace {

constexpr const char * TAG        = "oled";
constexpr uint8_t      kI2cAddr   = 0x3C;
constexpr uint32_t     kI2cFreqHz = 400'000;     // SH1107 happy at 400kHz

// SH1107 native panel = 64 wide × 128 tall (portrait). M5 markets it as
// "128×64" because the case is mounted in landscape orientation. To get
// readable horizontal text we draw to a logical 128×64 landscape buffer
// and rotate 90° CW into the native 16-page × 64-column layout when
// flushing.
constexpr int          kLogicalW  = 128;          // landscape width
constexpr int          kLogicalH  = 64;           // landscape height
constexpr int          kNativeW   = 64;           // native columns
constexpr int          kNativeH   = 128;          // native rows
constexpr int          kPages     = 16;           // native pages = 128 / 8

i2c_master_dev_handle_t s_dev = nullptr;
std::atomic<bool>       s_present{false};

// Native framebuffer, 16 pages × 64 columns = 1024 bytes. One byte
// covers 8 vertical pixels of one column on a single page.
uint8_t s_fb[kPages * kNativeW] = {};

// ===== Last-action telemetry, set from Notify*() callbacks. ============
struct LastAction {
    char text[24];
    int64_t at_ms;
};
LastAction s_last_action = {{0}, 0};
SemaphoreHandle_t s_action_mtx = nullptr;
constexpr uint16_t kDoorLockEp = 1;

// Total accepted taps since boot — increments in notifyTap() (which is
// only called on a successful unlock/lock from NFC).
std::atomic<uint32_t> s_tap_count{0};
std::atomic<uint32_t> s_tap_fail{0};
std::atomic<uint32_t> s_cache_hit{0};
std::atomic<uint32_t> s_cache_miss{0};

// Tap latency tracking — set on nfc_reading_start, sampled on
// notifyTap(). Atomics so cross-task is safe without locking.
std::atomic<int64_t> s_tap_start_us{0};         // T0 — card detected
std::atomic<int64_t> s_aid_us{0};               // T1 — AID selected
std::atomic<int64_t> s_auth_start_us{0};        // T2 — DDK auth begin
std::atomic<int64_t> s_auth_done_us{0};         // T3 — DDK auth complete
std::atomic<int32_t> s_last_tap_ms{-1};         // -1 = no measurement yet
// Last per-stage breakdown in ms (rounded). -1 = unknown.
std::atomic<int32_t> s_last_detect_ms{-1};      // T0..T1 (PN532+AID)
std::atomic<int32_t> s_last_prep_ms{-1};        // T1..T2 (auth setup)
std::atomic<int32_t> s_last_auth_ms{-1};        // T2..T3 (FastAuth crypto)
std::atomic<int32_t> s_last_lock_ms{-1};        // T3..T4 (SetLockState)

// CPU + temperature telemetry. The CPU% sampler uses idle-hook counters;
// the calibrated baseline is set in OledPanel::init() from a 200ms idle-
// only sample window. Temperature sensor uses the on-die ESP32-S3 sensor.
volatile uint32_t s_idle_ticks_core0 = 0;
volatile uint32_t s_idle_ticks_core1 = 0;
uint32_t s_idle_baseline_per_sec = 0;

// ===== Boot-phase tracking ============================================
// Five checkpoints the lock has to clear before the operator can rely on
// it. The render task shows a checklist splash until all five are green;
// then a 1.5s "READY" frame; then the normal status screen. Phases that
// flip back to false later (e.g. WiFi disconnect) still surface — they
// drive the alert rotation on the status screen, not a re-show of the
// boot splash.
//
// NFC is push-driven (NfcManager calls notifyNfcReady after PN532 firmware
// version read). Everything else is polled in collect_phases().
//
// All five phases are STICKY: once they have ever been observed true we
// keep them true for the rest of the boot. Otherwise transient blips
// (chip_stack_lock timing out under Apple Home's 210ms LockState poll,
// SNTP losing sync, etc.) flicker phases off and the splash latch never
// fires. The splash is a "did we successfully boot once" indicator, not
// a live health monitor — that role is the alert rotation on the main
// screen, which has access to the Status struct and can detect drops.
std::atomic<bool>    s_phase_nfc{false};

// OTA progress state — set by WebServerManager otaTask. While bytes_total>0
// the render task draws a full-screen OTA progress bar instead of the usual
// status. Cleared when OTA completes (success or fail).
std::atomic<uint32_t> s_ota_bytes_received{0};
std::atomic<uint32_t> s_ota_bytes_total{0};
std::atomic<bool>    s_phase_wifi_ever{false};
std::atomic<bool>    s_phase_ntp_ever{false};
std::atomic<bool>    s_phase_matter_ever{false};
std::atomic<bool>    s_phase_aliro_ever{false};
std::atomic<int64_t> s_all_ready_at_ms{0};   // > 0 once all 5 went green
constexpr int64_t    kReadySplashMs = 1500;  // hold "READY" for this long
}  // namespace (close so the C-linkage idle hooks below are visible to the
   //              esp_register_freertos_idle_hook_for_cpu API)

// IRAM_ATTR is MANDATORY here. ESP-IDF's freertos_hooks.c calls every
// registered idle hook from the idle task; if any hook lives in flash,
// the idle task panics with InstructionFetchError the moment another
// task (NVS write via ReaderDataManager, Matter NVS, mbedtls heap) has
// disabled the flash cache mid-write. The crash signature is
//   Guru Meditation (InstructionFetchError) on Core 0
//   PC = 0x3fc8xxxx with the next instruction in our hook
// followed by a corrupted backtrace and a hard reset every ~30-60s of
// normal operation. See commit history for the regression introduced
// when these hooks were first added without IRAM placement.
extern "C" {
static IRAM_ATTR bool oled_idle_hook_core0() { ++s_idle_ticks_core0; return true; }
static IRAM_ATTR bool oled_idle_hook_core1() { ++s_idle_ticks_core1; return true; }
}

namespace {
int   g_cpu_usage_pct = -1;
float g_temp_c        = -1000.0f;
temperature_sensor_handle_t s_temp_sensor = nullptr;

void sample_cpu_temp() {
    static int64_t last_us = 0;
    static uint32_t last_idle0 = 0, last_idle1 = 0;
    int64_t now_us = esp_timer_get_time();
    if (last_us == 0) {
        last_us = now_us;
        last_idle0 = s_idle_ticks_core0;
        last_idle1 = s_idle_ticks_core1;
        return;
    }
    int64_t dt = now_us - last_us;
    if (dt < 200000) return;
    uint32_t d0 = s_idle_ticks_core0 - last_idle0;
    uint32_t d1 = s_idle_ticks_core1 - last_idle1;
    last_idle0 = s_idle_ticks_core0;
    last_idle1 = s_idle_ticks_core1;
    last_us = now_us;
    if (s_idle_baseline_per_sec > 0) {
        uint32_t expected = (uint32_t)((uint64_t)s_idle_baseline_per_sec * dt / 1000000ULL);
        uint32_t observed = (d0 + d1) / 2;
        if (expected > 0) {
            int idle_pct = (int)((100 * observed) / expected);
            if (idle_pct > 100) idle_pct = 100;
            if (idle_pct < 0)   idle_pct = 0;
            g_cpu_usage_pct = 100 - idle_pct;
        }
    }
    if (s_temp_sensor) {
        float t = 0;
        if (temperature_sensor_get_celsius(s_temp_sensor, &t) == ESP_OK) {
            g_temp_c = t;
        }
    }
}

// ===== 5×7 ASCII font (printable 0x20..0x7E). Each glyph = 5 bytes,
//       each byte = 1 column of 8 pixels (LSB top). Public-domain
//       "linux console" / "Adafruit GFX classic" style, trimmed to
//       printable range to keep flash footprint tight (~480 bytes). =====
const uint8_t k5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // 0x20 ' '
    {0x00,0x00,0x5F,0x00,0x00}, // 0x21 '!'
    {0x00,0x07,0x00,0x07,0x00}, // 0x22 '"'
    {0x14,0x7F,0x14,0x7F,0x14}, // 0x23 '#'
    {0x24,0x2A,0x7F,0x2A,0x12}, // 0x24 '$'
    {0x23,0x13,0x08,0x64,0x62}, // 0x25 '%'
    {0x36,0x49,0x55,0x22,0x50}, // 0x26 '&'
    {0x00,0x05,0x03,0x00,0x00}, // 0x27 '''
    {0x00,0x1C,0x22,0x41,0x00}, // 0x28 '('
    {0x00,0x41,0x22,0x1C,0x00}, // 0x29 ')'
    {0x14,0x08,0x3E,0x08,0x14}, // 0x2A '*'
    {0x08,0x08,0x3E,0x08,0x08}, // 0x2B '+'
    {0x00,0x50,0x30,0x00,0x00}, // 0x2C ','
    {0x08,0x08,0x08,0x08,0x08}, // 0x2D '-'
    {0x00,0x60,0x60,0x00,0x00}, // 0x2E '.'
    {0x20,0x10,0x08,0x04,0x02}, // 0x2F '/'
    {0x3E,0x51,0x49,0x45,0x3E}, // 0x30 '0'
    {0x00,0x42,0x7F,0x40,0x00}, // 0x31 '1'
    {0x42,0x61,0x51,0x49,0x46}, // 0x32 '2'
    {0x21,0x41,0x45,0x4B,0x31}, // 0x33 '3'
    {0x18,0x14,0x12,0x7F,0x10}, // 0x34 '4'
    {0x27,0x45,0x45,0x45,0x39}, // 0x35 '5'
    {0x3C,0x4A,0x49,0x49,0x30}, // 0x36 '6'
    {0x01,0x71,0x09,0x05,0x03}, // 0x37 '7'
    {0x36,0x49,0x49,0x49,0x36}, // 0x38 '8'
    {0x06,0x49,0x49,0x29,0x1E}, // 0x39 '9'
    {0x00,0x36,0x36,0x00,0x00}, // 0x3A ':'
    {0x00,0x56,0x36,0x00,0x00}, // 0x3B ';'
    {0x00,0x08,0x14,0x22,0x41}, // 0x3C '<'
    {0x14,0x14,0x14,0x14,0x14}, // 0x3D '='
    {0x41,0x22,0x14,0x08,0x00}, // 0x3E '>'
    {0x02,0x01,0x51,0x09,0x06}, // 0x3F '?'
    {0x32,0x49,0x79,0x41,0x3E}, // 0x40 '@'
    {0x7E,0x11,0x11,0x11,0x7E}, // 0x41 'A'
    {0x7F,0x49,0x49,0x49,0x36}, // 0x42 'B'
    {0x3E,0x41,0x41,0x41,0x22}, // 0x43 'C'
    {0x7F,0x41,0x41,0x22,0x1C}, // 0x44 'D'
    {0x7F,0x49,0x49,0x49,0x41}, // 0x45 'E'
    {0x7F,0x09,0x09,0x09,0x01}, // 0x46 'F'
    {0x3E,0x41,0x49,0x49,0x7A}, // 0x47 'G'
    {0x7F,0x08,0x08,0x08,0x7F}, // 0x48 'H'
    {0x00,0x41,0x7F,0x41,0x00}, // 0x49 'I'
    {0x20,0x40,0x41,0x3F,0x01}, // 0x4A 'J'
    {0x7F,0x08,0x14,0x22,0x41}, // 0x4B 'K'
    {0x7F,0x40,0x40,0x40,0x40}, // 0x4C 'L'
    {0x7F,0x02,0x0C,0x02,0x7F}, // 0x4D 'M'
    {0x7F,0x04,0x08,0x10,0x7F}, // 0x4E 'N'
    {0x3E,0x41,0x41,0x41,0x3E}, // 0x4F 'O'
    {0x7F,0x09,0x09,0x09,0x06}, // 0x50 'P'
    {0x3E,0x41,0x51,0x21,0x5E}, // 0x51 'Q'
    {0x7F,0x09,0x19,0x29,0x46}, // 0x52 'R'
    {0x46,0x49,0x49,0x49,0x31}, // 0x53 'S'
    {0x01,0x01,0x7F,0x01,0x01}, // 0x54 'T'
    {0x3F,0x40,0x40,0x40,0x3F}, // 0x55 'U'
    {0x1F,0x20,0x40,0x20,0x1F}, // 0x56 'V'
    {0x3F,0x40,0x38,0x40,0x3F}, // 0x57 'W'
    {0x63,0x14,0x08,0x14,0x63}, // 0x58 'X'
    {0x07,0x08,0x70,0x08,0x07}, // 0x59 'Y'
    {0x61,0x51,0x49,0x45,0x43}, // 0x5A 'Z'
    {0x00,0x7F,0x41,0x41,0x00}, // 0x5B '['
    {0x02,0x04,0x08,0x10,0x20}, // 0x5C '\'
    {0x00,0x41,0x41,0x7F,0x00}, // 0x5D ']'
    {0x04,0x02,0x01,0x02,0x04}, // 0x5E '^'
    {0x40,0x40,0x40,0x40,0x40}, // 0x5F '_'
    {0x00,0x01,0x02,0x04,0x00}, // 0x60 '`'
    {0x20,0x54,0x54,0x54,0x78}, // 0x61 'a'
    {0x7F,0x48,0x44,0x44,0x38}, // 0x62 'b'
    {0x38,0x44,0x44,0x44,0x20}, // 0x63 'c'
    {0x38,0x44,0x44,0x48,0x7F}, // 0x64 'd'
    {0x38,0x54,0x54,0x54,0x18}, // 0x65 'e'
    {0x08,0x7E,0x09,0x01,0x02}, // 0x66 'f'
    {0x0C,0x52,0x52,0x52,0x3E}, // 0x67 'g'
    {0x7F,0x08,0x04,0x04,0x78}, // 0x68 'h'
    {0x00,0x44,0x7D,0x40,0x00}, // 0x69 'i'
    {0x20,0x40,0x44,0x3D,0x00}, // 0x6A 'j'
    {0x7F,0x10,0x28,0x44,0x00}, // 0x6B 'k'
    {0x00,0x41,0x7F,0x40,0x00}, // 0x6C 'l'
    {0x7C,0x04,0x18,0x04,0x78}, // 0x6D 'm'
    {0x7C,0x08,0x04,0x04,0x78}, // 0x6E 'n'
    {0x38,0x44,0x44,0x44,0x38}, // 0x6F 'o'
    {0x7C,0x14,0x14,0x14,0x08}, // 0x70 'p'
    {0x08,0x14,0x14,0x18,0x7C}, // 0x71 'q'
    {0x7C,0x08,0x04,0x04,0x08}, // 0x72 'r'
    {0x48,0x54,0x54,0x54,0x20}, // 0x73 's'
    {0x04,0x3F,0x44,0x40,0x20}, // 0x74 't'
    {0x3C,0x40,0x40,0x20,0x7C}, // 0x75 'u'
    {0x1C,0x20,0x40,0x20,0x1C}, // 0x76 'v'
    {0x3C,0x40,0x30,0x40,0x3C}, // 0x77 'w'
    {0x44,0x28,0x10,0x28,0x44}, // 0x78 'x'
    {0x0C,0x50,0x50,0x50,0x3C}, // 0x79 'y'
    {0x44,0x64,0x54,0x4C,0x44}, // 0x7A 'z'
    {0x00,0x08,0x36,0x41,0x00}, // 0x7B '{'
    {0x00,0x00,0x7F,0x00,0x00}, // 0x7C '|'
    {0x00,0x41,0x36,0x08,0x00}, // 0x7D '}'
    {0x10,0x08,0x08,0x10,0x08}, // 0x7E '~'
};

// ===== I2C primitives. SH1107 control byte: 0x00 = command, 0x40 = data.
esp_err_t send_cmd(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};
    return i2c_master_transmit(s_dev, buf, 2, pdMS_TO_TICKS(20));
}

esp_err_t send_data(const uint8_t * data, size_t len) {
    // Prepend the 0x40 control byte. SH1107 lets you send a stream of
    // data bytes after a single control byte, so a single transmit per
    // page is enough.
    static uint8_t buf[1 + 128];
    if (len > sizeof(buf) - 1) return ESP_ERR_INVALID_SIZE;
    buf[0] = 0x40;
    memcpy(buf + 1, data, len);
    return i2c_master_transmit(s_dev, buf, 1 + len, pdMS_TO_TICKS(30));
}

// ===== SH1107 init sequence (taken from the M5Stack Unit OLED Arduino
//       lib + datasheet). 128×64 landscape mode, normal display, ~50%
//       contrast (the panel is plenty bright at this setting).
bool sh1107_init() {
    const uint8_t init_cmds[] = {
        0xAE,             // Display off
        0xDC, 0x00,       // Display start line = 0
        0x81, 0x4F,       // Contrast = 0x4F (~31%)
        0x20,             // Memory mode = page (default)
        0xA0,             // Segment remap = normal
        0xC0,             // COM scan direction = normal
        0xA8, 0x3F,       // Multiplex ratio = 64
        0xD3, 0x60,       // Display offset = 0x60 (1.3" panel)
        0xD5, 0x51,       // Clock divide ratio
        0xD9, 0x22,       // Pre-charge
        0xDB, 0x35,       // VCOMH deselect
        0xAD, 0x8A,       // Charge pump enable (internal VPP)
        0xA4,             // Output follows GDDRAM
        0xA6,             // Normal (not inverted)
        0xAF,             // Display on
    };
    for (uint8_t c : init_cmds) {
        if (send_cmd(c) != ESP_OK) return false;
    }
    return true;
}

// Flush all 16 native pages × 64 columns. One I2C transmit per page.
void flush_all() {
    for (int p = 0; p < kPages; ++p) {
        send_cmd(0xB0 | p);          // page address
        send_cmd(0x00);              // column low  nibble = 0
        send_cmd(0x10);              // column high nibble = 0
        send_data(s_fb + p * kNativeW, kNativeW);
    }
}

void clear_fb() { memset(s_fb, 0, sizeof(s_fb)); }

// Set one pixel in landscape (x, y) coords (0..127 × 0..63), rotated
// 90° CCW (= 180° from the previous CW mapping) so the panel reads
// upside-down compared to v1.
//   col = kLogicalH - 1 - y     (0..63)
//   row = x                     (0..127)
inline void put_pixel(int x, int y, bool on) {
    if (x < 0 || x >= kLogicalW || y < 0 || y >= kLogicalH) return;
    int col  = kLogicalH - 1 - y;
    int row  = x;
    int page = row / 8;
    int bit  = row & 7;
    int idx  = page * kNativeW + col;
    if (on) s_fb[idx] |=  (1u << bit);
    else    s_fb[idx] &= ~(1u << bit);
}

// Draw a single 5×7 char in landscape coords. page_l = which 8-pixel
// row band in the landscape layout (0..7).
void draw_char(int x, int page_l, char c) {
    if (page_l < 0 || page_l >= 8)         return;
    if (x < 0 || x + 6 > kLogicalW)        return;
    if (c < 0x20 || c > 0x7E) c = '?';
    const uint8_t * g = k5x7[c - 0x20];
    int y_base = page_l * 8;
    for (int gx = 0; gx < 5; ++gx) {
        uint8_t col = g[gx];
        for (int gy = 0; gy < 8; ++gy) {
            put_pixel(x + gx, y_base + gy, (col >> gy) & 1);
        }
    }
    // Spacer column — clear 8 pixels.
    for (int gy = 0; gy < 8; ++gy) put_pixel(x + 5, y_base + gy, false);
}

void draw_str(int x, int page_l, const char * s) {
    while (*s && x + 6 <= kLogicalW) {
        draw_char(x, page_l, *s);
        x += 6;
        ++s;
    }
}

void draw_strf(int x, int page_l, const char * fmt, ...) {
    char buf[32];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    draw_str(x, page_l, buf);
}

// Double-size 5×7 -> 10×14 (each font pixel becomes 2×2). Spans two
// landscape pages (16 vertical pixels, with 2 padding). Used for the
// big LOCK / OPEN headline.
void draw_char_2x(int x, int page_l, char c) {
    if (page_l < 0 || page_l > 6)              return;
    if (x < 0 || x + 12 > kLogicalW)           return;
    if (c < 0x20 || c > 0x7E) c = '?';
    const uint8_t * g = k5x7[c - 0x20];
    int y_base = page_l * 8;
    for (int gx = 0; gx < 5; ++gx) {
        uint8_t col = g[gx];
        for (int gy = 0; gy < 7; ++gy) {        // height 7 → upscaled to 14
            bool on = (col >> gy) & 1;
            int px = x + gx * 2;
            int py = y_base + gy * 2;
            put_pixel(px,     py,     on);
            put_pixel(px + 1, py,     on);
            put_pixel(px,     py + 1, on);
            put_pixel(px + 1, py + 1, on);
        }
    }
    for (int dy = 0; dy < 14; ++dy) {           // 2-px spacer columns
        put_pixel(x + 10, y_base + dy, false);
        put_pixel(x + 11, y_base + dy, false);
    }
}

void draw_str_2x(int x, int page_l, const char * s) {
    while (*s && x + 12 <= kLogicalW) {
        draw_char_2x(x, page_l, *s);
        x += 12;
        ++s;
    }
}

// ===== Pixel-art icon system. Icons are stored row-major, 1 bit per
// pixel, MSB = leftmost pixel of the row. Width must be ≤ 16 for the
// uint16_t row type. Trivially extensible — add new bitmaps and call
// draw_icon().
template <int W, int H>
void draw_icon(int x, int y, const uint16_t (&rows)[H]) {
    for (int dy = 0; dy < H; ++dy) {
        uint16_t r = rows[dy];
        for (int dx = 0; dx < W; ++dx) {
            bool on = (r >> (W - 1 - dx)) & 1;
            if (on) put_pixel(x + dx, y + dy, true);
        }
    }
}

// 14×14 padlock — closed state. Drawn from top-down: arch (rows 0-5),
// body (rows 6-13), keyhole notch in the body.
const uint16_t kIconLockClosed[14] = {
    0b00001111100000,
    0b00011000110000,
    0b00110000011000,
    0b00110000011000,
    0b00110000011000,
    0b00110000011000,
    0b01111111111100,
    0b01111111111100,
    0b01110011111100,
    0b01110011111100,
    0b01110011111100,
    0b01111111111100,
    0b01111111111100,
    0b01111111111100,
};

// 14×14 padlock — open state. Arch hinged on the right and tilted up.
const uint16_t kIconLockOpen[14] = {
    0b00000000111110,
    0b00000001100011,
    0b00000011000011,
    0b00000011000011,
    0b00000011000000,
    0b00000011000000,
    0b01111111111100,
    0b01111111111100,
    0b01110011111100,
    0b01110011111100,
    0b01110011111100,
    0b01111111111100,
    0b01111111111100,
    0b01111111111100,
};

// 14×14 warning triangle with exclamation mark (used for alerts on top row).
const uint16_t kIconWarn[14] = {
    0b00000011000000,
    0b00000011000000,
    0b00000111100000,
    0b00000111100000,
    0b00001100110000,
    0b00001101110000,
    0b00011001011000,
    0b00011000011000,
    0b00110000001100,
    0b00110001101100,
    0b01100001100110,
    0b01100000000110,
    0b11111111111111,
    0b11111111111111,
};

// 8×8 magnifier (Detect — PN532 polling + AID select).
const uint16_t kIconDetect[8] = {
    0b00111100,
    0b01000010,
    0b10000001,
    0b10000001,
    0b10000001,
    0b01000110,
    0b00111100,
    0b00000011,
};

// 8×8 hourglass (Prep — between AID select and DDK begin).
const uint16_t kIconPrep[8] = {
    0b11111111,
    0b01000010,
    0b00100100,
    0b00011000,
    0b00011000,
    0b00100100,
    0b01000010,
    0b11111111,
};

// 8×8 key (Auth — DDK FastAuth crypto roundtrip).
const uint16_t kIconAuth[8] = {
    0b00111100,
    0b01000010,
    0b01000010,
    0b00111100,
    0b00010000,
    0b00010000,
    0b00011100,
    0b00010100,
};

// 8×8 small lock (Lock — SetLockState attribute write).
const uint16_t kIconLockMini[8] = {
    0b00111100,
    0b01000010,
    0b01000010,
    0b11111111,
    0b11011011,
    0b11011011,
    0b11111111,
    0b00000000,
};

// ===== Boot-phase snapshot. Mirrors the five checkpoints on the boot
// splash. Polled cheaply each refresh — no I/O, just memory reads.
struct Phases {
    bool nfc;          // PN532 firmware version successfully read
    bool wifi;         // STA associated AND has a non-zero IPv4 address
    bool ntp;          // wall-clock has rolled past 2020-01-01 (sntp ok)
    bool matter;       // chip::Server is up and either has a fabric or is
                       // currently in commissioning window
    bool aliro;        // AliroDelegate has been provisioned with reader
                       // group identifier (= "Add Accessory" finished)
    char wifi_ssid[33];
    char ip_addr[16];
    uint8_t fabric_count;
};

// Read all five phases. Each phase latches true and never goes back —
// this is a boot-progress indicator, not a live health monitor.
void collect_phases(Phases & p) {
    p.nfc          = s_phase_nfc.load();
    p.wifi_ssid[0] = 0;
    strcpy(p.ip_addr, "0.0.0.0");

    bool wifi_now = false;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        memcpy(p.wifi_ssid, ap.ssid, sizeof(ap.ssid));
        p.wifi_ssid[sizeof(p.wifi_ssid) - 1] = 0;
        esp_netif_t * sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (sta) {
            esp_netif_ip_info_t ip;
            if (esp_netif_get_ip_info(sta, &ip) == ESP_OK && ip.ip.addr != 0) {
                snprintf(p.ip_addr, sizeof(p.ip_addr), IPSTR, IP2STR(&ip.ip));
                wifi_now = true;
            }
        }
    }
    if (wifi_now) s_phase_wifi_ever.store(true);
    p.wifi = s_phase_wifi_ever.load();

    if (::time(nullptr) > 1577836800) s_phase_ntp_ever.store(true);   // 2020-01-01
    p.ntp = s_phase_ntp_ever.load();

    p.fabric_count = 0;
    bool matter_now = false;
    {
        // chip::Server may not be initialised yet very early in boot —
        // GetInstance() is fine to call but FabricTable touches state
        // that's only valid post-init. Guard with the chip stack lock.
        // Under Apple Home's 210ms LockState polling we can't always
        // acquire the lock in 20ms, so a single failure here would mean
        // we never latch matter — hence the sticky pattern below.
        using esp_matter::lock::chip_stack_lock;
        using esp_matter::lock::chip_stack_unlock;
        if (chip_stack_lock(pdMS_TO_TICKS(20)) == esp_matter::lock::SUCCESS) {
            p.fabric_count = chip::Server::GetInstance().GetFabricTable().FabricCount();
            bool window_open = chip::Server::GetInstance().GetCommissioningWindowManager()
                                   .IsCommissioningWindowOpen();
            chip_stack_unlock();
            matter_now = (p.fabric_count > 0) || window_open;
        }
    }
    if (matter_now) s_phase_matter_ever.store(true);
    p.matter = s_phase_matter_ever.load();

    if (aliro::instance().isProvisioned()) s_phase_aliro_ever.store(true);
    p.aliro = s_phase_aliro_ever.load();
}

// ===== Status snapshot — collected under chip stack lock once per
//       refresh cycle. Strings live in fixed buffers, not std::string,
//       so the render task doesn't churn heap.
struct Status {
    char     wifi_ssid[33];
    int8_t   wifi_rssi;
    char     ip_addr[16];
    bool     wifi_up;
    uint8_t  fabric_count;
    bool     aliro_provisioned;
    chip::app::Clusters::DoorLock::DlLockState lock_state;
    uint32_t uptime_s;
    uint32_t free_heap;
    char     last_action[24];
    char     reader_gid[18];          // first ~6 hex chars of Aliro reader GID
};

void collect(Status & s) {
    using esp_matter::lock::chip_stack_lock;
    using esp_matter::lock::chip_stack_unlock;

    s.wifi_up = false;
    s.wifi_ssid[0] = 0;
    strcpy(s.ip_addr, "-");
    s.wifi_rssi = 0;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        s.wifi_up = true;
        s.wifi_rssi = ap.rssi;
        memcpy(s.wifi_ssid, ap.ssid, sizeof(ap.ssid));
        s.wifi_ssid[sizeof(s.wifi_ssid) - 1] = 0;
        esp_netif_t * sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (sta) {
            esp_netif_ip_info_t ip;
            if (esp_netif_get_ip_info(sta, &ip) == ESP_OK) {
                snprintf(s.ip_addr, sizeof(s.ip_addr), IPSTR, IP2STR(&ip.ip));
            }
        }
    }

    s.lock_state    = chip::app::Clusters::DoorLock::DlLockState::kNotFullyLocked;
    s.fabric_count  = 0;
    int64_t lock_t0 = esp_timer_get_time();
    if (chip_stack_lock(pdMS_TO_TICKS(50)) == esp_matter::lock::SUCCESS) {
        int64_t held_t0 = esp_timer_get_time();
        chip::app::DataModel::Nullable<chip::app::Clusters::DoorLock::DlLockState> ns;
        if (chip::app::Clusters::DoorLock::Attributes::LockState::Get(kDoorLockEp, ns) ==
            chip::Protocols::InteractionModel::Status::Success && !ns.IsNull()) {
            s.lock_state = ns.Value();
        }
        s.fabric_count = chip::Server::GetInstance().GetFabricTable().FabricCount();
        chip_stack_unlock();
        int64_t held_ms = (esp_timer_get_time() - held_t0) / 1000;
        // Warn only if we held the lock noticeably long — sub-5ms is normal.
        if (held_ms > 30) ESP_LOGW(TAG, "OLED collect held chip lock %lldms", held_ms);
    } else {
        int64_t wait_ms = (esp_timer_get_time() - lock_t0) / 1000;
        ESP_LOGW(TAG, "OLED collect failed to acquire chip lock after %lldms", wait_ms);
    }
    s.aliro_provisioned = aliro::instance().isProvisioned();

    s.uptime_s  = static_cast<uint32_t>(esp_timer_get_time() / 1'000'000);
    s.free_heap = static_cast<uint32_t>(esp_get_free_heap_size());

    // Reader GID — placeholder (AliroDelegate doesn't expose the raw 16-byte
    // group identifier yet). Show "ok" if provisioned, "----" otherwise.
    snprintf(s.reader_gid, sizeof(s.reader_gid), "%s",
             s.aliro_provisioned ? "set" : "----");

    s.last_action[0] = 0;
    if (s_action_mtx && xSemaphoreTake(s_action_mtx, pdMS_TO_TICKS(10)) == pdTRUE) {
        strncpy(s.last_action, s_last_action.text, sizeof(s.last_action) - 1);
        s.last_action[sizeof(s.last_action) - 1] = 0;
        xSemaphoreGive(s_action_mtx);
    }
}

const char * lock_state_str(chip::app::Clusters::DoorLock::DlLockState s) {
    using LS = chip::app::Clusters::DoorLock::DlLockState;
    switch (s) {
    case LS::kLocked:         return "LOCKED";
    case LS::kUnlocked:       return "OPEN";
    case LS::kUnlatched:      return "UNLATCH";
    case LS::kNotFullyLocked: return "PARTIAL";
    default:                  return "?";
    }
}

// Returns one of the active alert messages (≤8 chars for the BIG line),
// or nullptr if everything is OK. If multiple alerts active, rotates by
// boot-uptime second so each gets visible time.
const char * compute_alert(const Status & s, uint32_t uptime_s) {
    static const char * msgs[5];
    int n = 0;
    if (s.fabric_count == 0)         msgs[n++] = "NOPAIR";
    if (!s.aliro_provisioned)        msgs[n++] = "NOKEYS";
    if (!s.wifi_up)                  msgs[n++] = "NONET";
    if (::time(nullptr) <= 1577836800) msgs[n++] = "NOTIME";
    // (extend with: pair window open, OTA available, etc.)
    if (n == 0) return nullptr;
    return msgs[(uptime_s / 2) % n];   // rotate every 2 seconds
}

// ===== Boot splash — drawn until all five phases go green. Shows a big
// "ALIRO LOCK" header (pages 0..1) then a five-row checklist (pages 3..7)
// where each row is "[x] LABEL" and the bracket flips to filled when the
// phase clears. Bottom-right corner shows uptime so a stuck phase can be
// distinguished from a freshly-booted one. =====
void render_boot(const Phases & p, uint32_t uptime_s) {
    clear_fb();

    // Pages 0..1: big "ALIRO LOCK" centred. 10 chars × 12 = 120 px →
    // (128 - 120) / 2 = 4 px from left.
    draw_str_2x(4, 0, "ALIRO LOCK");

    // Page 2: divider line.
    for (int i = 0; i < kLogicalW; ++i) put_pixel(i, 18, true);

    // Pages 3..7: checklist. 5 rows, one per phase. Each "[x] " prefix
    // is 4 chars × 6 = 24 px, leaving 17 chars × 6 = 102 px for labels.
    auto row = [](int page_l, bool ok, const char * fmt, ...) {
        char buf[24];
        va_list ap; va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        draw_strf(0,  page_l, "[%c]", ok ? 'x' : ' ');
        draw_str (24, page_l, buf);
    };

    row(3, p.nfc,    "NFC reader");
    if (p.wifi) {
        // Truncate SSID so the row fits in the 17-char budget.
        char buf[18];
        snprintf(buf, sizeof(buf), "WiFi %s", p.wifi_ssid);
        row(4, true,  "%s", buf);
    } else {
        row(4, false, "WiFi");
    }
    row(5, p.ntp,    "NTP time");
    if (p.matter) {
        row(6, true,  "Matter %u fab", p.fabric_count);
    } else {
        row(6, false, "Matter pair");
    }
    row(7, p.aliro,  "Aliro keys");

    // Uptime indicator in the bottom-right corner of page 7. Format
    // "12s" / "1m23s" — 5 chars max × 6 = 30 px, draw flush right.
    char up[8];
    if (uptime_s < 60) {
        snprintf(up, sizeof(up), "%lus", (unsigned long)uptime_s);
    } else {
        snprintf(up, sizeof(up), "%lum%02lus",
                 (unsigned long)(uptime_s / 60),
                 (unsigned long)(uptime_s % 60));
    }
    int up_w = (int)strlen(up) * 6;
    draw_str(kLogicalW - up_w, 7, up);

    flush_all();
}

// ===== "READY" splash shown for ~1.5s after the last phase clears, so
// the operator gets a positive confirmation before the screen flips into
// dense status mode. =====
void render_ready_splash(const Phases & p) {
    clear_fb();
    draw_str_2x(4,  1, "ALIRO LOCK");      // pages 1..2
    // Big "READY" centred — 5 chars × 12 = 60 px → x = (128-60)/2 = 34
    draw_str_2x(34, 4, "READY");           // pages 4..5
    // Subtitle: IP if we have one
    if (p.wifi) {
        int w = (int)strlen(p.ip_addr) * 6;
        draw_str((kLogicalW - w) / 2, 7, p.ip_addr);
    }
    flush_all();
}

void render_main() {
    Status s;
    collect(s);
    clear_fb();

    using LS = chip::app::Clusters::DoorLock::DlLockState;

    // ===== Pages 0..1 (top 16px) =====
    // Alternates between LOCK STATE and ACTIVE ALERT every 2 seconds.
    // - State view:  [lock icon 14] [BIG state text]   [WiFi bars]
    // - Alert view:  [warn icon 14] [BIG alert text]
    const char * alert = compute_alert(s, s.uptime_s);
    bool show_alert = alert && (s.uptime_s % 4 < 2);   // 2s alert / 2s state cycle

    if (show_alert) {
        draw_icon<14, 14>(2, 1, kIconWarn);
        // Centre alert text in the remaining width (no WiFi bars in alert view).
        int chars = 0; for (const char * p = alert; *p; ++p) ++chars;
        int w = chars * 12;
        int x = 22 + ((kLogicalW - 22) - w) / 2;
        if (x < 22) x = 22;
        draw_str_2x(x, 0, alert);
    } else {
        bool open = (s.lock_state == LS::kUnlocked || s.lock_state == LS::kUnlatched);
        draw_icon<14, 14>(2, 1, open ? kIconLockOpen : kIconLockClosed);

        const char * head = lock_state_str(s.lock_state);
        int chars = 0; for (const char * p = head; *p; ++p) ++chars;
        int w = chars * 12;
        int region_x0 = 22, region_w = (kLogicalW - 28) - region_x0;
        int x = region_x0 + (region_w - w) / 2;
        if (x < region_x0) x = region_x0;
        draw_str_2x(x, 0, head);

        // WiFi bars top-right (only in state view).
        if (s.wifi_up) {
            int bars = 1;
            if (s.wifi_rssi >= -55)      bars = 4;
            else if (s.wifi_rssi >= -65) bars = 3;
            else if (s.wifi_rssi >= -75) bars = 2;
            for (int b = 0; b < 4; ++b) {
                int x_b = kLogicalW - 24 + b * 6;
                int h_b = (b + 1) * 3 + 2;
                int y0  = 16 - h_b;
                for (int dx = 0; dx < 4; ++dx)
                    for (int dy = 0; dy < h_b; ++dy)
                        put_pixel(x_b + dx, y0 + dy, b < bars);
            }
        }
    }

    // Page 2 — divider line
    for (int i = 0; i < kLogicalW; ++i) put_pixel(i, 18, true);

    // ===== Info rows (pages 3..7), all 6-px font, max 21 chars =====
    // Page 3: time + IP
    // Page 4: GID
    // Page 5: counters (auth ok / fail / cache hit)
    // Page 6: timing
    // Page 7: system (heap / cpu / temp)

    // ---- Page 3: time + IP ----------------------------------------------
    char timebuf[10] = "--:--:--";
    {
        time_t now = ::time(nullptr);
        struct tm tm_local;
        if (now > 1577836800) {
            localtime_r(&now, &tm_local);
            snprintf(timebuf, sizeof(timebuf), "%02d:%02d:%02d",
                     tm_local.tm_hour, tm_local.tm_min, tm_local.tm_sec);
        }
    }
    // "14:23:45 10.0.1.99" = 8+1+9 = 18 chars × 6 = 108 px ✓
    if (s.wifi_up) draw_strf(0, 3, "%s %s", timebuf, s.ip_addr);
    else           draw_strf(0, 3, "%s no WiFi", timebuf);

    // ---- Page 4: Aliro reader GID + WiFi name ---------------------------
    // GID full 16-byte hex won't fit; use the small "ok"/"----" indicator
    // plus SSID (more useful daily than the GID hex).
    draw_strf(0, 4, "GID %s  %s", s.reader_gid, s.wifi_ssid);

    // ---- Page 5: tap counters (spaces between label & value) ------------
    {
        uint32_t ok    = s_tap_count.load();
        uint32_t fail  = s_tap_fail.load();
        uint32_t hit   = s_cache_hit.load();
        uint32_t miss  = s_cache_miss.load();
        uint32_t total = hit + miss;
        // "OK 11 Fail 1 Hit 9/12" = up to 21 chars × 6 = 126 px ✓
        draw_strf(0, 5, "OK %lu Fail %lu Hit %lu/%lu",
                  (unsigned long)ok, (unsigned long)fail,
                  (unsigned long)hit, (unsigned long)total);
    }

    // ---- Page 6: timing breakdown (spaces between cells) ----------------
    {
        int32_t det = s_last_detect_ms.load();
        int32_t prp = s_last_prep_ms  .load();
        int32_t aut = s_last_auth_ms  .load();
        int32_t lck = s_last_lock_ms  .load();
        bool have = (det >= 0 || aut >= 0);
        if (have) {
            int32_t d = (det < 0 ? 0 : det) + (prp < 0 ? 0 : prp);
            int32_t a = aut < 0 ? 0 : aut;
            int32_t l = lck < 0 ? 0 : lck;
            int32_t tot = d + a + l;
            // "T 282 A 226 D 50 L 5" = up to 20 chars × 6 = 120 ✓
            draw_strf(0, 6, "T %d A %d D %d L %d",
                      (int)tot, (int)a, (int)d, (int)l);
        } else {
            draw_str (0, 6, "T -- A -- D -- L --");
        }
    }

    // ---- Page 7: system (heap / cpu / temp) -----------------------------
    {
        char cpu_buf[6], temp_buf[6];
        if (g_cpu_usage_pct >= 0) snprintf(cpu_buf,  sizeof(cpu_buf),  "%d%%", g_cpu_usage_pct);
        else                      snprintf(cpu_buf,  sizeof(cpu_buf),  "--");
        if (g_temp_c > -100.0f)   snprintf(temp_buf, sizeof(temp_buf), "%dC", (int)g_temp_c);
        else                      snprintf(temp_buf, sizeof(temp_buf), "--");
        // "H 142K  CPU 12%  T 42C" = up to 22 chars — borderline, trim heap.
        draw_strf(0, 7, "H %luK CPU %s T %s",
                  (unsigned long)(s.free_heap / 1024),
                  cpu_buf, temp_buf);
    }

    flush_all();
}

// Full-screen OTA progress display. Called from render_task when an
// OTA is in progress. Uses the project's existing draw_* primitives.
// When received=0 we show "OTA READY" so the operator sees the request
// landed before bytes start streaming; once first chunk arrives we
// switch to the live progress bar.
void render_ota(uint32_t received, uint32_t total)
{
    clear_fb();

    if (received == 0) {
        draw_str_2x(4, 0, "OTA READY");
        char sz[24];
        snprintf(sz, sizeof(sz), "size %luK", (unsigned long)(total / 1024));
        draw_str(0, 3, sz);
        draw_str(0, 5, "waiting for data");
        draw_str(0, 7, "do not unplug");
        flush_all();
        return;
    }

    draw_str_2x(4, 0, "OTA UPDATE");

    char l1[24];
    uint32_t pct = total ? (received * 100 / total) : 0;
    snprintf(l1, sizeof(l1), "%lu%%  %luK/%luK",
             (unsigned long)pct,
             (unsigned long)(received / 1024),
             (unsigned long)(total / 1024));
    draw_str(0, 3, l1);

    // Progress bar — y=36..43 (page 4..5). Frame around full 128 px width,
    // fill proportional to pct (0..124 px inside).
    int bar_w = total ? (int)((uint64_t)received * 124 / total) : 0;
    for (int x = 0; x < kLogicalW; ++x) {
        put_pixel(x, 36, true);
        put_pixel(x, 44, true);
    }
    for (int y = 36; y <= 44; ++y) {
        put_pixel(0, y, true);
        put_pixel(kLogicalW - 1, y, true);
    }
    for (int x = 2; x < 2 + bar_w && x < kLogicalW - 2; ++x) {
        for (int y = 38; y <= 42; ++y) put_pixel(x, y, true);
    }

    draw_str(0, 7, "do not unplug");
    flush_all();
}

void render_task(void *) {
    uint32_t iter = 0;
    ESP_LOGI(TAG, "render_task entered");
    for (;;) {
        ++iter;

        // OTA takeover: full-screen progress bar while WebServerManager
        // is streaming a firmware image. Skip all the heavy state-collection
        // paths so we don't compete with the OTA task for CPU / I2C / flash.
        uint32_t ota_total = s_ota_bytes_total.load();
        if (ota_total > 0) {
            uint32_t ota_rx = s_ota_bytes_received.load();
            render_ota(ota_rx, ota_total);
            vTaskDelay(pdMS_TO_TICKS(150));
            continue;
        }

        sample_cpu_temp();

        Phases p;
        collect_phases(p);
        bool all_ready = p.nfc && p.wifi && p.ntp && p.matter && p.aliro;
        int64_t now_ms = esp_timer_get_time() / 1000;
        uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1'000'000);

        // Periodic heartbeat so we can tell from /api/logs whether the
        // render task is alive vs. blocked on something. Drops out once
        // we leave the boot phase (no longer interesting after that).
        if ((iter % 10) == 0 && s_all_ready_at_ms.load() == 0) {
            ESP_LOGI(TAG, "boot iter=%lu up=%us  nfc=%d wifi=%d ntp=%d matter=%d aliro=%d  fabric=%u",
                     (unsigned long)iter, uptime_s,
                     p.nfc, p.wifi, p.ntp, p.matter, p.aliro, p.fabric_count);
        }

        // Safety net: if some phase refuses to come online (NTP blocked
        // by router, fabric storage corrupt, etc.) the boot splash would
        // otherwise pin forever. After 30 seconds force transition to
        // the main screen — the alert rotation there is the right place
        // to surface the unhealthy phase, not a stuck splash.
        constexpr uint32_t kBootTimeoutSec = 30;
        bool force_ready = uptime_s >= kBootTimeoutSec;

        if ((all_ready || force_ready) && s_all_ready_at_ms.load() == 0) {
            // First time we're moving on. Latch the timestamp so we can
            // hold the READY splash for a fixed window. We never go back
            // to the boot view; phase regressions surface as alerts.
            s_all_ready_at_ms.store(now_ms);
            ESP_LOGI(TAG, "boot splash done after %us — phases nfc=%d wifi=%d ntp=%d matter=%d aliro=%d",
                     uptime_s, p.nfc, p.wifi, p.ntp, p.matter, p.aliro);
        }

        int64_t latched_at = s_all_ready_at_ms.load();

        if (latched_at == 0) {
            // Still in boot — splash with checklist. Refresh fast enough
            // (500ms) so the checkboxes feel responsive when phases come
            // online in quick succession near the end of boot.
            render_boot(p, uptime_s);
            vTaskDelay(pdMS_TO_TICKS(500));
        } else if (now_ms - latched_at < kReadySplashMs) {
            // Hold the READY frame.
            render_ready_splash(p);
            vTaskDelay(pdMS_TO_TICKS(250));
        } else {
            // Main screen. 5 s refresh — flicker is visible at 1 Hz on a
            // 1.3" OLED with full-frame redraw, and the only field that
            // changes once per second is the seconds digit on the time
            // row. ByteButton's 40 Hz LED refresh on the same I2C bus
            // also fights us when we redraw too often. notifyTap /
            // notify*Lock / notifyMatterActivity fire flushes on demand,
            // so latency-sensitive feedback (door lock state changing,
            // a tap being registered) is not gated by this delay.
            render_main();
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
}

void set_last_action(const char * fmt, ...) {
    if (!s_action_mtx) return;
    char buf[24];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (xSemaphoreTake(s_action_mtx, pdMS_TO_TICKS(10)) == pdTRUE) {
        strncpy(s_last_action.text, buf, sizeof(s_last_action.text) - 1);
        s_last_action.text[sizeof(s_last_action.text) - 1] = 0;
        s_last_action.at_ms = esp_timer_get_time() / 1000;
        xSemaphoreGive(s_action_mtx);
    }
}

}  // namespace

namespace aliro {

OledPanel & OledPanel::instance()
{
    static OledPanel inst;
    return inst;
}

bool OledPanel::isPresent() const { return s_present.load(); }

esp_err_t OledPanel::init()
{
    if (s_dev) return ESP_OK;

    i2c_master_bus_handle_t bus = aliro::i2c::shared_bus();
    if (!bus) return ESP_FAIL;

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = kI2cAddr;
    dev_cfg.scl_speed_hz    = kI2cFreqHz;
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c add_device failed: %s", esp_err_to_name(err));
        return err;
    }

    if (!sh1107_init()) {
        ESP_LOGE(TAG, "SH1107 init failed (probably not present at 0x%02X)", kI2cAddr);
        return ESP_FAIL;
    }

    s_action_mtx = xSemaphoreCreateMutex();

    // Bring up SNTP so the system clock has wall-time for the OLED
    // header. Matter's chip[TS] cluster does NOT actually fetch wall
    // time — it only stores Last-Known-Good-Time inside the fabric
    // table for cert validity checks. We need a real SNTP sync.
    // Set timezone Asia/Almaty (UTC+5, no DST since 2005).
    setenv("TZ", "ALMT-5", 1);
    tzset();
    aliro::startSystemClock();

    // CPU usage telemetry — register idle hooks on both cores. The hook
    // is just an inc, so cost is negligible. Calibrate baseline by
    // sampling 200ms of mostly-idle ticks.
    esp_register_freertos_idle_hook_for_cpu(oled_idle_hook_core0, 0);
    esp_register_freertos_idle_hook_for_cpu(oled_idle_hook_core1, 1);
    s_idle_ticks_core0 = s_idle_ticks_core1 = 0;
    int64_t cal_t0 = esp_timer_get_time();
    vTaskDelay(pdMS_TO_TICKS(200));
    int64_t cal_dt = esp_timer_get_time() - cal_t0;
    uint32_t cal_ticks = (s_idle_ticks_core0 + s_idle_ticks_core1) / 2;
    s_idle_baseline_per_sec = (uint32_t)((uint64_t)cal_ticks * 1000000ULL / (uint64_t)cal_dt);
    ESP_LOGI(TAG, "CPU idle baseline: %lu ticks/s", (unsigned long)s_idle_baseline_per_sec);

    // On-die temperature sensor.
    temperature_sensor_config_t tcfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 80);
    if (temperature_sensor_install(&tcfg, &s_temp_sensor) == ESP_OK &&
        temperature_sensor_enable(s_temp_sensor) == ESP_OK) {
        ESP_LOGI(TAG, "On-die temperature sensor up");
    } else {
        ESP_LOGW(TAG, "Temperature sensor init failed");
        s_temp_sensor = nullptr;
    }

    // Paint the first frame of the boot splash synchronously before the
    // render task spins up — otherwise there's a 500ms blank window after
    // the SH1107 lights up and before the task gets scheduled.
    {
        Phases p{};
        collect_phases(p);
        render_boot(p, 0);
    }

    s_present.store(true);
    // Priority 1 — display refresh is the lowest-priority task we own.
    // NFC polling, button polling, chip stack tasks all preempt it.
    //
    // Try internal SRAM first (3 KB is enough — render_task does no deep
    // recursion, and the only ESP_LOG calls inside fire in the boot
    // phase). PSRAM stacks cause visible flicker on the OLED: every
    // function call during flush_all() hits PSRAM cache, and a stall
    // between two of the 16 page writes lets ByteButton's 40 Hz LED
    // refresh sneak onto the I2C bus, leaving the SH1107 with a stale
    // page-address pointer.
    BaseType_t rc = xTaskCreate(render_task, "oled", 3072, nullptr, 1, nullptr);
    if (rc != pdPASS) {
        size_t free_int = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        ESP_LOGW(TAG, "xTaskCreate(oled, 3K internal) failed rc=%d — int=%zuB psram=%zuB; falling back to PSRAM stack",
                 (int)rc, free_int, free_psram);
        rc = xTaskCreatePinnedToCoreWithCaps(
            render_task, "oled", 4096, nullptr, 1, nullptr,
            tskNO_AFFINITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (rc != pdPASS) {
            ESP_LOGE(TAG, "xTaskCreate(oled, PSRAM) ALSO failed rc=%d — display will stay frozen", (int)rc);
            return ESP_FAIL;
        }
    }
    ESP_LOGI(TAG, "OLED panel ready @ 0x%02X (128x64 SH1107)", kI2cAddr);
    return ESP_OK;
}

void OledPanel::notifyTapStart()
{
    if (!s_present.load()) return;
    s_tap_start_us.store(esp_timer_get_time());
    // Reset later stages so a partial tap (auth fail) doesn't confuse the breakdown.
    s_aid_us.store(0);
    s_auth_start_us.store(0);
    s_auth_done_us.store(0);
}

void OledPanel::notifyAidSelected() { if (s_present.load()) s_aid_us.store(esp_timer_get_time()); }
void OledPanel::notifyAuthStart()   { if (s_present.load()) s_auth_start_us.store(esp_timer_get_time()); }
void OledPanel::notifyAuthDone()    { if (s_present.load()) s_auth_done_us.store(esp_timer_get_time()); }
void OledPanel::notifyAuthFail()    { if (s_present.load()) s_tap_fail.fetch_add(1); }
void OledPanel::notifyCacheHit()    { if (s_present.load()) s_cache_hit.fetch_add(1); }
void OledPanel::notifyCacheMiss()   { if (s_present.load()) s_cache_miss.fetch_add(1); }
void OledPanel::notifyNfcReady()    { s_phase_nfc.store(true); }   // safe pre-init
void OledPanel::notifyOtaProgress(size_t bytes_received, size_t bytes_total)
{
    s_ota_bytes_received.store((uint32_t)bytes_received);
    s_ota_bytes_total.store((uint32_t)bytes_total);
}

bool OledPanel::isAllReady()
{
    return s_all_ready_at_ms.load() > 0;
}

OledPanel::TapMetrics OledPanel::getTapMetrics()
{
    return TapMetrics{
        .total_ms   = s_last_tap_ms.load(),
        .detect_ms  = s_last_detect_ms.load(),
        .prep_ms    = s_last_prep_ms.load(),
        .auth_ms    = s_last_auth_ms.load(),
        .lock_ms    = s_last_lock_ms.load(),
        .tap_count  = s_tap_count.load(),
        .tap_fail   = s_tap_fail.load(),
        .cache_hit  = s_cache_hit.load(),
        .cache_miss = s_cache_miss.load(),
    };
}

void OledPanel::notifyTap(const char * uid_hex, const char * label)
{
    if (!s_present.load()) return;
    s_tap_count.fetch_add(1);
    int64_t now = esp_timer_get_time();
    int64_t t0 = s_tap_start_us.exchange(0);
    int64_t t1 = s_aid_us.exchange(0);
    int64_t t2 = s_auth_start_us.exchange(0);
    int64_t t3 = s_auth_done_us.exchange(0);

    auto delta_ms = [](int64_t a, int64_t b) -> int32_t {
        return (a > 0 && b > a) ? static_cast<int32_t>((b - a) / 1000) : -1;
    };

    int32_t total = delta_ms(t0, now);
    s_last_tap_ms   .store(total);
    s_last_detect_ms.store(delta_ms(t0, t1));
    s_last_prep_ms  .store(delta_ms(t1, t2));
    s_last_auth_ms  .store(delta_ms(t2, t3));
    s_last_lock_ms  .store(delta_ms(t3, now));

    const char * lbl = label ? label : "OK";
    if (total >= 0) set_last_action("Tap %dms %s", (int)total, lbl);
    else            set_last_action("Tap %s", lbl);
}

void OledPanel::notifyMatterUnlock(const char * source)
{
    if (!s_present.load()) return;
    set_last_action("Unlock %s", source ? source : "?");
}

void OledPanel::notifyMatterLock(const char * source)
{
    if (!s_present.load()) return;
    set_last_action("Lock %s", source ? source : "?");
}

}  // namespace aliro
