// Copyright (c) 2026 Mayday / Ilia Arestov. See /LICENSE.
#include "AccessIntegrations.hpp"
#include "AccessJson.hpp"
#include <driver/gpio.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <nvs.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>

namespace aliro::access {
namespace {
constexpr char tag[] = "access-integrations";
std::mutex mutex, saveMutex;
Config config;
Relay relay;
bool logicalLocked = true, initialized = false;
bool reserved[49]{};
uint32_t generation = 1, delivered = 0, dropped = 0;
const char *telegramStatus = "disabled";
esp_timer_handle_t relayTimer = nullptr;
QueueHandle_t queue = nullptr;
TaskHandle_t worker = nullptr;
enum class Kind : uint8_t { Locked, Unlocked, Denied, Test };
struct Event { Kind kind; uint32_t generation; unsigned source; };
uint64_t nowMs() { return esp_timer_get_time() / 1000; }
void writeRelay() {
    if (config.relayEnabled) gpio_set_level(static_cast<gpio_num_t>(config.relayPin), relay.active == config.activeHigh);
}
void relayTick(void *) {
    std::lock_guard<std::mutex> guard(mutex);
    if (relay.tick(nowMs())) {
        writeRelay();
        ESP_LOGI(tag, "Relay pulse completed; output inactive (logical lock state unchanged)");
    }
}
void enqueue(Kind kind, unsigned source = 0) {
    // Called while holding mutex. Fixed-size queue; no network or allocation.
    if (!config.telegramEnabled || !queue || (kind == Kind::Denied && !config.notifyDenied)) return;
    Event event{kind, generation, source};
    if (xQueueSend(queue, &event, 0) != pdTRUE) ++dropped;
}
bool persist(const Config &c) {
    auto *j = configJson(c, true);
    char *text = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!text) return false;
    nvs_handle_t nvs = 0;
    esp_err_t result = nvs_open("integrations", NVS_READWRITE, &nvs);
    if (result == ESP_OK) {
        result = nvs_set_str(nvs, "config", text);
        if (result == ESP_OK) result = nvs_commit(nvs);
        nvs_close(nvs);
    }
    // Token is stored in NVS, never in the public source or HTTP response.
    std::fill(text, text + std::strlen(text), '\0');
    cJSON_free(text);
    return result == ESP_OK;
}
struct Response { std::string body; bool overflow = false; };
esp_err_t receiveHttp(esp_http_client_event_t *e) {
    if (e->event_id == HTTP_EVENT_ON_DATA) {
        auto &r = *static_cast<Response *>(e->user_data);
        if (e->data_len < 0 || r.body.size() + e->data_len > 16384) { r.overflow = true; return ESP_FAIL; }
        r.body.append(static_cast<char *>(e->data), e->data_len);
    }
    return ESP_OK;
}
struct Bot {
    esp_http_client_handle_t http = nullptr;
    Response response;
    uint64_t nextRequest = 0;
    uint32_t version = 0;
    ~Bot() { close(); }
    void close() { if (http) esp_http_client_cleanup(http); http = nullptr; }
    cJSON *call(const Config &c, const char *method, cJSON *body) {
        esp_log_level_set("HTTP_CLIENT", ESP_LOG_NONE);
        if (nowMs() < nextRequest) return nullptr;
        if (std::time(nullptr) < 1700000000) { setStatus("clock_required"); nextRequest = nowMs() + 10000; return nullptr; }
        const std::string url = "https://api.telegram.org/bot" + c.token + "/" + method;
        response = {};
        if (!http) {
            esp_http_client_config_t options{};
            options.url = url.c_str(); options.timeout_ms = 5000;
            options.crt_bundle_attach = esp_crt_bundle_attach;
            options.event_handler = receiveHttp; options.user_data = &response;
            options.disable_auto_redirect = true; options.keep_alive_enable = true;
            http = esp_http_client_init(&options);
        } else esp_http_client_set_url(http, url.c_str());
        if (!http) { setStatus("error"); nextRequest = nowMs() + 30000; return nullptr; }
        char *payload = cJSON_PrintUnformatted(body);
        if (!payload) return nullptr;
        esp_http_client_set_method(http, HTTP_METHOD_POST);
        esp_http_client_set_header(http, "Content-Type", "application/json");
        esp_http_client_set_post_field(http, payload, std::strlen(payload));
        auto result = esp_http_client_perform(http);
        int code = esp_http_client_get_status_code(http);
        cJSON_free(payload);
        auto *json = result == ESP_OK && !response.overflow ? cJSON_Parse(response.body.c_str()) : nullptr;
        bool ok = code == 200 && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "ok"));
        if (!ok) {
            unsigned wait = 30;
            auto *retry = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(json, "parameters"), "retry_after");
            if (code == 429 && cJSON_IsNumber(retry)) wait = std::clamp(retry->valueint, 1, 3600);
            nextRequest = nowMs() + uint64_t(wait) * 1000;
            setStatus(code == 429 ? "rate_limited" : "error");
            // No URL, response body, bot token, chat ID or key data in logs.
            ESP_LOGW(tag, "Telegram request failed (HTTP %d); backing off", code);
            cJSON_Delete(json); close(); return nullptr;
        }
        nextRequest = nowMs() + 1000;
        setStatus("ready");
        return json;
    }
    void setStatus(const char *value) {
        std::lock_guard<std::mutex> guard(mutex);
        if (generation == version) telegramStatus = value;
    }
    bool send(const Config &c, const char *text) {
        auto *body = cJSON_CreateObject();
        cJSON_AddStringToObject(body, "chat_id", c.chatId.c_str());
        cJSON_AddStringToObject(body, "text", text);
        auto *reply = call(c, "sendMessage", body);
        bool ok = reply != nullptr;
        cJSON_Delete(reply); cJSON_Delete(body);
        std::lock_guard<std::mutex> guard(mutex);
        if (generation == version) { if (ok) ++delivered; else ++dropped; }
        return ok;
    }

};
void telegramWorker(void *) {
    Bot bot;
    for (;;) {
        Config current; uint32_t version;
        { std::lock_guard<std::mutex> guard(mutex); current = config; version = generation; }
        if (version != bot.version) {
            bot.close(); bot.version = version; bot.nextRequest = 0;
        }
        if (!current.telegramEnabled) { bot.close(); vTaskDelay(pdMS_TO_TICKS(500)); continue; }
        if (nowMs() < bot.nextRequest) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        Event event{};
        if (xQueueReceive(queue, &event, 0) == pdTRUE) {
            if (event.generation != version) continue;
            uint8_t mac[6]{}; esp_read_mac(mac, ESP_MAC_WIFI_STA);
            char text[180];
            const char *what = event.kind == Kind::Locked ? "Logical lock state: LOCKED" : event.kind == Kind::Unlocked ? "Logical lock state: UNLOCKED" : event.kind == Kind::Denied ? "NFC access denied" : "Telegram test notification";
            std::snprintf(text, sizeof(text), "Aliro %02X%02X%02X\n%s\nNo door-position sensor is configured.", mac[3], mac[4], mac[5], what);
            bot.send(current, text);
        } else vTaskDelay(pdMS_TO_TICKS(250));
    }
}
bool prepare(const Config &candidate, std::string &error) {
    if (candidate.relayEnabled && reserved[candidate.relayPin]) {
        error = "This GPIO is already used by the board configuration."; return false;
    }
    if (candidate.relayEnabled && !relayTimer) {
        esp_timer_create_args_t args{}; args.callback = relayTick; args.name = "relay-deadline";
        if (esp_timer_create(&args, &relayTimer) != ESP_OK || esp_timer_start_periodic(relayTimer, 20000) != ESP_OK) {
            if (relayTimer) esp_timer_delete(relayTimer);
            relayTimer = nullptr; error = "Cannot start relay timer."; return false;
        }
    }
    if (candidate.telegramEnabled && !worker) {
        if (!queue) queue = xQueueCreate(16, sizeof(Event));
        // The HTTP client logs complete URLs at verbose levels. Never allow
        // them to disclose the write-only bot token, even in debug builds.
        esp_log_level_set("HTTP_CLIENT", ESP_LOG_NONE);
        esp_log_level_set("TRANSPORT_BASE", ESP_LOG_NONE);
        if (!queue || xTaskCreate(telegramWorker, "telegram", 8192, nullptr, 2, &worker) != pdPASS) {
            error = "Cannot start Telegram worker."; return false;
        }
    }
    return true;
}
bool activate(const Config &candidate, std::string &error) {
    std::lock_guard<std::mutex> guard(mutex);
    if (config.relayEnabled) {
        gpio_set_level(static_cast<gpio_num_t>(config.relayPin), !config.activeHigh);
        // Keep the previous pin driven inactive. External pull resistors must
        // provide the same inactive level during boot and power loss.
    }
    relay.reset();
    if (candidate.relayEnabled) {
        gpio_num_t pin = static_cast<gpio_num_t>(candidate.relayPin);
        gpio_set_level(pin, !candidate.activeHigh); // Set latch before output mode.
        if (gpio_set_direction(pin, GPIO_MODE_OUTPUT) != ESP_OK) {
            error = "Cannot configure relay GPIO."; return false;
        }
    }
    config = candidate; ++generation;
    if (queue) xQueueReset(queue);
    telegramStatus = config.telegramEnabled ? "waiting" : "disabled";
    return true;
}
} // namespace

void reservePin(int pin) {
    if (!initialized && pin >= 0 && pin < 49) reserved[pin] = true;
}
void begin() {
    std::lock_guard<std::mutex> saving(saveMutex);
    if (initialized) return;
    initialized = true;
    nvs_handle_t nvs;
    Config stored;
    if (nvs_open("integrations", NVS_READONLY, &nvs) == ESP_OK) {
        size_t size = 0;
        if (nvs_get_str(nvs, "config", nullptr, &size) == ESP_OK && size > 0 && size <= 2048) {
            std::string text(size, '\0');
            if (nvs_get_str(nvs, "config", text.data(), &size) == ESP_OK) {
                auto *j = cJSON_Parse(text.c_str()); std::string error;
                if (!parse(j, stored, error)) stored = Config{};
                cJSON_Delete(j);
            }
            std::fill(text.begin(), text.end(), '\0');
        }
        nvs_close(nvs);
    }
    std::string error;
    if (!prepare(stored, error) || !activate(stored, error)) ESP_LOGE(tag, "Integrations disabled: initialization failed");
}
void lockChanged(bool locked, unsigned source) {
    std::lock_guard<std::mutex> guard(mutex);
    bool changed = logicalLocked != locked;
    logicalLocked = locked;
    relay.state(config, locked, nowMs()); writeRelay();
    if (config.relayEnabled) ESP_LOGI(tag, "Relay output %s (source=%u)", relay.active ? "active" : "inactive", source);
    if (changed) enqueue(locked ? Kind::Locked : Kind::Unlocked, source);
}
void accessDenied() { std::lock_guard<std::mutex> guard(mutex); enqueue(Kind::Denied); }
cJSON *status() {
    std::lock_guard<std::mutex> guard(mutex);
    auto *j = configJson(config, false);
    cJSON_AddBoolToObject(j, "relayActive", relay.active);
    cJSON_AddBoolToObject(j, "locked", logicalLocked);
    cJSON_AddStringToObject(j, "telegramStatus", telegramStatus);
    cJSON_AddNumberToObject(j, "delivered", delivered);
    cJSON_AddNumberToObject(j, "dropped", dropped);
    return j;
}
bool configure(const cJSON *j, std::string &error) {
    std::lock_guard<std::mutex> saving(saveMutex);
    Config candidate;
    { std::lock_guard<std::mutex> guard(mutex); candidate = config; }
    if (!parse(j, candidate, error) || !prepare(candidate, error)) return false;
    if (!persist(candidate)) { error = "Cannot save integration settings."; return false; }
    if (!activate(candidate, error)) {
        // Never boot into an output configuration which failed at runtime.
        candidate.relayEnabled = false; persist(candidate); activate(candidate, error); return false;
    }
    return true;
}
bool testNotification(std::string &error) {
    std::lock_guard<std::mutex> guard(mutex);
    if (!config.telegramEnabled || !queue) { error = "Configure Telegram first."; return false; }
    Event event{Kind::Test, generation, 0};
    if (xQueueSend(queue, &event, 0) != pdTRUE) { error = "Telegram queue is full."; return false; }
    return true;
}
} // namespace aliro::access
