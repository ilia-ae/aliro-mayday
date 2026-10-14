# Repository readiness / Готовность репозитория

14 October 2026 / 14 октября 2026.

## English

The public repository is [ilia-ae/aliro-mayday](https://github.com/ilia-ae/aliro-mayday).
It has new Git history. The old repository remains private. Public `main` includes
the document rights/revocation implementation and current bilingual documentation
integrated through [PR #1](https://github.com/ilia-ae/aliro-mayday/pull/1). The original
source-snapshot prerelease remains the initial sanitized snapshot. Hardware
acceptance is still pending: merging source is not a firmware deployment or Aliro
certification. The controller was not connected or flashed.

| Included source | Current role and verification |
| --- | --- |
| `aliro-esp32/` | Active firmware; builds on the installed pinned SDKs; NFC adapter and document policy tested on the host |
| `homekey-esp32/` | Frozen reference, unchanged by this update; original MIT license retained |
| `matter-controller/` | Controller example; Wi-Fi comes from local build configuration; changed object compiled, full image not validated here |
| `android-smartkey/`, `smartkey-esp32-patch/` | HCE fallback scaffolds; not a working Samsung Wallet integration |
| Root Python tools | PM3 research/UID-lock tools; their four tests passed; not Aliro tests |
| `aliro-site/` | Website source included; live site not updated and may have older feature claims |
| DigitalDoorKey | Pinned MIT source included, with local fixes and tests |
| UltraWideLock | Only its portable CBOR cursor/walker imported under ISC; local validator/policy is separate |
| esp-aliro, Nordic, ST, Silicon Labs, NXP, Python Aliro Reader, openaliro | Researched/referenced; not bundled or enabled as alternate backends |

Public release files are listed by SHA-256 in `SOURCE_MANIFEST.json`. The
reviewed 47 secret-scanner candidates are unchanged upstream fixtures/markers;
that classification is tied to file hashes, not a general exception for keys.
No private Git history, live runtime keys, local environment, SDK configuration,
logs or CSA PDF is part of the snapshot. Each bundled license retains its scope;
project-original code has commercial source-available terms, so the whole
repository is not OSI Open Source.

Verification covers 77 targeted scenarios: 36 firmware integration, 22 provisioning, nine
cryptographic document-validation and ten rights/revocation cases, plus one empty-CBOR encoder regression. The supported
suite passes 15/15 executables with AddressSanitizer/UndefinedBehaviorSanitizer.
The full suite is 15/16, with two known upstream BLE-flow failures. These do not
establish hardware timing, actual wallet behavior or power-loss durability.
The application builds at 2,564,656 bytes, leaving 2.17% in its OTA slot.

The repository now contains a GitHub Actions check for manifest/fixture integrity,
current local documentation links and the supported host suite. It explicitly
excludes the known failing BLE-flow executable. Firmware SDK builds and wallet
tests are not part of that workflow. Integrity checking is not a secret scanner.

Remaining acceptance/work: real old-key iPhone tests, Samsung Wallet via
SmartThings, clean-machine firmware build, physical relay validation, optional
schedule/reader-rule provisioning and certificate-chain support, BLE failures
before enabling that transport, UWB hardware and correction of live-site claims.
A valid clock is required for document-bound access. Revocation applies after
its signed document is delivered; it is not an online revocation service.

The current overview, build/rollback guide, policy, tests and competitor review
have English/Russian versions. Historical and upstream docs are not all
translated; the standard's full Russian translation is absent. See the
[documentation coverage index](README.md).

## Русский

Публичный репозиторий — [ilia-ae/aliro-mayday](https://github.com/ilia-ae/aliro-mayday).
У него новая история Git. Старый репозиторий остаётся приватным. Публичный `main`
включает проверку прав/отзыва и актуальные переводы, интегрированные через
[PR №1](https://github.com/ilia-ae/aliro-mayday/pull/1). Прежний source-snapshot
prerelease сохраняет исходный очищенный снимок. Аппаратная приёмка ещё нужна:
слияние исходников не является прошивкой контроллера или сертификацией Aliro.
Контроллер не подключался и не прошивался.

| Включённые исходники | Назначение и степень проверки |
| --- | --- |
| `aliro-esp32/` | Основная прошивка; сборка на установленном закреплённом SDK; адаптер NFC и документная политика проверены на компьютере |
| `homekey-esp32/` | Замороженная база, обновлением не менялась; сохранена MIT-лицензия |
| `matter-controller/` | Пример контроллера; Wi-Fi из локальной конфигурации; изменённый объект собран, полный image здесь не проверен |
| `android-smartkey/`, `smartkey-esp32-patch/` | Заготовки запасного HCE-пути; работающая интеграция Samsung Wallet из них не следует |
| Python-инструменты в корне | PM3/UID-lock исследования; четыре теста прошли, Aliro они не проверяют |
| `aliro-site/` | Исходники сайта включены; live-сайт не обновлялся и может содержать старые обещания |
| DigitalDoorKey | Включены MIT-исходники закреплённой версии, исправления и тесты |
| UltraWideLock | Включён только переносимый CBOR cursor/walker под ISC; валидатор и политика реализованы отдельно |
| esp-aliro, Nordic, ST, Silicon Labs, NXP, Python Aliro Reader, openaliro | Изучены и указаны в обзоре; не включены как альтернативные backends |

Файлы публичного выпуска перечислены с SHA-256 в `SOURCE_MANIFEST.json`.
47 кандидатов secret scanner — неизменённые публичные fixtures/маркеры upstream;
это классификация конкретных хэшей, а не общее исключение любых ключей.
Приватная история Git, действующие runtime-ключи, локальное окружение,
конфигурация SDK, логи и PDF CSA не входят в снимок. Лицензии компонентов
сохраняются; для собственного кода действуют commercial source-available условия,
поэтому весь репозиторий не является OSI Open Source.

Проверены 77 целевых сценариев: 36 интеграционных, 22 provisioning, девять криптографических
и десять по правам/отзыву, плюс проверка пустых CBOR-строк. Поддерживаемые 15/15 executables проходят
AddressSanitizer/UndefinedBehaviorSanitizer. Полный набор — 15/16: две известные
ошибки upstream BLE-flow. Это не доказывает аппаратные тайминги, поведение
настоящего кошелька или сохранение при внезапном отключении питания.
Application image — 2 564 656 байт; запас OTA-раздела 2,17%.

Включена проверка GitHub Actions: manifest, хэши fixtures, локальные ссылки
актуальных инструкций и поддерживаемый host-набор. Известный failing BLE-flow
исключён явно. SDK-сборка прошивки и настоящие кошельки этим workflow не проверяются.
Проверка целостности не является secret scanner.

Остаются настоящий iPhone со старым ключом, Samsung Wallet через SmartThings,
сборка прошивки на чистой машине, физическое реле, необязательные расписания,
выдача словаря ReaderRuleIds, доверие через цепочки сертификатов, исправления BLE
перед включением транспорта, UWB-аппаратура и актуализация live-сайта.
Доступ по документу требует часов. Отзыв начинает действовать после доставки
подписанного документа; онлайн-служба отзыва здесь не реализована.

Актуальные обзор, сборка/возврат, политика, тесты и сравнение конкурентов есть
на русском и английском. Исторические и upstream-документы переведены не все;
полного русского перевода стандарта нет. См. [оглавление и покрытие](README.md).

## Samsung Wallet: implemented core versus integration / Ядро и интеграция

The generic Aliro 1.0 NFC engine, Matter reader/issuer/device credential operations
and document rights/revocation are implemented and host-tested. Our controller
has not issued a Samsung Wallet key through SmartThings or completed a real
Samsung tap/revocation test. This is not a completed Samsung-specific integration.
Samsung describes onboarding a compatible lock through SmartThings/Matter, then
adding its key to Wallet; availability varies by device and region.
[Official Samsung workflow](https://news.samsung.com/global/samsung-wallet-launches-digital-home-key-for-smart-door-locks).

У нас реализованы и проверены на компьютере общий Aliro 1.0 NFC, операции Matter
с reader/issuer/device credentials и проверка документов/отзыва. Но наш контроллер
ещё не выдал ключ Samsung Wallet через SmartThings и не прошёл настоящий Samsung
tap/отзыв. Законченной Samsung-интеграции пока нет. Samsung описывает добавление
совместимого замка через SmartThings/Matter и последующую выдачу ключа Wallet;
доступность зависит от устройства и региона.
[Официальный сценарий Samsung](https://news.samsung.com/global/samsung-wallet-launches-digital-home-key-for-smart-door-locks).

The next proof is SmartThings onboarding → Wallet key issuance → first/repeated
NFC taps → clear/disabled-user/reboot rejection, with old iPhone keys preserved.
An alternative SDK can help implement the protocol; its source availability
does not prove that SmartThings will accept this reader or issue its Wallet key.

Следующая проверка: добавление в SmartThings → выдача ключа Wallet → первый/повторные
NFC taps → отказ после удаления/отключения пользователя/перезапуска, с сохранением
старых ключей iPhone. Другой SDK может помочь с протоколом, но наличие его кода
не доказывает, что SmartThings примет этот считыватель и выдаст ему Wallet key.

Samsung provisioning preparation and hardware acceptance: [English](samsung-bench-preparation-2026-10-14.en.md) / [Русский](samsung-bench-preparation-2026-10-14.md). The new callbacks are host-tested; actual Wallet issuance remains pending.
