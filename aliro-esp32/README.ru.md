# Прошивка Aliro NFC

[![Aliro 1.0: experimental NFC subset](https://img.shields.io/badge/Aliro%201.0-experimental%20NFC-orange)](../docs/aliro-access-policy-2026-10-14.ru.md)
[![Matter: DoorLock prototype](https://img.shields.io/badge/Matter-DoorLock%20prototype-2864a0)](README.ru.md)
[![ESP-IDF: tested v5.4.4](https://img.shields.io/badge/ESP--IDF-tested%20v5.4.4-00979D)](README.ru.md)
[![iPhone: existing key bench-tested](https://img.shields.io/badge/iPhone-existing%20key%20bench--tested-2e7d32)](../docs/repository-status-2026-10-14.ru.md)
[![Source integrity and supported host tests](https://github.com/ilia-ae/aliro-mayday/actions/workflows/source-and-nfc.yml/badge.svg?branch=main)](https://github.com/ilia-ae/aliro-mayday/actions/workflows/source-and-nfc.yml?query=branch%3Amain)
[![Hardware: ESP32-S3 + PN532](https://img.shields.io/badge/hardware-ESP32--S3%20%2B%20PN532-00979D)](../docs/wiring.ru.md)
[![Status: prototype](https://img.shields.io/badge/status-prototype-orange)](../docs/repository-status-2026-10-14.ru.md)
[![Licensing: mixed; see guide](https://img.shields.io/badge/licensing-mixed-blue)](../docs/licensing.ru.md)
[![Documentation: EN / RU](https://img.shields.io/badge/docs-EN%20%2F%20RU-6f42c1)](../docs/README.ru.md)

[English](README.md) · [Оглавление документации](../docs/README.ru.md) · [Фотографии стенда](../docs/demo-stand.ru.md)

Собственные дополнения: **Mayday / Ilia Arestov**. [Авторство и сторонние вклады](../AUTHORS.ru.md).

Экспериментальный контроллер ESP32-S3 + PN532 с выдачей ключей через Matter
DoorLock, хранением в NVS и локальным управлением. 15 октября 2026 владелец
подтвердил работу прежнего ключа iPhone после обновления. В текущем запуске
записаны три успешные аутентификации, ноль отказов и оба Matter-состояния —
открыто и закрыто; перевыдача ключа и сброс сопряжения не потребовались.
Поддерживаемый набор тестов проходит. Выдача Samsung Wallet, отзыв на устройстве
с отдельным тестовым ключом, физический привод и сертификация Aliro требуют
отдельного подтверждения.

Лицензирование смешанное: см. [области и сторонние исключения](../docs/licensing.ru.md).

## Подтверждения снимка исходников

**DigiCert + FreeTSA (RFC 3161): проверены.** Получены три квитанции
OpenTimestamps; подтверждённый Bitcoin-блок пока не включён в опубликованный пакет.
Доказательства относятся к снимку [`24e218e`](https://github.com/ilia-ae/aliro-mayday/tree/24e218ef1ea83dafd47447e0393ddd5cd949ccaa)
от 15 октября 2026 с подписью **Mayday / Ilia Arestov**.

[Доказательства и проверка](../docs/provenance/2026-10-15/README.ru.md) · [Скачать ZIP](https://aliro.mayday.software/proofs/aliro-snapshot-2026-10-15.zip) · [Манифест](../docs/provenance/2026-10-15/authorship-manifest.json)

## Текущая реализация

- DigitalDoorKey закреплён на `80e4a1e14eab3646b62ab3bdad6d2ed01d399375`.
- Выбор версии предпочитает `0x0100` и явно допускает Apple `0x0009`.
  Прежние AUTH0/AUTH1, identity считывателя, pairing и старые NVS-записи сохраняются.
- Неизвестному ключу нужен корректный документ от предоставленного издателя,
  подпись устройства AUTH1 и поддерживаемые AccessData с правом UnlockDoor.
- Подписанный отзыв `aliro-r` сохраняется независимо от успешного открытия.
  Текущая политика, отключение пользователя, удаление ключа и версия хранилища
  повторно проверяются перед логическим открытием.
- Доступ по документу требует доступных системных часов. Расписания и выдача
  словаря ReaderRuleIds не реализованы; неподдерживаемые ограничения дают отказ.
  Доверие через цепочки сертификатов не реализовано.
- Aliro BLE/UWB отключён. Matter commissioning через BLE доступен.

Подробности: [политика доступа и границы](../docs/aliro-access-policy-2026-10-14.ru.md),
[изменения DDK](components/DigitalDoorKey/UPSTREAM.md),
[тесты и стендовая приёмка](tests/README.ru.md).

## Зависимости для сборки

Проверены ESP-IDF **v5.4.4**
(`296b6eab9445fd720e71aecab961e2d3fbca9944`) и esp-matter
**`8d561526abb8bcfd8893031b5f1940719ec01109`**. Другие версии не проверялись.
Установите официальные инструменты и рекурсивные submodules esp-matter:

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

SDK устанавливаются отдельно и не входят в снимок. Registry-зависимости указаны
в `main/idf_component.yml`, часть версий задана диапазонами. Успешная сборка
на установленном окружении не доказывает воспроизводимость байт в байт на чистой
машине. Такая сборка ещё нужна для приёмки. Для WebUI нужен Bun; lockfile включён.

## Сборка без подключения контроллера

Из корня публичного репозитория, после установки обоих SDK:

```sh
. "$IDF_PATH/export.sh"
. "$ESP_MATTER_PATH/export.sh"
export _PW_ACTUAL_ENVIRONMENT_ROOT="$ESP_MATTER_PATH/connectedhomeip/connectedhomeip/.environment"
cd aliro-esp32/data
bun install --frozen-lockfile
bun run build
cd ..
# Только для НОВОЙ локальной конфигурации сборки:
idf.py set-target esp32s3
idf.py build
```

`sdkconfig`, `managed_components/`, `data/dist/` и `build/` генерируются локально.
Существующую стендовую конфигурацию сохраняйте; не сбрасывайте её `set-target`.
Помощник `just aliro-build` предполагает привычные расположения SDK в
`~/esp` / `~/CODE`; команды выше позволяют использовать другие каталоги.

Проверенный application image — **2 565 488 байт**, свободно **55 952 байт
(2,13%)** в OTA-разделе `0x280000`. SHA-256:
`81ea0fab5a18a168b4cbf43a89524094cdfafaf608ff9ac9ba91764199430dc5`.
Для дополнительных функций нужен новый контроль размера: запаса немного.

## Аппаратура и конфигурация

Основной стенд — ESP32-S3 + PN532 по SPI. Проводка и варианты плат описаны
в [схеме с подписями пинов](../docs/wiring.ru.md) и
[hardware-reference.md](../docs/hardware-reference.md). В `sdkconfig.defaults`
включена необязательная octal PSRAM с ignore-not-found и выбран flash 16 MB.
Перед первой установкой сверяйте реальную память модуля. В `partitions.csv`
два OTA-раздела по 2,5 MB, отдельные NVS Aliro/Matter, файловая система WebUI
и раздел coredump.

Замороженный Ethernet-стенд LilyGO находится в `homekey-esp32/`; Ethernet
в эту прошивку не перенесён. PN7160, PN7642 и UWB — будущие отдельные стенды,
а не проверенные альтернативные платформы этой сборки.

## Приёмка обновления и возврат

На подключённом стенде проверены полная приватная резервная копия, установка и
возврат только приложения без записи разделов данных. Нехватка памяти первой
сборки оставила задачу NimBLE host несозданной. Расширенная таблица credentials размером 17 KiB размещена одним блоком heap
вместо внутреннего BSS; при пороге malloc 16 KiB предпочтительна PSRAM. Буферы
Matter/NimBLE также могут использовать PSRAM; задачи, контроллер и DMA остаются
во внутренней памяти. Индексы credentials и формат записей NVS не меняются. В прежнем локальном `sdkconfig` выберите
`CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_DEFAULT=y` и
`CONFIG_ESP_MATTER_MEM_ALLOC_MODE_DEFAULT=y`: defaults не заменяют прежний выбор.

Перед обновлением существующего
стенда сохраните рабочее приложение, точную таблицу разделов, конфигурацию,
identity, ключи и Matter pairing. Резервные копии держите приватными: в них
могут быть действующие секреты. Используйте обновление только приложения,
совместимое с прежней таблицей. Обычный полный `idf.py flash` записывает больше,
чем приложение; для проверки старого ключа этот способ не подходит.

Не стирайте NVS, не меняйте разделы и не включайте необратимые настройки
безопасности при проверке совместимости. Проверьте старый ключ iPhone на первом
и повторных taps, после перезапуска и без сети. Затем проверьте удаление ключа,
отключение пользователя, подписанный отзыв и явную повторную выдачу, включая
перезапуск. После этого выдайте Samsung Wallet key через имеющийся SmartThings
hub и повторите проверки. Наличие хаба не доказывает совместимость. При возврате
восстановите рабочее приложение и совместимое приватное состояние: старый код
не обеспечивает новую политику документного отзыва.

См. [команды тестов и стендовую приёмку](tests/README.ru.md). Тесты заменяют
аппаратный транспорт и NVS API; они не воспроизводят настоящий кошелёк,
secure element, радиотайминги, реле или внезапное отключение питания.

## Структура исходников

В `main/` находятся адаптер, Matter delegate, операции над ключами и окончательная
проверка доступа. В `components/DigitalDoorKey/` — протокол, валидатор документов
и host tests. В `tests/` реальные адаптер и NVS serializer собираются с аппаратными
заглушками. В `data/` — Svelte WebUI; лицензии компонентов перечислены
в [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

Апрельские записи сохранены в исторических документах. [Оглавление](../docs/README.ru.md)
отделяет актуальные инструкции от архива: старые утверждения не являются
результатом текущей проверки.

Provisioning использует целую запись reader config и общие лимиты slots.
Ошибки reader/user/credential возвращаются без выдачи новых прав; stable sub-ID
и старые записи сохраняются. Часы запускаются без OLED.
См. [подготовку Samsung](../docs/samsung-bench-preparation-2026-10-14.ru.md).

Подготовка NFC использует стек 8 KiB, polling и проверка документов — 12 KiB.
Прежний стек подготовки 4 KiB переполнялся с обновлённым DDK. Запуск HTTP/WebSocket
сообщает фактическую готовность, повторяется после временной нехватки памяти и
учитывает IP, полученный до создания панели управления.
