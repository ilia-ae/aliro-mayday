# Регрессионные тесты NFC

[English](README.md) · [Оглавление](../../docs/README.md)

Эти тесты на компьютере собирают настоящий адаптер прошивки, операции над
ключами и ReaderDataManager/NVS serializer. Заменены аппаратный транспорт
и ESP NVS API. Проверяются криптография, старые сохранённые данные, выбор версии,
отзыв, ошибки хранения и политика пользователей.

Из корня репозитория:

```sh
cmake -S aliro-esp32/components/DigitalDoorKey/tests -B /tmp/aliro-ddk-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/aliro-ddk-tests --parallel
ctest --test-dir /tmp/aliro-ddk-tests --output-on-failure
```

Первая сборка скачивает закреплённые Catch2, mbedTLS и libsodium. Используется
тот же vendored tinycbor, что в прошивке. На macOS при несовместимости SDK
и linker выберите подходящий SDK: `-DCMAKE_OSX_SYSROOT=<SDK>` для конфигурации
и `SDKROOT=<SDK>` для сборки.

В закреплённом upstream две известные ошибки `aliro_ble_flow_test`, вне
включённого NFC firmware transport. Остальной набор запускается отдельно:

```sh
ctest --test-dir /tmp/aliro-ddk-tests --output-on-failure -E '^aliro_ble_flow_test$'
```

Исключение этого executable не означает успешность полного upstream-набора.

Для стендовой приёмки обновляйте без стирания NVS, проверьте существующий ключ
iPhone на первом и повторных taps, затем после перезапуска. Удалите device
credential / отключите пользователя и проверьте отказ до и после перезапуска.
Восстановление выполняйте явной повторной выдачей. Затем проверьте Samsung Wallet
через имеющийся SmartThings hub. Сохраните pairing, identity и рабочую прошивку
для возврата; не стирайте flash и не меняйте security eFuses в этом испытании.

Покрытие документов и подписанного отзыва описано в
[политике доступа](../../docs/aliro-access-policy-2026-10-14.md).
Для AddressSanitizer / UndefinedBehaviorSanitizer в протоколе, адаптере,
NVS serializer, tinycbor и msgpack включите `-DDDK_ENABLE_SANITIZERS=ON`
и пересоберите. На macOS:

```sh
UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=0 ctest --test-dir /tmp/aliro-ddk-tests --output-on-failure -E '^aliro_ble_flow_test$'
```

LeakSanitizer недоступен на проверенном macOS. Обычная конфигурация собирается
без sanitizers. Тесты не заменяют реальный кошелёк, аппаратные тайминги,
физический выход и проверку потери питания.

Отдельная проверка encoder сверяет канонические пустые byte/text strings
и расчёт размера. Первый Linux UBSan выявил нулевой memcpy, не диагностированный
на macOS; в прошивке и host-наборе используется
[одно исправление TinyCBOR](../components/cbor/LOCAL_PATCHES.md).

`firmware_provisioning_test` собирает настоящие AliroDelegate и DoorLockUserStubs
с заменой SDK boundary types и hardware hooks. 22 сценария проверяют slots,
выдачу ключей/reader config, ошибки/rollback, старые записи, запуск часов,
Standard/Fast, reboot и отзыв. См. [подготовку Samsung](../../docs/samsung-bench-preparation-2026-10-14.md).
