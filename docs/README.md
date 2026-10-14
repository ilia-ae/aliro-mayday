# Documentation / Документация

Current implementation documentation is available in English and Russian.
Актуальные основные материалы доступны на английском и русском.

| Topic / Тема | English | Русский |
| --- | --- | --- |
| Project, verified status, layout, licenses / Проект и статус | [README](../README.md) | [README](../README.ru.md) |
| SDK setup, firmware build, safe update and rollback / Сборка и обновление | [Firmware guide](../aliro-esp32/README.md) | [Прошивка](../aliro-esp32/README.ru.md) |
| GPIO, NFC SPI, display and button wiring / Подключение и пины | [Wiring diagram](wiring.en.md) | [Схема подключения](wiring.md) |
| Document rights, revocation, persistence and limits / Права и отзыв | [Access policy](aliro-access-policy-2026-10-14.en.md) | [Политика доступа](aliro-access-policy-2026-10-14.md) |
| Host tests, sanitizers and hardware acceptance / Проверки | [Tests](../aliro-esp32/tests/README.md) | [Тесты](../aliro-esp32/tests/README.ru.md) |
| Samsung provisioning and bench steps / Подготовка Samsung | [Bench preparation](samsung-bench-preparation-2026-10-14.en.md) | [Подготовка стенда](samsung-bench-preparation-2026-10-14.md) |
| Competitors, SDKs and implementations / Другие решения | [Review](aliro-competitors-2026-10-13.en.md) | [Обзор](aliro-competitors-2026-10-13.md) |
| Repository readiness, included projects, remaining work / Готовность репозитория | [Status (EN/RU)](repository-status-2026-10-14.md#english) | [Статус (RU/EN)](repository-status-2026-10-14.md#русский) |

The two language versions of each current guide describe the same scope and
limitations. Documentation completeness means coverage of the **implemented
subset**, not implementation of every Aliro feature or verified wallet support.
Translations of legal texts are not provided; original license texts govern.

Версии основных инструкций описывают одинаковые возможности и ограничения.
Полнота этих инструкций относится к **реализованной части**. Она не означает
полноту реализации стандарта или проверенную поддержку всех кошельков.
Юридические тексты не переведены; действуют оригиналы лицензий.

## Source and provenance / Исходники и происхождение

- [DigitalDoorKey pin and patches](../aliro-esp32/components/DigitalDoorKey/UPSTREAM.md) (English).
- [UltraWideLock CBOR import](../aliro-esp32/components/DigitalDoorKey/src/aliro/document_cbor/UPSTREAM.md) (English).
- [License scopes and third-party notices](../THIRD_PARTY_NOTICES.md) (English).
- [Reviewed public test fixtures](public-source-review-2026-10-13.json) (JSON).
- [Source manifest](../SOURCE_MANIFEST.json): all tracked release files except the manifest itself; hashes correspond to this branch, not to older release tags.

## Historical material / Исторические материалы

These remain in their original language and have **not** all been translated.
Earlier completion claims and build hashes describe their dated stages.
Они сохранены на исходном языке и **не** переведены целиком. Старые статусы
готовности и хэши сборок относятся к указанному этапу.

| Material / Материал | Language / Язык | Scope / Область |
| --- | --- | --- |
| [DDK update, 13 October](ddk-update-2026-10-13.md) | RU | Initial update before document policy / Первое обновление |
| [Initial validator port](aliro-porting-2026-10-13.md) | RU | First validator stage / Первый этап валидатора |
| [Old wire contract](../PROTOCOL-aliro.md) | EN | Early bring-up choices; superseded policy claims / Ранние решения |
| [Workspace history](../README.history.md), [TODO](../TODO.md) | EN | Earlier roadmap / Прежний план |
| [Gap analysis](project-gap-analysis.md), [execution plan](execution-plan.md), [cold-tap roadmap](aliro-stdauth-finishing-roadmap.md) | EN | April investigation / Апрельские исследования |
| [Hardware reference](hardware-reference.md) | EN | Board and wiring reference; no new hardware proof / Платы и проводка |
| [Documentation changelog](documentation-changelog.md) | EN | Documentation history / История документации |

Bundled HomeKey and vendor component documentation stays in upstream's language;
it is not a second verified Aliro user guide. Source trees for Android HCE,
SmartKey patches, Matter controller and the website have their own READMEs.

Документация HomeKey и сторонних компонентов сохранена на языке upstream.
Это не вторая подтверждённая инструкция Aliro. У Android HCE, SmartKey patches,
Matter controller и сайта есть собственные README.

## Aliro specification / Стандарт Aliro

Use the official [CSA Aliro 1.0 specification](https://csa-iot.org/wp-content/uploads/2026/02/26-42802-001_Aliro_1.0_specification.pdf).
The PDF and a complete Russian translation of the standard are **not** included.
Our policy guide explains the implemented subset; it does not replace the standard.

Официальный PDF CSA и полный русский перевод стандарта **не** включены.
Наш документ объясняет реализованную часть и не заменяет спецификацию.
