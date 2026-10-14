# Debugging aliro-esp32

> Historical bring-up/roadmap reference. Its status and assumptions predate the October document-policy update. Use the [current documentation](../docs/README.md) for verified behavior, build instructions and remaining acceptance work.

This is a layered system: Matter, WiFi, BLE, NFC (PN532), NVS, Aliro
state machine. To debug a specific issue, you need to know **which layer
to look at and how to read the right log lines**. This page is the map.

## TL;DR — four views that cover most failures

| Monitor | When to use | How |
|---|---|---|
| `idf.py monitor` (USB serial) | Boot logs, panics, deep crashes | `cd aliro-esp32 && idf.py -p /dev/cu.usbmodem* monitor` (Ctrl-]+x to quit) |
| `chip-tool` / commissioner logs on the host | Verify Matter provisioning, DoorLock reads / writes, command failures | Run `chip-tool` commands next to the serial log and correlate timestamps |
| `diag::boot_snapshot()` + heartbeat | Once-per-boot chip/heap/fabric state, then 30s pulses | Just read serial — they print under the `diag` tag |
| `just diag-tap` (repo root) | Per-tap Aliro auth parsing when iterating on `SELECT` / `AUTH0` / `AUTH1` | Keep `/tmp/aliro-debug/serial.log` via `tee`, then run `cd .. && just diag-tap` |

There is **no on-device HTTP / WebSocket log surface in `aliro-esp32/` yet**.
That work still belongs to [../TODO.md](../TODO.md) §7.8. If you want live
logs today, serial is the source of truth.

`just diag-tap` is a repo-side helper, not an on-device feature. It parses
the captured serial log at `/tmp/aliro-debug/serial.log` into one
screenful per NFC tap.

## Common log tags and what each means

The firmware uses `ESP_LOGx(tag, ...)` everywhere. Filter by tag to
narrow down which subsystem is talking. Examples:

| Tag                | Layer                                     | Examples |
|--------------------|-------------------------------------------|----------|
| `aliro-esp32`      | Our own app code                          | `[ev] commissioning COMPLETE`, `[attr] WRITE ep=1 cluster=DoorLock attr=0x0080` |
| `diag`             | The Diag module — boot snapshot + heartbeat + NFC/Aliro hooks | `[diag] -- heartbeat --`, `[diag] [nfc] tap #5 uid=...` |
| `chip[DL]`         | CHIP DeviceLayer — WiFi / IP / BLE state  | `WiFi station connected`, `IPv6 addr available` |
| `chip[DIS]`        | CHIP Discovery — mDNS / commissionable advertise | `Advertise commission parameter`, `Updating services` |
| `chip[SVR]`        | CHIP Server — pairing flow, fabric persistence  | `Fabric already commissioned`, `SetupQRCode` |
| `chip[ZCL]`        | Zigbee Cluster Library — attribute storage / read / write at the cluster layer | `Door Lock server initialized`, `WRITE ERR` |
| `chip[IM]`         | Interaction Model — subscriptions, reports | `No subscriptions to resume` |
| `chip[IN]`         | CHIP Interaction Network — CASE / sessions | `CASE Server enabling CASE session setups` |
| `wifi`             | ESP-IDF WiFi driver                       | `sta is connecting`, `Haven't to connect to a suitable AP now!` |
| `BLE_INIT`         | NimBLE initialisation                     | `Bluetooth MAC: ...` |
| `NimBLE`           | NimBLE GAP procedures                     | `GAP procedure initiated: advertise` |
| `mdns_responder`   | mDNS service                              | `mdns_service_remove_for_host` |
| `esp_matter_attribute` | Per-attribute reads/writes by core esp_matter | `R : Endpoint 0x0001's Cluster 0x00000003's Attribute 0x00000001` |
| `esp_matter_cluster`   | Cluster plugin lifecycle                  | `Cluster plugin init common callback` |
| `nvs_sec_provider` | NVS encryption (off in dev)               | rare |
| `NfcManager` | PN532 polling loop, ECP payload updates, tag detection, DDK auth precompute | `Found chip PN532`, `ECP data updated for Aliro (SHA-256(GID)[0:8])`, `Auth precompute: ...` |
| `HKAuthCtx` | High-level DDK auth control flow | `Auth0 Response Length: 2, DATA: 6984`, `Response not valid, something went wrong!` |
| `HKStdAuth` | StandardAuth key schedule / signature validation | `Auth1_keying_material: ...`, `signature verification result: 0` |
| `DigitalKeySC` | AES-GCM decrypt / secure context | `mbedtls_gcm_auth_decrypt failed: -18` |
| `lock-hdl` | Local lock toggle / actuator state | `lock-hdl: ep=1 -> UNLOCKED (src=10)` |

## How log levels are configured

Inside `app_main.cpp`:

```cpp
esp_log_level_set("*",          ESP_LOG_INFO);
esp_log_level_set("aliro-esp32", ESP_LOG_DEBUG);
esp_log_level_set("chip[DL]",   ESP_LOG_INFO);
// ... etc.
```

Bump any tag to `ESP_LOG_DEBUG` or `ESP_LOG_VERBOSE` while debugging,
re-flash, watch.

## Layer-by-layer playbooks

### "Apple Home / Google Home commissioning fails"

1. Watch for `[ev] BLE connection established` — phone reached us via BLE
2. Then `[ev] commissioning session started` — PASE handshake began
3. Then `[ev] commissioning COMPLETE` + `[ev] fabric committed` — done

If it stops between (1) and (3), look for:
- `chip[SVR] Fabric already commissioned. Disabling BLE advertisement` →
  device thinks it's already paired. Use `idf.py erase-flash` (or send
  `matter device factoryreset` over the chip-shell on serial) to wipe NVS.
- `wifi: sta is connecting, return error` storm → WiFi creds the
  commissioner sent are wrong, or radio is busy. Check `chip[DL]` lines
  immediately afterwards for the actual reason.
- Long silence after `kCHIPoBLEConnectionEstablished` → phone gave up.
  Apple Home can reject uncertified real VID/PID combinations before the
  commissioner reaches the useful setup flow. This tree currently uses the
  test vendor path `0xFFF1/0x8001`; if Apple caches a bad state, bump the
  test PID and retry, or use Google Home / SmartThings instead.

### "Lock works in Home app but doesn't show 'Set Up Home Key'"

The DoorLock cluster must advertise the **`AliroProvisioning` feature
bit (bit 13)** in its FeatureMap. Confirm:

```bash
chip-tool doorlock read feature-map 1 1
# expect current tree: 0x2100  (USR + ALIRO_PROVISIONING)
```

If feature-map is missing bit 13, the
`cluster::door_lock::feature::aliro_provisioning::add()` call in
`app_main.cpp` never ran or failed silently. In the current tree the
expected value is `0x2100`, not just `0x2000`, because the USR bit is
also set. Look at the boot log for `[init] DoorLock cluster:
AliroProvisioning feature enabled`.

iOS may also cache cluster info. After enabling the feature for the
first time, force-quit Home app and re-open, OR remove the accessory
and re-commission.

### "Phone tap doesn't trigger anything"

(§7.3 territory — PN532 path is already in tree; the question is whether
the current keys / tags / wallets are exercising it correctly)

1. Boot snapshot — does Diag print PN532 firmware version? If not,
   SPI wiring or DIP switches are wrong.
2. Heartbeat shows `nfc=0/-1s(uid=(none) '')` — never seen a tag.
   Check the new `homekey-esp32` regression flash works: that firmware
   logs every detected UID at INFO. If it sees the card and ours
   doesn't, the bug is in our NfcManager port, not the hardware.
3. `[diag] [nfc] tap #N uid=... info='ECP-Aliro'` is logged but
   `[diag] [aliro] ...` never follows → we detect the tag but don't
  start the Aliro state machine. Look at `NfcManager`'s
  `handleTagPresence`.

### "Aliro applet selects but the tap still rejects"

This is the current §7.3 territory: the interesting failures are now in
the auth flow, not in SPI wiring.

1. `Auth0 Response Length: 2, DATA: 6984` → cold tap / no cached
   endpoint. That is **not** a wiring failure. It means Apple Wallet is
   asking us to continue into Step-up; the current DDK guard still treats
   it as failure. See
   [../docs/aliro-stdauth-finishing-roadmap.md](../docs/aliro-stdauth-finishing-roadmap.md).
2. `Parsed FCI A5 template: ...` never appears after `Aliro applet
   selected successfully` → the HKDF salt will be wrong and later AUTH1
   decrypts will fail GCM verification.
3. `mbedtls_gcm_auth_decrypt failed: -18` → reader/device keying material
   diverged. Check the parsed FCI template and the reader-identifier
   inputs first.
4. `First-tap provisioning: endpoint pk not cached, tentatively adding`
   appears → the provisional endpoint helper ran. If the tap still fails,
   the remaining bug is later in Step-up / Access Document / signature
   handling, not in the basic endpoint lookup hook.

### "It worked yesterday and now nothing"

Read the boot snapshot. Specifically:
- `reset rsn`: BROWNOUT or PANIC means the device crashed; read the
  panic dump that came right before the reboot.
- `heap free`: < 30 KB → we're leaking. Check heartbeat output over
  a few minutes; if `min ever` keeps dropping, it's a leak.
- `fabrics`: 0 → device was wiped (intentional or accidental). Re-commission.

## Quick commands

```bash
# Just monitor on serial — full firehose, leave open during commissioning
cd aliro-esp32
idf.py -p /dev/cu.usbmodem* monitor

# Same but only show our app + diag tags
idf.py -p /dev/cu.usbmodem* monitor --print-filter "aliro-esp32:I diag:I"

# Capture the raw serial log for the tap parser
mkdir -p /tmp/aliro-debug
idf.py -p /dev/cu.usbmodem* monitor | tee /tmp/aliro-debug/serial.log

# Matter smoke check from the host side while serial is open
chip-tool basicinformation read product-name 0x1234 0
chip-tool doorlock read feature-map 0x1234 1

# Parse each tap into a one-screen summary (run from repo root)
cd ..
just diag-tap

# Parse the current log once and exit
python3 scripts/aliro-tap-diag.py --once

# Wipe Matter / Aliro state without reflashing the app (preferred if the
# CHIP shell is enabled in this build):
# Open the serial shell and run:
matter device factoryreset

# Or fully nuke and reflash:
idf.py -p /dev/cu.usbmodem* erase-flash
idf.py -p /dev/cu.usbmodem* flash
```

## What's still missing / will be added later

- richer Aliro-specific phase tracing once the final APDU / AUTH path is
  nailed down on real wallets
- local HTTP / WebSocket surfaces from [../TODO.md](../TODO.md) §7.8
- richer controller-surface logs once MQTT / metrics / webhooks land
