# NFC regression tests

[Русский](README.ru.md) · [Documentation index](../../docs/README.md)

These host tests compile the actual firmware adapter, credential operations and
ReaderDataManager/NVS serializer. Only the hardware transport and ESP NVS API
are substituted. They exercise cryptographic authentication, old persisted data,
version negotiation, revocation, storage failure and user access policies.

From the repository root:

```sh
cmake -S aliro-esp32/components/DigitalDoorKey/tests -B /tmp/aliro-ddk-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/aliro-ddk-tests --parallel
ctest --test-dir /tmp/aliro-ddk-tests --output-on-failure
```

The initial build downloads pinned Catch2, mbedTLS and libsodium test
dependencies. The parser is the same vendored tinycbor used by the firmware. On macOS, select a compatible SDK if the default SDK/linker pair
fails; pass `-DCMAKE_OSX_SYSROOT=<SDK>` to configure and `SDKROOT=<SDK>` to build.

The pinned upstream has two known failures in `aliro_ble_flow_test`, outside the
NFC firmware. Run the remaining suite separately with:

```sh
ctest --test-dir /tmp/aliro-ddk-tests --output-on-failure -E '^aliro_ble_flow_test$'
```

Do not describe an excluded suite as a fully passing upstream suite.

Hardware acceptance remains required: update without erasing NVS, confirm the
existing iPhone key on first and repeated taps, reboot and test again, then clear
the device credential / disable its user and verify rejection before and after
reboot. Restore and reprovision explicitly. Finally test Samsung Wallet through
the available SmartThings hub. Keep pairing, reader identity and old firmware
available for rollback; do not erase flash or burn security eFuses in this test.

Document access and signed-revocation coverage is described in
[the policy notes](../../docs/aliro-access-policy-2026-10-14.ru.md).
To instrument the protocol, real firmware adapter/NVS serializer, tinycbor and
msgpack parsers, configure with `-DDDK_ENABLE_SANITIZERS=ON` and rebuild. On macOS:

```sh
UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=0 ctest --test-dir /tmp/aliro-ddk-tests --output-on-failure -E '^aliro_ble_flow_test$'
```

LeakSanitizer is unavailable on this macOS runtime; the run checks AddressSanitizer
and UndefinedBehaviorSanitizer errors. The normal build defaults to sanitizers off.

An extra encoder regression checks canonical empty byte/text strings and
required-size accounting. The first Linux UBSan run found a zero-length null
memcpy that macOS did not diagnose; the firmware and host vendor both carry
[the same TinyCBOR fix](../components/cbor/LOCAL_PATCHES.md).

`firmware_provisioning_test` compiles the actual AliroDelegate and DoorLockUserStubs
with SDK boundary types and hardware hooks substituted. Its 22 cases exercise
slot enumeration, key/reader grants, failure/rollback, old records, clock startup,
Standard/Fast, reboot and revocation. See [Samsung bench preparation](../../docs/samsung-bench-preparation-2026-10-14.md).
