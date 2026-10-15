# Licensing and third-party notices

Public source availability does not make the entire repository OSI Open Source.
Project-original Aliro application code uses the source-available commercial
terms in [LICENSE](LICENSE); non-commercial personal/research use is permitted.
Commercial use of that original code needs a separate license.
See the [licensing guide](docs/licensing.md) ([Русский](docs/licensing.ru.md))
for permitted uses, the mixed-license scope and remaining release review.

Existing third-party permissions are preserved. They are not restricted by the
project license. Keep their copyright and license texts with redistributions.

- The PM3 Python package (`*.py` at the root and its `tests/`), and
  `scripts/export-public-source.py` and `scripts/verify-public-source.py`:
  [MIT](LICENSES/MIT.txt).
- `homekey-esp32/`: rednblkx/HomeKey-ESP32, [MIT](homekey-esp32/LICENSE) base;
  more specific file notices, including the CC BY-SA helper below, take precedence.
- Aliro's inherited `NfcManager.*`, `ReaderDataManager.*`, `DDKReaderData.h`,
  admin panel and Svelte UI: HomeKey-ESP32 / DigitalDoorKey, MIT; notices are
  retained in `aliro-esp32/main/LICENSE.upstream`, `components/admin_panel/LICENSE`,
  `data/LICENSE` and the DDK license. Our changes to these inherited files also
  use MIT, except material with a more specific license such as `objDiff.ts` below.
- `aliro-esp32/components/DigitalDoorKey/`, including local protocol fixes and
  upstream test changes: rednblkx/DigitalDoorKey,
  [MIT](aliro-esp32/components/DigitalDoorKey/LICENSE). See its UPSTREAM.md for
  the pinned revision and local changes.
- Exception inside DigitalDoorKey: `src/aliro/document_cbor/` adapts
  UltraWideLock's portable CBOR cursor under its retained [ISC license](aliro-esp32/components/DigitalDoorKey/src/aliro/document_cbor/LICENSE).
  The pinned source and modifications are recorded in that directory's UPSTREAM.md.
- `aliro-site/`: [source-available commercial terms](aliro-site/LICENSE).

## File-level exceptions

- `aliro-esp32/data/src/lib/utils/objDiff.ts` and
  `homekey-esp32/data/src/lib/utils/objDiff.ts`: **CC BY-SA 4.0**, attributed by
  their unchanged headers to Gabriel Gartz's
  [Stack Overflow answer](https://stackoverflow.com/a/8432188), with a TypeScript
  refactor noted. The official Stack Exchange API lists CC BY-SA 4.0 for that
  answer (checked 15 October 2026). Retain attribution, source, license and change
  notices, and satisfy applicable ShareAlike obligations for adaptations. The
  project's commercial restrictions do not apply to this material. Full text:
  [CC-BY-SA-4.0.txt](LICENSES/CC-BY-SA-4.0.txt).
- `aliro-esp32/components/libsodium/libsodium/m4/ax_check_link_flag.m4`,
  `ax_pthread.m4` and `ax_tls.m4`: **GPL-3.0-or-later with the configure-output
  exception printed in each macro**. Preserve their copyright and exception
  notices. Generated configure outputs have the stated exception; other uses
  of macro material remain governed by GPL. Full text:
  [GPL-3.0.txt](LICENSES/GPL-3.0.txt).
- libsodium's `src/libsodium/crypto_pwhash/argon2/` files carrying CC0 notices
  retain those notices rather than inheriting the surrounding ISC label. Other
  `Unlicense OR CC0-1.0` example/port headers retain that choice. Full text:
  [CC0-1.0.txt](LICENSES/CC0-1.0.txt).

[The dated inventory](docs/licensing-inventory-2026-10-15.json) records the
inspected texts and exception hashes. It is not a complete dependency SBOM or
legal clearance for distributing compiled firmware/UI.

External ESP-IDF, esp-matter, connectedhomeip, Arduino, component-manager and
JavaScript dependencies are obtained separately and retain their own licenses.
Header-only sources and assets may contain additional notices in their headers;
those notices apply even when there is no separate LICENSE file. The list below
indexes bundled license/notice files and does not replace their terms.

## Bundled component license files

- [aliro-esp32/components/DigitalDoorKey/LICENSE](aliro-esp32/components/DigitalDoorKey/LICENSE)
- [aliro-esp32/components/admin_panel/LICENSE](aliro-esp32/components/admin_panel/LICENSE)
- [aliro-esp32/components/cbor/LICENSE](aliro-esp32/components/cbor/LICENSE)
- [aliro-esp32/components/cbor/tinycbor/LICENSE](aliro-esp32/components/cbor/tinycbor/LICENSE)
- [aliro-esp32/components/esp-serial-flasher/LICENSE](aliro-esp32/components/esp-serial-flasher/LICENSE)
- [aliro-esp32/components/esp_delta_ota/LICENSE](aliro-esp32/components/esp_delta_ota/LICENSE)
- [aliro-esp32/components/esp_delta_ota/detools/LICENSE](aliro-esp32/components/esp_delta_ota/detools/LICENSE)
- [aliro-esp32/components/esp_delta_ota/detools/detools/HDiffPatch/LICENSE](aliro-esp32/components/esp_delta_ota/detools/detools/HDiffPatch/LICENSE)
- [aliro-esp32/components/esp_diag_data_store/LICENSE](aliro-esp32/components/esp_diag_data_store/LICENSE)
- [aliro-esp32/components/esp_diagnostics/LICENSE](aliro-esp32/components/esp_diagnostics/LICENSE)
- [aliro-esp32/components/esp_encrypted_img/LICENSE](aliro-esp32/components/esp_encrypted_img/LICENSE)
- [aliro-esp32/components/esp_insights/LICENSE](aliro-esp32/components/esp_insights/LICENSE)
- [aliro-esp32/components/esp_rcp_update/LICENSE](aliro-esp32/components/esp_rcp_update/LICENSE)
- [aliro-esp32/components/esp_secure_cert_mgr/LICENSE](aliro-esp32/components/esp_secure_cert_mgr/LICENSE)
- [aliro-esp32/components/esp_secure_cert_mgr/tools/LICENSE](aliro-esp32/components/esp_secure_cert_mgr/tools/LICENSE)
- [aliro-esp32/components/event_bus/LICENSE](aliro-esp32/components/event_bus/LICENSE)
- [aliro-esp32/components/jsmn/LICENSE](aliro-esp32/components/jsmn/LICENSE)
- [aliro-esp32/components/json_generator/LICENSE](aliro-esp32/components/json_generator/LICENSE)
- [aliro-esp32/components/json_parser/LICENSE](aliro-esp32/components/json_parser/LICENSE)
- [aliro-esp32/components/led_strip/LICENSE](aliro-esp32/components/led_strip/LICENSE)
- [aliro-esp32/components/libsodium/LICENSE](aliro-esp32/components/libsodium/LICENSE)
- [aliro-esp32/components/libsodium/libsodium/LICENSE](aliro-esp32/components/libsodium/libsodium/LICENSE)
- [aliro-esp32/components/loggable/LICENSE](aliro-esp32/components/loggable/LICENSE)
- [aliro-esp32/components/loggable_espidf/LICENSE](aliro-esp32/components/loggable_espidf/LICENSE)
- [aliro-esp32/components/mdns/LICENSE](aliro-esp32/components/mdns/LICENSE)
- [aliro-esp32/components/msgpack-c/upstream/COPYING](aliro-esp32/components/msgpack-c/upstream/COPYING)
- [aliro-esp32/components/msgpack-c/upstream/LICENSE_1_0.txt](aliro-esp32/components/msgpack-c/upstream/LICENSE_1_0.txt)
- [aliro-esp32/components/msgpack-c/upstream/NOTICE](aliro-esp32/components/msgpack-c/upstream/NOTICE)
- [aliro-esp32/components/pn532_cxx/LICENSE](aliro-esp32/components/pn532_cxx/LICENSE)
- [aliro-esp32/components/pn532_hal/LICENSE](aliro-esp32/components/pn532_hal/LICENSE)
- [aliro-esp32/components/rmaker_common/LICENSE](aliro-esp32/components/rmaker_common/LICENSE)

## Development fixtures

Some bundled Espressif examples contain publicly published test PEM keys and
certificates. These are upstream development fixtures, not deployed credentials.
Do not use them as production device secrets. They keep the corresponding
component's license and provenance.

## Specification

The CSA Aliro Specification PDF is not redistributed. Obtain it from CSA using
the official link in PROTOCOL-aliro.md or the project update notes.
