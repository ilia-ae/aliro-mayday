#pragma once
//
// MatterAdminGlue — replaces HomeSpan-specific calls in the vendored
// rednblkx WebServerManager with Matter-stack equivalents.
//
// The original WebServerManager assumes a HomeSpan-managed pairing flow
// (controllerListBegin/End, processSerialCommand("H"/"X"/"A"),
// setWifiCredentials, setPairingCode). Aliro runs on Matter, so each
// call maps to a different SDK:
//
//   homeSpan.controllerListBegin/End       -> chip::Server fabric table
//   homeSpan.processSerialCommand("H")     -> erase Matter fabric NVS
//   homeSpan.processSerialCommand("X")     -> erase WiFi creds + reboot
//   homeSpan.processSerialCommand("A")     -> OpenBasicCommissioningWindow
//   homeSpan.setWifiCredentials            -> esp_wifi_set_config
//   homeSpan.setPairingCode                -> stored admin web PIN
//
// The web Pairing Code field is repurposed as the *web admin PIN*. Matter
// commissioning PIN is set at factory provisioning time (chip-factory NVS)
// and cannot be changed at runtime.

#include <cstdint>
#include <string>

namespace matter_admin {

// True iff the Matter fabric table has at least one paired controller.
// Mirrors the test `homeSpan.controllerListBegin() != end()` in the
// original WebServerManager.
bool isPaired();

// Trigger erase of all Matter fabric data (matter NVS partition) followed
// by a clean reboot. Equivalent of HomeKit "factory reset pairings".
void erasePairingsAndReboot();

// Erase WiFi credentials NVS and reboot — device will come up in
// commissioning mode (BLE advertising) waiting for fresh setup.
void eraseWifiAndReboot();

// Open a Basic Commissioning Window for `seconds` so any controller can
// commission this device using the original setup payload. This is the
// "make me joinable again" admin button — mapped to homeSpan.A in the
// original WebServerManager.
//
// Default 180s matches Matter spec recommendation.
//
// Returns true on success, false if the chip stack is not running yet
// or if a commissioning window is already open.
bool openCommissioningWindow(uint16_t seconds = 180);

// Set WiFi STA credentials and commit to NVS. Reboot is the caller's
// responsibility (so the response can be sent before reset).
//
// In rednblkx, homeSpan.setWifiCredentials persists creds and reboots;
// here we only persist — caller decides the reboot timing.
bool setWifiCredentials(const std::string & ssid, const std::string & password);

// Persist the *web admin PIN* (NOT Matter setup PIN — Matter PIN is
// factory-provisioned and immutable at runtime). Used by the
// captive-portal flow that lets an installer change web access PIN.
bool setAdminWebPin(const std::string & pin);

// Read back the current Matter setup payload (QR + manual code) for
// display on the admin UI. Returns the QR string ("MT:..." form) and
// the 11-digit manual code.
struct SetupPayload {
    std::string qr;          // e.g. "MT:-24J0CEK01940648G00"
    std::string manualCode;  // e.g. "14970112338"
};
SetupPayload getSetupPayload();

}  // namespace matter_admin
