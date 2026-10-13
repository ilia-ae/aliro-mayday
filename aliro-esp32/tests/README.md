# NFC regression tests

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

The initial build downloads pinned Catch2, mbedTLS, libsodium and tinycbor test
dependencies. On macOS, select a compatible SDK if the default SDK/linker pair
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
