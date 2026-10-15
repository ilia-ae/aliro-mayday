# aliro.mayday

[Русский](README.ru.md) · [Documentation index](docs/README.md) · [Wiring diagram](docs/wiring.md)

Experimental Aliro NFC access controller for ESP32-S3 + PN532, with Matter
DoorLock provisioning and local administration. This repository began as a clean
source snapshot dated **13 October 2026**, with new Git history.

## Verified status

- On 15 October 2026, the owner confirmed the existing iPhone key works after
  the application-only update. The current boot records three authenticated taps,
  zero tap failures and both locked and unlocked Matter states. The verified
  private backup, saved key and pairing were preserved. Matter, PN532 NFC and
  HTTP/WebSocket administration are running. The installed HTTP startup/disconnect
  fixes and acceptance report are in [PR #6](https://github.com/ilia-ae/aliro-mayday/pull/6),
  awaiting merge. Samsung Wallet validation through SmartThings remains pending.
- The updated firmware builds with ESP-IDF v5.4.4 / esp-matter 8d56152.
  Fifty-eight firmware scenarios pass, including 22 actual provisioning callback
  cases and old iPhone records,
  signature rejection, credential revocation, reboot and persistence failures.
- DigitalDoorKey is pinned to
  `80e4a1e14eab3646b62ab3bdad6d2ed01d399375`; local patches are documented in
  [UPSTREAM.md](aliro-esp32/components/DigitalDoorKey/UPSTREAM.md).
- Step-up validates signed AccessData before learning a device key from a
  provisioned issuer. Cached document keys re-check current permissions and expiry.
  Signed `aliro-r` revocations are stored independently of successful device access
  and enforced after reboot. [Policy and limits](docs/aliro-access-policy-2026-10-14.md)
  describe strict handling of unsupported schedules/extensions and missing clocks.
- Existing Apple v9 framing, identity, pairing and independent iPhone keys are
  preserved. The current saved-key iPhone bench test passed; on-device revocation
  and reboot tests using a separate test key remain required.
- This is a development prototype. Aliro certification, Samsung/Google Wallet
  compatibility and a physical relay output are not established by this release.
  BLE/UWB access transport is not enabled in this firmware; Matter BLE
  commissioning remains available.

The full host suite has two known upstream BLE-flow test failures;
[testing notes](aliro-esp32/tests/README.md) explain the scope.
[October update](docs/ddk-update-2026-10-13.md) records the changes and remaining
hardware acceptance checks.

Public `main` includes the document-policy fixes, signed revocation and bilingual
guides integrated through [PR #1](https://github.com/ilia-ae/aliro-mayday/pull/1),
plus provisioning/persistence fixes from [PR #2](https://github.com/ilia-ae/aliro-mayday/pull/2).
The original source-snapshot prerelease remains unchanged. Publishing this source
does not establish real-wallet compatibility or deploy firmware to a controller.
[Repository readiness](docs/repository-status-2026-10-14.md) records
what is included, translated and still awaiting verification.

Samsung provisioning preparation and the V4_HUB/Fold7 bench procedure are
documented [here](docs/samsung-bench-preparation-2026-10-14.md). Real Wallet
key issuance remains unproven; Fold7 testing is deferred.

## Build and test

Follow [firmware setup and build instructions](aliro-esp32/README.md). Build the
Svelte WebUI from `aliro-esp32/data/` before the firmware; local `sdkconfig`,
managed components and generated binaries are excluded from this source release.

```sh
cmake -S aliro-esp32/components/DigitalDoorKey/tests -B /tmp/aliro-ddk-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/aliro-ddk-tests --parallel
ctest --test-dir /tmp/aliro-ddk-tests --output-on-failure
```

Keep existing reader identity, Matter pairing and NVS data when testing an
update on the bench. The legacy `homekey-esp32/` tree is an unchanged reference.

## Source layout

| Directory | Purpose |
| --- | --- |
| `aliro-esp32/` | Active ESP32-S3 + PN532 Aliro firmware and host tests |
| `homekey-esp32/` | Frozen HomeKey reference firmware |
| `matter-controller/` | Matter controller example; Wi-Fi is local build configuration |
| `android-smartkey/`, `smartkey-esp32-patch/` | Separate Android HCE fallback experiments |
| Root Python tools | Earlier Proxmark3 reader / UID-lock research tools |
| `aliro-site/` | Website source, matching EN/RU prototype status and public-code links |
| `docs/` | Protocol notes, acceptance checks and research |

[Historical workspace notes](README.history.md) preserve the earlier roadmap.
Use the verified status above for the current release.

## Related implementations and product comparisons

[Competitor and implementation review](docs/aliro-competitors-2026-10-13.md)
compares Aqara, Nuki, Schlage, esp-aliro, UltraWideLock and other vendor SDKs,
with source links and practical priorities for this project.

## License and provenance

Original Aliro application code uses [source-available commercial terms](LICENSE).
MIT and other third-party rights remain in force; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Public availability does not
make the whole repository OSI Open Source.

The CSA specification PDF is not redistributed. `SOURCE_MANIFEST.json` records
the release's files and SHA-256 hashes. The source has been reviewed for personal
credentials; [the review](docs/public-source-review-2026-10-13.json) classifies
upstream public test keys and marker strings by file hash. The old development
repository and its history are not included.

Project website: [aliro.mayday.software](https://aliro.mayday.software/).
