# Execution Plan

> Historical bring-up/roadmap reference. Its status and assumptions predate the October document-policy update. Use the [current documentation](README.md) for verified behavior, build instructions and remaining acceptance work.

This is the shortest sensible path from the current repo state to a real
Aliro controller proof on the existing hardware rig.

It assumes the project keeps the current ESP32-S3 + PN532 bench setup as
the primary platform until the local Aliro transaction path is proven.

## Goal

Reach a state where:

- the current `aliro-esp32/` firmware commissions reliably,
- accepts Aliro provisioning through Matter,
- completes a real NFC transaction on the ESP32-S3 + PN532 rig,
- drives the lock action locally,
- and emits enough telemetry to debug failures without guesswork.

## Ordered Plan

1. **Freeze the current documentation / code contract.**
   Outcome: README, TODO, protocol docs, and debug docs all describe the
   same hardware map, provisioning caps, and recovery flow.

2. **Pin and record a known-good esp-matter toolchain.**
   Outcome: a fresh checkout can build `aliro-esp32/` without reverse-
   engineering the environment.

3. **Re-verify the regression baseline on `homekey-esp32/`.**
   Outcome: the ESP32-S3 + PN532 rig is confirmed sane before any new
   Aliro debugging begins.

4. **Exercise the current Matter provisioning path end-to-end.**
   Outcome: `SetAliroReaderConfig`, user storage, and credential storage
   are proven against a real commissioner, not just inferred from code.

5. **Stabilize the ported PN532 transaction layer in `aliro-esp32/`.**
   Outcome: the already-landed polling / ECP / tag-detect path is proven
   on real phones and no longer treated as a speculative port.

6. **Finish the live Aliro auth path on real wallets.**
   Outcome: cold-tap `AUTH0 = 6984` is treated as Step-up instead of
   failure, the FCI / key-schedule inputs are correct, and the first
   successful cold tap seeds the warm FastAuth path.

7. **Bind the transaction result to the controller action.**
   Outcome: a successful Aliro authentication produces an actual unlock
   path and a failed one produces a structured deny path.

8. **Run real-phone validation on Samsung Fold 7 and iPhone.**
   Outcome: the project is proven in the environments it claims to target,
   including negative cases such as revoke / deny / retry.

9. **Add the first controller surfaces: HTTP status + WebSocket events +
   structured logs.**
   Outcome: failures can be diagnosed without only staring at raw serial
   logs.

10. **Add MQTT / metrics / reset / OTA only after the local core is
    stable.**
    Outcome: the product starts to look operational rather than like a lab
    demo.

## What To Delay On Purpose

- OM27160 / PN7160-EVK evaluation until the current PN532 path can serve
  as a stable baseline
- OM27642 / PN7642-native work until there is a concrete reason to change
  architecture
- BLE + UWB / `ALBU` work until expedited NFC is proven
- Telegram / bridge / cloud workflow until the local controller loop is
  reliable

## Definition Of "Good Enough To Move On"

You should only move from one block to the next when the previous one is
true in practice, not just in source code.

Minimum acceptable checkpoint:

- `homekey-esp32/` still unlocks on the current rig
- `aliro-esp32/` builds reproducibly
- Matter commissioner can provision reader config and credentials
- at least one real phone can tap and trigger an unlock decision
- logs are rich enough to explain failure without guesswork

If one of those is false, keep working on the current block instead of
opening a new front.