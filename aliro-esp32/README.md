# Aliro NFC firmware

[Русский](README.ru.md) · [Documentation index](../docs/README.md)

Experimental ESP32-S3 + PN532 controller with Matter DoorLock provisioning,
NVS credential storage and local administration. The iPhone Standard/Fast path
worked on the May bench and the owner confirmed the existing key still opened
and closed on 14 October before updating. The update builds and passes the
supported host suite; updated-firmware iPhone and Samsung Wallet checks remain
separate acceptance items. A logical unlock is tested;
a physical relay output and Aliro certification are not established.

## Current implementation

- DigitalDoorKey is pinned to `80e4a1e14eab3646b62ab3bdad6d2ed01d399375`.
- Version negotiation prefers `0x0100` and explicitly permits Apple `0x0009`.
  Legacy AUTH0/AUTH1 framing, reader identity, pairing and old NVS records remain.
- An unknown device key requires a valid document from a provisioned issuer,
  its AUTH1 signature and supported AccessData authorizing UnlockDoor.
- Signed `aliro-r` revocations persist independently of a successful unlock.
  Current policy, disabled users, credential deletion and storage revision are
  checked again before logical unlock.
- Document-bound access requires a usable system wall-clock. Schedules and a
  provisioned reader-rule dictionary are not implemented; unsupported restrictions
  are rejected. Certificate-chain trust is not implemented.
- Aliro BLE/UWB access is disabled. Matter BLE commissioning is available.

Read [access policy and limits](../docs/aliro-access-policy-2026-10-14.md),
[local DDK changes](components/DigitalDoorKey/UPSTREAM.md) and
[tests / hardware acceptance](tests/README.md).

## Build prerequisites

The tested toolchain is ESP-IDF **v5.4.4**
(`296b6eab9445fd720e71aecab961e2d3fbca9944`) and esp-matter
**`8d561526abb8bcfd8893031b5f1940719ec01109`**. Other versions are not validated.
Install their official toolchains and esp-matter's recursive submodules:

```sh
git clone --branch v5.4.4 --recursive https://github.com/espressif/esp-idf.git esp-idf
cd esp-idf
./install.sh esp32s3
. ./export.sh
cd ..
git clone https://github.com/espressif/esp-matter.git esp-matter
cd esp-matter
git checkout 8d561526abb8bcfd8893031b5f1940719ec01109
git submodule update --init --recursive
./install.sh
. ./export.sh
```

The SDKs are external dependencies, not bundled in the public snapshot.
Registry dependencies are declared in `main/idf_component.yml`. Several use
version ranges; the tested installed toolchain does not establish byte-for-byte
reproducibility on a clean machine. A clean-machine firmware build remains an
acceptance item. The WebUI requires Bun and its committed lockfile.

## Build without connecting a controller

From the public repository root, with both SDKs installed:

```sh
. "$IDF_PATH/export.sh"
. "$ESP_MATTER_PATH/export.sh"
export _PW_ACTUAL_ENVIRONMENT_ROOT="$ESP_MATTER_PATH/connectedhomeip/connectedhomeip/.environment"
cd aliro-esp32/data
bun install --frozen-lockfile
bun run build
cd ..
# For a NEW local build configuration only:
idf.py set-target esp32s3
idf.py build
```

Generated `sdkconfig`, `managed_components/`, `data/dist/` and `build/` are local.
Keep an existing bench configuration instead of resetting it with `set-target`.
The `just aliro-build` helper assumes SDK installations under the user's usual
`~/esp` / `~/CODE` locations; the commands above support other locations.

The checked application image is **2,565,488 bytes**, with **55,952 bytes (2.13%)**
free in a `0x280000` OTA slot. Its SHA-256 is
`81ea0fab5a18a168b4cbf43a89524094cdfafaf608ff9ac9ba91764199430dc5`.
There is little room for additional firmware code without another size review.

## Existing hardware and configuration

The active bench target is ESP32-S3 + PN532 on SPI. Wiring and board variants
are recorded in the [labelled wiring guide](../docs/wiring.md) and
[hardware-reference.md](../docs/hardware-reference.md).
`sdkconfig.defaults` enables optional octal PSRAM with ignore-not-found and
currently selects 16 MB flash. Confirm the actual module's memory before a
first installation. `partitions.csv` has two 2.5 MB OTA slots, separate
Aliro/Matter NVS, a WebUI filesystem and a coredump partition.

The frozen LilyGO Ethernet baseline is in `homekey-esp32/`; Ethernet has not been
ported into this firmware. PN7160, PN7642 and UWB are separate future bring-up
paths, not alternate validated targets of this build.

## Update and rollback acceptance

A full private backup was verified on the connected bench, and application-only
installation and rollback were checked without writing data partitions. An initial
startup memory shortage was isolated to the uncreated NimBLE host task. The
expanded 17 KiB credential table now uses one heap block instead of internal BSS;
with the configured 16 KiB malloc threshold, PSRAM is preferred. Matter/NimBLE
buffers may also use PSRAM; controller/DMA memory and task stacks remain internal.
Credential indices and NVS record layouts are unchanged. Existing local build configurations must select
`CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_DEFAULT=y` and
`CONFIG_ESP_MATTER_MEM_ALLOC_MODE_DEFAULT=y`; defaults alone do not override old
`sdkconfig` choices.

Before updating an existing bench, preserve the known-good application, exact
partition table, configuration,
reader identity, credential data and Matter pairing. Keep any backups private:
they can contain live keys. Use an application-only update compatible with the
existing layout. A generic full `idf.py flash` writes more than the application;
do not use it as the existing-key regression procedure.

Do not erase NVS, change partitions or enable irreversible security settings
during compatibility testing. Test the old iPhone key on first/repeated taps,
after reboot and without network. Then test deleted credentials, disabled users,
signed revocation and explicit reprovisioning, including another reboot.
Finally commission and provision Samsung Wallet through the available
SmartThings hub and repeat these checks. A hub's presence alone does not prove
compatibility. If reverting, restore the known-good application and compatible
private state; an old build does not enforce the new document revocation policy.

See [host test commands and acceptance checks](tests/README.md). These tests
substitute hardware transport and NVS API; they do not reproduce a real wallet,
secure element, radio timing, relay or sudden power loss.

## Source layout

`main/` holds the firmware adapter, Matter delegate, credential operations and
final access checks. `components/DigitalDoorKey/` contains the protocol engine,
document validator and host tests. `tests/` compiles the actual adapter and NVS
serializer with hardware stubs. `data/` is the Svelte WebUI; support components
and their licenses are indexed in [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

The previous April bring-up notes are retained in historical documents.
The [documentation index](../docs/README.md) identifies current guides and
archived research; archived claims are not current verification results.

Current provisioning uses a complete reader configuration record and shared
slot limits. Reader/user/credential errors propagate without publishing grants;
stable sub-ID and old key records are retained. System time sync starts without
OLED. See [Samsung bench preparation](../docs/samsung-bench-preparation-2026-10-14.md).

NFC precomputation uses an 8 KiB stack; polling/document verification uses 12 KiB.
The previous 4 KiB precompute worker overflowed with the updated DDK. HTTP/WebSocket
startup reports actual readiness, retries temporary allocation failures, and
handles an IP-up event that arrived before the admin object was constructed.
