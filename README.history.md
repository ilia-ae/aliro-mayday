# apple-homekey-pm3-reader

> Historical bring-up/roadmap reference. Its status and assumptions predate the October document-policy update. Use the [current documentation](docs/README.md) for verified behavior, build instructions and remaining acceptance work.

Experimental smart-lock workspace. Aliro NFC on ESP32-S3 + PN532 is the
active target. The iPhone path worked on the May 2026 bench; the controller is
offline during the October update, so the updated firmware still needs real
iPhone regression taps and Samsung Wallet validation through the available hub.

DigitalDoorKey is pinned to `80e4a1e14eab3646b62ab3bdad6d2ed01d399375`.
The update preserves reader identity, pairing and old NVS records, fixes
signature handling and credential revocation, and rejects unknown device keys.
See [October update and acceptance checks](docs/ddk-update-2026-10-13.md).
The older April roadmap below is historical context, not current test evidence.

Public release preparation uses a clean source snapshot; the existing private
Git history must stay private because it contained real Wi-Fi credentials.
Use [the exporter](scripts/export-public-source.py) and preserve
[license scopes and third-party notices](THIRD_PARTY_NOTICES.md). Public source
availability includes commercial restrictions on project-original Aliro code;
MIT and other third-party permissions remain in force.

Seven tracks live side by side (one planned):

- **`aliro-esp32/`** — **primary target.** ESP32-S3 + PN532 **Aliro access
  controller** firmware, built on [espressif/esp-matter](https://github.com/espressif/esp-matter).
  Not just an NFC reader — the full controller: Aliro transaction +
  lock output + Matter provisioning + local HTTP API + MQTT telemetry +
  tamper/anomaly detection + CCTV webhook triggers + Prometheus metrics.
  Cross-platform by design: Apple Wallet, Google Wallet, Samsung Wallet
  all commission through the standard Matter DoorLock cluster with the
  `ALIRO` feature. Current state: esp-matter boots, advertises for BLE
  commissioning, and already contains initial DoorLock +
  `AliroProvisioning` plumbing; the PN532 + DDK path now reaches live
  Aliro `SELECT` + `AUTH0` on iPhone, parses the SELECT-response FCI for
  the key schedule, and has the local `AUTH_OK` lock path wired, but the
  cold-tap Step-up transition after `AUTH0 = 6984` is still the main
  §7.3 blocker. See [aliro-esp32/README.md](aliro-esp32/README.md) and
  [docs/aliro-stdauth-finishing-roadmap.md](docs/aliro-stdauth-finishing-roadmap.md).
- **`homekey-esp32/`** — **frozen as regression baseline.** Vendored fork
  of [rednblkx/HomeKey-ESP32](https://github.com/rednblkx/HomeKey-ESP32),
  full HomeKey stack (ECDH + AES-GCM, HomeKit pairing, MQTT) on the
  same ESP32 + PN532 rig. Not being rewritten; we reflash it before
  each Aliro dev session to verify the rig is healthy (see
  [scripts/flash-homekey.sh](scripts/flash-homekey.sh) / [flash-aliro.sh](scripts/flash-aliro.sh)).
  `just upstream-*` keeps it current with rednblkx.
- **`android-smartkey/` + `smartkey-esp32-patch/`** — Kotlin Android HCE
  companion with matching C++ patches for the homekey-esp32 firmware.
  Wire spec: [PROTOCOL.md](PROTOCOL.md). **Deprioritised** now that
  Aliro covers Android via Samsung/Google Wallet — kept as a fallback
  for phones without wallet Aliro support.
- **`homekey_reader.py`** — drives a Proxmark3 (Iceman fork) into Apple
  Enhanced Contactless Polling (ECP) Access mode. Research / sniffing
  tool; PM3 is the only hardware here that can sniff 14A, so it's
  still useful for reverse-engineering real Aliro locks when they land.
- **`lock_mvp.py`** — pre-HomeKey DIY lock: UID allowlist via PM3, HF 14A
  + LF EM410x. Superseded, kept for UID-based fallback.
- **`aliro-site/`** — project website at `aliro.mayday.software`.
  Cloudflare Worker + Angular 21 SPA. **Git subtree** of the private repo
  [`razqqm/aliro-mayday-software`](https://github.com/razqqm/aliro-mayday-software)
  — edits happen here, `just site-push` mirrors to the private repo,
  Cloudflare Workers Build deploys from there. Dual-repo split because
  CF's clone phase trips on the `homekey-esp32/` subtree metadata in
  the monorepo. See [aliro-site/README.md](aliro-site/README.md).
- **`aliro-bridge/`** — **planned** backend service (see [TODO.md §7.13](TODO.md)).
  Node.js + TypeScript, subscribes MQTT from one or more aliro-esp32
  controllers, forwards to Telegram (Bot API + Mini App), dispatches
  CCTV webhooks, aggregates long-term analytics. Runs as Docker on
  NAS/RPi or Cloudflare Worker. Optional — ESP32 works standalone on
  LAN; bridge adds remote + messaging features.

## Historical roadmap (April 2026)

| Track | State |
|---|---|
| `aliro-esp32/` | built locally; Matter skeleton + initial Aliro provisioning path landed; live iPhone traces now reach Aliro `SELECT`/`AUTH0`, but cold-tap Step-up and end-to-end wallet unlock are still pending |
| `homekey-esp32/` | working firmware, iPhone tap + T-ETH-Lite Ethernet/WebUI baseline confirmed; **frozen** — use as baseline |
| `android-smartkey/` | partial (see [TODO.md §6](TODO.md)); deprioritised |
| `aliro-site/` | skeleton + RU/EN landing; subtree of private `razqqm/aliro-mayday-software`; CF deploy live |
| `aliro-bridge/` | planned — not started yet (§7.13) |
| Aliro spec | downloaded at `docs/aliro-1.0-specification.pdf` — **not committed** (CSA license) |

## Testing rig — what we hold that the upstream author doesn't

We finalize the Aliro path because we have both wallets on hand for
end-to-end taps:

| Device | Wallet | Tests |
|---|---|---|
| **iPhone 16 Pro Max** | Apple Wallet → Home Key (Apple's contribution to Aliro) | Apple-side ECP wakeup, live `SELECT`/`AUTH0` traces, cold-tap Step-up debugging |
| **Samsung Fold 7** (Galaxy Z Fold7) | Samsung Wallet → Aliro Digital Key | Samsung-side polling pattern, Aliro-only crypto path, multi-wallet validation once the current iPhone cold-tap blocker is closed |

Both wallets are the target validation devices for the same lock through
the standard Matter DoorLock cluster `SetAliroReaderConfig` command (the
keys land in our [AliroDelegate](aliro-esp32/main/AliroDelegate.h)
regardless of which phone provisioned them). The current live iPhone
path already reaches Aliro `SELECT`/`AUTH0`; full unlock-by-tap
validation is still the open §7.3 issue.

**This matters because:** in [`rednblkx/DigitalDoorKey`](https://github.com/rednblkx/DigitalDoorKey)
the author landed `feat: experimental Aliro support` (commit `e592f5f3`,
2026-02-11) and refactored the auth classes for HomeKey/Aliro
duality (`refactor: renamed all auth classes to reflect their new duality`,
2026-02-12) — but the upstream
[`rednblkx/HomeKey-ESP32`](https://github.com/rednblkx/HomeKey-ESP32)
firmware that wires DDK still hard-codes the HomeKey path
(`grep -r Aliro homekey-esp32/main/` → nothing). A reasonable read of
the timeline is the author has the Apple side to test against but not a
Samsung Wallet device, so the Aliro branch sits experimental in the
library and never gets exercised end-to-end. With both phones in hand
we can drive both flows and finalize the integration.

The DDK copy in `aliro-esp32/components/DigitalDoorKey/` is pinned, with
local compatibility and security patches documented in
[UPSTREAM.md](aliro-esp32/components/DigitalDoorKey/UPSTREAM.md).

## Role in the bigger picture

```
smart-lock (PN532 + Aliro via Matter)     ← end goal (was: HomeKey)
└── apple-homekey-pm3-reader  (this)
    ├── aliro-esp32/                      ← PRIMARY: Aliro access controller (§7)
    ├── aliro-bridge/                     ← planned: MQTT → Telegram / CCTV / analytics (§7.13)
    ├── homekey-esp32/                    ← FROZEN baseline (vendored subtree)
    ├── android-smartkey/                 ← custom HCE fallback (§6, deprioritised)
    ├── smartkey-esp32-patch/             ← draft C++ patches for homekey-esp32
    ├── PROTOCOL.md                       ← SmartKey wire spec
    ├── PROTOCOL-aliro.md                 ← Aliro controller wire spec + NVS + integration surfaces
    ├── docs/documentation-changelog.md   ← ongoing changelog for project-authored docs
    ├── docs/aliro-stdauth-finishing-roadmap.md ← cold-tap / 6984 Step-up notes
    ├── scripts/                          ← flash-homekey.sh / flash-aliro.sh
    ├── aliro-site/                       ← PROJECT SITE (subtree of razqqm/aliro-mayday-software)
    ├── homekey_reader.py                 ← PM3 ECP research / sniffer
    ├── lock_mvp.py                       ← legacy UID-allowlist lock
    ├── docs/aliro-1.0-specification.pdf  ← Aliro 1.0 spec (local only, .gitignored)
    └── pm3-jtag-rig                      ← PM3 flashing infra (external)
```

PM3 infrastructure (flashing / JTAG fallback):
[razqqm/pm3-jtag-rig](https://github.com/razqqm/pm3-jtag-rig) (private).

## HomeKey-ESP32 firmware (`homekey-esp32/`) — **frozen baseline**

Fork of [rednblkx/HomeKey-ESP32](https://github.com/rednblkx/HomeKey-ESP32),
vendored via `git subtree`. Implements the full HomeKey reader stack
(ECP polling → NDEF select → ECDH → AES-GCM → HomeKit accessory) on
ESP32 + PN532.

**Status (April 2026):** frozen. Active development has moved to
[aliro-esp32/](aliro-esp32/) — see [TODO.md](TODO.md) §7. This firmware
stays as:

- the working iPhone-HomeKey path until Aliro is live end-to-end;
- the regression baseline — reflash via
  [scripts/flash-homekey.sh](scripts/flash-homekey.sh) before each Aliro
  dev session to confirm the rig is healthy;
- the field-service baseline on LilyGO T-ETH-Lite-ESP32S3 where WebUI /
  OTA / logs stay reachable over LAN even when USB access is awkward;
- upstream-synced via `just upstream-*`; the default workflow keeps new
  feature experiments out of the subtree until they are proven (see
  [smartkey-esp32-patch/](smartkey-esp32-patch/) for the drafts that
  *would* have landed here).

### Hardware (bench + field baseline)

- **Bench host MCU**: ESP32-S3-WROOM-1 DevKitC (dual USB-C: native USB + CP2102 UART)
- **Field-service host MCU**: LilyGO T-ETH-Lite-ESP32S3 (ESP32-S3 + W5500 Ethernet).
  Current repo-local baseline: DHCP LAN + WebUI confirmed under the
  `T-ETH-Lite-ESP32S3` Ethernet preset. W5500 occupies GPIO
  `9/10/11/12/13/14`, so PN532 must be remapped away from the default
  HomeKey pins when Ethernet is enabled.
- **NFC (current)**: PN532 breakout boards, both the original module and
  Chinese clones; all current units are run in **SPI** mode
  (SET0=H, SET1=L)
- **Breakout**: Y65592 screw-terminal adapter (solderless)
- **Incoming eval kit**: NXP
  [OM27160 / PN7160-EVK](https://www.nxp.com/design/design-center/development-boards-and-designs/PN7160-EVK)
  — official development kit for the PN7160 Plug'n Play NFC controller.
  Ordered and pending arrival; not part of the current ESP32-S3 + PN532
  bring-up path yet.
- **Reference kit (PN7642 / future Aliro path)**: NXP
  [OM27642 / OM27642EVK](https://www.nxp.com/design/design-center/development-boards-and-designs/OM27642)
  — official development kit for the PN7642 secure NFC controller
  (PN7642 reader board + LPCXpresso55S16 host board). Not part of the
  current ESP32-S3 + PN532 rig; kept here as the canonical NXP hardware
  reference for later PN7642-native work.

Detailed technical profiles for every board above live in
[docs/hardware-reference.md](docs/hardware-reference.md).

### Hardware Decision Table

| When | Use this hardware | Why |
|---|---|---|
| **Now** | ESP32-S3 + PN532 + Y65592 | This is the only proven rig for the current regression loop and for TODO §7.1-§7.3 bring-up. |
| **Next** | NXP [OM27160 / PN7160-EVK](https://www.nxp.com/design/design-center/development-boards-and-designs/PN7160-EVK) | Evaluate only after the current PN532 path is stable enough that an NCI-based comparison answers a concrete question. |
| **Later** | NXP [OM27642 / OM27642EVK](https://www.nxp.com/design/design-center/development-boards-and-designs/OM27642) | Treat as a future architecture study for a PN7642-native secure-reader path, not as the next bench replacement. |

Full comparison and roadmap-driven gap analysis:
[docs/hardware-reference.md](docs/hardware-reference.md) and
[docs/project-gap-analysis.md](docs/project-gap-analysis.md).

Recommended implementation order:
[docs/execution-plan.md](docs/execution-plan.md).

Documentation-only change log (by subsystem):
[docs/documentation-changelog.md](docs/documentation-changelog.md).

Current Apple cold-tap / `6984` Step-up notes:
[docs/aliro-stdauth-finishing-roadmap.md](docs/aliro-stdauth-finishing-roadmap.md).

### Provisioning / maintenance modes

| Capability | `homekey-esp32/` | `aliro-esp32/` | Notes |
|---|---|---|---|
| Setup AP | `HomeSpan-Setup` / `homespan`; recovery path may expose `HomeKey-ESP32` / `homekey123` | no captive portal | `homekey-esp32/` setup is HomeSpan-driven; fallback AP comes from the repo-local captive-portal path |
| Ethernet / LAN management | yes | not yet implemented | Repo-local baseline is LilyGO T-ETH-Lite-ESP32S3 + W5500 |
| BLE commissioning | no | yes | `aliro-esp32/` uses Matter BLE commissioning |
| LAN WebUI / OTA | yes | not yet implemented | `homekey-esp32/` only, via HTTP/WebSocket/OTA endpoints |

### Wiring — 8 lines, SPI (`Default` preset)

| PN532 | ESP32-S3 GPIO | Role | Required? |
|---|---|---|---|
| `5V` | `5V` | power | yes |
| `GND` | `GND` | ground | yes |
| `SCK` | `12` | SPI clock | yes |
| `M` (MISO) | `13` | SPI MISO | yes |
| `MO/SDA/TX` | `11` | SPI MOSI | yes |
| `NSS/SCL/RX` | `10` | SPI CS | yes |
| `IRQ` | `14` | frame-ready interrupt | **optional** — see note |
| `RST` | `9` | hardware reset (future use) | **optional** — see note |

Pin mapping for the 4 SPI lines is runtime-configurable via the
captive-portal WebUI on first boot — the table matches the firmware's
built-in `Default` preset. IRQ/RST aren't in the WebUI; they're just
wired for tidiness + future upgrades.

This table is **DevKitC-only**. On the LilyGO T-ETH-Lite-ESP32S3 LAN
baseline, these pins collide with the onboard W5500 Ethernet controller
(`GPIO9/10/11/12/13/14`). Use a custom PN532 remap there instead; the
current repo-local transport-verified starting point is documented in
[docs/hardware-reference.md](docs/hardware-reference.md).

**IRQ (GPIO 14)**: The HAL's `SpiTransport` supports an IRQ pin for
interrupt-driven "frame ready" signalling — lower CPU, faster wake,
better sleep. Stock firmware hardcodes it to `GPIO_NUM_NC`
(`NfcManager.cpp`) and doesn't expose it in the WebUI. To activate,
patch `NfcManager.cpp` to pass `GPIO_NUM_14` and rebuild from source.
Having the line pre-wired costs nothing and avoids re-wiring later.

**RST (GPIO 9)**: PN532 has an internal pull-up on its RST line, so
the chip runs fine with the pin either floating or held high by an
undriven ESP32 input. Stock firmware doesn't touch GPIO 9 or drive
the RST line — we just wire it for mechanical tidiness and for the
future option of issuing a hardware reset if the PN532 ever hangs.
GPIO 9 on ESP32-S3 is a plain-function pin (not strap, not flash, not
PSRAM-reserved) so this is safe.

### Flashing

- **Web flasher** (Chrome/Edge): <https://homekey-esp32.vercel.app> →
  pick `ESP32-S3` target, connect via left USB-C.
- **Local build** from this tree:
  ```bash
  cd homekey-esp32
  nix develop        # provides ESP-IDF toolchain via flake.nix
  idf.py set-target esp32s3
  idf.py build flash monitor
  ```

### Configuration

On the stock Wi-Fi-first path, HomeSpan raises the setup AP
`HomeSpan-Setup` / `homespan`. If the firmware falls into the repo-local
recovery captive-portal path, you may instead see `HomeKey-ESP32` /
`homekey123`.

For the DevKitC bench path, connect to the setup AP, open the captive
portal, keep the NFC pin preset on `Default`, and pair with Apple Home
through the HomeKit accessory setup code shown on screen.

For the LilyGO T-ETH-Lite-ESP32S3 field baseline, first enable the
`T-ETH-Lite-ESP32S3` Ethernet preset, then remap PN532 away from the
W5500 pins, and continue over LAN. Once Ethernet is up the device stays
reachable via DHCP IP and `HK-<suffix>.local`, so Wi-Fi STA credentials
are optional for the frozen HomeKey baseline.

### Keeping in sync with rednblkx/HomeKey-ESP32

Three remotes are configured in this repo:

- `origin` → razqqm/apple-homekey-pm3-reader (you are here)
- `homekey-upstream` → rednblkx/HomeKey-ESP32 (read-only)
- `homekey-fork` → razqqm/HomeKey-ESP32 (your fork, for contributions back)

Manual flow (no automation):

```bash
just upstream-check    # any new commits from rednblkx?
just upstream-pull     # squash-merge them into homekey-esp32/
just upstream-push     # push your homekey-esp32/ changes to razqqm/HomeKey-ESP32
```

If a local `homekey-esp32/` fix must land, it is committed here as a
normal repo commit. The default Aliro workflow still treats
`homekey-esp32/` as a frozen baseline, so speculative feature work stays
outside the subtree until proven.

## Android companion key (`android-smartkey/` + `smartkey-esp32-patch/`) — **deprioritised**

Custom protocol for tap-to-unlock from Android phones, independent of
Apple HomeKey. Was designed to run alongside HomeKey on the same reader.
See the full wire spec in [PROTOCOL.md](PROTOCOL.md).

**Status (April 2026):** deprioritised. Aliro 1.0 (February 2026) covers
Android natively through Samsung Wallet, Google Wallet, and the Matter
DoorLock cluster — no custom HCE app or bespoke reader patches required
(see [TODO.md](TODO.md) §7 and [PROTOCOL-aliro.md](PROTOCOL-aliro.md)).
Kept as a fallback for Android devices without wallet-side Aliro support;
may be revived if Aliro proves unworkable in a specific deployment.

- **`android-smartkey/`** — Kotlin app. Target `minSdk 29`, `compileSdk 35`.
  Registers a `HostApduService` for AID `F0534D52544B4559` ("F0SMRTKEY")
  plus an Android-15 Polling Loop Filter so we pre-empt Google Wallet
  on locked screens. Currently a Phase-1 scaffold — JVM-testable
  `ApduCodec`, stub HCE service, placeholder Compose UI.
- **`smartkey-esp32-patch/`** — draft C++ handler + `.patch` files for
  the ESP32 firmware. Adds a second AID branch in `NfcManager.cpp` and
  emits a second ECP2 annotation in the polling loop. Kept out of the
  `homekey-esp32/` subtree until we can build + flash + test them so
  `just upstream-pull` stays frictionless.

## HomeKey reader (`homekey_reader.py`)

Drives PM3 to emit HomeKey-lock ECP polling frames.

Research / sniffing tool, not a functioning lock. Full HomeKey auth
(ECDH + AES-GCM) lives in [`homekey-esp32/`](homekey-esp32/) — this
script is for observing what real iPhones / certified locks put on the
wire, which PM3 can do (14A sniff) and PN532 cannot.

## What it does

- Configures PM3 polling loop annotation for ECP Access with a chosen
  Target Card Identifier (TCI / reader identifier)
- Offers single-poll or continuous polling
- Sniffs 14a traffic so you can record what iPhone actually replies
- Has presets for known HomeKey-certified locks (Aqara, Schlage, Yale, Level)

## Requirements

- macOS (the port autodetect assumes `/dev/cu.usbmodemiceman*`)
- `proxmark3` installed (Iceman fork):
  ```bash
  brew tap RfidResearchGroup/proxmark3
  brew install --HEAD --with-generic proxmark3
  ```
- Proxmark3 Easy V5 or RDV4 flashed with Iceman
- Python 3.8+ (optionally `pyserial` for port-busy detection)

## Run

```bash
python3 homekey_reader.py
```

Interactive menu:

```
1) Set ECP TCI (reader identifier)
2) Enable ECP Access polling
3) Single poll (one iPhone tap)
4) Continuous poll (Ctrl+C to stop)
5) Sniff 14a (10 s)
6) Show last trace
7) Antenna tune (hw tune)
8) Disable ECP (back to stock)
9) Info: card on antenna
0) Show current pm3 prefs
```

Typical flow: `2` (enable ECP) → `4` (continuous poll) → present iPhone
near HF antenna → watch trace. Stop with Ctrl+C. Then `6` to decode.

## Non-interactive daemon

For dropping this into a lab rig:

```bash
python3 homekey_reader.py --tci 0007 --non-interactive
```

Starts continuous poll with Aqara-style TCI. Use `systemd` / `launchd`
to supervise.

## TCI presets

| Name | TCI | Source |
|---|---|---|
| default | `ffff` | Generic/testing |
| aqara-u200 | `0007` | Observed on Aqara U200 |
| schlage-encode-plus | `0006` | Observed on Schlage Encode Plus |
| yale-assure-lock-2 | `0005` | Observed on Yale Assure 2 Touchscreen |
| level-lock-plus | `0003` | Observed on Level Lock+ |

These values were observed via `hf 14a sniff` against real locks — they
may change across firmware versions.

## Notes on what iPhone actually does

- **Express Mode active (default for HomeKey)**: iPhone replies to any
  ECP Access frame with a HomeKey applet select — no tap required
- **No HomeKey configured**: iPhone ignores ECP Access entirely
- **Lock screen unlock required** on some beta iOS: iPhone shows
  "HomeKey" sheet but needs Face ID confirmation

This tool triggers the poll; it does not *unlock* anything. If you see
`5a` / `5e` / `90 00` responses in trace, the iPhone is engaging with
the ECP — that is the signal that HomeKey pipeline is wired up.

## Related projects

**Own repos:**
- [razqqm/HomeKey-ESP32](https://github.com/razqqm/HomeKey-ESP32) — personal fork of the ESP32 firmware (also vendored here as `homekey-esp32/`)
- [razqqm/pm3-jtag-rig](https://github.com/razqqm/pm3-jtag-rig) — ESP32-S3 Wi-Fi JTAG flasher/debugger for Proxmark3 (fallback if PM3 native USB ever fails)

**Production firmware (basis of `homekey-esp32/`):**
- [rednblkx/HomeKey-ESP32](https://github.com/rednblkx/HomeKey-ESP32) — ESP32 HomeKit lock with HomeKey support (~900★). The canonical non-Python HomeKey reader implementation.
- [rednblkx/HAP-ESPHome](https://github.com/rednblkx/HAP-ESPHome) — same stack exposed as an ESPHome component (alpha)
- [hackerembassy/HomeKey-Reader](https://github.com/hackerembassy/HomeKey-Reader) — downstream fork tuned for 4MB-flash ESP32s + HA Tags integration

**Protocol references (reverse engineering):**
- [kormax/apple-home-key-reader](https://github.com/kormax/apple-home-key-reader) — Python/nfcpy reader on PN532 (original research reference)
- [kormax/apple-home-key](https://github.com/kormax/apple-home-key) — protocol reverse-engineering docs
- [kupa22/apple-homekey](https://github.com/kupa22/apple-homekey) — independent parallel protocol research
- [kormax/apple-enhanced-contactless-polling](https://github.com/kormax/apple-enhanced-contactless-polling) — ECP frame examples for every Apple mode (VAS, Transit, Access, GymKit)
- [kormax/apple-vas](https://github.com/kormax/apple-vas) — Apple VAS protocol
- [kormax/aliro](https://github.com/kormax/aliro) — Aliro protocol research; primary external reference for [aliro-esp32/](aliro-esp32/) and [PROTOCOL-aliro.md](PROTOCOL-aliro.md)
- [gm3197 VAS spec gist](https://gist.github.com/gm3197/ad0959476346cef69b75ea0523214350) — reverse-engineered VAS spec

---

## DIY Lock MVP (`lock_mvp.py`)

Minimum-viable lock: PM3 polls, Python decides. No relay, no network,
no crypto — just "who tapped, is it allowed, toggle state, log it."
Wire an actuator in later.

### Run

```bash
# Enrol your first card (tap when prompted):
python3 lock_mvp.py --enroll

# See what's enrolled:
python3 lock_mvp.py --list

# Start the lock:
python3 lock_mvp.py
# → polls every 0.8s, toggles state on authorised taps,
#   writes events.jsonl, prints to stdout
```

### Live keyboard commands (while lock is running)

| Key | Action |
|---|---|
| `e` | Arm enrol mode — next **unknown** tap gets auto-added with timestamp name |
| `l` | List current allowlist |
| `s` | Show lock state (OPEN/CLOSED + last UID + time) |
| `h` | Help |
| `q` | Quit cleanly (same as Ctrl+C) |

No Enter key needed — keys are consumed cbreak-style. Auto-enrolled cards
get a placeholder name `card-<YYYYMMDD-HHMMSS>` which you can rename later
by editing `allowlist.json`.

### Data files (auto-created, gitignored)

- `allowlist.json` — map of `UID -> {name, proto, added_at}`
- `events.jsonl` — append-only event log: `start`, `AUTH_OK`, `DENY`, `stop`

### What it supports

| Protocol | Source | UID format |
|---|---|---|
| **ISO14443-A** (Mifare Classic, DESFire, NTAG, plastic EMV) | `hf 14a info` | 4 or 7 bytes hex |
| **LF EM410x** (EU/RU building fobs) | `lf em 410x reader` | 5 bytes hex |

Bank cards are readable but emit a **random UID each tap** — you can
enrol one tap's UID but the next tap will have a different UID. Use a
Mifare or EM fob for stable identity.

### What it intentionally skips (MVP scope)

- No Mifare Classic sector authentication (just UID match — trivial to
  clone with a magic card; good enough for prototyping)
- No DESFire AID authentication
- No HomeKey crypto (see `homekey_reader.py` for that side)
- No actuator/relay control (extend `run_loop` to flip a GPIO or HTTP-call
  your physical lock)
- No web UI / API — stdin/stdout + JSON files

### Extend points

To wire a real lock, add a callback in `run_loop` on `AUTH_OK`:

```python
# after lock.toggle(uid):
if lock.state == "OPEN":
    os.system("curl -s http://lock.local/api/open")
# or GPIO on Pi:
# import RPi.GPIO as GPIO; GPIO.output(PIN, GPIO.HIGH)
```

Durable allowlist via SQLite → swap `Allowlist.load/save` to use
`sqlite3`. Remote allowlist → fetch from your own server with signed
updates.
