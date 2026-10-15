# aliro.mayday

[English](README.md) · [Документация](docs/README.ru.md) · [Схема подключения](docs/wiring.ru.md)

Экспериментальный контроллер Aliro NFC на ESP32-S3 + PN532 с выдачей ключей
через Matter DoorLock и локальным управлением. Репозиторий начат как очищенный
снимок исходников **13 октября 2026** с новой историей Git.

## Проверенное состояние

- Перед обновлением 14 октября 2026 владелец подтвердил открытие и закрытие
  прежним ключом iPhone. Полная приватная резервная копия проверена. 15 октября
  подтверждены запуск Matter, PN532 NFC и HTTP/WebSocket-панели с сохранённой
  конфигурацией считывателя. Прежний ключ iPhone на новой прошивке и Samsung Wallet
  через SmartThings ещё предстоит проверить.
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
  Проверка настоящего устройства всё ещё обязательна.
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
MIT и другие лицензии сохраняются; см. [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
Публичность исходников не делает весь репозиторий OSI Open Source.

PDF CSA и полный перевод стандарта не распространяются в репозитории.
`SOURCE_MANIFEST.json` содержит пути и SHA-256 файлов текущей ветки.
[Проверка публичных fixtures](docs/public-source-review-2026-10-13.json)
классифицирует обнаруженные тестовые ключи/маркеры по хэшам. Старая приватная
история разработки не включена.

Сайт: [aliro.mayday.software](https://aliro.mayday.software/).
