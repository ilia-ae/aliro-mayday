# aliro-esp32

Experimental ESP32-S3 + PN532 Aliro NFC controller with Matter provisioning,
NVS credential storage and local administration. iPhone Standard/Fast taps
worked on the May 2026 bench. The October update builds and passes host NFC
regressions, but the controller is offline; real iPhone and Samsung tests remain
required before using the new build.

The update preserves existing identity and NVS field names, prefers Aliro
`0x0100` with explicit legacy Apple `0x0009` compatibility, rejects unknown device
keys, and enforces persisted credential deletion / disabled user checks.
Step-up document-based enrollment remains disabled until full document and
access-policy validation is implemented. Details and rollback/acceptance steps:
[October update](../docs/ddk-update-2026-10-13.md),
[host tests](tests/README.md), [pinned DDK](components/DigitalDoorKey/UPSTREAM.md).

The April roadmap below is historical; refer to the October notes for current
verified status and limitations.

## Historical roadmap (April 2026)

| Section | What | Done? |
|---|---|---|
| §7.1 | Project skeleton, esp-matter boot, BLE commissioning | landed in tree; cleanup / reproducibility still in progress |
| §7.2 | Matter DoorLock server with `ALIRO` feature | initial provisioning path landed; ecosystem validation still pending |
| §7.3 | PN532 + Aliro NFC transaction (spec §8.3) | live iPhone traces now reach Aliro `SELECT` / `AUTH0`; cold-tap Step-up and end-to-end unlock are still pending |
| §7.4 | homekey-esp32 kept as regression baseline | ongoing |
| §7.5 | E2E tests with iPhone + Samsung Fold 7 | blocked on §7.2+§7.3 |

## Prerequisites

1. **ESP-IDF v5.2+** installed. If you already have the `nix develop`
   environment used for [../homekey-esp32/](../homekey-esp32/), that
   works — it brings v5.3.
2. **Bun** installed for the bundled Svelte WebUI build.
3. **esp-matter** cloned next to this repo and exported:
   ```bash
   cd ~/CODE
   git clone --depth 1 https://github.com/espressif/esp-matter.git
   cd esp-matter
   ./install.sh                      # pulls connectedhomeip submodules (~4 GB, ~20 min)
   . ./export.sh                     # sets ESP_MATTER_PATH, MATTER_SDK_PATH
   ```
   The root [CMakeLists.txt](CMakeLists.txt) reads `$ESP_MATTER_PATH`
   and fails fast if unset.

## Build

```bash
cd aliro-esp32
. $IDF_PATH/export.sh
. $ESP_MATTER_PATH/export.sh
idf.py set-target esp32s3
cd data
bun install --frozen-lockfile
bun run build
cd ..
idf.py build
```

## Flash + commission

**Same hardware rig as homekey-esp32** — swap firmware only, don't
re-wire.

In practice there are two repo-local host shapes:

- **DevKitC-class ESP32-S3 + PN532** — primary `aliro-esp32/` bench target
- **LilyGO T-ETH-Lite-ESP32S3 + PN532** — frozen `homekey-esp32/` LAN
   baseline only for now; this tree does not implement Ethernet yet

For future PN7642-native bring-up, the official NXP reference platform
is [OM27642 / OM27642EVK](https://www.nxp.com/design/design-center/development-boards-and-designs/OM27642)
("Development Kit for PN7642 Secure, Compact NFC Controller with
Programmable Memory and Crypto Acceleration"). This repo does not use
that kit yet; the current firmware target is still the ESP32-S3 + PN532
rig described in the repo-root README.

Current PN532 testing in this repo uses breakout boards from both the
original module line and Chinese clones. A separate NXP
[OM27160 / PN7160-EVK](https://www.nxp.com/design/design-center/development-boards-and-designs/PN7160-EVK)
kit is expected but has not arrived yet, so there is no PN7160-specific
bring-up path in this tree so far.

Per-board technical details are documented in
[../docs/hardware-reference.md](../docs/hardware-reference.md).

```bash
idf.py -p /dev/cu.usbmodem* flash monitor
```

On boot the serial log prints:
- A QR-code URL (paste into Apple Home, Google Home, or SmartThings to
  commission)
- A manual pairing code (for `chip-tool`)

Commission via chip-tool smoke test:

```bash
chip-tool pairing ble-wifi 0x1234 "YourWiFi" "password" 20202021 3840
chip-tool basicinformation read product-name 0x1234 0
```

If product-name comes back → Matter stack is alive and skeleton §7.1 is
complete. Move on to provisioning validation (§7.2) and the live PN532 /
Aliro runtime checks (§7.3).

## What exists already vs what is still missing

Already in tree:

- Matter boot + BLE commissioning
- DoorLock endpoint with `AliroProvisioning`
- NVS-backed Aliro delegate / credential stubs
- PN532 HAL + `NfcManager` port
- ECP payload updates from reader GID changes
- tag detection + DDK auth-context precompute
- live Aliro AID select + `AUTH0` on iPhone
- SELECT-response FCI `A5` parsing into the DDK key schedule
- full 16-byte `reader_group_sub_identifier` mirrored into DDK reader data
- local `AUTH_OK` hook wired to the lock toggle path

Still missing / not yet proven:

- treating cold-tap `AUTH0 = 6984` as Step-up instead of terminal fail
- real-wallet proof of `AUTH1` decrypt / endpoint match / signature verify
- warm-tap FastAuth validation after the cold path successfully caches an endpoint
- local HTTP API / WebSocket / MQTT / Prometheus surfaces
- Ethernet support in this tree

Current cold-tap / `6984` notes are tracked in
[../docs/aliro-stdauth-finishing-roadmap.md](../docs/aliro-stdauth-finishing-roadmap.md).

## Tap-debug workflow

When iterating on the Aliro auth path, keep the raw serial log and the
tap parser side by side:

```bash
mkdir -p /tmp/aliro-debug
cd aliro-esp32
idf.py -p /dev/cu.usbmodem* monitor | tee /tmp/aliro-debug/serial.log

# in a second shell, from the repo root
cd ..
just diag-tap
```

`just diag-tap` reads `/tmp/aliro-debug/serial.log` and renders each tap
as `detect → select → FCI parse → AUTH0/AUTH1 → endpoint match → result`.

## Swap back to homekey-esp32 firmware

See §7.4 in the repo-root TODO. The same PN532-wired ESP32-S3 can run
either firmware — `just upstream-*` keeps homekey-esp32 current, and
we flash it back before each Aliro dev session to confirm the rig is
healthy.

```bash
cd ../homekey-esp32
nix develop
idf.py -p /dev/cu.usbmodem* flash monitor  # iPhone tap should unlock
```

## Layout

```
aliro-esp32/
├── CMakeLists.txt          # root project, pulls esp-matter via ESP_MATTER_PATH
├── sdkconfig.defaults      # ESP32-S3 + BLE + WiFi + Matter NVS + PSRAM
├── partitions.csv          # 2× 1.8 MB OTA slots + encrypted nvs_keys + aliro_nvs
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   ├── app_main.cpp        # Matter boot + DoorLock + PN532/NfcManager wiring
│   ├── NfcManager.cpp      # PN532 polling, ECP payload updates, DDK auth path
│   ├── AliroDelegate.cpp   # NVS-backed DoorLock delegate
│   └── Diag.cpp            # boot snapshot + heartbeat + network diagnostics
└── components/             # copied-in support libs (PN532 HAL, DDK, EventBus, loggable)
```

## Known issues at first build

The project now has local build artifacts in this workspace, but the
esp-matter toolchain is still not pinned tightly enough to call builds
fully reproducible. Expect at least one of the following on a fresh
`idf.py build` attempt; each has a known workaround.

**Symbol / header mismatches against installed esp-matter.**
`main/app_main.cpp` uses `esp_matter::console::init()`,
`node::create(&node_config, attr_cb, id_cb)`, and the
`attribute::callback_type_t` / `identification::callback_type_t`
signatures. These have moved across esp-matter releases. If the
compiler complains about missing headers or bad arity, grep for the
actual symbol in `$ESP_MATTER_PATH/components/esp_matter/` and adjust.
Pin the esp-matter commit in your notes once a green build lands so
the next checkout is reproducible.

**esp-matter `install.sh` on macOS + nix.**
esp-matter's `install.sh` expects a system Python (pyenv / Homebrew
Python usually works; the nix-provided Python from the
homekey-esp32 `flake.nix` doesn't have the required packages out of
the box). Workaround: install esp-matter *outside* `nix develop` using
Homebrew or pyenv Python first, then source its `export.sh` from any
shell. Don't try to unify the two toolchains.

**PSRAM is off by default — flip on if your module has it.**
`sdkconfig.defaults` leaves `CONFIG_SPIRAM` unset, matching
`homekey-esp32/sdkconfig.defaults` (which ships with no PSRAM lines and
boots fine on the rig — implying a `-N8` module without PSRAM). If you
later confirm a `-N8R8` or `-N16R8` variant on the DevKitC, enable
`CONFIG_SPIRAM=y` via `idf.py menuconfig` under
`Component config → ESP PSRAM` — Matter + TLS buffers benefit.

**Partition table doesn't fit once Matter is fully linked.**
The 1.8 MB OTA slots in `partitions.csv` are *hopefully* enough but
have not been measured. If you see `region 'iram0_0_seg' overflowed`
or a linker "section too large" error, bump both `ota_0` / `ota_1` by
128 KB and shrink `aliro_nvs` / `matter_kvs` by the same total.

**Commissioning QR URL doesn't render in SmartThings / Home app.**
esp-matter's default `Vendor ID` and `Product ID` are test values
(`0xFFF1` / `0x8001` in this tree) that most commissioning apps reject unless the
app is in a dev/beta mode. For production, register a real VID/PID
with CSA; for dev, use `chip-tool` which accepts any VID/PID.

## Design note — why not a subtree of esp-matter

esp-matter is a ~4 GB SDK that changes weekly. Vendoring it the way we
vendor rednblkx/HomeKey-ESP32 as a subtree would bloat the repo and
turn every esp-matter bump into a subtree merge conflict. Instead we
treat esp-matter as a toolchain dependency (like ESP-IDF itself):
installed out-of-tree, referenced by env var. The project code here is
just the thin app layer plus the Aliro reader logic (which is all we
actually want to version).
