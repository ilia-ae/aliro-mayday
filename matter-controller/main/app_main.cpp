#include <sdkconfig.h>
/* matter-controller — central Matter hub for the aliro-esp32 DoorLock fleet.
 *
 * What this firmware does on every boot:
 *   1. Init WS2812 LED indicator (GPIO48) — visual state machine.
 *   2. Auto-connect to WiFi (creds preserved in NVS by esp_wifi).
 *   3. Start esp-matter controller stack (commissioner enabled).
 *   4. Register chip-shell so `matter esp controller pairing/...` is usable
 *      for one-time DoorLock commissioning over BLE-WiFi.
 *   5. Once an IPv4 address arrives, the resubscriber task wakes up and
 *      issues `Subscribe` to LockState (cluster 0x0101 attribute 0x0000)
 *      on every previously-paired DoorLock node, with auto_resubscribe=true.
 *      Apple TV's job — but on a hardware we control.
 *
 * The set of subscribed nodes is currently a compile-time list because we
 * don't yet have a UI. Add a node ID once via chip-shell pairing, then
 * add it to kKnownNodes[] and reflash. Future: read the fabric table and
 * subscribe wildcard.
 */

#include <esp_err.h>
#include <esp_log.h>
#include <esp_event.h>
#include <esp_wifi.h>
#include <nvs_flash.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_controller_client.h>
#include <esp_matter_controller_console.h>
#include <esp_matter_controller_subscribe_command.h>
#include <esp_matter_controller_utils.h>
#include <esp_matter_ota.h>
#include <platform/CHIPDeviceConfig.h>
#include <platform/PlatformManager.h>

#include <app/server/Server.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <credentials/FabricTable.h>
#include <lib/core/TLV.h>
#include <lib/support/CHIPMem.h>

#include <common_macros.h>

#include "led_indicator.hpp"

static const char *TAG = "ctrl-main";

// -----------------------------------------------------------------------------
// Config — edit here, no menuconfig needed.
// -----------------------------------------------------------------------------

// Configure local Wi-Fi in menuconfig; sdkconfig is excluded from Git.
// No deployment credentials belong in the source or its public export.
static constexpr const char *kWifiSsid = CONFIG_MATTER_CONTROLLER_WIFI_SSID;
static constexpr const char *kWifiPass = CONFIG_MATTER_CONTROLLER_WIFI_PASSWORD;

// Known DoorLock node IDs. Add to this list after each pairing.
// 0x3E9 = 1001 — first DoorLock (paired 2026-04-30 16:59 from fabric
// 0B71DDA839473DA7).
static constexpr uint64_t kKnownNodes[] = { 0x3E9 };

// DoorLock cluster IDs we subscribe to.
static constexpr uint16_t kDoorLockEndpoint   = 1;
static constexpr uint32_t kDoorLockClusterId  = 0x0101;
static constexpr uint32_t kAttrLockState      = 0x0000;
// Subscription cadence (sec). Min=1 means report immediately on change.
// Max=60 means heartbeat at least once per minute even if state doesn't
// change — gives Apple-TV-like persistent presence.
static constexpr uint16_t kSubMinInterval     = 1;
static constexpr uint16_t kSubMaxInterval     = 60;

// -----------------------------------------------------------------------------
// LED reactions to WiFi events.
// -----------------------------------------------------------------------------

static void wifi_event_cb(void *, esp_event_base_t base, int32_t id, void *)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
        case WIFI_EVENT_STA_DISCONNECTED:
            led::set_state(led::State::kWifiConnecting);
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        led::set_state(led::State::kWifiConnected);
        ESP_LOGI(TAG, "WiFi up — IP acquired");
    }
}

// -----------------------------------------------------------------------------
// LockState parsing for LED reaction.
// -----------------------------------------------------------------------------
//
// LockState attribute (DoorLock cluster 0x0101 attr 0x0000) is a Nullable<enum8>:
//   0 = NotFullyLocked
//   1 = Locked
//   2 = Unlocked
//   3 = Unlatched
//
// TLV payload from esp-matter is the raw enum8. We just decode the byte.
//
static void on_attribute_report(uint64_t remote_node_id,
                                const chip::app::ConcreteDataAttributePath &path,
                                chip::TLV::TLVReader *data)
{
    ESP_LOGI(TAG, "report from node 0x%llX ep=%u cluster=0x%lX attr=0x%lX",
             (unsigned long long)remote_node_id, path.mEndpointId,
             (unsigned long)path.mClusterId, (unsigned long)path.mAttributeId);

    if (path.mClusterId != kDoorLockClusterId || path.mAttributeId != kAttrLockState) {
        return;
    }
    if (!data) return;

    uint8_t state = 0xFF;
    CHIP_ERROR err = data->Get(state);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "could not decode LockState: %" CHIP_ERROR_FORMAT, err.Format());
        return;
    }

    ESP_LOGI(TAG, "LockState = %u (node 0x%llX)", state, (unsigned long long)remote_node_id);

    switch (state) {
    case 1: led::set_state(led::State::kLocked);   break;
    case 2: led::set_state(led::State::kUnlocked); break;
    default: led::set_state(led::State::kSubscribed); break;
    }
}

// `subscribe_done_cb` from esp_matter_controller is fired on subscription
// TEARDOWN (OnDone), not on establishment. Treat it as loss — the
// subscribe_command instance self-deletes right after this returns.
static void on_subscribe_done(uint64_t remote_node_id, uint32_t subscription_id)
{
    ESP_LOGW(TAG, "subscription %lu torn down for node 0x%llX",
             (unsigned long)subscription_id, (unsigned long long)remote_node_id);
    led::set_state(led::State::kSubscriptionLost);
}

// Fired on initial CASE/connection failure. `auto_resubscribe=true` means the
// ReadClient's default policy will keep retrying — we just paint red.
static void on_subscribe_failure(void *subscribe_command)
{
    ESP_LOGW(TAG, "subscription connection failure — auto_resubscribe will retry");
    led::set_state(led::State::kSubscriptionLost);
}

// -----------------------------------------------------------------------------
// Auto-subscribe task.
// -----------------------------------------------------------------------------
//
// Runs once on first IP_EVENT_STA_GOT_IP. Iterates kKnownNodes[] and issues a
// subscribe with auto_resubscribe=true so the subscription survives:
//   - device reboots
//   - WiFi reconnects
//   - controller reboots (controller persists fabric NOC in NVS)
//
// The free-standing `send_subscribe_attr_command(...)` swallows callbacks —
// to receive ReportData we need to instantiate `subscribe_command` ourselves
// with our cbs. The class self-deletes on OnDone (see esp_matter_controller_
// subscribe_command.cpp:181), so a leak-free fire-and-forget is just
// chip::Platform::New<subscribe_command>(...) + send_command().
static esp_err_t subscribe_lock_state(uint64_t node_id)
{
    using namespace chip;
    using namespace chip::app;
    using esp_matter::controller::subscribe_command;

    Platform::ScopedMemoryBufferWithSize<AttributePathParams> attr_paths;
    Platform::ScopedMemoryBufferWithSize<EventPathParams>     event_paths;
    attr_paths.Alloc(1);
    if (!attr_paths.Get()) return ESP_ERR_NO_MEM;
    attr_paths[0] = AttributePathParams(kDoorLockEndpoint, kDoorLockClusterId, kAttrLockState);

    auto *cmd = Platform::New<subscribe_command>(
        node_id,
        std::move(attr_paths),
        std::move(event_paths),
        kSubMinInterval,
        kSubMaxInterval,
        /*auto_resubscribe=*/true,
        on_attribute_report,
        /*event_cb=*/nullptr,
        on_subscribe_done,
        on_subscribe_failure,
        /*keep_subscription=*/true);
    if (!cmd) return ESP_ERR_NO_MEM;
    return cmd->send_command();
}

static void auto_subscribe_task(void *)
{
    // Give the Matter stack a beat to settle after IP came up.
    vTaskDelay(pdMS_TO_TICKS(2000));

    for (uint64_t node_id : kKnownNodes) {
        ESP_LOGI(TAG, "auto-subscribing to LockState on node 0x%llX", (unsigned long long)node_id);

        esp_matter::lock::chip_stack_lock(portMAX_DELAY);
        esp_err_t err = subscribe_lock_state(node_id);
        esp_matter::lock::chip_stack_unlock();

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "subscribe_lock_state failed for node 0x%llX: %s",
                     (unsigned long long)node_id, esp_err_to_name(err));
            led::set_state(led::State::kSubscriptionLost);
        }

        // Stagger subscribes by 1 sec so concurrent CASE handshakes don't
        // contend for crypto buffers.
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    vTaskDelete(nullptr);
}

// -----------------------------------------------------------------------------
// Matter device-layer event callback. Trigger auto-subscribe on first IP.
// -----------------------------------------------------------------------------

static void app_event_cb(const ChipDeviceEvent *event, intptr_t /*arg*/)
{
    using namespace chip::DeviceLayer;
    static bool s_subscribe_kicked = false;

    switch (event->Type) {
    case DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "[ev] interface IP changed");
        if (!s_subscribe_kicked) {
            s_subscribe_kicked = true;
            xTaskCreate(auto_subscribe_task, "auto_sub", 4096, nullptr, 5, nullptr);
        }
        break;
    case DeviceEventType::kInternetConnectivityChange:
        ESP_LOGI(TAG, "[ev] internet connectivity (ipv4=%d ipv6=%d)",
                 event->InternetConnectivityChange.IPv4,
                 event->InternetConnectivityChange.IPv6);
        break;
    default:
        break;
    }
}

// -----------------------------------------------------------------------------
// app_main
// -----------------------------------------------------------------------------

extern "C" void app_main()
{
    ESP_LOGI(TAG, "============================================================");
    ESP_LOGI(TAG, "matter-controller booting");
    ESP_LOGI(TAG, "============================================================");

    led::init();
    led::set_state(led::State::kBoot);

    // NVS first — required by chip's persistent storage delegate.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase, doing it now");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Default event loop is needed for both WiFi and Matter.
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_cb, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,  IP_EVENT_STA_GOT_IP, &wifi_event_cb, nullptr));

    // chip-shell — keep the controller, factoryreset, wifi, diagnostics commands
    // available on USB so we can pair new DoorLocks without reflashing.
#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::init();
#if CONFIG_ESP_MATTER_CONTROLLER_ENABLE
    esp_matter::console::controller_register_commands();
#endif
#endif

    // Matter stack — NVS-backed persistence delegate, fabric table, etc.
    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "esp_matter::start failed: 0x%x", err));

    // Commissioner — in-process, no separate Matter server. NodeId 112233 in
    // fabric_id=1 listens on ephemeral port 5580 for inbound CASE.
#if CONFIG_ESP_MATTER_COMMISSIONER_ENABLE
    esp_matter::lock::chip_stack_lock(portMAX_DELAY);
    esp_matter::controller::matter_controller_client::get_instance().init(112233, 1, 5580);
    esp_matter::controller::matter_controller_client::get_instance().setup_commissioner();
    esp_matter::lock::chip_stack_unlock();
#endif

    // Subscribe report routing is handled per-subscribe_command instance —
    // see auto_subscribe_task() / subscribe_lock_state() below.

    // An explicit build setting replaces the saved network; empty settings
    // preserve a network already provisioned in NVS.
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    wifi_config_t wifi_cfg = {};
    if (kWifiSsid[0] != '\0') {
        strncpy(reinterpret_cast<char *>(wifi_cfg.sta.ssid), kWifiSsid, sizeof(wifi_cfg.sta.ssid));
        strncpy(reinterpret_cast<char *>(wifi_cfg.sta.password), kWifiPass, sizeof(wifi_cfg.sta.password));
        wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    } else {
        ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg));
        if (wifi_cfg.sta.ssid[0] == '\0') {
            ESP_LOGW(TAG, "No WiFi configured; set the network through menuconfig and rebuild");
            return;
        }
    }
    led::set_state(led::State::kWifiConnecting);
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_err_t conn_err = esp_wifi_connect();
    if (conn_err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect early returned: %s (will retry on STA_START)",
                 esp_err_to_name(conn_err));
    }

    ESP_LOGI(TAG, "init done — waiting for IP, will auto-subscribe to %u known nodes",
             (unsigned)(sizeof(kKnownNodes) / sizeof(kKnownNodes[0])));
}
