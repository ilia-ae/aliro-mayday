# Documentation

[Русский](README.ru.md)

English is the primary repository language. Current guide base filenames contain English;
Russian translations use `.ru.md` or `README.ru.md`.

| Topic | English | Russian translation |
| --- | --- | --- |
| Project, verified status, layout and licenses | [Overview](../README.md) | [Русский](../README.ru.md) |
| License scopes, commercial use and third-party exceptions | [Licensing](licensing.md) | [Русский](licensing.ru.md) |
| Contribution rights, acceptance and PR process | [Contributing](../CONTRIBUTING.md) | [Русский](../CONTRIBUTING.ru.md) |
| Project-original authorship and upstream credits | [Authorship](../AUTHORS.md) | [Русский](../AUTHORS.ru.md) |
| Snapshot timestamps and independent verification | [Evidence](provenance/2026-10-15/README.md) | [Русский](provenance/2026-10-15/README.ru.md) |
| SDK setup, build, update and rollback | [Firmware guide](../aliro-esp32/README.md) | [Русский](../aliro-esp32/README.ru.md) |
| GPIO, NFC SPI, display and buttons | [Wiring](wiring.md) | [Русский](wiring.ru.md) |
| Demo stand photos and retouching notes | [Photos](demo-stand.md) | [Русский](demo-stand.ru.md) |
| Local web UI launch, debugging and screenshot gallery | [UI debug guide](controller-ui-debug.md) | [Русский](controller-ui-debug.ru.md) |
| Document rights, revocation and persistence | [Access policy](aliro-access-policy-2026-10-14.md) | [Русский](aliro-access-policy-2026-10-14.ru.md) |
| Host tests and hardware acceptance | [Tests](../aliro-esp32/tests/README.md) | [Русский](../aliro-esp32/tests/README.ru.md) |
| Samsung provisioning and bench steps | [Bench preparation](samsung-bench-preparation-2026-10-14.md) | [Русский](samsung-bench-preparation-2026-10-14.ru.md) |
| Competitors, SDKs and implementations | [Review](aliro-competitors-2026-10-13.md) | [Русский](aliro-competitors-2026-10-13.ru.md) |
| Repository readiness and remaining work | [Status](repository-status-2026-10-14.md) | [Русский](repository-status-2026-10-14.ru.md) |

Each current guide and its translation describe the same scope and limitations.
Coverage refers to the **implemented subset**, not complete Aliro support or
verified compatibility with every wallet. Original legal texts govern; they
are not translated here.

## Source and provenance

- [DigitalDoorKey pin and patches](../aliro-esp32/components/DigitalDoorKey/UPSTREAM.md).
- [UltraWideLock CBOR import](../aliro-esp32/components/DigitalDoorKey/src/aliro/document_cbor/UPSTREAM.md).
- [License scopes and notices](../THIRD_PARTY_NOTICES.md).
- [Reviewed public fixtures](public-source-review-2026-10-13.json).
- [Source manifest](../SOURCE_MANIFEST.json): all tracked release files except
  the manifest itself; hashes describe this branch, not earlier tags.

## Historical material

Historical and upstream documents retain their original language and have not
all been translated. Earlier completion claims and build hashes refer to their
dated stages.

| Material | Language | Scope |
| --- | --- | --- |
| [DDK update, 13 October](ddk-update-2026-10-13.md) | RU | Initial update before document policy |
| [Initial validator port](aliro-porting-2026-10-13.md) | RU | First validator stage |
| [Old wire contract](../PROTOCOL-aliro.md) | EN | Early bring-up choices; superseded policy claims |
| [Workspace history](../README.history.md), [TODO](../TODO.md) | EN | Earlier roadmap |
| [Gap analysis](project-gap-analysis.md), [execution plan](execution-plan.md), [cold-tap roadmap](aliro-stdauth-finishing-roadmap.md) | EN | April investigation |
| [Hardware reference](hardware-reference.md) | EN | Board variants and historical context |
| [Documentation changelog](documentation-changelog.md) | EN | Documentation history |

Bundled HomeKey and vendor docs remain in upstream's language. Android HCE,
SmartKey patches, the Matter controller and website have their own READMEs.

## Aliro specification

Use the official [CSA Aliro 1.0 specification](https://csa-iot.org/wp-content/uploads/2026/02/26-42802-001_Aliro_1.0_specification.pdf).
The PDF and a complete Russian translation are not included. Our access-policy
guide describes the implemented subset and does not replace the standard.
