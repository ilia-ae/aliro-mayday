# aliro.mayday

Experimental Aliro NFC access controller for ESP32-S3 + PN532, with Matter
DoorLock provisioning and local administration. This repository began as a clean
source snapshot dated **13 October 2026**, with new Git history.

## Verified status

- The iPhone Standard/Fast path worked on the May 2026 bench. The controller
  was offline during the October update; the new build still needs real
  iPhone regression tests and Samsung Wallet validation through SmartThings.
- The updated firmware builds with ESP-IDF v5.4.4 / esp-matter 8d56152.
  Seventeen firmware regression scenarios pass, covering old iPhone records,
  signature rejection, credential revocation, reboot and persistence failures.
- DigitalDoorKey is pinned to
  `80e4a1e14eab3646b62ab3bdad6d2ed01d399375`; local patches are documented in
  [UPSTREAM.md](aliro-esp32/components/DigitalDoorKey/UPSTREAM.md).
- Step-up checks issuer kid/signature, device binding, signed item digests,
  validity and stored iteration. Unknown device keys stay rejected while
  application AccessData policy and certificate-chain support remain unfinished.
  [Porting notes](docs/aliro-porting-2026-10-13.md) explain the supported subset.
- This is a development prototype. Aliro certification, Samsung/Google Wallet
  compatibility and a physical relay output are not established by this release.
  BLE/UWB access transport is not enabled in this firmware; Matter BLE
  commissioning remains available.

The full host suite has two known upstream BLE-flow test failures;
[testing notes](aliro-esp32/tests/README.md) explain the scope.
[October update](docs/ddk-update-2026-10-13.md) records the changes and remaining
hardware acceptance checks.

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
| `aliro-site/` | Website source; draft feature claims are not validation evidence |
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
