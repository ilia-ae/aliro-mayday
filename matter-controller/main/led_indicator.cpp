// LED indicator for matter-controller — WS2812 on GPIO48 (ESP32-S3-DevKitC built-in RGB LED).
// Reflects controller state: WiFi, fabric, subscriptions, lock state of the subscribed DoorLock.

#include "led_indicator.hpp"

#include <atomic>
#include <esp_log.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <led_strip.h>

namespace led {

static const char *TAG = "led_ind";

constexpr gpio_num_t  kLedGpio     = GPIO_NUM_48;
constexpr uint32_t    kLedRmtResHz = 10 * 1000 * 1000;
constexpr uint32_t    kTickMs      = 100;

static led_strip_handle_t g_led = nullptr;
static std::atomic<State> g_state{State::kBoot};

void set_state(State s)
{
    g_state.store(s, std::memory_order_relaxed);
}

State get_state()
{
    return g_state.load(std::memory_order_relaxed);
}

// Soft brightness scaling — 25% to keep the on-board LED comfortable for
// continuous viewing.
static inline uint8_t scale(uint8_t v) { return static_cast<uint8_t>((v * 25) / 100); }

static void rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!g_led) return;
    led_strip_set_pixel(g_led, 0, scale(r), scale(g), scale(b));
    led_strip_refresh(g_led);
}

static void render_tick(uint32_t tick)
{
    if (!g_led) return;
    State s = g_state.load(std::memory_order_relaxed);
    bool half_sec = (tick % 5) < 3;        // 300ms on, 200ms off
    bool one_sec  = (tick % 10) < 5;       // 500ms on/off
    uint8_t pulse = (tick % 20 < 10) ? (tick % 10) * 25 : (10 - (tick % 10)) * 25;

    switch (s) {
    case State::kBoot:
        // Dim blue — controller booting, no WiFi yet.
        rgb(0, 0, 80);
        break;
    case State::kWifiConnecting:
        // Blink blue — WiFi connect in progress.
        rgb(0, 0, half_sec ? 200 : 0);
        break;
    case State::kWifiConnected:
        // Cyan — WiFi up but no fabric / no devices yet.
        rgb(0, 120, 120);
        break;
    case State::kPairing:
        // Bright yellow blink — actively commissioning a new DoorLock.
        rgb(half_sec ? 255 : 0, half_sec ? 200 : 0, 0);
        break;
    case State::kSubscribed:
        // Steady green — at least one DoorLock subscribed and reachable.
        rgb(0, 180, 0);
        break;
    case State::kLocked:
        // Orange — last reported LockState=Locked.
        rgb(255, 90, 0);
        break;
    case State::kUnlocked:
        // Pulsing green — LockState=Unlocked.
        rgb(0, 80 + pulse, 0);
        break;
    case State::kSubscriptionLost:
        // Red blink — subscription dropped, auto-resubscribe in flight.
        rgb(one_sec ? 200 : 30, 0, 0);
        break;
    case State::kError:
        // Fast red blink — fatal init error.
        rgb(half_sec ? 255 : 0, 0, 0);
        break;
    }
}

static void led_task(void *)
{
    uint32_t tick = 0;
    for (;; ++tick) {
        render_tick(tick);
        vTaskDelay(pdMS_TO_TICKS(kTickMs));
    }
}

esp_err_t init()
{
    led_strip_config_t strip_cfg = {};
    strip_cfg.strip_gpio_num = kLedGpio;
    strip_cfg.max_leds       = 1;
    strip_cfg.led_model      = LED_MODEL_WS2812;
    strip_cfg.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;

    led_strip_rmt_config_t rmt_cfg = {};
    rmt_cfg.clk_src       = RMT_CLK_SRC_DEFAULT;
    rmt_cfg.resolution_hz = kLedRmtResHz;

    esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &g_led);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip init failed: 0x%x — LED disabled", err);
        g_led = nullptr;
        return err;
    }
    led_strip_clear(g_led);

    BaseType_t ok = xTaskCreate(led_task, "led_task", 2048, nullptr, 1, nullptr);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "led_task xTaskCreate failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "LED indicator on GPIO%d", kLedGpio);
    return ESP_OK;
}

} // namespace led
