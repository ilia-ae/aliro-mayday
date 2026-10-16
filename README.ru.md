# aliro.mayday

[![Aliro 1.0: experimental NFC subset](https://img.shields.io/badge/Aliro%201.0-experimental%20NFC-orange)](docs/aliro-access-policy-2026-10-14.ru.md)
[![Matter: DoorLock prototype](https://img.shields.io/badge/Matter-DoorLock%20prototype-2864a0)](aliro-esp32/README.ru.md)
[![ESP-IDF: tested v5.4.4](https://img.shields.io/badge/ESP--IDF-tested%20v5.4.4-00979D)](aliro-esp32/README.ru.md)
[![iPhone: existing key bench-tested](https://img.shields.io/badge/iPhone-existing%20key%20bench--tested-2e7d32)](docs/repository-status-2026-10-14.ru.md)
[![Source integrity and supported host tests](https://github.com/ilia-ae/aliro-mayday/actions/workflows/source-and-nfc.yml/badge.svg?branch=main)](https://github.com/ilia-ae/aliro-mayday/actions/workflows/source-and-nfc.yml?query=branch%3Amain)
[![Hardware: ESP32-S3 + PN532](https://img.shields.io/badge/hardware-ESP32--S3%20%2B%20PN532-00979D)](docs/wiring.ru.md)
[![Status: prototype](https://img.shields.io/badge/status-prototype-orange)](docs/repository-status-2026-10-14.ru.md)
[![Licensing: mixed; see guide](https://img.shields.io/badge/licensing-mixed-blue)](docs/licensing.ru.md)
[![Documentation: EN / RU](https://img.shields.io/badge/docs-EN%20%2F%20RU-6f42c1)](docs/README.ru.md)

[English](README.md) · [Документация](docs/README.ru.md) · [Схема подключения](docs/wiring.ru.md) · [Фотографии стенда](docs/demo-stand.ru.md) · [Галерея панели и отладка](docs/controller-ui-debug.ru.md)

Собственный материал проекта: **Mayday / Ilia Arestov**. [Авторство и сторонние вклады](AUTHORS.ru.md).

Экспериментальный контроллер Aliro NFC на ESP32-S3 + PN532 с выдачей ключей
через Matter DoorLock и локальным управлением. Репозиторий начат как очищенный
снимок исходников **13 октября 2026** с новой историей Git.

## Локальная веб-панель

Запустите настоящий интерфейс с демоданными: `cd aliro-esp32/data` → `bun run debug` (из корня репозитория). Требуются Bun 1.3.13 и Node.js 22.12+; рекомендуется Node 24 LTS. Скрипт не подключается к контроллеру и не прошивает его.

[Галерея панели и отладка](docs/controller-ui-debug.ru.md).

## Подтверждения снимка исходников

**DigiCert + FreeTSA (RFC 3161): проверены.** Получены три квитанции
OpenTimestamps; подтверждённый Bitcoin-блок пока не включён в опубликованный пакет.
Доказательства относятся к снимку [`24e218e`](https://github.com/ilia-ae/aliro-mayday/tree/24e218ef1ea83dafd47447e0393ddd5cd949ccaa)
от 15 октября 2026 с подписью **Mayday / Ilia Arestov**.

[Доказательства и проверка](docs/provenance/2026-10-15/README.ru.md) · [Скачать ZIP](https://aliro.mayday.software/proofs/aliro-snapshot-2026-10-15.zip) · [Манифест](docs/provenance/2026-10-15/authorship-manifest.json)

## Проверенное состояние

- 15 октября 2026 владелец подтвердил работу прежнего ключа iPhone после
  обновления только приложения. В текущем запуске записаны три успешные
  аутентификации, ноль отказов и оба Matter-состояния: открыто и закрыто.
  Проверенная приватная резервная копия, ключ и сопряжение сохранены. Matter,
  PN532 NFC и HTTP/WebSocket-панель работают. Исправления запуска/закрытия HTTP
  и результат проверки включены в `main` через
  [PR №6](https://github.com/ilia-ae/aliro-mayday/pull/6). Samsung Wallet через SmartThings ещё предстоит проверить.
- Прошивка собирается с ESP-IDF v5.4.4 / esp-matter 8d56152. Проходят
  58 сценариев прошивки, включая 22 проверки реальных provisioning callbacks
  и старые iPhone-записи, отказ неверной
  подписи, отзыв, перезапуск и ошибки сохранения.
- DigitalDoorKey закреплён на `80e4a1e14eab3646b62ab3bdad6d2ed01d399375`;
  изменения описаны в [UPSTREAM.md](aliro-esp32/components/DigitalDoorKey/UPSTREAM.md).
- Step-up проверяет подписанные AccessData перед выдачей доступа ключу
  устройства от предоставленного издателя. Кэш проверяет текущие права и сроки.
  Подписанный `aliro-r` сохраняется независимо от успешного открытия и действует
  после перезапуска. [Политика и ограничения](docs/aliro-access-policy-2026-10-14.ru.md)
  описывают неподдерживаемые расписания/extensions и отсутствие часов.
- Прежние Apple v9 framing, identity, pairing и независимые ключи сохраняются.
  Сохранённый ключ iPhone прошёл стендовую проверку; отзыв и перезапуск на устройстве
  ещё нужно проверить с отдельным тестовым ключом.
- Это прототип. Сертификация Aliro, Samsung/Google Wallet и физическое реле
  этим выпуском не подтверждены. BLE/UWB access отключён; commissioning Matter
  через BLE остаётся доступным.

Полный host-набор имеет две известные ошибки upstream BLE-flow;
[описание тестов](aliro-esp32/tests/README.ru.md) объясняет границу проверки.
[Статус репозитория](docs/repository-status-2026-10-14.ru.md) поясняет различие
между актуальным `main` и прежним source-release. Исправления и переводы
включены через [PR №1](https://github.com/ilia-ae/aliro-mayday/pull/1), исправления
выдачи и сохранения ключей — через [PR №2](https://github.com/ilia-ae/aliro-mayday/pull/2). Публикация
исходников не подтверждает поддержку настоящего кошелька и не прошивает контроллер.

Подготовка provisioning и стенда V4_HUB/Fold7 описана
[здесь](docs/samsung-bench-preparation-2026-10-14.ru.md). Выдача настоящего Wallet key
ещё не подтверждена; проверка Fold7 отложена.

## Сборка и тесты

Следуйте [инструкции прошивки](aliro-esp32/README.ru.md). Сначала соберите
Svelte WebUI в `aliro-esp32/data/`; `sdkconfig`, managed components и бинарные
артефакты исключены из снимка и создаются локально.

```sh
cmake -S aliro-esp32/components/DigitalDoorKey/tests -B /tmp/aliro-ddk-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/aliro-ddk-tests --parallel
ctest --test-dir /tmp/aliro-ddk-tests --output-on-failure
```

На стенде сохраняйте reader identity, Matter pairing и NVS. Замороженный
`homekey-esp32/` остаётся неизменной базой.

## Структура исходников

| Каталог | Назначение |
| --- | --- |
| `aliro-esp32/` | Основная прошивка ESP32-S3 + PN532 и тесты на компьютере |
| `homekey-esp32/` | Замороженная HomeKey-база |
| `matter-controller/` | Пример Matter controller; Wi-Fi задаётся локально |
| `android-smartkey/`, `smartkey-esp32-patch/` | Отдельные заготовки запасного Android HCE-пути |
| Python в корне | Исследовательские Proxmark3 / UID-lock инструменты |
| `aliro-site/` | Исходники сайта, соответствующие EN/RU-статусы и ссылки на код |
| `docs/` | Политика, приёмка, исследования и исторические записи |

[История рабочего каталога](README.history.md) сохраняет прежний план.
Текущий статус приведён выше; [оглавление](docs/README.ru.md) отмечает архив
и границы переводов.

## Другие реализации и готовые продукты

[Обзор конкурентов](docs/aliro-competitors-2026-10-13.ru.md) сравнивает Aqara,
Nuki, Schlage, esp-aliro, UltraWideLock и SDK производителей с источниками
и приоритетами проекта. Наличие проекта в обзоре не означает перенос его кода.

## Лицензия и происхождение

Для собственного Aliro-кода действуют [commercial source-available условия](LICENSE).
[Лицензионная инструкция](docs/licensing.ru.md) описывает разрешённое применение
и области смешанных лицензий. Права MIT/ISC/Apache/Boost/BSD сохраняются. Среди
исключений — UI-функция CC BY-SA и GPL-макросы Autoconf с исключением для результатов;
см. [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Весь репозиторий не является
OSI Open Source. **Лицензия v1.1** явно разрешает некоммерческие изменения, форки
и распространение на её условиях; коммерческие пункты и оплата не менялись.
Новые вклады требуют явного согласия по [CONTRIBUTING.ru.md](CONTRIBUTING.ru.md),
с сохранением прав за авторами. Проверка всего коммерческого выпуска остаётся
отдельной задачей.

PDF CSA и полный перевод стандарта не распространяются в репозитории.
`SOURCE_MANIFEST.json` содержит пути и SHA-256 файлов текущей ветки.
[Проверка публичных fixtures](docs/public-source-review-2026-10-13.json)
классифицирует обнаруженные тестовые ключи/маркеры по хэшам. Старая приватная
история разработки не включена.

Сайт: [aliro.mayday.software](https://aliro.mayday.software/).
