# Samsung Wallet bench preparation

[Русский](samsung-bench-preparation-2026-10-14.ru.md) · [Documentation index](README.md)

14 October 2026. Target bench: SmartThings V4_HUB firmware 0.62.7 and Galaxy Z
Fold7. Network/device identifiers and account data are not in the repository.
A connected controller was backed up and an application-only update attempted
without NVS or pairing writes. Its startup memory shortage is being addressed;
Samsung Wallet integration is not yet proven. Fold7 testing is deferred.

## Completed code work

- Shared limits drive both Matter attributes and storage. All 16 announced
  endpoint slots can be enumerated and provisioned; old slots 1–8 remain readable.
  Individual legacy user/credential blob layouts are unchanged; three credentials
  per user remain the limit.
- Failed SetUser persistence cannot publish new grants in RAM. Requested
  disable keeps previous assignments denied in RAM and reports failure.
  Invalid/oversized requests are not silently truncated.
- Aliro public keys are checked as P-256 points before storage. The reader signing
  key must correspond to the verification key.
- New reader configuration uses one `readerCfg1` blob. Legacy fields load without
  boot migration. The stable sub-ID remains in `subId`; an invalid/unreadable
  existing value is not replaced with a random identity.
- Reader config/NFC mirror errors report failure. Failed mirror attempts restore
  the previous reader configuration or Matter credential record; rollback failure
  is not reported as success.
- Explicit ClearReaderConfig persists a clear marker, erases legacy configuration
  fields and clears the NFC mirror, retaining the sub-ID. The marker prevents
  fallback to old keys. Failed storage does not promise denial across power loss.
  Old software cannot read the new format: downgrade requires compatible private
  backups rather than replacing the application alone.
- System time sync starts independently of OLED. Document-bound access still
  requires a usable clock; old independent iPhone keys do not.
- Final unlock also requires a successfully provisioned reader configuration.
  Legacy Apple v9 APDU and cryptographic framing are unchanged.

## Offline verification

22 new cases compile the actual AliroDelegate, DoorLockUserStubs, DDKAuthContext
and ReaderDataManager. SDK boundary types, hardware hooks and NVS API are
substituted. Coverage includes slot enumeration, slot 16 grant/read-back,
open/write/mirror failures, rollback, old fields, stable sub-ID, malformed points,
disabled users, Standard → Fast → reboot → revocation. The simulated phone derives
its persistent key itself instead of copying the reader's saved key.

The supported ASan/UBSan suite passes 15/15 executables; the full suite is 15/16
with the same two upstream BLE-flow failures. It does not test actual Samsung,
SmartThings commissioning, secure elements, radio timing or sudden power loss.

ESP-IDF 5.4.4 / esp-matter 8d56152: application **2,564,864 bytes**, leaving
**56,576 bytes (2.16%)** in the unchanged OTA slot. SHA-256:
`dc8b7408b67c80520963468e48be29675f56152bb23d735229d5d49c81bf2977`.
Public repository contains source; the bench application package is local.
No alternate SDK/backend was enabled.

## When the controller is connected

1. Read existing firmware/partition/reader readiness and check the old iPhone
   key first. Privately back up the known-good application, configuration, NVS and
   pairing. Do not erase flash/NVS or change eFuses. Installing an update is a
   separate action after this check.
2. Use an application-only update compatible with the actual partition table.
   Check the old iPhone, repeated taps and reboot after updating.
3. Add SmartThings as another Matter controller through multi-admin / the existing
   device's commissioning window. Retain the Apple fabric and reader identity;
   adding a second controller does not call for factory reset.
4. Check Add Digital Home Key, issue a Fold7 key and read back users/credentials.
   Record SmartThings/Wallet versions and account region privately. A hub or
   successful Matter unlock does not prove Wallet readiness.
5. Check first/repeated NFC taps, reboot, disabled user, ClearCredential,
   post-reboot rejection and explicit reprovisioning. Check iPhone again. Physical
   lock output is a separate acceptance item; host tests cover logical state.

Samsung's workflow is a compatible lock in SmartThings/Matter, then a Wallet key.
Availability depends on device/region. Certified-product acceptance does not
prove acceptance of a prototype using test VID/PID; do not impersonate another
manufacturer's identifiers. If Wallet onboarding is absent, diagnose provisioning
and service availability before the NFC transaction. [Samsung](https://news.samsung.com/global/samsung-wallet-launches-digital-home-key-for-smart-door-locks).

Official [hub release notes](https://community.smartthings.com/t/hub-firmware-and-edge-driver-release-notes-0-62-7/310908)
identify the 0.62.13 rollout with Matter OTA/local connectivity fixes. This work
has not updated the hub; record its actual version before acceptance.

Remaining document-policy limits are in [access policy](aliro-access-policy-2026-10-14.md).
Schedules, provisioned reader-rule dictionaries, PKI chains, Aliro BLE/UWB and
certification are not added by this stage.
