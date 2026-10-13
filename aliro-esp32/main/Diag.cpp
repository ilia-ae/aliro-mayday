#include "Diag.h"

#include <esp_log.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_idf_version.h>
#include <esp_app_desc.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_timer.h>
#include <esp_netif.h>
#include <esp_ping.h>
#include <ping/ping_sock.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>

#include <platform/CHIPDeviceLayer.h>
#include <credentials/FabricTable.h>
#include <app/server/Server.h>

static const char *TAG = "diag";

namespace diag {

// ---- internal NFC stats ----------------------------------------------------
static struct {
    int64_t  last_event_us = 0;
    uint32_t total_events = 0;
    uint8_t  last_uid[10] = {0};
    uint8_t  last_uid_len = 0;
    char     last_info[32] = {0};
} g_nfc;

static struct {
    int64_t  last_phase_us = 0;
    char     last_phase[32] = {0};
    char     last_detail[64] = {0};
} g_aliro;

// Rolling WiFi health stats — populated by event handlers + ping task.
static struct {
    int64_t  last_connect_us  = 0;
    int64_t  last_disconnect_us = 0;
    uint32_t connect_count    = 0;
    uint32_t disconnect_count = 0;
    uint8_t  last_disc_reason = 0;
    // Gateway-ping rolling stats over last N successful pings.
    uint32_t ping_seq         = 0;
    uint32_t ping_loss        = 0;
    uint32_t ping_min_ms      = 0;
    uint32_t ping_avg_ms      = 0;
    uint32_t ping_max_ms      = 0;
    uint32_t ping_last_ms     = 0;
    uint8_t  gateway_ip[4]    = {0};
} g_wifi;

// ---- helpers ---------------------------------------------------------------
static void format_uid(const uint8_t *uid, size_t len, char *out, size_t out_sz)
{
    if (!uid || !len) { snprintf(out, out_sz, "(none)"); return; }
    size_t pos = 0;
    for (size_t i = 0; i < len && pos + 3 < out_sz; i++) {
        pos += snprintf(out + pos, out_sz - pos, "%02X", uid[i]);
    }
}

static const char *reset_reason_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:    return "POWERON";
    case ESP_RST_EXT:        return "EXT_RESET";
    case ESP_RST_SW:         return "SW_REBOOT";
    case ESP_RST_PANIC:      return "PANIC";
    case ESP_RST_INT_WDT:    return "INT_WDT";
    case ESP_RST_TASK_WDT:   return "TASK_WDT";
    case ESP_RST_WDT:        return "OTHER_WDT";
    case ESP_RST_DEEPSLEEP:  return "DEEP_SLEEP";
    case ESP_RST_BROWNOUT:   return "BROWNOUT";
    case ESP_RST_SDIO:       return "SDIO";
    case ESP_RST_USB:        return "USB";
    case ESP_RST_JTAG:       return "JTAG";
    default:                 return "UNKNOWN";
    }
}

static const char *chip_model_name(esp_chip_model_t m)
{
    switch (m) {
    case CHIP_ESP32:    return "ESP32";
    case CHIP_ESP32S2:  return "ESP32-S2";
    case CHIP_ESP32S3:  return "ESP32-S3";
    case CHIP_ESP32C2:  return "ESP32-C2";
    case CHIP_ESP32C3:  return "ESP32-C3";
    case CHIP_ESP32C6:  return "ESP32-C6";
    case CHIP_ESP32H2:  return "ESP32-H2";
    case CHIP_ESP32P4:  return "ESP32-P4";
    default:            return "?";
    }
}

// Map WiFi disconnect reason codes (esp_wifi_types.h:wifi_err_reason_t) to
// human-readable names. The most common values for our debugging are:
//   201 NO_AP_FOUND       — SSID not in scan range
//   200 ASSOC_FAIL         — auth handshake refused (wrong PSK / WPA3 mismatch)
//   202 AUTH_FAIL          — same family
//   2/3/4/5/6              — beacon timeout / BSSID change / etc. → roaming
static const char *wifi_disc_reason_name(uint8_t r)
{
    switch (r) {
    case 1:   return "UNSPECIFIED";
    case 2:   return "AUTH_EXPIRE";
    case 3:   return "AUTH_LEAVE";
    case 4:   return "ASSOC_EXPIRE";
    case 5:   return "ASSOC_TOOMANY";
    case 6:   return "NOT_AUTHED";
    case 7:   return "NOT_ASSOCED";
    case 8:   return "ASSOC_LEAVE";
    case 14:  return "MIC_FAILURE";
    case 15:  return "4WAY_HANDSHAKE_TIMEOUT";
    case 16:  return "GROUP_KEY_UPDATE_TIMEOUT";
    case 200: return "BEACON_TIMEOUT";
    case 201: return "NO_AP_FOUND";
    case 202: return "AUTH_FAIL";
    case 203: return "ASSOC_FAIL";
    case 204: return "HANDSHAKE_TIMEOUT";
    case 205: return "CONNECTION_FAIL";
    default:  return "OTHER";
    }
}

static const char *wifi_phy_name(wifi_phy_mode_t m)
{
    switch (m) {
    case WIFI_PHY_MODE_LR:    return "LR";
    case WIFI_PHY_MODE_11B:   return "11b";
    case WIFI_PHY_MODE_11G:   return "11g";
    case WIFI_PHY_MODE_HT20:  return "11n_HT20";
    case WIFI_PHY_MODE_HT40:  return "11n_HT40";
    case WIFI_PHY_MODE_HE20:  return "11ax_HE20";
    default:                  return "?";
    }
}

// ---- WiFi event handler ----------------------------------------------------
// Logs every connection lifecycle transition. Critical for figuring out
// whether the lock dropped because of beacon timeout (mesh roaming churn) vs
// auth failure vs something else.
static void diag_wifi_event_cb(void * /*arg*/, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            // Apple Matter (CASE handshake, subscription-resumption) needs
            // sub-second packet response. Default PS_MIN_MODEM sleeps the
            // radio between DTIMs and drops inbound SYN/UDP — producing
            // CHIP error 0x32 (timeout) and "Not Responding" in Apple Home.
            // Setting it on STA_START rather than STA_CONNECTED ensures the
            // policy is in place before any association attempt AND across
            // every disconnect→reconnect cycle (roaming, AP reboot, etc.).
            if (esp_err_t ps_err = esp_wifi_set_ps(WIFI_PS_NONE); ps_err != ESP_OK) {
                ESP_LOGW(TAG, "[diag] [wifi] esp_wifi_set_ps(NONE) failed: %s",
                         esp_err_to_name(ps_err));
            } else {
                ESP_LOGI(TAG, "[diag] [wifi] power-save disabled (PS_NONE) for Matter latency");
            }
            ESP_LOGI(TAG, "[diag] [wifi] STA started, attempting connect");
            break;
        case WIFI_EVENT_STA_CONNECTED: {
            auto *ev = (wifi_event_sta_connected_t *)data;
            wifi_phy_mode_t phy = WIFI_PHY_MODE_HT20;
            esp_wifi_sta_get_negotiated_phymode(&phy);
            g_wifi.connect_count++;
            g_wifi.last_connect_us = esp_timer_get_time();
            ESP_LOGI(TAG, "[diag] [wifi] CONNECTED to '%.*s' "
                          "ch=%u BSSID=%02X:%02X:%02X:%02X:%02X:%02X "
                          "auth=%d phy=%s (#%lu)",
                     ev->ssid_len, ev->ssid, ev->channel,
                     ev->bssid[0], ev->bssid[1], ev->bssid[2],
                     ev->bssid[3], ev->bssid[4], ev->bssid[5],
                     ev->authmode, wifi_phy_name(phy),
                     (unsigned long)g_wifi.connect_count);
            break;
        }
        case WIFI_EVENT_STA_DISCONNECTED: {
            auto *ev = (wifi_event_sta_disconnected_t *)data;
            g_wifi.disconnect_count++;
            g_wifi.last_disconnect_us = esp_timer_get_time();
            g_wifi.last_disc_reason = ev->reason;
            int64_t connected_for = g_wifi.last_connect_us
                ? (g_wifi.last_disconnect_us - g_wifi.last_connect_us) / 1000000
                : -1;
            ESP_LOGW(TAG, "[diag] [wifi] DISCONNECTED reason=%u (%s) rssi=%d "
                          "connected_for=%llds (#%lu)",
                     ev->reason, wifi_disc_reason_name(ev->reason),
                     ev->rssi, (long long)connected_for,
                     (unsigned long)g_wifi.disconnect_count);
            break;
        }
        case WIFI_EVENT_STA_AUTHMODE_CHANGE:
            ESP_LOGW(TAG, "[diag] [wifi] auth mode changed by AP");
            break;
        case WIFI_EVENT_STA_BSS_RSSI_LOW:
            ESP_LOGW(TAG, "[diag] [wifi] BSS RSSI low — roaming likely");
            break;
        case WIFI_EVENT_STA_BEACON_TIMEOUT:
            ESP_LOGW(TAG, "[diag] [wifi] BEACON_TIMEOUT — AP stopped beaconing or out of range");
            break;
        default:
            ESP_LOGD(TAG, "[diag] [wifi] event id=%ld", (long)id);
            break;
        }
    } else if (base == IP_EVENT) {
        switch (id) {
        case IP_EVENT_STA_GOT_IP: {
            auto *ev = (ip_event_got_ip_t *)data;
            g_wifi.gateway_ip[0] = ip4_addr1(&ev->ip_info.gw);
            g_wifi.gateway_ip[1] = ip4_addr2(&ev->ip_info.gw);
            g_wifi.gateway_ip[2] = ip4_addr3(&ev->ip_info.gw);
            g_wifi.gateway_ip[3] = ip4_addr4(&ev->ip_info.gw);
            ESP_LOGI(TAG, "[diag] [wifi] IP=" IPSTR " gw=" IPSTR " mask=" IPSTR " (changed=%d)",
                     IP2STR(&ev->ip_info.ip), IP2STR(&ev->ip_info.gw),
                     IP2STR(&ev->ip_info.netmask), ev->ip_changed);
            break;
        }
        case IP_EVENT_STA_LOST_IP: {
            ESP_LOGW(TAG, "[diag] [wifi] STA lost IP — restarting DHCP client");
            // ESP-IDF does not auto-renew the DHCP lease on lost-IP. The
            // station stays on L2, mDNS keeps advertising stale records,
            // and the controller (iPhone) sees the accessory as "no
            // connection". Manually bounce dhcpc + a unicast packet so
            // the lease is requested again.
            esp_netif_t *ni = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (ni) {
                esp_netif_dhcpc_stop(ni);
                esp_netif_dhcpc_start(ni);
            }
            // Clear gateway so peer-ping task pauses until we get IP back.
            memset(g_wifi.gateway_ip, 0, sizeof(g_wifi.gateway_ip));
            break;
        }
        case IP_EVENT_GOT_IP6: {
            auto *ev = (ip_event_got_ip6_t *)data;
            ESP_LOGI(TAG, "[diag] [wifi] IPv6=" IPV6STR " scope=%d",
                     IPV62STR(ev->ip6_info.ip), ev->ip_index);
            break;
        }
        default:
            break;
        }
    }
}

void install_wifi_event_logger()
{
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                               diag_wifi_event_cb, NULL);
    esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID,
                               diag_wifi_event_cb, NULL);
    ESP_LOGI(TAG, "[diag] [wifi] event logger installed");
}

// ---- gateway ping ---------------------------------------------------------
// Run an ICMP ping every N seconds against the default gateway. Lets us tell
// "WiFi is up but lossy" from "WiFi is down" — and quantifies the loss.
static void on_ping_success(esp_ping_handle_t hdl, void * /*args*/)
{
    uint32_t elapsed = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed, sizeof(elapsed));
    g_wifi.ping_last_ms = elapsed;
    g_wifi.ping_seq++;
    if (g_wifi.ping_min_ms == 0 || elapsed < g_wifi.ping_min_ms) g_wifi.ping_min_ms = elapsed;
    if (elapsed > g_wifi.ping_max_ms) g_wifi.ping_max_ms = elapsed;
    g_wifi.ping_avg_ms = (g_wifi.ping_avg_ms * 7 + elapsed) / 8;  // EMA
}

static void on_ping_timeout(esp_ping_handle_t /*hdl*/, void * /*args*/)
{
    g_wifi.ping_loss++;
    g_wifi.ping_seq++;
}

static void gateway_ping_task(void *arg)
{
    uint32_t interval = (uint32_t)(uintptr_t)arg;
    // Wait for IP first.
    while (g_wifi.gateway_ip[0] == 0) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    ip_addr_t target;
    IP_ADDR4(&target, g_wifi.gateway_ip[0], g_wifi.gateway_ip[1],
                       g_wifi.gateway_ip[2], g_wifi.gateway_ip[3]);
    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.target_addr = target;
    config.count = ESP_PING_COUNT_INFINITE;
    config.interval_ms = interval * 1000;
    config.timeout_ms = 1000;

    esp_ping_callbacks_t cbs = {};
    cbs.on_ping_success = on_ping_success;
    cbs.on_ping_timeout = on_ping_timeout;

    esp_ping_handle_t ping;
    if (esp_ping_new_session(&config, &cbs, &ping) == ESP_OK) {
        esp_ping_start(ping);
        ESP_LOGI(TAG, "[diag] [wifi] gateway ping running (target=%u.%u.%u.%u, interval=%lus)",
                 g_wifi.gateway_ip[0], g_wifi.gateway_ip[1],
                 g_wifi.gateway_ip[2], g_wifi.gateway_ip[3],
                 (unsigned long)interval);
    } else {
        ESP_LOGE(TAG, "[diag] [wifi] failed to start gateway ping");
    }
    vTaskDelete(NULL);
}

void start_gateway_ping(uint32_t ping_interval_sec)
{
    if (ping_interval_sec == 0) {
        ESP_LOGW(TAG, "[diag] [wifi] gateway ping disabled");
        return;
    }
    BaseType_t r = xTaskCreate(gateway_ping_task, "diag_gwping", 4096,
                               (void *)(uintptr_t)ping_interval_sec, 1, NULL);
    ESP_LOGI(TAG, "[diag] [wifi] gateway ping task %s",
             r == pdPASS ? "spawned" : "FAILED to spawn");
}

// ---- peer (controller iPhone) ping -----------------------------------------
struct peer_state_t {
    bool     used        = false;
    char     label[12]   = {0};
    uint8_t  ip[4]       = {0};
    uint32_t seq         = 0;
    uint32_t loss        = 0;
    uint32_t min_ms      = 0;
    uint32_t avg_ms      = 0;
    uint32_t max_ms      = 0;
    uint32_t last_ms     = 0;
    int64_t  last_ok_us  = 0;
};
static peer_state_t g_peer;

static void on_peer_ok(esp_ping_handle_t hdl, void *)
{
    uint32_t elapsed = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed, sizeof(elapsed));
    g_peer.last_ms = elapsed;
    g_peer.seq++;
    if (g_peer.min_ms == 0 || elapsed < g_peer.min_ms) g_peer.min_ms = elapsed;
    if (elapsed > g_peer.max_ms) g_peer.max_ms = elapsed;
    g_peer.avg_ms = (g_peer.avg_ms * 7 + elapsed) / 8;
    g_peer.last_ok_us = esp_timer_get_time();
}
static void on_peer_loss(esp_ping_handle_t, void *)
{
    g_peer.loss++;
    g_peer.seq++;
}

struct peer_ping_args {
    uint8_t  ip[4];
    char     label[12];
    uint32_t interval_sec;
};

static void peer_ping_task(void *arg)
{
    auto *a = static_cast<peer_ping_args *>(arg);
    while (g_wifi.gateway_ip[0] == 0) {           // wait for IP
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    g_peer.used = true;
    memcpy(g_peer.ip,    a->ip,    4);
    strncpy(g_peer.label, a->label, sizeof(g_peer.label) - 1);

    ip_addr_t target;
    IP_ADDR4(&target, a->ip[0], a->ip[1], a->ip[2], a->ip[3]);

    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.target_addr = target;
    config.count       = ESP_PING_COUNT_INFINITE;
    config.interval_ms = a->interval_sec * 1000;
    config.timeout_ms  = 1000;

    esp_ping_callbacks_t cbs = {};
    cbs.on_ping_success = on_peer_ok;
    cbs.on_ping_timeout = on_peer_loss;

    esp_ping_handle_t ping;
    if (esp_ping_new_session(&config, &cbs, &ping) == ESP_OK) {
        esp_ping_start(ping);
        ESP_LOGI(TAG, "[diag] [peer] %s ping running (target=%u.%u.%u.%u, interval=%lus)",
                 a->label, a->ip[0], a->ip[1], a->ip[2], a->ip[3],
                 (unsigned long)a->interval_sec);
    } else {
        ESP_LOGE(TAG, "[diag] [peer] failed to start %s ping", a->label);
    }
    delete a;
    vTaskDelete(NULL);
}

void start_peer_ping(uint8_t a, uint8_t b, uint8_t c, uint8_t d,
                     const char *label, uint32_t interval_sec)
{
    if (interval_sec == 0) return;
    auto *args = new peer_ping_args{};
    args->ip[0] = a; args->ip[1] = b; args->ip[2] = c; args->ip[3] = d;
    strncpy(args->label, label ? label : "peer", sizeof(args->label) - 1);
    args->interval_sec = interval_sec;
    BaseType_t r = xTaskCreate(peer_ping_task, "diag_peer", 4096, args, 1, NULL);
    ESP_LOGI(TAG, "[diag] [peer] %s ping task %s (target=%u.%u.%u.%u)",
             args->label, r == pdPASS ? "spawned" : "FAILED to spawn",
             a, b, c, d);
    if (r != pdPASS) delete args;
}

// ---- public API ------------------------------------------------------------

void boot_snapshot()
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint32_t flash_size_bytes = 0;
    esp_flash_get_size(NULL, &flash_size_bytes);

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);

    const esp_app_desc_t *app = esp_app_get_description();

    ESP_LOGI(TAG, "================ BOOT SNAPSHOT ================");
    ESP_LOGI(TAG, "[diag] chip       : %s rev v%u.%u, %d cores, %s%s%s%s",
             chip_model_name(chip.model),
             (unsigned)(chip.revision / 100),
             (unsigned)(chip.revision % 100),
             chip.cores,
             (chip.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi " : "",
             (chip.features & CHIP_FEATURE_BT)       ? "BT "   : "",
             (chip.features & CHIP_FEATURE_BLE)      ? "BLE "  : "",
             (chip.features & CHIP_FEATURE_IEEE802154) ? "Thread" : "");
    ESP_LOGI(TAG, "[diag] flash size : %lu bytes (%.1f MB)",
             (unsigned long)flash_size_bytes, flash_size_bytes / 1024.0 / 1024.0);
    ESP_LOGI(TAG, "[diag] BT MAC     : %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "[diag] reset rsn  : %s", reset_reason_name(esp_reset_reason()));
    ESP_LOGI(TAG, "[diag] IDF        : %d.%d.%d  (build " IDF_VER ")",
             ESP_IDF_VERSION_MAJOR, ESP_IDF_VERSION_MINOR, ESP_IDF_VERSION_PATCH);
    ESP_LOGI(TAG, "[diag] app        : %s v%s, built %s %s",
             app->project_name, app->version, app->date, app->time);
    ESP_LOGI(TAG, "[diag] heap free  : %lu B (largest block %lu B), min ever %lu B",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT),
             (unsigned long)esp_get_minimum_free_heap_size());
    ESP_LOGI(TAG, "================================================");
}

// Watchdog for stuck "no IPv4" condition: kicks dhcpc every 30s if WiFi
// is associated but we still don't have an IPv4 address. Without this an
// IPv4 lost event with the AP not re-issuing a DHCP renewal silently
// turns the accessory invisible to Apple Home.
static void ipv4_watchdog_task(void *)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(30 * 1000));
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) continue;       // not associated
        esp_netif_t *ni = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (!ni) continue;
        esp_netif_ip_info_t info = {};
        esp_netif_get_ip_info(ni, &info);
        if (info.ip.addr == 0) {
            ESP_LOGW(TAG, "[diag] [wifi] no IPv4 while associated to '%.32s' (rssi=%d) — bouncing DHCP",
                     ap.ssid, ap.rssi);
            esp_netif_dhcpc_stop(ni);
            esp_netif_dhcpc_start(ni);
        }
    }
}

static void heartbeat_task(void *arg)
{
    uint32_t interval_sec = (uint32_t)(uintptr_t)arg;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(interval_sec * 1000));

        // ---- uptime / heap ------------------------------------------------
        uint64_t up_us = esp_timer_get_time();
        uint32_t up_s  = (uint32_t)(up_us / 1000000);

        // ---- WiFi state ---------------------------------------------------
        wifi_ap_record_t ap = {};
        const char *ssid = "-";
        int rssi = 0;
        uint8_t channel = 0;
        char bssid_buf[20] = "-";
        wifi_phy_mode_t phy = WIFI_PHY_MODE_HT20;
        bool wifi_ok = false;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            wifi_ok = true;
            ssid = (const char *)ap.ssid;
            rssi = ap.rssi;
            channel = ap.primary;
            snprintf(bssid_buf, sizeof(bssid_buf),
                     "%02X:%02X:%02X:%02X:%02X:%02X",
                     ap.bssid[0], ap.bssid[1], ap.bssid[2],
                     ap.bssid[3], ap.bssid[4], ap.bssid[5]);
            esp_wifi_sta_get_negotiated_phymode(&phy);
        }

        // ---- IP / gateway / DNS -----------------------------------------
        char ip_str[16] = "-";
        char gw_str[16] = "-";
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) {
            esp_netif_ip_info_t ip;
            if (esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr) {
                snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip.ip));
                snprintf(gw_str, sizeof(gw_str), IPSTR, IP2STR(&ip.gw));
            }
        }

        // ---- Matter fabric count -----------------------------------------
        size_t fabric_count = 0;
        const auto &fabrics = chip::Server::GetInstance().GetFabricTable();
        fabric_count = fabrics.FabricCount();

        // ---- NFC tally ----------------------------------------------------
        char uid_buf[24];
        format_uid(g_nfc.last_uid, g_nfc.last_uid_len, uid_buf, sizeof(uid_buf));
        int64_t since_nfc_s = g_nfc.last_event_us
            ? (int64_t)((up_us - g_nfc.last_event_us) / 1000000)
            : -1;

        // ---- Aliro phase --------------------------------------------------
        int64_t since_aliro_s = g_aliro.last_phase_us
            ? (int64_t)((up_us - g_aliro.last_phase_us) / 1000000)
            : -1;

        // ---- WiFi-since-events --------------------------------------------
        int64_t since_disc_s = g_wifi.last_disconnect_us
            ? (int64_t)((up_us - g_wifi.last_disconnect_us) / 1000000)
            : -1;
        // Ping loss percentage (over the lifetime of pinging)
        uint32_t ping_total = g_wifi.ping_seq;
        uint32_t loss_pct = ping_total ? (g_wifi.ping_loss * 100 / ping_total) : 0;

        // -- Two-line heartbeat: line A = system + WiFi, line B = app state.
        // reset_rsn included so we can see the PREVIOUS boot's crash type
        // over WiFi /api/logs without needing USB serial — the boot
        // snapshot scrolls off the 512-line ring buffer in seconds.
        ESP_LOGI(TAG, "[diag] HB-A | up=%lus heap=%lu/%lu B reset=%s  "
                      "wifi=%s ssid=\"%s\" ch=%u rssi=%d phy=%s bssid=%s "
                      "ip=%s gw=%s",
                 (unsigned long)up_s,
                 (unsigned long)esp_get_free_heap_size(),
                 (unsigned long)esp_get_minimum_free_heap_size(),
                 reset_reason_name(esp_reset_reason()),
                 wifi_ok ? "UP" : "DOWN",
                 ssid, channel, rssi, wifi_phy_name(phy), bssid_buf,
                 ip_str, gw_str);

        ESP_LOGI(TAG, "[diag] HB-B | wifi connects=%lu disc=%lu (last %llds, reason=%u %s)  "
                      "ping last=%lums avg=%lums min=%lums max=%lums loss=%lu%% (n=%lu)  "
                      "fabrics=%u  nfc=%lu/%llds(uid=%s '%s')  aliro=%llds(%s '%s')",
                 (unsigned long)g_wifi.connect_count,
                 (unsigned long)g_wifi.disconnect_count,
                 (long long)since_disc_s,
                 (unsigned)g_wifi.last_disc_reason,
                 wifi_disc_reason_name(g_wifi.last_disc_reason),
                 (unsigned long)g_wifi.ping_last_ms,
                 (unsigned long)g_wifi.ping_avg_ms,
                 (unsigned long)g_wifi.ping_min_ms,
                 (unsigned long)g_wifi.ping_max_ms,
                 (unsigned long)loss_pct, (unsigned long)ping_total,
                 (unsigned)fabric_count,
                 (unsigned long)g_nfc.total_events,
                 (long long)since_nfc_s,
                 uid_buf,
                 g_nfc.last_info,
                 (long long)since_aliro_s,
                 g_aliro.last_phase,
                 g_aliro.last_detail);

        // ---- Per-fabric NodeId snapshot ----------------------------------
        // Cross-checks `_matter._tcp` mDNS instance names. Each instance is
        // `<CompressedFabricId>-<NodeId>` — if NodeId here changes across
        // reboots while the count stays at the same value, FabricTable
        // persistence is broken (NOC not durably stored).
        if (fabric_count > 0) {
            char fab_buf[160];
            int off = 0;
            for (auto it = fabrics.cbegin(); it != fabrics.cend(); ++it) {
                int n = snprintf(fab_buf + off, sizeof(fab_buf) - off,
                                 "%sfi=%u node=0x%016llX vendor=0x%04X",
                                 off ? "; " : "",
                                 it->GetFabricIndex(),
                                 (unsigned long long)it->GetNodeId(),
                                 it->GetVendorId());
                if (n < 0 || (size_t)(off + n) >= sizeof(fab_buf)) break;
                off += n;
            }
            ESP_LOGI(TAG, "[diag] HB-FAB | %s", fab_buf);
        }

        if (g_peer.used) {
            uint32_t peer_total = g_peer.seq;
            uint32_t peer_loss_pct = peer_total ? (g_peer.loss * 100 / peer_total) : 0;
            int64_t  since_peer_s = g_peer.last_ok_us
                ? (esp_timer_get_time() - g_peer.last_ok_us) / 1000000 : -1;
            ESP_LOGI(TAG, "[diag] HB-C | peer %s %u.%u.%u.%u  "
                          "ping last=%lums avg=%lums min=%lums max=%lums loss=%lu%% (n=%lu)  "
                          "last_ok=%llds",
                     g_peer.label,
                     g_peer.ip[0], g_peer.ip[1], g_peer.ip[2], g_peer.ip[3],
                     (unsigned long)g_peer.last_ms,
                     (unsigned long)g_peer.avg_ms,
                     (unsigned long)g_peer.min_ms,
                     (unsigned long)g_peer.max_ms,
                     (unsigned long)peer_loss_pct, (unsigned long)peer_total,
                     (long long)since_peer_s);
        }
    }
}

void start_heartbeat(uint32_t interval_sec)
{
    if (interval_sec == 0) {
        ESP_LOGW(TAG, "[diag] heartbeat disabled");
        return;
    }
    xTaskCreate(ipv4_watchdog_task, "diag_ipv4wd", 3072, NULL, 1, NULL);
    BaseType_t r = xTaskCreate(heartbeat_task, "diag_hb", 4096,
                               (void *)(uintptr_t)interval_sec, 1, NULL);
    if (r != pdPASS) {
        ESP_LOGE(TAG, "[diag] failed to spawn heartbeat task");
    } else {
        ESP_LOGI(TAG, "[diag] heartbeat task running, interval=%lus",
                 (unsigned long)interval_sec);
    }
}

void note_nfc_event(const uint8_t *uid, size_t uid_len, const char *info)
{
    g_nfc.last_event_us = esp_timer_get_time();
    g_nfc.total_events++;
    if (uid && uid_len > 0 && uid_len <= sizeof(g_nfc.last_uid)) {
        memcpy(g_nfc.last_uid, uid, uid_len);
        g_nfc.last_uid_len = uid_len;
    }
    snprintf(g_nfc.last_info, sizeof(g_nfc.last_info), "%s", info ? info : "");

    char uid_buf[24];
    format_uid(g_nfc.last_uid, g_nfc.last_uid_len, uid_buf, sizeof(uid_buf));
    ESP_LOGI(TAG, "[diag] [nfc] tap #%lu uid=%s len=%u info='%s'",
             (unsigned long)g_nfc.total_events, uid_buf,
             (unsigned)g_nfc.last_uid_len, g_nfc.last_info);
}

void note_aliro_phase(const char *phase, const char *detail)
{
    g_aliro.last_phase_us = esp_timer_get_time();
    snprintf(g_aliro.last_phase, sizeof(g_aliro.last_phase), "%s", phase ? phase : "");
    snprintf(g_aliro.last_detail, sizeof(g_aliro.last_detail), "%s", detail ? detail : "");
    ESP_LOGI(TAG, "[diag] [aliro] %s — %s",
             g_aliro.last_phase, g_aliro.last_detail);
}

}  // namespace diag
