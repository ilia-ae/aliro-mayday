// LockDoor / UnlockDoor command handlers + WS2812 LED state machine.
//
// Apple Home subscribes to the LockOperation event after pairing. When
// the user taps "Open" / "Close" in Home, iOS sends LockDoor (cmd 0x00) /
// UnlockDoor (cmd 0x01) and *waits for a LockOperation event* with the
// new state to dismiss its in-progress UI. Just updating LockState via
// `attribute::update()` is NOT enough — Home shows "not responding"
// until it gives up.
//
// `DoorLockServer::SetLockState(ep, state, OperationSourceEnum::kRemote,
// userIdx, creds, fabricIdx, nodeId)` updates the attribute *and* fires
// LockOperation. We forward the fabric/node from the callback.
//
// LED indicator (ESP32-S3 DevKitC built-in WS2812 RGB on GPIO 48):
//   Boot                 — dim blue
//   Commissioning adv    — blue, slow blink 1 Hz   (BLE advertising)
//   Commissioning active — blue, fast blink ~5 Hz  (PASE/CASE in flight)
//   Commissioning OK     — three quick green blinks then settle
//   PairedNoAliro        — solid violet            (paired, but reader keys not provisioned yet)
//   Locked   (with keys) — solid orange
//   Unlocked             — solid green
//   NFC reading          — RED, fast blink ~5 Hz   (PN532 sees ANY 14443A tag, SELECT-AID running)
//   DDK auth in progress — CYAN, fast blink ~5 Hz  (HomeKey applet selected → DDK FastAuth/StandardAuth)
//   NFC tap accepted     — short white flash → green (handled by nfc_tap_unlock)
//   NFC tap rejected     — solid red 1 s, then back to prior state
//   Fabric lost          — yellow, slow blink
//   Error                — red, fast blink

#include <app/clusters/door-lock-server/door-lock-server.h>
#include <app/reporting/reporting.h>
#include <clusters/DoorLock/AttributeIds.h>
#include <clusters/DoorLock/ClusterId.h>
#include <esp_matter_attribute_utils.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_err.h>
#include <esp_log.h>
#include <led_strip.h>

#if CONFIG_ALIRO_BYTEBUTTON_PANEL
#include "ByteButtonPanel.hpp"
#endif
#if CONFIG_ALIRO_OLED_PANEL
#include "OledPanel.hpp"
#endif
#include <atomic>
#include "AccessIntegrations.hpp"

static const char *TAG = "lock-hdl";

namespace {

constexpr int      kLedGpio     = 48;
constexpr uint32_t kLedRmtResHz = 10 * 1000 * 1000;
constexpr uint32_t kTickMs      = 100;

led_strip_handle_t g_led = nullptr;

enum class State : uint8_t {
    kBoot,
    kCommissioningAdv,
    kCommissioningActive,
    kCommissioningOk,
    kPairedNoAliro,
    kLocked,
    kUnlocked,
    kNfcReading,        // red fast blink  — PN532 sees ANY tag, SELECT-AID phase
    kDDKAuth,           // cyan fast blink — HomeKey applet selected, running DDK auth
    kNfcRejected,       // solid red 1 s   — auth failed, will revert to prior
    kFabricLost,
    kError,
};

std::atomic<State> g_state               {State::kBoot};
std::atomic<State> g_state_before_tap    {State::kLocked};
std::atomic<State> g_state_before_nfc    {State::kLocked};
std::atomic<int>   g_tap_ticks           {0};
std::atomic<int>   g_ok_ticks            {0};
std::atomic<int>   g_nfc_reject_ticks    {0};

// User-requested LED brightness scale (0..100). 30 = ~30 % to spare the
// eyes when the lock sits on a desk during development.
constexpr uint8_t kBrightnessPct = 30;

inline uint8_t scale(uint8_t v)
{
    return static_cast<uint8_t>((static_cast<uint16_t>(v) * kBrightnessPct) / 100);
}

inline void put(uint8_t r, uint8_t g, uint8_t b)
{
    if (!g_led) return;
    led_strip_set_pixel(g_led, 0, scale(r), scale(g), scale(b));
    led_strip_refresh(g_led);
}

void render_tick(uint32_t tick)
{
    State s = g_state.load();

    if (g_tap_ticks.load() > 0) {
        put(80, 80, 80);                                     // bright white
        if (g_tap_ticks.fetch_sub(1) <= 1) {
            g_state.store(g_state_before_tap.load());
        }
        return;
    }
    if (s == State::kNfcRejected) {
        // Solid red, decremented per tick. When the count hits zero,
        // restore the LED to whatever state we were in before the tap
        // started (typically kLocked).
        put(160, 0, 0);
        if (g_nfc_reject_ticks.fetch_sub(1) <= 1) {
            g_state.store(g_state_before_nfc.load());
        }
        return;
    }
    if (s == State::kCommissioningOk && g_ok_ticks.load() > 0) {
        bool on = ((g_ok_ticks.load() % 2) == 0);
        put(0, on ? 80 : 0, 0);
        if (g_ok_ticks.fetch_sub(1) <= 1) {
            // Default post-commissioning state — accessory is paired but
            // no Aliro keys yet. Will be flipped to kLocked by AliroDelegate
            // when SetAliroReaderConfig succeeds.
            g_state.store(State::kPairedNoAliro);
        }
        return;
    }

    switch (s) {
    case State::kBoot:
        put(0, 0, 24);                                       // dim blue
        break;
    case State::kCommissioningAdv: {
        bool on = ((tick / 5) % 2) == 0;                     // 1 Hz blue
        put(0, 0, on ? 80 : 0);
        break;
    }
    case State::kCommissioningActive: {
        bool on = ((tick / 1) % 2) == 0;                     // ~5 Hz blue
        put(0, 0, on ? 120 : 0);
        break;
    }
    case State::kPairedNoAliro:
        put(80, 0, 100);                                     // solid violet (R+B, magenta-leaning)
        break;
    case State::kLocked:
        // True orange: heavy red, low-mid green, no blue. WS2812 GRB
        // ordering: with R=200, G=40, B=0 the tile reads as orange,
        // distinct from yellow (which has R≈G).
        put(200, 40, 0);
        break;
    case State::kUnlocked:
        put(0, 140, 0);                                      // solid green
        break;
    case State::kNfcReading: {
        // Red, ~5 Hz fast blink — fires when PN532 detects ANY 14443A tag
        // (Flipper, MIFARE, EM410x, …). Held only during SELECT-AID
        // (~10-15 ms typical), then either flips to kDDKAuth (HomeKey
        // applet found) or kNfcRejected (generic non-key tag).
        bool on = ((tick / 1) % 2) == 0;
        put(on ? 180 : 0, 0, 0);
        break;
    }
    case State::kDDKAuth: {
        // Cyan, ~5 Hz fast blink — only fires after SELECT-AID succeeded
        // and DDK auth (FastAuth or StandardAuth, kAliro path) is in
        // flight. Typical duration 100-500 ms; user sees "the lock is
        // actively talking to the credential" before LED resolves to
        // white-flash→green (success) or red-1s (auth failed).
        bool on = ((tick / 1) % 2) == 0;
        put(0, on ? 120 : 0, on ? 120 : 0);
        break;
    }
    case State::kNfcRejected:
        // handled above (transient red, decremented by g_nfc_reject_ticks)
        break;
    case State::kFabricLost: {
        bool on = ((tick / 10) % 2) == 0;                    // 1 Hz solid yellow
        put(on ? 140 : 0, on ? 140 : 0, 0);                  // R=G yellow
        break;
    }
    case State::kError: {
        bool on = ((tick / 2) % 2) == 0;                     // ~5 Hz red
        put(on ? 120 : 0, 0, 0);
        break;
    }
    case State::kCommissioningOk:
        // handled by transient counter above
        break;
    }
}

void led_task(void *)
{
    uint32_t tick = 0;
    for (;; ++tick) {
        render_tick(tick);
        vTaskDelay(pdMS_TO_TICKS(kTickMs));
    }
}

void led_init()
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
        return;
    }
    led_strip_clear(g_led);
    // 4 KB — same reasoning as ByteButton tasks. tee_vprintf in app_main
    // burns ~280 B of stack per ESP_LOG call (vsnprintf into a 256-byte
    // line buffer + RemoteLog::push), so 2 KB stacks blow up under any
    // unexpected log volume.
    xTaskCreate(led_task, "led", 4096, nullptr, 1, nullptr);
    ESP_LOGI(TAG, "WS2812 LED state task started on GPIO %d", kLedGpio);
}

}  // namespace

namespace lockind {
void init() { led_init(); g_state.store(State::kBoot); }
void set_commissioning_advertising() { g_state.store(State::kCommissioningAdv); }
void set_commissioning_active()      { g_state.store(State::kCommissioningActive); }
void set_commissioning_ok()
{
    g_ok_ticks.store(6);
    g_state.store(State::kCommissioningOk);
}
void set_paired_no_aliro() { g_state.store(State::kPairedNoAliro); }
void set_locked()          { g_state.store(State::kLocked); }
void set_unlocked()        { g_state.store(State::kUnlocked); }
void set_fabric_lost()     { g_state.store(State::kFabricLost); }
void set_error()           { g_state.store(State::kError); }

void flash_tap()
{
    State s = g_state.load();
    if (s != State::kCommissioningOk) {
        g_state_before_tap.store(s);
    }
    g_tap_ticks.store(3);
}

// Called from NfcManager when PN532 detects ANY 14443A passive target
// (HomeKey iPhone, Flipper emulation, generic MIFARE, …). Switches LED
// to red fast-blink for the SELECT-AID window. Subsequent transition:
//   SELECT succeeds → ddk_auth_start (cyan)
//   SELECT fails    → nfc_reading_failed (red 1 s)
void nfc_reading_start()
{
    State s = g_state.load();
    if (s != State::kNfcReading && s != State::kNfcRejected &&
        s != State::kDDKAuth && s != State::kCommissioningOk) {
        g_state_before_nfc.store(s);
    }
    g_state.store(State::kNfcReading);
#if CONFIG_ALIRO_OLED_PANEL
    // T0 for tap latency measurement — OledPanel pairs this with
    // notifyTap() (called from nfc_tap_unlock further below) to compute
    // total tap-to-unlock time visible on the panel.
    aliro::OledPanel::notifyTapStart();
#endif
#if CONFIG_ALIRO_BYTEBUTTON_PANEL
    // LED 0 cyan-blinks while we're reading the card.
    aliro::ByteButtonPanel::notifyNfcReading();
#endif
}

// Called from NfcManager::handleHomeKeyAuth right before the DDK
// FastAuth/StandardAuth round. Switches LED to cyan fast-blink so the
// user can distinguish "PN532 saw a card" (red) from "lock is actively
// authenticating an Apple/Samsung wallet credential" (cyan).
void ddk_auth_start()
{
    State s = g_state.load();
    if (s != State::kNfcReading && s != State::kDDKAuth &&
        s != State::kNfcRejected && s != State::kCommissioningOk) {
        g_state_before_nfc.store(s);
    }
    g_state.store(State::kDDKAuth);
}

// Auth succeeded. Caller (event-bus subscriber → nfc_tap_unlock) will
// transition us to kUnlocked via SetLockState; here we just clear the
// reading state so the unlock-green takes over.
void nfc_reading_ok()
{
    if (g_state.load() == State::kNfcReading) {
        // Briefly white-flash, then the unlock path lands us in kUnlocked.
        flash_tap();
    }
}

// Auth failed (Flipper / wrong key / replay). Hold solid red for ~1 s,
// then revert to whatever state we had before the tap (typically kLocked).
void nfc_reading_failed()
{
    aliro::access::accessDenied();
    g_nfc_reject_ticks.store(10);   // 10 × 100 ms tick = 1 s
    g_state.store(State::kNfcRejected);
#if CONFIG_ALIRO_BYTEBUTTON_PANEL
    aliro::ByteButtonPanel::notifyNfcResult(false);
#endif
}
}  // namespace lockind

// -----------------------------------------------------------------------------

using chip::EndpointId;
using chip::FabricIndex;
using chip::NodeId;
using chip::Optional;
using chip::ByteSpan;
using chip::app::DataModel::List;
template <typename T> using Nullable = chip::app::DataModel::Nullable<T>;
using chip::app::Clusters::DoorLock::OperationErrorEnum;
using chip::app::Clusters::DoorLock::OperationSourceEnum;
using chip::app::Clusters::DoorLock::DlLockState;
// LockOpCredentials is a top-level alias in door-lock-server.h, not in
// the DoorLock namespace.

static void apply_lock_state_change(EndpointId endpointId, DlLockState newState,
                                    OperationSourceEnum source,
                                    const Nullable<FabricIndex> & fabricIdx,
                                    const Nullable<NodeId> & nodeId)
{
    // Use the 7-arg overload so DoorLockServer also fires the
    // LockOperation event — that's what Apple Home subscribes to and
    // dismisses its "in progress" UI on. Without the event the tile
    // ends up at "not responding".
    const bool accepted = DoorLockServer::Instance().SetLockState(endpointId, newState,
                                            source,
                                            Nullable<uint16_t>(),
                                            Nullable<List<const LockOpCredentials>>(),
                                            fabricIdx, nodeId);

    // Actuate only after Matter accepts the state/event update. Preserve the
    // existing Aliro validation and the Apple Home LockOperation event path.
    if (!accepted) { ESP_LOGW(TAG, "Lock state update rejected; output unchanged"); return; }
    if (newState == DlLockState::kLocked || newState == DlLockState::kUnlocked)
        aliro::access::lockChanged(newState == DlLockState::kLocked, static_cast<unsigned>(source));

    if (newState == DlLockState::kLocked) {
        lockind::set_locked();
        ESP_LOGI(TAG, "ep=%u -> LOCKED (src=%u)", endpointId, (unsigned)source);
    } else if (newState == DlLockState::kUnlocked) {
        lockind::set_unlocked();
        ESP_LOGI(TAG, "ep=%u -> UNLOCKED (src=%u)", endpointId, (unsigned)source);
    }
}

// Public wrapper used by NfcManager when an Aliro tap is authenticated.
// The event Source is kAliro (0x0A) — Apple Home iOS 18+ recognizes this
// and renders the timeline entry as a tap. fabric/node are null because
// the actuator was a local credential, not a fabric controller.
//
// Toggles the lock state: tap-while-locked → unlock, tap-while-unlocked
// → lock. Matches the UX of every commercial Apple Home Key lock — single
// physical tap is the entire user-facing action, no separate lock action.
namespace lockind {
void nfc_tap_unlock(EndpointId endpointId)
{
    // Toggle based on the *pre-tap* LED state — by the time we get here the
    // current g_state is kDDKAuth (cyan blink) because ddk_auth_start()
    // flipped it during the auth round. The lock-state-relevant snapshot
    // is in g_state_before_nfc, saved by nfc_reading_start / ddk_auth_start
    // before they overwrote g_state. Without this we'd always read
    // kDDKAuth and the toggle would always pick UNLOCK.
    State pre = g_state_before_nfc.load();
    bool isUnlocked = (pre == State::kUnlocked);
    DlLockState newState = isUnlocked ? DlLockState::kLocked : DlLockState::kUnlocked;
    ESP_LOGI(TAG, "NFC tap toggle: pre-tap state=%u (unlocked=%d) -> %s",
             (unsigned)pre, isUnlocked,
             newState == DlLockState::kUnlocked ? "UNLOCK" : "LOCK");

    apply_lock_state_change(endpointId, newState,
                            OperationSourceEnum::kAliro,
                            Nullable<FabricIndex>(), Nullable<NodeId>());
    flash_tap();   // brief white-flash on the WS2812 indicator.
#if CONFIG_ALIRO_OLED_PANEL
    aliro::OledPanel::notifyTap(nullptr,
        newState == DlLockState::kUnlocked ? "open" : "close");
#endif
#if CONFIG_ALIRO_BYTEBUTTON_PANEL
    aliro::ByteButtonPanel::notifyNfcResult(true);
#endif
}

// Explicit Lock / Unlock helpers — used by ByteButtonPanel BTN0 (unlock)
// and BTN1 (lock). Source = kManual (a physical button pressed on the
// device, not a remote Matter command, not an Aliro NFC tap). Apple Home
// renders these as "manually unlocked"/"manually locked" timeline entries.
void unlock_local(EndpointId endpointId)
{
    apply_lock_state_change(endpointId, DlLockState::kUnlocked,
                            OperationSourceEnum::kManual,
                            Nullable<FabricIndex>(), Nullable<NodeId>());
}
void lock_local(EndpointId endpointId)
{
    apply_lock_state_change(endpointId, DlLockState::kLocked,
                            OperationSourceEnum::kManual,
                            Nullable<FabricIndex>(), Nullable<NodeId>());
}
}  // namespace lockind

bool emberAfPluginDoorLockOnDoorLockCommand(EndpointId endpointId,
                                            const Nullable<FabricIndex> & fabricIdx,
                                            const Nullable<NodeId> & nodeId,
                                            const Optional<ByteSpan> & pinCode,
                                            OperationErrorEnum & err)
{
    ESP_LOGI(TAG, "LockDoor ep=%u from fabric=%d node=0x%llx pinLen=%u",
             endpointId,
             fabricIdx.IsNull() ? 0 : fabricIdx.Value(),
             (unsigned long long)(nodeId.IsNull() ? 0 : nodeId.Value()),
             (unsigned)(pinCode.HasValue() ? pinCode.Value().size() : 0));
#if CONFIG_ALIRO_BYTEBUTTON_PANEL
    aliro::ByteButtonPanel::notifyMatterActivity();
#endif
#if CONFIG_ALIRO_OLED_PANEL
    aliro::OledPanel::notifyMatterLock("Matter");
#endif
    apply_lock_state_change(endpointId, DlLockState::kLocked,
                            OperationSourceEnum::kRemote, fabricIdx, nodeId);
    return true;
}

bool emberAfPluginDoorLockOnDoorUnlockCommand(EndpointId endpointId,
                                              const Nullable<FabricIndex> & fabricIdx,
                                              const Nullable<NodeId> & nodeId,
                                              const Optional<ByteSpan> & pinCode,
                                              OperationErrorEnum & err)
{
    ESP_LOGI(TAG, "UnlockDoor ep=%u from fabric=%d node=0x%llx pinLen=%u",
             endpointId,
             fabricIdx.IsNull() ? 0 : fabricIdx.Value(),
             (unsigned long long)(nodeId.IsNull() ? 0 : nodeId.Value()),
             (unsigned)(pinCode.HasValue() ? pinCode.Value().size() : 0));
#if CONFIG_ALIRO_BYTEBUTTON_PANEL
    aliro::ByteButtonPanel::notifyMatterActivity();
#endif
#if CONFIG_ALIRO_OLED_PANEL
    aliro::OledPanel::notifyMatterUnlock("Matter");
#endif
    apply_lock_state_change(endpointId, DlLockState::kUnlocked,
                            OperationSourceEnum::kRemote, fabricIdx, nodeId);
    return true;
}

bool emberAfPluginDoorLockOnDoorUnboltCommand(EndpointId endpointId,
                                              const Nullable<FabricIndex> & fabricIdx,
                                              const Nullable<NodeId> & nodeId,
                                              const Optional<ByteSpan> & pinCode,
                                              OperationErrorEnum & err)
{
    return emberAfPluginDoorLockOnDoorUnlockCommand(endpointId, fabricIdx, nodeId, pinCode, err);
}
