// ByteButtonPanel — driver + glue for M5Stack Unit ByteButton (U192).
// Pins: SDA=GPIO8, SCL=GPIO9, I2C addr 0x47.
//
// Why this is its own component (not folded into led_strip / NfcManager):
// - Keeps ESP-IDF i2c_master fully isolated from the rest of the lock state
//   so a wiring fault on this unit can never deadlock the door logic.
// - Runs on its own task, so a slow STM32 ack on the unit never blocks
//   the chip stack or NFC polling.

#include "ByteButtonPanel.hpp"

#include <algorithm>
#include <atomic>
#include <functional>
#include <esp_log.h>
#include <esp_timer.h>
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <app/clusters/door-lock-server/door-lock-server.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <app-common/zap-generated/attributes/Accessors.h>
#include <app/server/Server.h>
#include <credentials/FabricTable.h>
#include <esp_matter.h>
#include <esp_wifi.h>
#include <platform/PlatformManager.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <setup_payload/ManualSetupPayloadGenerator.h>
#include <setup_payload/SetupPayload.h>
#include <platform/CommissionableDataProvider.h>

#include "AliroDelegate.h"
#include "MatterAdminGlue.hpp"
#include "I2cBus.hpp"

// Forward decls from LockHandlers.cpp — `lockind` is a top-level namespace.
namespace lockind {
    void nfc_tap_unlock(chip::EndpointId endpointId);
    void unlock_local (chip::EndpointId endpointId);
    void lock_local   (chip::EndpointId endpointId);
}

namespace {

constexpr const char * TAG          = "byte-btn";
constexpr uint8_t      kI2cAddr     = 0x47;
constexpr uint32_t     kI2cFreqHz   = 100'000;   // ByteButton STM32 firmware is happy at 100kHz
constexpr uint16_t     kDoorLockEp  = 1;

// Register map — Unit ByteButton I2C protocol v1.
//   - 0x00: button bitmap, **active-low** (bit N = 0 when button N pressed,
//     1 when released). Confirmed against the M5Unit-ByteButton README and
//     Arduino src/unit_byte.cpp.
//   - 0x10..0x17: per-LED brightness (NOT a global register).
//   - 0x20+N*4: LED N colour as 4 bytes [B, G, R, 0] — derived from the
//     Arduino driver casting `uint32_t color` (0x00RRGGBB) to uint8_t* on a
//     little-endian MCU. That puts B at the lowest address.
//   - 0xFE: firmware version (single byte) — used as liveness probe.
constexpr uint8_t REG_BTN_BITMAP    = 0x00;
constexpr uint8_t REG_LED_RGB888_0  = 0x20;       // +N*4 for LED N
constexpr uint8_t REG_FW_VERSION    = 0xFE;

i2c_master_dev_handle_t  s_dev = nullptr;
std::atomic<bool>        s_present{false};

esp_err_t reg_read(uint8_t reg, uint8_t * out, size_t len)
{
    // STM32G031 ByteButton firmware seems to require Arduino-Wire-style
    // STOP between the register-pointer write and the data read (i.e. NOT
    // repeated-start). i2c_master_transmit_receive issues a repeated-start
    // and that's why the button bitmap never updated. Two separate calls
    // = two transactions with STOP in between, matching Wire semantics.
    esp_err_t err = i2c_master_transmit(s_dev, &reg, 1, pdMS_TO_TICKS(30));
    if (err != ESP_OK) return err;
    return i2c_master_receive(s_dev, out, len, pdMS_TO_TICKS(30));
}

esp_err_t reg_write(uint8_t reg, const uint8_t * data, size_t len)
{
    uint8_t buf[1 + 36];                          // up to 9 LEDs * 4 bytes
    if (len > sizeof(buf) - 1) return ESP_ERR_INVALID_SIZE;
    buf[0] = reg;
    memcpy(buf + 1, data, len);
    return i2c_master_transmit(s_dev, buf, 1 + len, pdMS_TO_TICKS(50));
}

// Write one LED's RGB888 colour. Stride = 4 bytes per LED. The 4th byte is
// reserved/padding in the protocol; we send 0.
esp_err_t led_set(uint8_t idx, uint8_t r, uint8_t g, uint8_t b)
{
    if (idx >= 9) return ESP_ERR_INVALID_ARG;
    // Protocol packs RGB888 as 0x00RRGGBB (low-byte = B). Verified against
    // the M5Stack Arduino library `setLEDColor(idx, RGB888)`.
    uint8_t payload[4] = { b, g, r, 0 };
    return reg_write(REG_LED_RGB888_0 + idx * 4, payload, 4);
}

// Mirror of the last colour we asked each LED to display. Updated by
// the try_led wrapper in render_task on every successful write so the
// batched refresh below can re-send the current intended state.
uint8_t s_led_mirror[9 * 4] = {0};

// Re-send all 9 LEDs in one batched I2C transaction. Used to unstick
// the STM32G031 ByteButton firmware when individual writes ACK but the
// LEDs stop refreshing — observed after I2C bus contention spikes (e.g.
// during OTA / sustained high-rate render cycles). 37-byte transaction
// (1 reg + 9*4 data) is a single transmit, no repeated-start, which
// some STM32 firmware revs handle more reliably than 9 separate frames.
esp_err_t led_refresh_all()
{
    return reg_write(REG_LED_RGB888_0, s_led_mirror, sizeof(s_led_mirror));
}

// Read raw button bitmap from reg 0x00. Polarity (0=pressed vs 1=pressed)
// auto-detected at init by sampling the idle bus state — see s_active_low.
std::atomic<uint8_t> s_active_low{1};

uint8_t read_buttons_raw()
{
    uint8_t bm = 0xFF;
    if (reg_read(REG_BTN_BITMAP, &bm, 1) != ESP_OK) return 0xFF;
    return bm;
}

uint8_t read_buttons()
{
    uint8_t raw = read_buttons_raw();
    return s_active_low.load() ? static_cast<uint8_t>(~raw) : raw;
}

// Read current LockState attribute via DoorLockServer's data model.
// We read rather than mirror an internal flag because Matter writes to
// LockState come from many places (CASE commands, NFC, manual cmd, etc).
chip::app::Clusters::DoorLock::DlLockState current_lock_state()
{
    chip::app::DataModel::Nullable<chip::app::Clusters::DoorLock::DlLockState> ns;
    auto status = chip::app::Clusters::DoorLock::Attributes::LockState::Get(kDoorLockEp, ns);
    if (status != chip::Protocols::InteractionModel::Status::Success) {
        return chip::app::Clusters::DoorLock::DlLockState::kNotFullyLocked;
    }
    if (ns.IsNull()) return chip::app::Clusters::DoorLock::DlLockState::kNotFullyLocked;
    return ns.Value();
}

// (LED rendering now lives in render_task — needs to be there to share
// the chip-stack lock window across multiple data-model reads.)

// Until-when timestamp for the commissioning window — used by render_task
// to pulse LEDs 2 and 3 while a window is open.
std::atomic<int64_t> s_commissioning_open_until_ms{0};   // monotonic ms

// NFC reading / result indicators (LEDs 0 + 1).
//   - reading_until_ms: while now < this value, LED 0 = cyan blink.
//     Set on notifyNfcReading(), expires 2s later (covers worst-case auth).
//   - result_until_ms : while now < this value, LED 1 (and briefly LED 0)
//     show last result colour. Set on notifyNfcResult().
std::atomic<int64_t> s_nfc_reading_until_ms{0};
std::atomic<int64_t> s_nfc_result_until_ms{0};
std::atomic<bool>    s_nfc_result_ok{false};

// LED 7 — flashes on every Matter activity hook (attribute change,
// DoorLock command). Set from arbitrary tasks, read by render_task.
std::atomic<int64_t> s_matter_activity_until_ms{0};      // monotonic ms

// Helper: schedule a chip-event-loop callback. ALL DoorLockServer /
// CommissioningWindow / FabricTable operations must run on the chip task.
void schedule(std::function<void()> fn)
{
    auto * heap = new std::function<void()>(std::move(fn));
    chip::DeviceLayer::PlatformMgr().ScheduleWork(
        [](intptr_t p) {
            auto * f = reinterpret_cast<std::function<void()> *>(p);
            (*f)();
            delete f;
        },
        reinterpret_cast<intptr_t>(heap));
}

// Used by both BTN2 (new HomeKey user) and BTN3 (new Matter controller).
// Matter doesn't differentiate at the protocol level — the same window
// accepts any commissioner that has the setup payload. The two buttons
// are just UX labels.
void btn1_open_commissioning()
{
    schedule([] {
        if (matter_admin::openCommissioningWindow(180)) {
            int64_t now = esp_timer_get_time() / 1000;
            s_commissioning_open_until_ms.store(now + 180'000);
        }
    });
}

// 50Hz button poll with edge detection. Press = previously 0, now 1.
// We don't debounce — the STM32 firmware on the unit already debounces
// before exposing bits via the I2C status register.
void poll_task(void *)
{
    // Seed prev with the first real read so we don't fire phantom edges
    // for whatever state the buttons happen to be in at boot.
    uint8_t prev = read_buttons();
    uint8_t prev_raw = read_buttons_raw();
    ESP_LOGI(TAG, "poll seed: raw=0x%02X normalized=0x%02X (active_low=%u)",
             prev_raw, prev, (unsigned)s_active_low.load());
    for (;;) {
        uint8_t raw = read_buttons_raw();
        uint8_t cur = s_active_low.load() ? static_cast<uint8_t>(~raw) : raw;

        // (Diagnostic heartbeat removed for production — was triggering
        // I2C activity every 100ms that competed with the NFC poll loop
        // for CPU on the chip task, making taps feel laggy.)

        prev_raw = raw;

        uint8_t pressed = cur & ~prev;            // rising edges only
        prev = cur;

        if (pressed & 0x01) {                     // BTN0 → Unlock
            ESP_LOGI(TAG, "BTN0 -> Unlock");
            chip::DeviceLayer::PlatformMgr().ScheduleWork(
                [](intptr_t) { ::lockind::unlock_local(kDoorLockEp); }, 0);
        }
        if (pressed & 0x02) {                     // BTN1 → Lock
            ESP_LOGI(TAG, "BTN1 -> Lock");
            chip::DeviceLayer::PlatformMgr().ScheduleWork(
                [](intptr_t) { ::lockind::lock_local(kDoorLockEp); }, 0);
        }
        if (pressed & 0x04) {                     // BTN2 → pair new HomeKey user
            ESP_LOGI(TAG, "BTN2 -> open pairing window for new HomeKey user");
            btn1_open_commissioning();
        }
        if (pressed & 0x08) {                     // BTN3 → pair new Matter controller
            ESP_LOGI(TAG, "BTN3 -> open pairing window for new Matter controller");
            btn1_open_commissioning();
        }
        // BTN4 / BTN5 / BTN6 — indicators only, presses are no-op.
        // BTN7 — reserved.
        // (Other buttons reserved for phase 2 — commissioning window,
        // setup-code dump, factory reset.)

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// 5Hz LED render. LED 0 = LockState, LED 8 = system status pulse.
// LEDs 1..7 reserved for phase 2 (WiFi / Matter / fabric / credentials etc).
//
// Must hold the CHIP stack lock around any data-model read (attribute
// storage, fabric table). Without the lock, attribute-storage.cpp:638
// calls VerifyOrDie and aborts the firmware. We release the lock before
// the I2C write so we don't block the chip stack on the I2C transaction.
void render_task(void *)
{
    using esp_matter::lock::chip_stack_lock;
    using esp_matter::lock::chip_stack_unlock;
    ESP_LOGI(TAG, "btn-render task entered");
    uint32_t tick = 0;
    // Track I2C health across iterations. If we see persistent failures,
    // the STM32G031 on the ByteButton has likely glitched (RF interference
    // / brown-out / runaway state) and we re-init the bus. Without this
    // the LED matrix goes dark silently for the rest of uptime — buttons
    // still register (poll_task reads bitmap on a separate transaction).
    int err_run = 0;
    uint32_t last_err_log_tick = 0;
    auto try_led = [&](uint8_t idx, uint8_t r, uint8_t g, uint8_t b) {
        // Mirror state so led_refresh_all() can replay the current frame.
        // Layout matches what reg_write expects starting at REG_LED_RGB888_0.
        if (idx < 9) {
            s_led_mirror[idx * 4 + 0] = b;
            s_led_mirror[idx * 4 + 1] = g;
            s_led_mirror[idx * 4 + 2] = r;
            s_led_mirror[idx * 4 + 3] = 0;
        }
        esp_err_t e = led_set(idx, r, g, b);
        if (e != ESP_OK) {
            ++err_run;
            if (tick - last_err_log_tick >= 25) {  // ~5s at 200ms period
                ESP_LOGW(TAG, "led_set[%u] failed: %s (run=%d)", idx,
                         esp_err_to_name(e), err_run);
                last_err_log_tick = tick;
            }
        } else {
            err_run = 0;
        }
    };
    for (;; ++tick) {
        // Snapshot under lock — capture the values, then drop the lock
        // before I2C writes.
        chip::app::Clusters::DoorLock::DlLockState lock_state =
            chip::app::Clusters::DoorLock::DlLockState::kNotFullyLocked;
        uint8_t fab_cnt = 0;
        int64_t lock_t0 = esp_timer_get_time();
        if (chip_stack_lock(pdMS_TO_TICKS(50)) == esp_matter::lock::SUCCESS) {
            int64_t held_t0 = esp_timer_get_time();
            lock_state = current_lock_state();
            fab_cnt    = chip::Server::GetInstance().GetFabricTable().FabricCount();
            chip_stack_unlock();
            int64_t held_ms = (esp_timer_get_time() - held_t0) / 1000;
            if (held_ms > 30) ESP_LOGW(TAG, "btn-render held chip lock %lldms", held_ms);
        } else {
            int64_t wait_ms = (esp_timer_get_time() - lock_t0) / 1000;
            ESP_LOGW(TAG, "btn-render lock acquire timed out after %lldms", wait_ms);
        }
        bool aliro_ok = aliro::instance().isProvisioned();

        // ── LED 0 / LED 1 — NFC pipeline view ─────────────────────────
        //   LED 0: cyan blink while reading a card; result flash for
        //          ~1s after tap; otherwise mirrors lock state.
        //   LED 1: holds result colour ~2s after tap; otherwise mirrors
        //          lock state.
        using chip::app::Clusters::DoorLock::DlLockState;
        uint8_t lock_r=0, lock_g=0, lock_b=0;
        switch (lock_state) {
        case DlLockState::kLocked:         lock_r=255; lock_g=  0; lock_b=  0; break;
        case DlLockState::kUnlocked:       lock_r=  0; lock_g=255; lock_b=  0; break;
        case DlLockState::kUnlatched:      lock_r=255; lock_g=255; lock_b=255; break;
        case DlLockState::kNotFullyLocked: lock_r=  0; lock_g=  0; lock_b=255; break;
        default:                           lock_r=255; lock_g=  0; lock_b=255; break;
        }

        int64_t now_ms_nfc = esp_timer_get_time() / 1000;
        bool reading        = (s_nfc_reading_until_ms.load() > now_ms_nfc);
        int64_t result_until = s_nfc_result_until_ms.load();
        bool result_active   = (result_until > now_ms_nfc);
        bool result_ok       = s_nfc_result_ok.load();

        uint8_t res_r = result_ok ? 0   : 255;
        uint8_t res_g = result_ok ? 255 : 0;
        uint8_t res_b = 0;

        // LED 0 priority: result flash (last 1s) > reading > lock state.
        if (result_active && (result_until - now_ms_nfc) > 1000) {
            try_led(0, res_r, res_g, res_b);
        } else if (reading) {
            // Cyan blink ~5Hz (tick at 40ms; toggle every other frame).
            bool on = (now_ms_nfc / 100) & 1;
            try_led(0, 0, on ? 255 : 0, on ? 255 : 0);
        } else {
            try_led(0, lock_r, lock_g, lock_b);
        }

        // LED 1 priority: result colour (full 2s window) > lock state.
        if (result_active) {
            try_led(1, res_r, res_g, res_b);
        } else {
            try_led(1, lock_r, lock_g, lock_b);
        }

        int64_t now_ms = esp_timer_get_time() / 1000;
        uint8_t pulse = (tick % 10 < 5)
                          ? static_cast<uint8_t>((tick % 5) * 50)
                          : static_cast<uint8_t>((4 - (tick % 5)) * 50);

        // LEDs 2 and 3 — pairing buttons. Idle = white (button is ready
        // to use); blue pulse while a commissioning window is open
        // (something is happening — exchange in progress).
        int64_t comm_until = s_commissioning_open_until_ms.load();
        if (comm_until > now_ms) {
            uint8_t b = static_cast<uint8_t>(80 + pulse);
            try_led(2, 0, 0, b);
            try_led(3, 0, 0, b);
        } else {
            try_led(2, 255, 255, 255);
            try_led(3, 255, 255, 255);
        }

        // LED 4 — WiFi: green strong, yellow medium, red weak, off if down.
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            int8_t rssi = ap.rssi;        // typical: -30 strong, -80 weak
            if (rssi >= -55)      try_led(4, 0, 255, 0);     // green
            else if (rssi >= -70) try_led(4, 200, 200, 0);   // yellow
            else                  try_led(4, 255, 0, 0);     // red
        } else {
            try_led(4, 0, 0, 0);
        }

        // LED 5 — Matter status: green=fabric+CASE, blue=fabric only,
        // red=no fabric. We approximate "CASE active" by fabric_count>0
        // — a more precise probe would check SecureSession count, but
        // for a status light the proxy is fine.
        if (fab_cnt == 0)         try_led(5, 255, 0, 0);
        else if (aliro_ok)        try_led(5, 0, 255, 0);
        else                      try_led(5, 0, 0, 200);

        // LED 6 — Aliro reader/credentials provisioned indicator.
        try_led(6, 0, aliro_ok ? 200 : 0, 0);

        // LED 7 — Matter activity ping. Idle = white (panel alive,
        // ready for commands); blue flash for ~300ms on every Matter
        // attribute change / DoorLock command.
        if (s_matter_activity_until_ms.load() > now_ms) {
            try_led(7, 0, 0, 255);
        } else {
            try_led(7, 255, 255, 255);
        }

        // LED 8 — corner status diode, solid violet (decorative accent).
        try_led(8, 180, 0, 220);

        // Re-blast all 9 LEDs in one batched I2C transaction. This
        // ensures the STM32G031 sees a single enqueue (one malloc on
        // its side) instead of 9 successive enqueues — minimises heap
        // churn on the ByteButton MCU which has only ~2 KB free heap
        // for the rgb_buffer FIFO. Also serves as a "kick" for the
        // WS2812 driver in case any prior frame got lost.
        reg_write(REG_LED_RGB888_0, s_led_mirror, sizeof(s_led_mirror));

        if ((tick % 25) == 0) {
            ESP_LOGI(TAG, "btn-render tick=%lu lock_state=%u fab=%u aliro=%d "
                          "mirror[0]=%02X%02X%02X mirror[8]=%02X%02X%02X",
                     (unsigned long)tick, (unsigned)lock_state, fab_cnt, aliro_ok,
                     s_led_mirror[0], s_led_mirror[1], s_led_mirror[2],
                     s_led_mirror[32], s_led_mirror[33], s_led_mirror[34]);
        }

        // Persistent-failure recovery: if the STM32G031 stopped ACK'ing
        // (err_run climbs across loops without success), re-poke a probe
        // read every ~5s. The peripheral's I2C state machine can hang
        // after RF interference / brown-out; a register read is cheap
        // and re-syncs both sides without forcing a reboot.
        if (err_run >= 9 && (tick % 25) == 0) {
            uint8_t ver = 0;
            if (reg_read(REG_FW_VERSION, &ver, 1) == ESP_OK) {
                ESP_LOGW(TAG, "STM32 recovered (fw=0x%02X), LED writes "
                              "still failing — retrying", ver);
                err_run = 0;  // probe got an ACK; let next loop try LEDs again
            } else {
                ESP_LOGE(TAG, "STM32 unresponsive on probe read");
            }
        }

        // Note: previously had a batched 37-byte all-LED refresh here
        // every 5 s. Removing it — the M5Stack ByteButton STM32 firmware
        // mishandles transactions over ~32 bytes: LEDs light briefly
        // then go fully dark and never recover until full board power
        // cycle (and probably not even then, since STM32 may persist
        // its bad state in its own SRAM). Individual per-LED writes
        // above are the supported protocol.

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

}  // namespace

namespace aliro {

ByteButtonPanel & ByteButtonPanel::instance()
{
    static ByteButtonPanel inst;
    return inst;
}

bool ByteButtonPanel::isPresent() const { return s_present.load(); }

void ByteButtonPanel::notifyMatterActivity()
{
    if (!s_present.load()) return;
    int64_t now = esp_timer_get_time() / 1000;
    s_matter_activity_until_ms.store(now + 300);
}

void ByteButtonPanel::notifyCommissioningOpened(uint32_t duration_seconds)
{
    if (!s_present.load()) return;
    int64_t now = esp_timer_get_time() / 1000;
    s_commissioning_open_until_ms.store(now + (int64_t)duration_seconds * 1000);
}

void ByteButtonPanel::notifyCommissioningClosed()
{
    if (!s_present.load()) return;
    s_commissioning_open_until_ms.store(0);
}

void ByteButtonPanel::notifyNfcReading()
{
    if (!s_present.load()) return;
    int64_t now = esp_timer_get_time() / 1000;
    // 2-second window covers cold-cache auth (~1.3s) plus margin.
    s_nfc_reading_until_ms.store(now + 2000);
}

void ByteButtonPanel::notifyNfcResult(bool ok)
{
    if (!s_present.load()) return;
    int64_t now = esp_timer_get_time() / 1000;
    s_nfc_result_ok.store(ok);
    s_nfc_result_until_ms.store(now + 2000);   // 2s result hold
    s_nfc_reading_until_ms.store(0);           // stop reading-blink
}

esp_err_t ByteButtonPanel::init()
{
    if (s_dev) return ESP_OK;                    // dup-init guard

    i2c_master_bus_handle_t bus = aliro::i2c::shared_bus();
    if (!bus) return ESP_FAIL;

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = kI2cAddr;
    dev_cfg.scl_speed_hz    = kI2cFreqHz;
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
        return err;
    }

    // Probe via firmware-version register — if the unit isn't there the
    // read will NACK and we bail without spawning tasks. This is also
    // useful diagnostic info when wiring is wrong vs. address mismatch.
    uint8_t fw_ver = 0;
    err = reg_read(REG_FW_VERSION, &fw_ver, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ByteButton not responding at 0x%02X: %s — panel disabled",
                 kI2cAddr, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "ByteButton firmware version reg=0x%02X", fw_ver);
    s_present.store(true);

    // IMPORTANT — do NOT issue the 0xFD=0x01 soft-reset here.
    // The M5Stack STM32G031 firmware handles that command by:
    //   HAL_TIM_PWM_MspDeInit(&htim3);    // tear down WS2812 PWM
    //   my_gpio_deinit();
    //   i2c_port_set_to_input();
    //   while (SDA || SCL) { if (++t > 60000) break; }   // wait idle, ~3ms
    //   if (t < 60000) NVIC_SystemReset();
    // If our I2C bus isn't idle within 3 ms (OLED on the shared bus,
    // bus contention, another transaction in flight), NVIC_SystemReset
    // never fires — but TIM3 stays de-initialised and PA7 stays
    // floating. The STM32 keeps responding to subsequent I2C reads
    // (FW version, register reads, etc.) but the WS2812 chain never
    // gets another pulse for the rest of its uptime. LEDs go dark,
    // and our next ESP32 reboot re-runs init, re-issues 0xFD=0x01,
    // re-deinits TIM3, repeats. Board power-cycle isn't enough
    // because we hammer the panel again immediately on every boot.
    // M5Unit-ByteButton-Internal-FW source: code/app/Core/Src/main.c
    // line 295-326 (Slave_Complete_Callback / 0xFD branch).
    //
    // We don't need a reset anyway — brightness defaults to 255 in
    // sk6812_init at every STM32 boot, rgb_show_mode is 0 unless
    // explicitly persisted via 0xF0, and our per-LED writes via
    // render_task immediately drive the chain.


    // Per-LED brightness lives at 0x10..0x18 (one byte per LED). 0xFF =
    // 100% — user wants full brightness for the door panel.
    for (uint8_t i = 0; i < 9; ++i) {
        uint8_t b = 0xFF;
        reg_write(0x10 + i, &b, 1);
    }

    // Auto-detect button polarity: sample the bus while no buttons are
    // pressed (boot-time assumption). If the byte reads 0xFF we're
    // active-low (factory default per README); if 0x00 we're active-high.
    {
        uint8_t raw = read_buttons_raw();
        if (raw == 0x00)        s_active_low.store(0);
        else if (raw == 0xFF)   s_active_low.store(1);
        // anything else = something is pressed at boot OR firmware variant —
        // assume active-low and let the verbose log in poll_task surface
        // the issue at runtime.
        ESP_LOGI(TAG, "polarity probe: raw=0x%02X => active_low=%u",
                 raw, (unsigned)s_active_low.load());
    }

    // Startup self-test — sweep all 9 LEDs through R, G, B for 200ms each.
    // Visual confirmation that render path + brightness + colour byte order
    // are working. Without this the panel looks dead until LockState changes.
    auto sweep = [](uint8_t r, uint8_t g, uint8_t b) {
        for (uint8_t i = 0; i < 9; ++i) led_set(i, r, g, b);
    };
    sweep(80,  0,  0); vTaskDelay(pdMS_TO_TICKS(200));
    sweep( 0, 80,  0); vTaskDelay(pdMS_TO_TICKS(200));
    sweep( 0,  0, 80); vTaskDelay(pdMS_TO_TICKS(200));
    sweep( 0,  0,  0);

    // Priorities deliberately low — NFC polling (PN532) on the lock side
    // runs at priority 5+. Keep these below it so they never preempt
    // the tap-handling pipeline.
    // 4 KB stacks. The render task in particular logs from hot paths
    // that funnel through tee_vprintf in app_main, which uses a 256-byte
    // stack buffer + vsnprintf — that pushed the original 3072B stack
    // over its limit on Matter-attribute-read storms.
    // 4 KB → 3 KB for render. By the time ByteButton init runs (after
    // Matter, WebServer, OLED), free internal SRAM is ~17 KB; a 4 KB
    // task allocation fails with errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY.
    // 3 KB fits and is enough — measured stack usage in render_task
    // peaks at ~2.2 KB even with ESP_LOG calls via tee_vprintf.
    BaseType_t rc_poll = xTaskCreate(poll_task,   "btnpoll", 3072, nullptr, 2, nullptr);
    BaseType_t rc_render = xTaskCreate(render_task, "btnled",  3072, nullptr, 2, nullptr);
    if (rc_poll != pdPASS || rc_render != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate failed (poll=%d render=%d) — LED rendering disabled",
                 (int)rc_poll, (int)rc_render);
    }

    ESP_LOGI(TAG, "ByteButton panel ready @ 0x%02X (poll=%d render=%d)",
             kI2cAddr, (int)rc_poll, (int)rc_render);
    return ESP_OK;
}

}  // namespace aliro
