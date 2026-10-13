# Licensing and third-party notices

Public source availability does not make the entire repository OSI Open Source.
Project-original Aliro application code uses the source-available commercial
terms in [LICENSE](LICENSE); non-commercial personal/research use is permitted.
Commercial use of that original code needs a separate license.

Existing third-party permissions are preserved. They are not restricted by the
project license. Keep their copyright and license texts with redistributions.

- The PM3 Python package (`*.py` at the root and its `tests/`), and
  `scripts/export-public-source.py`: [MIT](LICENSES/MIT.txt).
- `homekey-esp32/`: rednblkx/HomeKey-ESP32, [MIT](homekey-esp32/LICENSE).
- Aliro's inherited `NfcManager.*`, `ReaderDataManager.*`, `DDKReaderData.h`,
  admin panel and Svelte UI: HomeKey-ESP32 / DigitalDoorKey, MIT; notices are
  retained in `aliro-esp32/main/LICENSE.upstream`, `components/admin_panel/LICENSE`,
  `data/LICENSE` and the DDK license. Our changes to these inherited files also
  use MIT.
- `aliro-esp32/components/DigitalDoorKey/`, including local protocol fixes and
  upstream test changes: rednblkx/DigitalDoorKey,
  [MIT](aliro-esp32/components/DigitalDoorKey/LICENSE). See its UPSTREAM.md for
  the pinned revision and local changes.
- `aliro-site/`: [source-available commercial terms](aliro-site/LICENSE).

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
