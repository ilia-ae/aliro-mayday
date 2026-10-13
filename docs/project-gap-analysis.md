# Project Gap Analysis

This file answers a practical question: what is still missing for the
project to reach its stated goals?

It is tied to the current roadmap in [TODO.md](../TODO.md) section 7,
the Aliro wire contract in [PROTOCOL-aliro.md](../PROTOCOL-aliro.md), the
project summary in [README.md](../README.md), and the actual code already
present in `aliro-esp32/`.

This is not a generic wishlist. It is a gap register for the current
project direction.

## Executive Summary

- The current ESP32-S3 + PN532 rig is sufficient for the next meaningful
  milestones. The biggest missing pieces are no longer new boards; they
  are firmware, protocol, controller-surface, and real-device validation
  work.
- OM27160 / PN7160-EVK is the right next hardware experiment only after
  the current PN532 path can answer a stable baseline question.
- OM27642 / OM27642EVK is not the next incremental step. It is a later
  architecture branch for a PN7642-native secure-reader design.
- The highest-value missing work today is: finish the Aliro NFC
  transaction path on the current rig, prove end-to-end unlocks with real
  wallets, then add the controller surfaces around that core.

## What Already Exists

Before listing gaps, it is important to state what is already real.

### Working baseline already available

- `homekey-esp32/` is the proven regression baseline on the current
  ESP32-S3 + PN532 rig.
- The current bench rig is known-good for HomeKey work and remains the
  fastest way to distinguish hardware faults from new Aliro bugs.

### `aliro-esp32/` is beyond an empty scaffold

The current Aliro branch is not just an empty directory anymore. It
already contains:

- an ESP-Matter bootable project skeleton
- a DoorLock endpoint with PIN + USER + `AliroProvisioning` enabled
- an Aliro delegate that persists reader config in NVS
- DoorLock user / credential stubs with NVS-backed persistence
- an early PN532 / DDK NFC path: `NfcManager`, reader init, ECP payload
  updates, polling loop, tag detection, and `kAliro` auth-context
  precompute
- diagnostics and heartbeat infrastructure intended to feed later NFC and
  controller-level visibility

That means the project has already crossed from "idea" into "partially
implemented platform". The remaining gaps are specific and actionable.

## Full Gap Analysis

## 1. Current Bench Rig Gaps

These are gaps in the hardware baseline you are using right now.

### What is still missing

- The exact ESP32-S3 module SKU is still not frozen in the docs.
  Functionally this is not blocking, but it matters for memory planning,
  reproducibility, and release-grade notes.
- The current PN532 path is operational, but there is no measured RF
  baseline captured in repo docs for original boards vs Chinese clones.
  That leaves read-range and reliability differences as folklore instead
  of recorded data.
- The optional PN532 `IRQ` and `RST` lines are documented physically, but
  the Aliro path does not yet use them as part of a finished reader
  strategy.
- The rig is still a bench assembly, not a repeatable fixture. Mechanical
  stability and wiring hygiene can still create noise during bring-up.

### Why these gaps matter

None of them block TODO §7.1-§7.3 by themselves. They matter because they
slow debugging and reduce confidence when a failure could be either RF,
power, wiring, or firmware.

## 2. Platform Gaps For `aliro-esp32/` (TODO §7.1)

This is the lowest layer of the Aliro controller effort.

### Already present

- ESP32-S3 target project exists
- build artifacts exist in the workspace
- Matter boot + BLE commissioning path is already the intended shape

### Still missing

- Reproducible toolchain pinning still needs to be treated as a first-class
  concern. The project depends on esp-matter and upstream Matter behavior,
  so version drift remains a real risk.
- The current dev-vs-release security policy is still unfinished. Flash
  encryption and NVS encryption are discussed, but the project does not yet
  have a completed operational model for when and how those become active.
- The day-to-day regression workflow still depends on discipline around the
  `homekey-esp32/` baseline rather than a strongly enforced validation
  gate.

### Practical interpretation

The platform is real enough to work on, but not yet locked down enough to
call reproducible.

## 3. Matter Provisioning Gaps (TODO §7.2)

This is the part that lets Apple Home, Google Home, or SmartThings treat
the device as a standards-based Aliro-capable lock.

### Already present

- DoorLock endpoint creation exists
- `AliroProvisioning` feature is enabled
- reader config persistence exists via `SetAliroReaderConfig`
- user / credential storage stubs exist and persist through NVS

### Still missing

- The final provisioning story is not yet fully converged across TODO,
  protocol docs, and implementation details. The remaining gap is no
  longer the raw numeric caps; it is end-to-end ecosystem behavior and
  the final user / credential semantics under real commissioners.
- Real commissioner behavior still needs to be verified across the target
  ecosystems. A project can look correct at the cluster level and still
  fail on Apple Home or SmartThings because of ecosystem-specific
  expectations.
- The unlock-side event model is still incomplete. Provisioning exists as
  data flow, but the full controller behavior after a successful Aliro tap
  is not finished.
- The "user <-> credential <-> endpoint key" semantics need to be proven by
  real commissioning flows, not only by local stubs.

### Practical interpretation

The project has the start of a provisioning plane, but not yet a proven
multi-ecosystem provisioning result.

## 4. NFC Transaction Gaps (TODO §7.3)

This is the single biggest missing block for the main project goal.

### What is still missing

- The live iPhone path already reaches Aliro `SELECT` and `AUTH0`, so the
  main gap is no longer "port PN532". The remaining work is finishing the
  real auth sequence on actual wallets.
- Cold taps currently return `AUTH0 = 6984`, which means "no cached
  endpoint, enter Step-up". The vendored DDK guard still treats that as a
  failure and emits `cmdFlowFailed`.
- The current tree now parses the SELECT-response FCI `A5` template and
  feeds it into the DDK key schedule, and it now passes the full 16-byte
  `reader_group_sub_identifier`, but those fixes still need end-to-end
  wallet proof.
- First-tap endpoint persistence is not finished. A tentative `5A
  devicePk` insertion helper exists in StandardAuth, but the real Access
  Document / issuer-verification / persistence flow is still missing.
- Warm-tap FastAuth cannot be trusted until the cold path populates the
  endpoint cache and a first successful real-wallet unlock is observed.
- The lock actuator hook after `AUTH_OK` is wired, but still unproven in
  a real successful Aliro tap flow.

### Practical interpretation

Until this section is done, the project is not yet an Aliro lock
controller in practice. It is a commissioned Matter lock platform that
can already wake the Aliro applet and start the auth flow, but still
stops at the cold-tap Step-up boundary.

## 5. Controller Surface Gaps (TODO §7.8-§7.13)

The roadmap is explicit that the product is a controller, not just a
reader. That means the surrounding surfaces matter.

### Still missing

- Local HTTP API for status, manual control, credential management, and
  LAN-side automation
- WebSocket event stream for dashboards and live observation
- MQTT telemetry and alert fan-out
- Prometheus metrics surface
- outbound webhook integrations
- `aliro-bridge/` service for Telegram and long-term analytics

### Practical interpretation

Even if Aliro unlock starts working tomorrow, the broader controller
product is still missing its control plane and observability plane.

## 6. Security And Operations Gaps (TODO §7.6 and §7.10)

These are the missing pieces that determine whether the system is just a
demo or something operationally defensible.

### Still missing

- A coordinated factory reset path that clears Matter state and Aliro state
  together
- OTA workflow for updating the controller without bench reflashing
- completed replay / anomaly / tamper detection logic
- final confirmation that private material never escapes the intended
  storage boundary in deployed builds
- real latency baselines and anomaly thresholds from actual phones on the
  actual rig
- operational runbooks for recovery, reprovisioning, and fault isolation

### Practical interpretation

Security hardening is no longer just about crypto primitives. The project
still needs lifecycle controls.

## 7. Real Device Validation Gaps (TODO §7.5)

This is the proof layer.

### Still missing

- Real commissioning and unlock verification on Samsung Fold 7
- Real commissioning and unlock verification on current iPhone path for
  Aliro, not just HomeKey baseline behavior
- Cross-device validation with multiple enrolled devices against one lock
- Regression proof that the hardware stays sane when swapping firmware
  repeatedly between baseline and Aliro builds
- A recorded failure matrix covering expected bad cases: revoked key,
  timeout, replay, unknown endpoint, network loss, and reprovisioning

### Practical interpretation

Without this, the project may be internally coherent but still not proven
in the environments it claims to target.

## 8. Hardware Expansion Gaps

These are the missing pieces for the future NXP-controller branches.

### OM27160 / PN7160-EVK

Still missing:

- exact kit variant in hand
- first host choice
- first transport choice (`I2C` vs `SPI`)
- level / power-domain validation in the chosen setup
- success criteria for deciding whether PN7160 actually improves the
  project versus the current PN532 path

### OM27642 / OM27642EVK

Still missing:

- an explicit decision on whether it remains reference hardware only or
  becomes an actual implementation target
- a host-side software strategy for a PN7642-native path
- a migration plan from the current single-MCU bench design to a split
  controller architecture
- criteria for when that migration is worth the cost

### Practical interpretation

Both NXP kits still have value, but neither of them is the next blocker
to remove.

## What Is Not Missing Yet

This section is here to prevent wasted effort.

- You do not need a new board to continue TODO §7.1 and §7.2.
- You do not need OM27642 before proving the current PN532-based Aliro
  path.
- You do not need BLE + UWB hardware before the expedited NFC flow works.
- You do not need Telegram, webhooks, or cloud-side bridge logic before
  the local unlock path is real.

## Priority Order Based On Current Tasks

If the goal is maximum project progress with minimum wasted motion, the
recommended order is:

1. Finish doc / code / runtime convergence for the current `aliro-esp32`
   platform state.
2. Complete the Aliro NFC transaction path on the existing ESP32-S3 +
   PN532 rig.
3. Prove real-wallet end-to-end commissioning and unlock on that same rig.
4. Add controller surfaces around the working local core: HTTP, WS, MQTT,
   metrics, reset, OTA, alerting.
5. Only then evaluate OM27160 as a controlled comparison.
6. Treat OM27642 as a later architecture branch, not as the current
   delivery path.

## Bottom Line

What the project is missing most is not another board. It is the
completion of the Aliro transaction engine and the controller behavior
around it.

The current rig is good enough to reach that proof point. New hardware
starts paying off only after that core path becomes stable.

Recommended next actions are captured in
[execution-plan.md](execution-plan.md).