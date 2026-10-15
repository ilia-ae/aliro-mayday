// aliro-esp32 — Matter reader skeleton, §7.1 / §7.2 of TODO.md.
//
// Boots esp-matter, brings up DoorLock device-type endpoint with the
// ALIRO feature bit, advertises BLE for commissioning, and verbosely
// logs every Matter event so the pairing flow can be observed live.
//
// §7.3 (PN532 driver + Aliro NFC transaction) is not wired here yet.

#include <esp_log.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_netif.h>
#include <mdns.h>
#include <inttypes.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <app/server/Server.h>
#include <platform/CHIPDeviceConfig.h>
#include <platform/PlatformManager.h>
// NOTE: v1.4.2 relocation — moved from app/server/ to setup_payload/
#include <setup_payload/OnboardingCodesUtil.h>

#include "Diag.h"
#include "AliroDelegate.h"
#include "DoorLockStorageLimits.hpp"
#include "SystemClock.hpp"
#include "NfcManager.hpp"
#if CONFIG_ALIRO_BYTEBUTTON_PANEL
#include "ByteButtonPanel.hpp"
#endif
#if CONFIG_ALIRO_OLED_PANEL
#include "OledPanel.hpp"
#endif
#include "ReaderDataManager.hpp"
#include "DoorLockAccessPolicy.hpp"
#include "eventStructs.hpp"

// admin_panel — vendored from rednblkx with HomeSpan→Matter shim. Provides
// the local admin web UI (HTTP + WebSocket) used by /start_config_ap to
// open a Basic Commissioning Window without going through the existing
// fabric admin (Apple Home / SmartThings) — required for the multi-tenant
// "business controller" deployment model.
#include "ConfigManager.hpp"
#include "WebServerManager.hpp"
#include "WebSocketLogSinker.h"
#include "ConsoleLogSinker.h"
#include "loggable.hpp"
#include "loggable_espidf.hpp"

#include <serialization.hpp>
#include <event_bus.hpp>

#include <memory>
#include <atomic>

// Boot-time hooks defined in DoorLockUserStubs.cpp.
//
//   doorlock_stubs_load_from_nvs() — must be called AFTER nvs_flash_init().
//      Reads users + credentials from NVS namespace `dl_store` into the
//      in-RAM g_users[]/g_creds[][] arrays. Without this the DoorLock
//      cluster would lose every previously-provisioned PIN/RFID/Aliro
//      credential across reboots, even though SetCredential persisted
//      them — symptom: AliroDelegate's readerData_t.issuers[] stays
//      empty and every iPhone tap fails DDK auth.
//
//   replay_aliro_credentials_to_aliro_delegate() — must be called AFTER
//      doorlock_stubs_load_from_nvs() AND AFTER
//      AliroDelegate::setReaderDataSink(). Pushes the in-RAM Aliro
//      credentials into ReaderDataManager so DDK has issuer/endpoint
//      keys to verify a tapping phone's signature.
void doorlock_stubs_load_from_nvs();
void replay_aliro_credentials_to_aliro_delegate();

// Forward decls for LED indicator hooks (defined in LockHandlers.cpp).
namespace lockind {
void init();
void set_commissioning_advertising();
void set_commissioning_active();
void set_commissioning_ok();
void set_paired_no_aliro();
void set_locked();
void set_unlocked();
void set_fabric_lost();
void set_error();
void flash_tap();
void nfc_tap_unlock(chip::EndpointId endpointId);
void nfc_reading_start();
void ddk_auth_start();
void nfc_reading_ok();
void nfc_reading_failed();
}

using namespace esp_matter;
using namespace chip::app::Clusters;

static const char *TAG = "aliro-esp32";

// Holds the constructed-but-not-yet-begin'd WebServerManager. Populated in
// app_main, started on first kInterfaceIpAddressChanged.
static std::unique_ptr<WebServerManager> s_web_server_pending;
static std::atomic<WebServerManager*> s_web_server_for_start{nullptr};
static std::atomic<bool> s_web_server_started{false};

// Friendly name resolved by mDNS (`http://aliro.local/`). DHCP hostname
// is set to the same string so consumer routers can also list us as
// "aliro" in the device list. Both fields are runtime-only — they reset
// on `idf.py erase-flash` but persist normally across reboots once the
// netif/mdns stacks have committed them.
static constexpr const char * kAliroHostname = "aliro";

static void aliro_admin_publish_mdns()
{
    static bool s_mdns_published = false;
    if (s_mdns_published) return;

    // 1) DHCP hostname — shows as "aliro" in router client lists.
    esp_netif_t * sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta != nullptr) {
        esp_netif_set_hostname(sta, kAliroHostname);
    }

    if (mdns_init() != ESP_OK) {
        ESP_LOGW(TAG, "[mdns] mdns_init failed");
        return;
    }
    mdns_instance_name_set("aliro.mayday lock");

    // 2) Delegate hostname: adds a second A-record `aliro.local → ESP-IP`
    //    alongside the MAC-based hostname Matter publishes. Delegates run
    //    in mDNS independently — Matter cannot clobber them because they
    //    are keyed by a different hostname string.
    esp_netif_ip_info_t ip_info = {};
    if (sta && esp_netif_get_ip_info(sta, &ip_info) == ESP_OK
            && ip_info.ip.addr != 0) {
        mdns_ip_addr_t addr = {};
        addr.addr.u_addr.ip4.addr = ip_info.ip.addr;
        addr.addr.type = ESP_IPADDR_TYPE_V4;
        addr.next = nullptr;
        esp_err_t del_err = mdns_delegate_hostname_add(kAliroHostname, &addr);
        if (del_err != ESP_OK) {
            ESP_LOGW(TAG, "[mdns] delegate_hostname_add failed (%s)",
                     esp_err_to_name(del_err));
        } else {
            ESP_LOGI(TAG, "[mdns] aliro.local → " IPSTR " (delegated A-record)",
                     IP2STR(&ip_info.ip));
        }
    }

    // 3) HTTP service on the delegated hostname so browsers can discover us.
    mdns_service_add(kAliroHostname, "_http", "_tcp", 80, NULL, 0);

    ESP_LOGI(TAG, "[mdns] aliro.local published, _http._tcp.local on :80");
    s_mdns_published = true;
}

void aliro_admin_kick_webserver_if_needed();

static void retry_admin_start_later()
{
    // IP-up can arrive before app_main returns its 16 KiB stack to the heap.
    // Retry on the existing timer task instead of leaving a failed HTTP start
    // permanently marked as running or blocking the Matter event loop.
    static esp_timer_handle_t timer = [] {
        esp_timer_handle_t result = nullptr;
        esp_timer_create_args_t args{};
        args.callback = [](void*) { aliro_admin_kick_webserver_if_needed(); };
        args.name = "admin_retry";
        return esp_timer_create(&args, &result) == ESP_OK ? result : nullptr;
    }();
    if (timer) esp_timer_start_once(timer, 1000000);
}

void aliro_admin_kick_webserver_if_needed()
{
    auto * pending = s_web_server_for_start.load();
    if (!pending) return;
    if (s_web_server_started.exchange(true)) return; // coalesce queued starts

    // Dispatch WebServerManager::begin() off the Matter device-layer event
    // loop. begin() runs httpd_start + NVS reads + WebSocket handler
    // registration (~100–500 ms). Running it inline on the event loop
    // caused CASE/subscription packet drops when ATV tried to establish
    // session right after the first DHCP lease (because Matter's event
    // loop was blocked). ScheduleWork() runs on the Matter thread but
    // with the event loop free to process incoming packets between calls.
    const auto scheduled = chip::DeviceLayer::PlatformMgr().ScheduleWork(
        [](intptr_t arg) {
            auto * ws = reinterpret_cast<WebServerManager *>(arg);
            aliro_admin_publish_mdns();
            size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            ESP_LOGI("aliro-esp32", "[admin] starting WebServerManager "
                     "(free internal heap: %u)", (unsigned)free_internal);
            ws->begin();
            if (ws->isRunning()) {
                ESP_LOGI("aliro-esp32", "[admin] WebServerManager up on :80");
            } else {
                s_web_server_started.store(false);
                retry_admin_start_later();
            }
        },
        reinterpret_cast<intptr_t>(pending));
    if (scheduled != CHIP_NO_ERROR) {
        s_web_server_started.store(false);
        retry_admin_start_later();
    }
}

// Pretty names for `attribute::callback_type_t` so logs are readable.
static const char *attr_cb_type_name(attribute::callback_type_t t)
{
    switch (t) {
    case attribute::callback_type_t::PRE_UPDATE:   return "PRE_UPDATE";
    case attribute::callback_type_t::POST_UPDATE:  return "POST_UPDATE";
    case attribute::callback_type_t::READ:         return "READ";
    case attribute::callback_type_t::WRITE:        return "WRITE";
    default:                                       return "?";
    }
}

// Identify cluster cb name.
static const char *id_cb_type_name(identification::callback_type_t t)
{
    switch (t) {
    case identification::callback_type_t::START:  return "START";
    case identification::callback_type_t::STOP:   return "STOP";
    case identification::callback_type_t::EFFECT: return "EFFECT";
    default:                                      return "?";
    }
}

// Map a few well-known cluster IDs to short names so attribute logs are
// easy to read. Anything unknown gets printed as 0x%04X.
static const char *cluster_name(uint32_t id)
{
    switch (id) {
    case 0x0003: return "Identify";
    case 0x0004: return "Groups";
    case 0x001D: return "Descriptor";
    case 0x001F: return "AccessControl";
    case 0x0028: return "BasicInformation";
    case 0x002A: return "OTAProviderRequestor";
    case 0x002B: return "LocalizationConfiguration";
    case 0x0030: return "GeneralCommissioning";
    case 0x0031: return "NetworkCommissioning";
    case 0x003C: return "AdministratorCommissioning";
    case 0x003E: return "OperationalCredentials";
    case 0x0101: return "DoorLock";
    default:     return nullptr;
    }
}

// Decode and log every Matter device-layer event. Comprehensive — comments
// document why each branch is interesting for a door-lock + Aliro flow.
static void app_event_cb(const ChipDeviceEvent *event, intptr_t /*arg*/)
{
    using namespace chip::DeviceLayer;
    switch (event->Type) {
    // BLE commissioning lifecycle ------------------------------------
    case DeviceEventType::kCHIPoBLEConnectionEstablished:
        ESP_LOGI(TAG, "[ev] BLE connection established (commissioner reached us)");
        lockind::set_commissioning_active();
        break;
    case DeviceEventType::kCHIPoBLEConnectionClosed:
        ESP_LOGI(TAG, "[ev] BLE connection closed");
        break;
    case DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "[ev] commissioning session started (PASE handshake begin)");
        lockind::set_commissioning_active();
        break;
    case DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "[ev] commissioning session stopped");
        break;
    case DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "[ev] commissioning window opened");
        lockind::set_commissioning_advertising();
#if CONFIG_ALIRO_BYTEBUTTON_PANEL
        // Default basic-window duration is 180s (kBasicCommissioningWindowTimeout).
        // We don't get the exact timeout from the event, so use the spec
        // default — slightly off only matters for the visual decay.
        aliro::ByteButtonPanel::notifyCommissioningOpened(180);
#endif
        break;
    case DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "[ev] commissioning window closed");
#if CONFIG_ALIRO_BYTEBUTTON_PANEL
        aliro::ByteButtonPanel::notifyCommissioningClosed();
#endif
        break;
    case DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "[ev] commissioning COMPLETE — fabric joined, BLE shutting down");
        lockind::set_commissioning_ok();   // green-blink success cue, then -> Locked
        break;
    case DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGW(TAG, "[ev] fail-safe timer expired (commissioner abandoned)");
        lockind::set_error();
        break;
    // Fabric (the "home" the device belongs to) lifecycle ------------
    case DeviceEventType::kFabricWillBeRemoved:
        ESP_LOGW(TAG, "[ev] fabric WILL be removed (controller asked to forget us)");
        break;
    case DeviceEventType::kFabricRemoved:
        ESP_LOGW(TAG, "[ev] fabric REMOVED (we are no longer paired with that home)");
        lockind::set_fabric_lost();
        break;
    case DeviceEventType::kFabricCommitted:
        ESP_LOGI(TAG, "[ev] fabric committed to persistent storage");
        break;
    case DeviceEventType::kFabricUpdated:
        ESP_LOGI(TAG, "[ev] fabric NOC updated (UpdateNOC pre-commit)");
        break;
    // Network --------------------------------------------------------
    case DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "[ev] interface IP address changed");
        // Trigger admin_panel WebServerManager.begin() on first WiFi IP.
        // Defined in app_main further down; declared extern here.
        extern void aliro_admin_kick_webserver_if_needed();
        aliro_admin_kick_webserver_if_needed();
        break;
    case DeviceEventType::kInternetConnectivityChange:
        ESP_LOGI(TAG, "[ev] internet connectivity changed (ipv4=%d ipv6=%d)",
                 event->InternetConnectivityChange.IPv4,
                 event->InternetConnectivityChange.IPv6);
        break;
    case DeviceEventType::kThreadConnectivityChange:
        ESP_LOGI(TAG, "[ev] Thread connectivity changed");
        break;
    case DeviceEventType::kServiceProvisioningChange:
        ESP_LOGI(TAG, "[ev] service-provisioning state changed");
        break;
    // Default: log as hex so we can spot unhandled events while debugging.
    default:
        ESP_LOGD(TAG, "[ev] type=0x%X", (unsigned)event->Type);
        break;
    }
}

// Cluster attribute-update callback. Called for every attribute the
// app marks as application-managed. Apple Home's Aliro provisioning flow
// will trigger several of these — log them so we see exactly what the
// commissioner writes (e.g. AliroReaderVerificationKey, AliroReaderGroupIdentifier).
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type,
                                         uint16_t endpoint_id,
                                         uint32_t cluster_id,
                                         uint32_t attribute_id,
                                         esp_matter_attr_val_t *val,
                                         void * /*priv_data*/)
{
    const char *cl = cluster_name(cluster_id);
    char cl_buf[16];
    if (!cl) {
        snprintf(cl_buf, sizeof(cl_buf), "0x%04lX", (unsigned long)cluster_id);
        cl = cl_buf;
    }
    ESP_LOGI(TAG, "[attr] %s ep=%u cluster=%s attr=0x%04lX type=%u",
             attr_cb_type_name(type), endpoint_id, cl,
             (unsigned long)attribute_id,
             val ? (unsigned)val->type : 0);

#if CONFIG_ALIRO_BYTEBUTTON_PANEL
    // Light up the panel "matter activity" LED on every attribute write.
    // PRE_UPDATE / POST_UPDATE / WRITE are the interesting ones — READ
    // is too chatty (subscriptions report every interval).
    if (type == attribute::callback_type_t::PRE_UPDATE ||
        type == attribute::callback_type_t::POST_UPDATE ||
        type == attribute::callback_type_t::WRITE) {
        aliro::ByteButtonPanel::notifyMatterActivity();
    }
#endif

    // DoorLock cluster ALIRO writes — flag specifically.
    if (cluster_id == 0x0101) {
        switch (attribute_id) {
        case 0x0080: ESP_LOGI(TAG, "  -> AliroReaderVerificationKey");        break;
        case 0x0081: ESP_LOGI(TAG, "  -> AliroReaderGroupIdentifier");        break;
        case 0x0082: ESP_LOGI(TAG, "  -> AliroReaderGroupSubIdentifier");     break;
        case 0x0083: ESP_LOGI(TAG, "  -> AliroExpeditedTxnSupportedProtoVer"); break;
        case 0x0087: ESP_LOGI(TAG, "  -> NumberOfAliroCredentialIssuerKeysSupported"); break;
        case 0x0088: ESP_LOGI(TAG, "  -> NumberOfAliroEndpointKeysSupported"); break;
        default: break;
        }
    }
    return ESP_OK;
}

// Identify cluster handler.
static esp_err_t app_identification_cb(identification::callback_type_t type,
                                       uint16_t endpoint_id,
                                       uint8_t effect_id,
                                       uint8_t effect_variant,
                                       void * /*priv_data*/)
{
    ESP_LOGI(TAG, "[identify] %s ep=%u effect=%u variant=%u",
             id_cb_type_name(type), endpoint_id, effect_id, effect_variant);
    // TODO: blink the on-board RGB LED so the user can confirm the right
    // device when commissioner asks "which one is this?".
    return ESP_OK;
}

// Forward decl — defined right below app_main.
static void install_nonblocking_usb_serial_jtag();

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#endif

static void install_nonblocking_usb_serial_jtag()
{
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    // Bigger TX buffer than the default 256B — our log volume needs slack.
    cfg.tx_buffer_size = 4096;
    cfg.rx_buffer_size = 1024;
    if (usb_serial_jtag_driver_install(&cfg) == ESP_OK) {
        usb_serial_jtag_vfs_use_driver();
    }
#endif
}

// === Tee logging: USB-Serial-JTAG (when host attached) + in-memory ring ===
// Every ESP_LOG call is funnelled through tee_vprintf:
//   1. The line is formatted into a stack buffer.
//   2. Always pushed into RemoteLog so /api/logs can read it back.
//   3. Forwarded to the original vprintf (USB-JTAG) only when
//      usb_serial_jtag_is_connected() — otherwise we'd block on a full
//      TX FIFO when the lock is on a wall charger.
//
// This replaces the older watchdog-task design: a single hook on the
// hot path is simpler than a 2-second poll task swapping vprintfs, and
// it gives us remote logs as a free side effect.
#include "RemoteLog.hpp"

static vprintf_like_t s_default_vprintf = nullptr;

static int tee_vprintf(const char * fmt, va_list args)
{
    char line[256];
    va_list args_copy;
    va_copy(args_copy, args);
    int n = vsnprintf(line, sizeof(line), fmt, args_copy);
    va_end(args_copy);
    if (n < 0) n = 0;
    if (n > (int)sizeof(line) - 1) n = sizeof(line) - 1;

    aliro::RemoteLog::instance().push(line, (size_t)n);

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    if (s_default_vprintf && usb_serial_jtag_is_connected()) {
        return s_default_vprintf(fmt, args);
    }
    return n;
#else
    if (s_default_vprintf) return s_default_vprintf(fmt, args);
    return n;
#endif
}

static void install_tee_vprintf()
{
    // esp_log_set_vprintf returns the previously-installed vprintf, which
    // is the IDF default that writes to the chosen console. Capture it so
    // tee_vprintf can forward to it conditionally.
    s_default_vprintf = esp_log_set_vprintf(tee_vprintf);
}

extern "C" void app_main()
{
    // First thing: install the non-blocking USB-Serial-JTAG driver. Default
    // ESP-IDF VFS for USB-Serial-JTAG blocks every ESP_LOGI for up to ~50ms
    // when the host isn't draining the TX FIFO (e.g. plugged into a wall
    // charger, not a computer). With our PN532 verbose + diag heartbeats
    // that produced multi-second NFC tap stalls. The driver-based VFS uses
    // a real FIFO + drops on overflow instead of stalling.
    install_nonblocking_usb_serial_jtag();

    // Hook ESP_LOG → RemoteLog ring buffer (always) + USB-Serial-JTAG
    // (only when a host is actually draining it). This is what makes
    // `GET /api/logs?since=N` work and what kills the wall-charger TX
    // FIFO stall in one shot.
    install_tee_vprintf();

    // Bump default log levels so commissioning + cluster activity is visible.
    // Without this the chip stack runs at WARN/INFO mix; we want the full
    // commissioner conversation in logs while we develop §7.2.
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("chip[DL]", ESP_LOG_INFO);
    esp_log_level_set("chip[DIS]", ESP_LOG_INFO);
    esp_log_level_set("chip[SVR]", ESP_LOG_INFO);
    // Maximum verbosity on the IM, ZCL, DMG and command-dispatch tags so
    // *every* incoming Apple Home command/attribute read/write/event is
    // visible in the serial log. Apple's "not responding" UI almost
    // always traces back to either a command never reaching us (network
    // / mDNS) or one that we mishandle — and only the full chip[DMG] /
    // chip[IM] / chip[ZCL] / esp_matter_command flow lets us tell which.
    // Matter cluster/IM/EM debug logs were on DEBUG — that's per-attribute-
    // read spam during Apple Home subscription updates and per-message dump
    // during exchange manager work. INFO keeps important state changes,
    // drops the per-byte cluster trace.
    esp_log_level_set("chip[ZCL]", ESP_LOG_INFO);
    esp_log_level_set("chip[DMG]", ESP_LOG_INFO);
    esp_log_level_set("chip[IM]",  ESP_LOG_INFO);
    esp_log_level_set("chip[EM]",  ESP_LOG_INFO);
    esp_log_level_set("esp_matter_command", ESP_LOG_INFO);
    // Apple Home polls LockState every ~210 ms — at INFO this floods both
    // the USB-Serial-JTAG console and the RemoteLog ring buffer (16 KB
    // wraps in ~17 s, evicting actually-useful boot/event logs). Drop to
    // WARN; we still see attribute writes and any cluster-level errors.
    esp_log_level_set("esp_matter_attribute", ESP_LOG_WARN);
    // Our own modules — keep DEBUG for now so we still see [init], [ev],
    // [nfc] traces while developing.
    esp_log_level_set("aliro-esp32", ESP_LOG_DEBUG);
    esp_log_level_set("aliro",      ESP_LOG_DEBUG);
    esp_log_level_set("doorlock-stubs", ESP_LOG_DEBUG);
    esp_log_level_set("lock-hdl",   ESP_LOG_DEBUG);
    // (esp_matter_attribute is set to WARN above on line 482 — there used
    //  to be a duplicate INFO override here that silently undid it.)
    // PN532 driver chain — pn532_hal logs SPI errors, pn532_cxx logs the
    // wire-level NACK/ACK and frame transceive. Without DEBUG-level here
    // a non-responding PN532 (wrong wiring, no power, SAM-vs-SPI mode
    // switches in wrong position on V3 clones) just shows up as
    // "no taps ever" with no diagnostic.
    // PN532 chatter: was VERBOSE → produces 50+ printf/sec (every byte
    // logged), choking the USB-Serial-JTAG TX FIFO and burning CPU on
    // log formatting. Production-level WARN is plenty for fault detection.
    // To re-enable for debugging a wiring fault, change these back to VERBOSE.
    esp_log_level_set("PN532::SPI",          ESP_LOG_WARN);
    esp_log_level_set("PN532::Frontend",     ESP_LOG_WARN);
    esp_log_level_set("PN532::Transport",    ESP_LOG_WARN);
    esp_log_level_set("NfcManager",          ESP_LOG_INFO);
    // DDK kAliro auth path — FastAuth + StandardAuth + crypto. Without
    // VERBOSE here, an iPhone tap that selects the Aliro applet but fails
    // the cert chain / signature verification just logs "HomeKey
    // authentication failed" with no diagnostic. We need every TLV +
    // ECDH step in the trace to identify whether it's:
    //   (a) wrong AID (we send HomeKey AID, applet rejects)
    //   (b) wrong ECP TCI (we wake HomeKey applet, Aliro flow mismatches)
    //   (c) issuer key derivation off (cert verify fails)
    //   (d) endpoint cert format different in Aliro v1.0
    // DDK auth chain — was VERBOSE (every TLV / ECDH / HKDF step). Each
    // tap fired ~30-50 ESP_LOGV calls inside the auth roundtrip, costing
    // 3-10ms of pure printf time on the FastAuth path. INFO keeps the
    // pass/fail summary, drops the byte-by-byte protocol trace.
    // To re-enable for cert/auth debugging, set these back to VERBOSE.
    // DDK + aliro chain was at INFO/VERBOSE. tee_vprintf in app_main now
    // pushes every ESP_LOG line through vsnprintf + RemoteLog mutex +
    // memcpy + (when USB host present) the original vprintf — adding ~200
    // µs per call. DDK auth fires 30+ INFO logs, doubling tap latency
    // (226 → 423 ms warm-cache observed). Drop to WARN to put auth back
    // in the 200 ms range; re-enable INFO/VERBOSE per-tag during cert
    // / pairing debug sessions.
    esp_log_level_set("HK_HomeKit",          ESP_LOG_WARN);
    esp_log_level_set("DDKAuthContext",      ESP_LOG_WARN);
    esp_log_level_set("FastAuth",            ESP_LOG_WARN);
    esp_log_level_set("StandardAuth",        ESP_LOG_WARN);
    esp_log_level_set("DigitalKeySecure",    ESP_LOG_WARN);
    esp_log_level_set("doorlock-stubs",      ESP_LOG_INFO);
    esp_log_level_set("aliro",               ESP_LOG_INFO);
    esp_log_level_set("NfcManager",          ESP_LOG_WARN);

    // Hold for ~2.5s so the USB-CDC re-enumeration after EN-reset finishes
    // before we print init-time diagnostics. Without this the host pyserial
    // monitor reconnects ~600-1000ms in and we lose [init] lines.
    vTaskDelay(pdMS_TO_TICKS(2500));

    ESP_LOGI(TAG, "============================================================");
    ESP_LOGI(TAG, "aliro-esp32 booting (verbose logging enabled)");
    ESP_LOGI(TAG, "============================================================");

    // WS2812 LED state machine — show "boot" (dim blue) until commissioning
    // window opens or we restore Locked/Unlocked from a paired fabric.
    lockind::init();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase, doing it now");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, "[init] NVS ready");

    // EventBus mutex/queue/task come up here. NfcManager's constructor
    // calls register_topic() which takes the mutex — without init() first
    // the firmware aborts with `assert xQueueTakeMutexRecursive: pxMutex`.
    if (auto eb_err = EventBus::Bus::instance().init(); eb_err != ESP_OK) {
        ESP_LOGE(TAG, "EventBus init failed: 0x%x — NFC tap unavailable", eb_err);
    } else {
        ESP_LOGI(TAG, "[init] EventBus ready");
    }

    // Comprehensive boot snapshot — chip info, memory, fabric pre-state.
    diag::boot_snapshot();
    // Install BEFORE matter::start so we don't miss the first WIFI/IP events.
    diag::install_wifi_event_logger();
    // Heartbeat every 30s — uptime, heap, WiFi, fabric count, NFC tally.
    diag::start_heartbeat(30);
    // Ping the gateway every 5s to quantify loss / latency from the device's
    // own perspective — answers "is it our WiFi or someone else's network?".
    diag::start_gateway_ping(5);
    // Continuous L3 reachability check to the controller iPhone. Apple
    // Home "no connection" / "device not responding" UI usually surfaces
    // when iOS cannot reach the accessory over Matter. A unicast ping
    // here separates "the L3 path itself is broken" (ping fails) from
    // "L3 OK but Matter mDNS / CASE handshake doesn't make it" (ping
    // succeeds but iOS still complains). Heartbeat HB-C reports rolling
    // RTT + loss to that IP.
    // Peer ping disabled — hardcoded IP was stale (old subnet).
    // Re-enable when iPhone IP is reliably discoverable via mDNS.
    // diag::start_peer_ping(192, 168, 55, 153, "iPhone", 5);

    node::config_t node_config;
    node_t *node = node::create(&node_config,
                                app_attribute_update_cb,
                                app_identification_cb,
                                /*priv_data=*/nullptr);
    if (!node) {
        ESP_LOGE(TAG, "failed to create Matter node");
        return;
    }
    ESP_LOGI(TAG, "[init] root node created");

    endpoint::door_lock::config_t door_lock_config;
    // Wire AliroDelegate BEFORE the endpoint is registered. esp_matter's
    // DoorLockDelegateInitCB (esp_matter_delegate_callbacks.cpp) only fires
    // when config.delegate is non-null at endpoint-create time. Without
    // this, every Aliro attribute read returns Status::Failure to Apple
    // Home, surfaced as "Specified action or command indicated is not
    // supported" in the Set Up Home Key dialog.
    // delegate lives on the inner cluster config (esp_matter_cluster.h:door_lock).
    door_lock_config.door_lock.delegate = &aliro::instance();
    // Default config_t leaves LockState=0 (NotFullyLocked) and
    // actuator_enabled=false. Apple Home reads these immediately after
    // commissioning and treats actuator_enabled=false as "this Matter
    // lock cannot actuate" — the UI then reports "not responding" even
    // though our Aliro flow succeeded. Surface as a working dead-bolt
    // that starts in Locked state so the lock tile in Apple Home is
    // functional from the moment we are added.
    door_lock_config.door_lock.lock_state       = uint8_t{1}; // 1 = Locked
    door_lock_config.door_lock.lock_type        = 0;       // 0 = DeadBolt
    door_lock_config.door_lock.actuator_enabled = true;
    endpoint_t *door_lock_ep =
        endpoint::door_lock::create(node, &door_lock_config,
                                    ENDPOINT_FLAG_NONE, /*priv_data=*/nullptr);
    if (!door_lock_ep) {
        ESP_LOGE(TAG, "failed to create DoorLock endpoint");
        return;
    }
    ESP_LOGI(TAG, "[init] DoorLock endpoint created, id=%u",
             endpoint::get_id(door_lock_ep));

    cluster_t *door_lock_cluster =
        cluster::get(door_lock_ep, chip::app::Clusters::DoorLock::Id);
    if (!door_lock_cluster) {
        ESP_LOGE(TAG, "failed to get DoorLock cluster");
        return;
    }
    // Matter 1.4 DoorLock conformance: when ALIRO is present, USR is
    // mandatory but PIN/RFID are only OPTIONAL (per cluster XML
    // `otherwiseConform { mandatory ALIRO ; optional [PIN|RFID|FGP|FACE] }`).
    // We are an NFC-only Aliro reader: advertising PIN makes Apple Home
    // iOS 26 show a "Setting up Access Codes" screen that hangs — Apple
    // expects keypad-style PIN provisioning that doesn't apply to us.
    // Final FeatureMap = USR | ALIRO_PROVISIONING (0x2100).
    //
    // esp-matter's `feature::user::add()` refuses unless PIN or RFID is
    // also set, so we register the USR feature bit and its mandatory
    // attributes/commands/events directly with the public APIs.
    {
        // 1) Set USR bit on the FeatureMap. door_lock::create() pre-created
        //    FeatureMap with value 0; aliro_provisioning::add() below ORs
        //    in 0x2000. Pre-OR USR bit (0x100) here.
        attribute_t *fm = attribute::get(door_lock_cluster,
            chip::app::Clusters::Globals::Attributes::FeatureMap::Id);
        if (!fm) {
            ESP_LOGE(TAG, "FeatureMap attribute missing on DoorLock cluster");
            return;
        }
        esp_matter_attr_val_t v = esp_matter_invalid(nullptr);
        attribute::get_val(fm, &v);
        v.val.u32 |= static_cast<uint32_t>(chip::app::Clusters::DoorLock::Feature::kUser);
        if (attribute::set_val(fm, &v) != ESP_OK) {
            ESP_LOGE(TAG, "failed to OR USR bit into FeatureMap");
            return;
        }

        // 2) USR-mandatory attributes (in door_lock namespace).
        namespace dl_attr = cluster::door_lock::attribute;
        namespace dl_cmd  = cluster::door_lock::command;
        namespace dl_evt  = cluster::door_lock::event;
        dl_attr::create_number_of_total_users_supported(door_lock_cluster, aliro::storage::users);
        dl_attr::create_credential_rules_support(door_lock_cluster, 0x01); // Single
        dl_attr::create_number_of_credentials_supported_per_user(door_lock_cluster, aliro::storage::credentials_per_user);
        dl_attr::create_expiring_user_timeout(door_lock_cluster, 0);

        // 3) USR-mandatory commands.
        dl_cmd::create_set_user(door_lock_cluster);
        dl_cmd::create_get_user(door_lock_cluster);
        dl_cmd::create_get_user_response(door_lock_cluster);
        dl_cmd::create_clear_user(door_lock_cluster);
        dl_cmd::create_set_credential(door_lock_cluster);
        dl_cmd::create_set_credential_response(door_lock_cluster);
        dl_cmd::create_get_credential_status(door_lock_cluster);
        dl_cmd::create_get_credential_status_response(door_lock_cluster);
        dl_cmd::create_clear_credential(door_lock_cluster);

        // 4) USR-mandatory event.
        dl_evt::create_lock_user_change(door_lock_cluster);
    }
    ESP_LOGI(TAG, "[init] DoorLock cluster: USR feature enabled (no PIN — Aliro-only reader)");

    if (cluster::door_lock::feature::aliro_provisioning::add(door_lock_cluster) != ESP_OK) {
        ESP_LOGE(TAG, "failed to add Aliro Provisioning feature");
        return;
    }
    ESP_LOGI(TAG, "[init] DoorLock cluster: AliroProvisioning feature enabled");

    // Sanity dump: enumerate every command registered on DoorLock cluster
    // before esp_matter::start() seals the dynamic endpoint into ember.
    // If 0x001B (SetAliroReaderConfig) and 0x001C (ClearAliroReaderConfig)
    // are not in this list, the iOS commissioner gets UNSUPPORTED_COMMAND
    // and shows "Unable to Add Accessory: Specified action or command
    // indicated is not supported on the device."
    {
        size_t accepted = 0, generated = 0;
        for (command_t *c = command::get_first(door_lock_cluster); c; c = command::get_next(c)) {
            uint16_t f = command::get_flags(c);
            uint32_t id = command::get_id(c);
            const char *kind = (f & COMMAND_FLAG_ACCEPTED) ? "ACC"
                              : (f & COMMAND_FLAG_GENERATED) ? "GEN"
                              : "?";
            ESP_LOGI(TAG, "[init] DoorLock cmd id=0x%04" PRIx32 " flags=0x%x (%s)", id, f, kind);
            if (f & COMMAND_FLAG_ACCEPTED) accepted++;
            if (f & COMMAND_FLAG_GENERATED) generated++;
        }
        ESP_LOGI(TAG, "[init] DoorLock cluster has %u accepted / %u generated commands",
                 (unsigned)accepted, (unsigned)generated);
    }

    // ------------------------------------------------------------------
    // Aliro / DoorLock NVS state — load BEFORE esp_matter::start().
    // ------------------------------------------------------------------
    //
    // After esp_matter::start() the DoorLock cluster begins answering
    // attribute reads from any Apple Home / commissioner connected over
    // Matter. If we load credentials *after* start there is a race window
    // where reads return empty credentials, which iOS may interpret as
    // "credentials revoked" and either re-run SetCredential or — worse —
    // surface "Not Responding" before subscription even establishes.
    //
    // All three calls below are pure in-memory work (NVS read + sink
    // pointer assignment), no Matter dependency, safe to run pre-start.
    static ReaderDataManager s_reader_data_manager;
    if (!s_reader_data_manager.begin()) {
        ESP_LOGW(TAG, "ReaderDataManager::begin failed (NVS issue?) — "
                      "NFC tap will be disabled until next reboot");
    }
    aliro::instance().setReaderDataSink(&s_reader_data_manager);
    // DoorLock cluster's persisted users + credentials → in-RAM g_creds[][].
    doorlock_stubs_load_from_nvs();
    // Mirror DoorLock-cluster credentials into AliroDelegate's readerData_t.
    // Without this, every reboot loses DDK's view of issuers + endpoints
    // and the next iPhone tap fails auth until Apple Home re-runs SetCredential.
    replay_aliro_credentials_to_aliro_delegate();

    ESP_LOGI(TAG, "[init] internal heap before Matter: free=%u largest=%u",
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    err = esp_matter::start(app_event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_matter::start failed: 0x%x", err);
        lockind::set_error();
        return;
    }
    ESP_LOGI(TAG, "[init] esp-matter started");
    if (!WebServerManager::reserveWebSocketStack())
        ESP_LOGW(TAG, "[admin] sender stack reservation failed; startup will retry dynamically");
    aliro::startSystemClock();

    // BasicInformation::ProductName / VendorName overridden via compile-
    // time defines (see main/CMakeLists.txt: CHIP_DEVICE_CONFIG_DEVICE_*).
    // Apple Home reads the attribute during commissioning and pre-fills
    // it as the suggested accessory name in the "Pair Accessory" sheet.
    ESP_LOGI(TAG, "[init] ProductName='%s' VendorName='%s' (compile-time)",
             CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME,
             CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME);

    // Set initial LED state based on whether we're already paired AND
    // whether the Aliro reader keys are provisioned in NVS:
    //   no fabric                 → kBoot/kCommissioningAdv (driven by events)
    //   paired, no Aliro keys yet → kPairedNoAliro (violet)
    //   paired, keys provisioned  → kLocked (orange)
    //
    // Per-fabric NodeId is also logged here so a "device not responding"
    // session can be cross-checked against the mDNS instance name
    // (`<CompressedFabricId>-<NodeId>` in `_matter._tcp` advertisements):
    // a NodeId that changes across reboots while CompressedFabricId stays
    // stable indicates broken FabricTable persistence.
    {
        auto & ft = chip::Server::GetInstance().GetFabricTable();
        size_t fabric_count = ft.FabricCount();
        bool aliro_ok = aliro::instance().isProvisioned();
        ESP_LOGI(TAG, "[init] fabric count at boot: %u, aliro provisioned: %d",
                 (unsigned)fabric_count, aliro_ok ? 1 : 0);
        for (auto it = ft.cbegin(); it != ft.cend(); ++it) {
            ESP_LOGI(TAG, "[init]   fabric idx=%u: NodeId=0x%016llX FabricId=0x%016llX VendorId=0x%04X",
                     it->GetFabricIndex(),
                     (unsigned long long)it->GetNodeId(),
                     (unsigned long long)it->GetFabricId(),
                     it->GetVendorId());
        }
        if (fabric_count > 0) {
            if (aliro_ok) {
                lockind::set_locked();
            } else {
                lockind::set_paired_no_aliro();
            }
        }
    }

    PrintOnboardingCodes(chip::RendezvousInformationFlags(
        chip::RendezvousInformationFlag::kBLE));

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::wifi_register_commands();
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::init();
#endif

    // ------------------------------------------------------------------
    // PN532 + DDK Aliro NFC stack
    // ------------------------------------------------------------------
    //
    // ReaderDataManager + AliroDelegate are already wired (above, pre-
    // start). NfcManager comes up here because PN532 SPI init can take
    // 100–300 ms and we don't want to block esp_matter::start() on it.
    //
    // GPIO pin order is the same `[CS, SCK, MISO, MOSI]` array layout
    // upstream rednblkx uses; values are the ESP32-S3 SPI2 IO_MUX
    // defaults (matches the "Default" preset in
    // homekey-esp32/main/include/config.hpp).
    constexpr std::array<uint8_t, 4> kNfcGpioPins = {
        10,  // CS   — GPIO10
        12,  // SCK  — GPIO12
        13,  // MISO — GPIO13
        11,  // MOSI — GPIO11
    };
    constexpr bool kAuthPrecomputeEnabled = true;
    // Gate NfcManager polling until the OLED boot splash has latched
    // (all 5 phases ready, or the 30 s force-ready timeout). Without
    // this, tapping a phone mid-boot triggers DDK auth before Matter
    // has loaded its fabrics and the unlock command can't be dispatched
    // — confusing UX (OLED shows "boot" but the lock partially
    // reacts).
    NfcManager::setPaused(true);
    static auto s_nfc_manager = std::make_unique<NfcManager>(
        s_reader_data_manager, kNfcGpioPins, kAuthPrecomputeEnabled);
    if (!s_nfc_manager->begin()) {
        ESP_LOGE(TAG, "NfcManager::begin failed — NFC tap unavailable");
    } else {
        // Refresh ECP polling data with the current reader GID — required
        // before the first tap so Apple Wallet's ECP wakeup recognizes us.
        s_nfc_manager->updateEcpData();
    }

#if CONFIG_ALIRO_OLED_PANEL
    // Spawn a one-shot "boot gate" task: waits for OLED to signal all
    // 5 phases ready (or 30 s force-ready), then un-pauses NfcManager.
    xTaskCreate([](void*){
        while (!aliro::OledPanel::isAllReady()) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        NfcManager::setPaused(false);
        ESP_LOGI("aliro-esp32",
                 "[boot-gate] all phases ready — NFC polling resumed");
        vTaskDelete(NULL);
    }, "boot_gate", 3072, nullptr, 1, nullptr);   // ESP_LOGI via tee_vprintf needs >2K
#else
    // No OLED → no phase tracking, immediately un-pause.
    NfcManager::setPaused(false);
#endif
    // Wire the AliroDelegate ↔ NfcManager link last, so that subsequent
    // SetAliroReaderConfig writes from Apple Home / Samsung Wallet auto-
    // refresh ECP polling data and invalidate the precomputed auth pool.
    aliro::instance().setNfcManager(s_nfc_manager.get());

    // Subscribe to NFC_BUS_TOPIC so that whenever NfcManager publishes
    // a HOMEKEY_TAP / TAG_TAP event we forward it to the DoorLock cluster
    // as an Aliro-source unlock. The same callback fires for both Apple
    // and Samsung wallet taps — DDK FastAuth/StandardAuth handles the
    // crypto difference internally.
    auto & event_bus = EventBus::Bus::instance();
    auto nfc_topic   = event_bus.get_topic(NFC_BUS_TOPIC);
    if (nfc_topic.has_value()) {
        event_bus.subscribe(nfc_topic.value(),
            [](const EventBus::Event & ev, void * /*context*/) {
                if (ev.payload_size == 0 || ev.payload == nullptr) return;
                std::span<const uint8_t> payload(
                    static_cast<const uint8_t *>(ev.payload), ev.payload_size);
                std::error_code ec;
                NfcEvent nfc = alpaca::deserialize<NfcEvent>(payload, ec);
                if (ec) {
                    ESP_LOGE(TAG, "[nfc] failed to deserialize NfcEvent: %s",
                             ec.message().c_str());
                    return;
                }
                // NfcManager publishes HOMEKEY_TAP for BOTH success and failure
                // of the DDK auth round; status=true/false lives inside
                // EventHKTap.data (alpaca-encoded). We must check it — otherwise
                // every Flipper / random ISO 14443A card emulation would unlock
                // the door (security hole observed during bring-up testing).
                if (nfc.type != HOMEKEY_TAP) {
                    // TAG_TAP / FORCE_AUTH_FLOW / UPDATE_ECP — generic 14443A
                    // detect (Flipper, MIFARE Classic, EM410x, …) without
                    // an Apple HomeKey applet, so SELECT-AID failed before
                    // DDK auth even ran. Flip LED to solid-red-1s so the
                    // user can tell "PN532 saw you, but you're not a key."
                    ESP_LOGI(TAG, "[nfc] event type=%u — not a HomeKey/Aliro tap, "
                                  "showing red reject indicator",
                             (unsigned)nfc.type);
                    lockind::nfc_reading_failed();
                    return;
                }

                std::span<const uint8_t> tap_payload(nfc.data.data(), nfc.data.size());
                EventHKTap tap = alpaca::deserialize<EventHKTap>(tap_payload, ec);
                if (ec) {
                    ESP_LOGE(TAG, "[nfc] failed to deserialize EventHKTap: %s",
                             ec.message().c_str());
                    return;
                }
                if (!tap.status) {
                    ESP_LOGW(TAG, "[nfc] HomeKey/Aliro auth FAILED — not unlocking "
                                  "(unrecognised card, wrong issuer key, or replay)");
                    lockind::nfc_reading_failed();
                    return;
                }

                ESP_LOGI(TAG, "[nfc] HOMEKEY_TAP authenticated "
                              "(issuer=%u B, endpoint=%u B, reader=%u B) "
                              "— scheduling Aliro Unlock on DoorLock ep=1",
                         (unsigned)tap.issuerId.size(),
                         (unsigned)tap.endpointId.size(),
                         (unsigned)tap.readerId.size());

                // CRITICAL: SetLockState / DoorLockServer must run on the chip
                // task (which holds the chip stack lock). Calling it from this
                // event-bus subscriber callback (we're on the NFC polling task)
                // trips `Chip stack locking error → chipDie → abort()` in
                // attribute-storage.cpp. ScheduleWork puts the lambda on the
                // chip event loop, where the lock is acquired automatically.
                auto* pending = new (std::nothrow) EventHKTap(std::move(tap));
                if (!pending) {
                    ESP_LOGE(TAG, "[nfc] unlock rejected: work allocation failed");
                    lockind::nfc_reading_failed();
                    return;
                }
                const auto scheduled = chip::DeviceLayer::PlatformMgr().ScheduleWork(
                    [](intptr_t arg) {
                        std::unique_ptr<EventHKTap> accepted(reinterpret_cast<EventHKTap*>(arg));
                        const bool provisioned=aliro::instance().isProvisioned();
                        const bool current=provisioned && s_reader_data_manager.authorizationCurrent(
                            accepted->issuerId,accepted->endpointId,accepted->credentialRevision);
                        const bool userAllowed=current && aliro_user_access_allowed(
                            s_reader_data_manager.getReaderDataCopy(),accepted->issuerId,accepted->endpointId);
                        if (!userAllowed) {
                            ESP_LOGW(TAG, "[nfc] unlock rejected: reader=%d current-credential=%d user-access=%d",
                                     provisioned,current,userAllowed);
                            lockind::nfc_reading_failed();
                            return;
                        }
                        lockind::nfc_reading_ok();
                        lockind::nfc_tap_unlock(/*endpointId=*/1);
                    }, reinterpret_cast<intptr_t>(pending));
                if (scheduled != CHIP_NO_ERROR) {
                    ESP_LOGE(TAG, "[nfc] unlock rejected: Matter work scheduling failed");
                    delete pending;
                    lockind::nfc_reading_failed();
                }
            });
        ESP_LOGI(TAG, "[init] NFC_BUS_TOPIC subscribed — auth-OK taps fire DoorLock Unlock");
    } else {
        ESP_LOGE(TAG, "[init] NFC_BUS_TOPIC not registered — taps will NOT unlock");
    }

    // ------------------------------------------------------------------
    // Admin web panel (vendored from rednblkx)
    // ------------------------------------------------------------------
    {
        // ConfigManager loads NVS-backed config (msgpack) — fine to do
        // synchronously; uses ~3 KB of stack and ~1 KB of heap.
        static auto s_config_manager = std::make_unique<ConfigManager>();
        if (!s_config_manager->begin()) {
            ESP_LOGE(TAG, "[admin] ConfigManager::begin failed — using defaults");
        }
        // WebServerManager.begin() needs ~12 KB of internal-RAM heap (http
        // server task, esp_littlefs lazy-mutex, ws_send_task stack). At
        // app_main time, Matter just allocated WiFi/BLE buffers and free
        // internal heap is <1 KB, even with SPIRAM_MALLOC_RESERVE_INTERNAL
        // bumped. We defer the begin() call until the first WiFi IP-up
        // event (kInterfaceIpAddressChanged) — by then the transient peak
        // is past and ~30+ KB of internal RAM is free.
        s_web_server_pending = std::make_unique<WebServerManager>(
            *s_config_manager, s_reader_data_manager);
        s_web_server_for_start.store(s_web_server_pending.get());
        ESP_LOGI(TAG, "[init] admin_panel constructed (begin() deferred to "
                      "kInterfaceIpAddressChanged when heap settles)");
    }

#if CONFIG_ALIRO_BYTEBUTTON_PANEL
    // External M5Stack Unit ByteButton on I2C (SDA=GPIO8, SCL=GPIO9, addr 0x47).
    // Init is non-fatal — if the unit is absent or the I2C transmit NACKs,
    // the lock keeps running without the panel.
    if (aliro::ByteButtonPanel::instance().init() == ESP_OK) {
        ESP_LOGI(TAG, "[init] ByteButton panel attached");
    } else {
        ESP_LOGW(TAG, "[init] ByteButton panel not detected — continuing without it");
    }
#endif
#if CONFIG_ALIRO_OLED_PANEL
    // External M5Stack Unit OLED 1.3" SH1107 — addr 0x3C on the same
    // PORT.A I2C bus (shared with ByteButton via I2cBus). Status display.
    if (aliro::OledPanel::instance().init() == ESP_OK) {
        ESP_LOGI(TAG, "[init] OLED panel attached");
    } else {
        ESP_LOGW(TAG, "[init] OLED panel not detected — continuing without it");
    }
#endif

    ESP_LOGI(TAG, "============================================================");
    ESP_LOGI(TAG, "READY — BLE advertising for commissioning");
    ESP_LOGI(TAG, "============================================================");

    // DHCP may finish before the admin object exists; that IP-up event was
    // intentionally ignored. Catch up once construction is complete. The
    // atomic scheduling guard coalesces this with a concurrent IP-up event.
    auto * sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info{};
    if (sta && esp_netif_get_ip_info(sta, &ip_info) == ESP_OK && ip_info.ip.addr) {
        aliro_admin_kick_webserver_if_needed();
    }
}
