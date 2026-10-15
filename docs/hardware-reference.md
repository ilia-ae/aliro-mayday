# Hardware Reference

This file records the physical boards and kits that matter to this repo.
It is intentionally more detailed than the short hardware summary in the
root README.

For the current Aliro firmware, use the labelled
[wiring guide](wiring.md) / [схему подключения](wiring.ru.md). The GPIO9 reset
suggestion from earlier bring-up is not valid with the current I²C peripherals.

For a roadmap-driven analysis of what is still missing, see
[project-gap-analysis.md](project-gap-analysis.md).

For the recommended implementation order, see
[execution-plan.md](execution-plan.md).
## Status Matrix

| Board / kit | Status in this repo | Why it matters |
|---|---|---|
| ESP32-S3-WROOM-1 DevKitC-class board | active | Main host MCU platform for `homekey-esp32/` and `aliro-esp32/` |
| LilyGO T-ETH-Lite-ESP32S3 (ESP32-S3 + W5500) | active | Field-serviceable `homekey-esp32/` baseline with LAN/WebUI access |
| PN532 breakout boards (original + Chinese clones) | active | Current NFC front-end used for all bench bring-up |
| Y65592 screw-terminal adapter | active | Solderless interconnect between the ESP32 board and PN532 module |
| NXP OM27160 / PN7160-EVK | ordered | Upcoming PN7160 evaluation platform for NCI-based experiments |
| NXP OM27642 / OM27642EVK | reference | Official PN7642 secure-reader evaluation kit for future native work |

## Project-Oriented Comparison Matrix

This matrix compares the hardware options the way they matter to this
repo's actual tasks, not as generic product marketing.

| Hardware option | Interfaces in project context | Power model in project context | Host model | Best fit in this repo | Main integration risks |
|---|---|---|---|---|---|
| ESP32-S3 DevKitC-class board + PN532 breakout + Y65592 | PN532 over SPI; USB for flash / monitor; Wi-Fi + BLE for Matter commissioning; optional GPIO IRQ / RST | Single USB-powered bench rig; ESP32 dev board is the power source; PN532 is fed from the same rig and shares ground; SPI side must stay 3.3 V-safe | Single MCU host directly drives the NFC reader over wires | Best current choice for `homekey-esp32/` regression, `aliro-esp32/` platform bring-up, Matter work in TODO §7.1-§7.2, and first Aliro NFC work in §7.3 | Manual wiring, clone-to-clone PN532 variation, older PN532 software model, weaker RF reproducibility than an official eval kit |
| LilyGO T-ETH-Lite-ESP32S3 + PN532 breakout | PN532 over remapped SPI pins; onboard W5500 Ethernet over its own SPI wiring; USB for first flash, LAN afterwards for WebUI / OTA / log access | Single-board field rig powered over USB-C or external 5 V; Ethernet and PN532 share the same ESP32-S3 host but not the same usable GPIO set | Single MCU host with an onboard Ethernet sidecar | Best current choice when the frozen `homekey-esp32/` baseline must stay maintainable over LAN without relying on Wi-Fi STA | W5500 consumes GPIO `9/10/11/12/13/14`, so HomeKey default PN532 pins collide; no confirmed MCU-controlled user LED; board-specific PN532 remap must be preserved per deployment |
| NXP OM27160 / PN7160-EVK | PN7160 host link over I2C or SPI; Arduino / Raspberry Pi style interface boards in the kit; NCI software model instead of PN532 commands | Eval-kit-managed power path, not yet frozen in repo docs because the exact in-hand variant is unknown; treat voltage and level assumptions as kit-specific until verified on arrival | External host talks to a dedicated NFC controller through a standard host interface | Best near-term hardware for comparing the current PN532 path against a more modern NCI-based NXP stack after the current ESP32 + PN532 MVP is stable | Exact board variant still unknown, first host choice still open, transport choice still open, and little direct code reuse from the current PN532 command path |
| NXP OM27642 / OM27642EVK | PN7642 NFC board plus LPCXpresso55S16 host board; host side talks over I2C or SPI; PC-side tooling via NXP NFC Cockpit and reader library | Two-board evaluation platform with its own host board and RF board; not a drop-in power or wiring replacement for the current USB bench rig | Split secure-reader architecture: dedicated secure NFC controller board plus separate host MCU board | Best long-term reference for production-style Aliro / secure-reader architecture, RF tuning, card detection behavior, and post-MVP platform decisions | Real platform migration, new HAL assumptions, different bring-up flow, and limited direct reuse of the current direct-wired ESP32 + PN532 design |
| Y65592 screw-terminal adapter | No protocol interface; passive terminal breakout only | Pass-through only; it does not regulate or translate anything | No host role | Only useful as a mechanical aid in the current bench rig | Loose terminals, wiring mistakes, and contact quality issues; no firmware-level risk by itself |

### Reading the matrix correctly

- The first row is the real working rig today. It is the only option that
  already satisfies the repo's regression loop and current Aliro bring-up.
- The second row is the current field-service variant of that baseline: it
  trades bench simplicity for LAN maintainability on `homekey-esp32/`.
- OM27160 is the most realistic next experiment if the goal is to compare
  PN532 against a modern NXP controller without jumping straight into a
  full secure-reader platform rewrite.
- OM27642 is not an "upgrade board" for the current rig. It is a future
  architecture study target.

## Recommended Choice By Project Task

| Project task | Recommended hardware | Why |
|---|---|---|
| Keep `homekey-esp32/` as a known-good regression baseline | ESP32-S3 + PN532 + Y65592 | This is the already-proven path with working iPhone HomeKey taps. Changing hardware here would break the whole regression gate. |
| Keep a field-serviceable HomeKey baseline reachable over LAN | LilyGO T-ETH-Lite-ESP32S3 + PN532 + W5500 preset | This is the current repo-local maintenance board: DHCP LAN, WebUI, OTA, and mDNS were already verified, so it is the right choice when USB access is awkward. |
| Execute TODO §7.1 platform bring-up for `aliro-esp32/` | ESP32-S3 + PN532 + Y65592 | The whole plan explicitly says "same MCU, same PN532 wiring, same GPIO map - swap firmware only". This keeps the variable under test in firmware, not hardware. |
| Execute TODO §7.2 Matter DoorLock + ALIRO feature work | ESP32-S3 + PN532 + Y65592 | DoorLock cluster, commissioning, HTTP API, MQTT, and telemetry work do not need new NFC hardware yet. The stable host board matters more than the future reader family at this stage. |
| Execute TODO §7.3 first Aliro NFC transaction on the bench | ESP32-S3 + PN532 + Y65592 first, OM27160 second | First get a transaction path working on the known rig. After that, OM27160 becomes useful as a comparison target for host-controller integration and NCI migration experiments. |
| Evaluate whether a modern NXP controller simplifies future integration | OM27160 / PN7160-EVK | This is the right step if the question is "what does NCI buy us compared with PN532?" without yet committing to a PN7642-class architecture shift. |
| Study a production-style secure-reader direction for later phases | OM27642 / OM27642EVK | This is the most relevant official NXP reference once the question changes from "get the prototype working" to "what should the next architecture look like?" |
| Improve mechanical repeatability of the current bench rig | Keep Y65592 or replace it with a cleaner harness, but do not treat it as a platform change | This is a wiring-quality decision, not a controller or reader decision. |

### Practical decision rule for this repo

For the current roadmap, the right order is:

1. Keep shipping the ESP32-S3 + PN532 rig through TODO §7.1-§7.3.
2. Keep the T-ETH-Lite board as the field-serviceable `homekey-esp32/`
   baseline when LAN access matters, but do not let it replace the simpler
   DevKitC-class Aliro bench target yet.
3. Use OM27160 only after the current path is stable enough that a second
   NFC stack will answer a concrete question instead of multiplying
   unknowns.
4. Treat OM27642 as a later architecture study, not as a near-term bench
   replacement.

## ESP32-S3-WROOM-1 DevKitC-Class Board

### What it is

The current host board is an ESP32-S3 development board built around an
ESP32-S3-WROOM-1 module. In practice this is a DevKitC-class board with
two USB connectors: one path to the ESP32-S3 native USB peripheral and a
second path through a USB-UART bridge used for flashing and serial logs.

### Key technical characteristics

- MCU family: ESP32-S3
- CPU: dual-core Xtensa LX7 class MCU, up to 240 MHz
- Wireless: 2.4 GHz Wi-Fi plus Bluetooth 5 LE
- USB: native USB on-chip, which is useful for flashing, console output,
  and future HID / accessory experiments
- GPIO domain: 3.3 V logic
- Powering in this repo: typically from USB 5 V, with the dev board
  regulating down for the module and exposing 5 V and 3V3 rails on the
  headers
- SDK ecosystem: ESP-IDF, which is also the base expected by both the
  frozen HomeKey baseline and the active Aliro firmware work

### Why this board is used here

- It is the stable host platform already proven with the vendored
  `homekey-esp32/` baseline.
- It exposes enough free GPIOs to dedicate clean SPI lines to the NFC
  reader while still leaving room for future lock control, LEDs, sensors,
  and diagnostic pins.
- Native USB matters for fast bench iteration. The second USB-UART path is
  useful when native USB configuration changes or crashes.

### GPIO usage in the current rig

The current default pin map for the PN532 path is:

- `GPIO10` -> PN532 chip-select (`NSS`)
- `GPIO11` -> SPI MOSI
- `GPIO12` -> SPI clock
- `GPIO13` -> SPI MISO
- PN532 `IRQ` is not connected to ESP32 in the current Aliro SPI driver
- PN532 `RST` is not driven by a GPIO; follow the module power/reset requirements
- `GPIO8` -> shared I²C `SDA` for ByteButton and OLED
- `GPIO9` -> shared I²C `SCL`; do not connect PN532 reset here
- `GPIO48` -> built-in RGB indicator on the configured target; no relay pin is assigned

This mapping is intentionally conservative: it avoids boot-strap and flash
signals and keeps the NFC wiring easy to reason about during bring-up.

### Important caveats

- The exact module SKU on a given board may be `-N8`, `-N8R8`, or another
  flash / PSRAM combination. The repo does not currently freeze that SKU in
  documentation.
- If a board with PSRAM is later confirmed, that only changes available
  memory; it does not change the external NFC wiring model documented here.

## LilyGO T-ETH-Lite-ESP32S3

### What it is

This is an ESP32-S3 board with an onboard W5500 Ethernet controller. In
this repo it is not the primary Aliro development target; it is the
current field-serviceable `homekey-esp32/` host because it can be
provisioned once over USB and then maintained over LAN.

### Key technical characteristics

- MCU family: ESP32-S3
- Onboard network controller: W5500 Ethernet MAC/PHY over SPI
- Verified Ethernet preset in `homekey-esp32/`:
  `T-ETH-Lite-ESP32S3`
- W5500 GPIO usage in this repo context:
  - `GPIO9`  -> W5500 CS
  - `GPIO10` -> W5500 SCK
  - `GPIO11` -> W5500 MISO
  - `GPIO12` -> W5500 MOSI
  - `GPIO13` -> W5500 interrupt
  - `GPIO14` -> W5500 reset

### Why this board matters here

- It keeps the frozen `homekey-esp32/` baseline reachable via DHCP LAN,
  WebUI, OTA, and mDNS when USB access is inconvenient.
- It is the shortest path from "bench-only prototype" to "device that can
  be serviced over the network" without inventing new firmware features.

### PN532 implications

The default HomeKey-ESP32 PN532 wiring collides with the onboard W5500,
so Ethernet-enabled deployments on this board must use a custom PN532
SPI remap.

Current repo-local transport-verified starting point:

- `SS`   -> `GPIO18`
- `SCK`  -> `GPIO15`
- `MISO` -> `GPIO16`
- `MOSI` -> `GPIO17`

That remap got the PN532 transport back to a healthy state on the LAN
baseline. Treat it as a known-good starting point, then re-verify after
every board / PN532 module swap.

### Important caveats

- The board's visible RJ45 LEDs are Ethernet link / activity indicators,
  not a confirmed MCU-controlled status LED.
- This board is excellent for the frozen HomeKey baseline but is not yet
  the preferred `aliro-esp32/` target because the current Aliro tree does
  not implement Ethernet.

## PN532 Breakout Boards

### What they are

The current NFC front-end is a family of breakout boards built around the
NXP PN532. The repo uses both original modules and Chinese clones. At the
silicon level they are still PN532-based readers; the differences are in
antenna tuning, board layout, voltage translation, regulators, connector
labels, and general assembly quality.

### PN532 chip-level characteristics

- NFC operating frequency: 13.56 MHz
- Device type: integrated NFC controller / transceiver
- Embedded controller: 80C51-based core inside the PN532
- Operating modes: reader / writer, card emulation, and peer-to-peer
- Protocol support: ISO/IEC 14443 A/B, FeliCa, ISO/IEC 18092 NFC-IP1,
  and common MIFARE-family workflows exposed through PN532 firmware
- Position in lifecycle: legacy / older generation part; good for
  prototyping, but not the direction NXP is pushing for new NCI-centric
  designs

### Board-level characteristics used in this repo

- Host interface options on the module family: SPI, I2C, and HSU/UART
- Repo-selected host interface: SPI only
- Wiring style in this repo: 6 required lines plus optional `IRQ` and `RST`
- Powering in the current rig: from the ESP32 board's 5 V rail
- Logic expectations: the SPI path must be compatible with a 3.3 V host
  because the ESP32-S3 is a 3.3 V device

### Original boards vs Chinese clones

The repo intentionally treats these boards as the same integration class,
but not as electrically identical units.

What usually stays the same:

- PN532 silicon and command set
- 13.56 MHz NFC function
- exposure of SPI / I2C / UART-style interfaces
- overall header naming close to `SCK`, `MISO`, `MOSI`, `NSS`, `IRQ`, `RST`

What often changes between vendors and clone batches:

- antenna matching network quality
- RF field strength and read range
- regulator choice and heat behavior
- level shifter implementation or absence of it on some signal paths
- default interface mode out of the box
- DIP switch placement, solder-jumper layout, or silk labels

That distinction matters operationally: if one clone behaves worse than an
original board, the problem is usually board-level RF or power integrity,
not a firmware-level difference in PN532 command semantics.

### Interface selection notes

The repo assumes SPI mode for all current boards. On the V3-style boards in
this rig, SPI is selected with `SET0=H` and `SET1=L`. Other board revisions
may use different physical selectors while still ending up in the same PN532
SPI operating mode.

### Why PN532 is still in active use here

- It already works end-to-end with the frozen `homekey-esp32/` baseline.
- It is easy to wire directly to an ESP32 without a second host processor.
- It is good enough for current NFC transaction development and regression
  checks while the Aliro controller stack is still under construction.

### Known limitations for long-term direction

- It is not the cleanest fit for a modern NCI-based NXP software stack.
- Clone variability makes reproducibility worse than with an official eval
  kit.
- For future production-like work, PN7160 and especially PN7642 are closer
  to the hardware families that NXP currently positions for new designs.

## Y65592 Screw-Terminal Adapter

### What it is

This is a passive breakout / terminal board used only to make the bench rig
solderless and reversible.

### Technical relevance

- No active silicon
- No protocol translation
- No voltage conversion
- No RF function

Its only job is mechanical wiring convenience. If the rig becomes flaky,
this adapter is worth checking for loose screw terminals, but it is not a
firmware or protocol variable.

## NXP OM27160 / PN7160-EVK

### What it is

OM27160 is NXP's evaluation family for the PN7160 Plug'n Play NFC
controller. NXP positions it as a development kit for fast host-side NFC
integration rather than as a finished access-control design.

### Official kit composition

According to NXP, the PN7160 customer development kit includes:

- a PN7160 controller board
- Arduino and Raspberry Pi interface boards
- a sample card

Standalone controller boards are also sold separately, but the repo tracks
the development-kit class under the OM27160 / PN7160-EVK naming.

### Key technical characteristics

- NFC controller family: PN7160
- Host interfaces: I2C or SPI
- Header compatibility: Arduino and Raspberry Pi style expansion
- Software ecosystem: Linux, Android, FreeRTOS, and no-OS flows are all
  called out by NXP
- Software model: NCI-based NFC controller integration rather than a
  PN532-style command set

### Why it matters to this repo

- It is a practical bridge between the current PN532 bench setup and a more
  modern NXP NFC stack.
- It should be better aligned with reusable host integration work than the
  older PN532 family.
- It is a reasonable intermediate evaluation target before committing to the
  more secure PN7642 path.

### Current status in this repo

- Ordered, not yet delivered
- No code in this repo currently targets PN7160 directly
- No pinout or host-transport decision has been frozen yet for this kit

### Integration questions to answer when it arrives

- Which exact OM27160 variant is on hand
- Whether the first bring-up path will be I2C or SPI
- Whether it will be driven directly from ESP32 or first validated with a
  Linux / Raspberry Pi-class host
- What level-shifting or power-domain constraints apply in the chosen setup

## NXP OM27642 / OM27642EVK

### What it is

OM27642 is the official NXP development-kit family for the PN7642 secure,
compact NFC controller. This is the closest official hardware reference in
this repo for a future PN7642-native secure-reader direction.

### Official kit composition

NXP describes the OM27642EVK as including:

- a PN7642-based NFC board
- a 45 x 45 mm antenna on the NFC board
- Arduino connector exposure on the NFC board
- an LPCXpresso55S16 host development board
- an NTAG 424 DNA tag
- an RF field detector

### Key technical characteristics

- NFC controller family: PN7642
- Host side: LPCXpresso55S16 board connected over I2C or SPI
- RF features: onboard dynamic power control for RF tuning / optimization
- Detection features: low-power and ultra-low-power card detection
- Software ecosystem: supported by NFC Cockpit on PC and by NXP's NFC
  Reader Library
- Target application space called out by NXP: access management and smart
  lock designs

### Why it matters to this repo

- It is the most relevant official NXP reference for a secure-reader
  architecture rather than just a generic NFC dev module.
- It is closer to the kind of hardware split expected in production access
  systems: dedicated secure NFC controller plus a host MCU board.
- It is the clearest board family to study if the repo moves from
  ESP32-plus-PN532 prototyping toward a PN7642-native design.

### Current status in this repo

- Reference hardware only
- Not currently used in bench bring-up
- No local firmware, BSP, or wiring notes assume OM27642 ownership yet

### Architectural impact if adopted later

Moving to OM27642-class hardware would be a real platform shift, not a
simple reader swap. Expected changes would include:

- a different NFC controller programming model
- different host-side HAL and transport assumptions
- different RF tuning and power-management behavior
- likely migration away from the current direct ESP32-to-PN532 wiring model

## Practical Summary For This Repo

- Current real bench work: ESP32-S3 DevKitC-class board + PN532 breakout
  boards via SPI
- Near-term incoming hardware: NXP OM27160 / PN7160-EVK
- Longer-term official secure-reader reference: NXP OM27642 / OM27642EVK

When documentation or TODO items mention "the hardware rig" without further
qualification, they mean the ESP32-S3 + PN532 SPI bench setup, not the NXP
evaluation kits.