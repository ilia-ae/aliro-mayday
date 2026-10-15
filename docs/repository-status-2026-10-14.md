# Repository readiness

[Русский](repository-status-2026-10-14.ru.md)

14 October 2026.

The public repository is [ilia-ae/aliro-mayday](https://github.com/ilia-ae/aliro-mayday).
It has new Git history. The old repository remains private. Public `main` includes
the document rights/revocation implementation and current bilingual documentation
integrated through [PR #1](https://github.com/ilia-ae/aliro-mayday/pull/1), plus
provisioning/persistence fixes from [PR #2](https://github.com/ilia-ae/aliro-mayday/pull/2). The original
source-snapshot prerelease remains the initial sanitized snapshot. Hardware
acceptance is still pending: merging source is not a firmware deployment or Aliro
certification. A full private backup and an application-only update were verified on the
connected controller. The initial build exposed a startup memory shortage before
NFC; an application-only rollback restored the old firmware without data writes.
Updated-firmware startup and phone acceptance are tracked separately.

| Included source | Current role and verification |
| --- | --- |
| `aliro-esp32/` | Active firmware; builds on the installed pinned SDKs; NFC adapter and document policy tested on the host |
| `homekey-esp32/` | Frozen reference, unchanged by this update; original MIT license retained |
| `matter-controller/` | Controller example; Wi-Fi comes from local build configuration; changed object compiled, full image not validated here |
| `android-smartkey/`, `smartkey-esp32-patch/` | HCE fallback scaffolds; not a working Samsung Wallet integration |
| Root Python tools | PM3 research/UID-lock tools; their four tests passed; not Aliro tests |
| `aliro-site/` | Current English/Russian prototype status, public source links and browser tests; hosting is deployed separately |
| DigitalDoorKey | Pinned MIT source included, with local fixes and tests |
| UltraWideLock | Only its portable CBOR cursor/walker imported under ISC; local validator/policy is separate |
| esp-aliro, Nordic, ST, Silicon Labs, NXP, Python Aliro Reader, openaliro | Researched/referenced; not bundled or enabled as alternate backends |

Public release files are listed by SHA-256 in `SOURCE_MANIFEST.json`. The
reviewed 47 secret-scanner candidates are unchanged upstream fixtures/markers;
that classification is tied to file hashes, not a general exception for keys.
No private Git history, live runtime keys, local environment, SDK configuration,
logs or CSA PDF is part of the snapshot. Each bundled license retains its scope;
project-original code has commercial source-available terms, so the whole
repository is not OSI Open Source.

Verification covers 77 targeted scenarios: 36 firmware integration, 22 provisioning, nine
cryptographic document-validation and ten rights/revocation cases, plus one empty-CBOR encoder regression. The supported
suite passes 15/15 executables with AddressSanitizer/UndefinedBehaviorSanitizer.
The full suite is 15/16, with two known upstream BLE-flow failures. These do not
establish hardware timing, actual wallet behavior or power-loss durability.
The application builds at 2,564,864 bytes, leaving 2.16% in its OTA slot.

The repository now contains a GitHub Actions check for manifest/fixture integrity,
current local documentation links and the supported host suite. It explicitly
excludes the known failing BLE-flow executable. Firmware SDK builds and wallet
tests are not part of that workflow. Integrity checking is not a secret scanner.

Remaining acceptance/work: real old-key iPhone tests, Samsung Wallet via
SmartThings, clean-machine firmware build, physical relay validation, optional
schedule/reader-rule provisioning and certificate-chain support, BLE failures
before enabling that transport, UWB hardware.
A valid clock is required for document-bound access. Revocation applies after
its signed document is delivered; it is not an online revocation service.

The current overview, wiring diagrams, build/rollback guide, policy, tests and competitor review
have English/Russian versions. Historical and upstream docs are not all
translated; the standard's full Russian translation is absent. See the
[documentation coverage index](README.md).

## Samsung Wallet: implementation and acceptance

The generic Aliro 1.0 NFC engine, Matter reader/issuer/device credential operations
and document rights/revocation are implemented and host-tested. Our controller
has not issued a Samsung Wallet key through SmartThings or completed a real
Samsung tap/revocation test. This is not a completed Samsung-specific integration.
Samsung describes onboarding a compatible lock through SmartThings/Matter, then
adding its key to Wallet; availability varies by device and region.
[Official Samsung workflow](https://news.samsung.com/global/samsung-wallet-launches-digital-home-key-for-smart-door-locks).

The next proof is SmartThings onboarding → Wallet key issuance → first/repeated
NFC taps → clear/disabled-user/reboot rejection, with old iPhone keys preserved.
An alternative SDK can help implement the protocol; its source availability
does not prove that SmartThings will accept this reader or issue its Wallet key.

[Samsung bench procedure](samsung-bench-preparation-2026-10-14.md).
