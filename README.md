# aliro.mayday

[![Aliro 1.0: experimental NFC subset](https://img.shields.io/badge/Aliro%201.0-experimental%20NFC-orange)](docs/aliro-access-policy-2026-10-14.md)
[![Matter: DoorLock prototype](https://img.shields.io/badge/Matter-DoorLock%20prototype-2864a0)](aliro-esp32/README.md)
[![ESP-IDF: tested v5.4.4](https://img.shields.io/badge/ESP--IDF-tested%20v5.4.4-00979D)](aliro-esp32/README.md)
[![iPhone: existing key bench-tested](https://img.shields.io/badge/iPhone-existing%20key%20bench--tested-2e7d32)](docs/repository-status-2026-10-14.md)
[![Source integrity and supported host tests](https://github.com/ilia-ae/aliro-mayday/actions/workflows/source-and-nfc.yml/badge.svg?branch=main)](https://github.com/ilia-ae/aliro-mayday/actions/workflows/source-and-nfc.yml?query=branch%3Amain)
[![Hardware: ESP32-S3 + PN532](https://img.shields.io/badge/hardware-ESP32--S3%20%2B%20PN532-00979D)](docs/wiring.md)
[![Status: prototype](https://img.shields.io/badge/status-prototype-orange)](docs/repository-status-2026-10-14.md)
[![Licensing: mixed; see guide](https://img.shields.io/badge/licensing-mixed-blue)](docs/licensing.md)
[![Documentation: EN / RU](https://img.shields.io/badge/docs-EN%20%2F%20RU-6f42c1)](docs/README.md)

[Русский](README.ru.md) · [Documentation index](docs/README.md) · [Wiring diagram](docs/wiring.md) · [Demo stand photos](docs/demo-stand.md) · [Web UI gallery and debugging](docs/controller-ui-debug.md)

Project-original work: **Mayday / Ilia Arestov**. [Authorship and upstream credits](AUTHORS.md).

Experimental Aliro NFC access controller for ESP32-S3 + PN532, with Matter
DoorLock provisioning and local administration. This repository began as a clean
source snapshot dated **13 October 2026**, with new Git history.

## Local web UI

[Open the live panel demo](https://aliro.mayday.software/demo/index.html?lang=en#/): sample data, no controller connection.
The shared panel supports EN/RU (English by default) and light, dark and system themes.

Run the actual interface with sample data: `cd aliro-esp32/data` → `bun run debug` (from the repository root). Requires Bun 1.3.13 and Node.js 22.12+; Node 24 LTS is recommended. The script does not connect to or flash a controller.

[Web UI gallery and debugging](docs/controller-ui-debug.md).

## Snapshot timestamp evidence

**DigiCert + FreeTSA (RFC 3161): verified.** Three OpenTimestamps calendar receipts
are included; a verified Bitcoin block is not yet included in the published package.
The evidence identifies snapshot [`24e218e`](https://github.com/ilia-ae/aliro-mayday/tree/24e218ef1ea83dafd47447e0393ddd5cd949ccaa)
dated 15 October 2026, credited to **Mayday / Ilia Arestov**.

[Proofs and verification](docs/provenance/2026-10-15/README.md) · [Download ZIP](https://aliro.mayday.software/proofs/aliro-snapshot-2026-10-15.zip) · [Manifest](docs/provenance/2026-10-15/authorship-manifest.json)

## Verified status

- On 15 October 2026, the owner confirmed the existing iPhone key works after
  the application-only update. The current boot records three authenticated taps,
  zero tap failures and both locked and unlocked Matter states. The verified
  private backup, saved key and pairing were preserved. Matter, PN532 NFC and
  HTTP/WebSocket administration are running. The installed HTTP startup/disconnect
  fixes and acceptance report were merged into `main` through
  [PR #6](https://github.com/ilia-ae/aliro-mayday/pull/6). Samsung Wallet validation through SmartThings remains pending.
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
The [licensing guide](docs/licensing.md) explains permitted use and the mixed
license scope. MIT/ISC/Apache/Boost/BSD rights remain in force. File-level
exceptions include a CC BY-SA UI helper and GPL Autoconf macros with an output
exception; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Public availability
does not make the whole repository OSI Open Source. **License v1.1** expressly
permits non-commercial modifications, forks and redistribution under its terms;
commercial-use and fee clauses are unchanged. New contributions require the
explicit grant described in [CONTRIBUTING.md](CONTRIBUTING.md), with copyright
retained by their authors. Artifact-wide commercial release review remains
separate work.

The CSA specification PDF is not redistributed. `SOURCE_MANIFEST.json` records
the release's files and SHA-256 hashes. The source has been reviewed for personal
credentials; [the review](docs/public-source-review-2026-10-13.json) classifies
upstream public test keys and marker strings by file hash. The old development
repository and its history are not included.

Project website: [aliro.mayday.software](https://aliro.mayday.software/).

## Relay and Telegram notifications

[Relay and Telegram notifications](docs/access-integrations.md). Implemented in shared source and the browser demo; disabled by default. The physical controller has not been flashed or tested with these integrations. Telegram is notifications only.
