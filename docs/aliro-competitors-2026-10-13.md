# Конкуренты и реализации Aliro для aliro.mayday

Состояние на 13 октября 2026 года. Для ближайшего развития aliro.mayday полезнее
всего официальный esp-aliro как второй протокольный эталон на ESP32 и
UltraWideLock как пример переносимого стека и проверки документов. Nuki и Aqara
уже предлагают пользователям Apple и Samsung Wallet. Конкурировать одним
обещанием «телефон открывает дверь» недостаточно: проекту нужны подтверждённый
Samsung-путь, надёжный отзыв и работающий выход на существующий замок.

Aliro отвечает за локальную транзакцию с телефоном, а Matter — за commissioning,
настройку и управление замком. Поддержка Matter в продукте сама по себе не
доказывает выдачу ключей всеми кошельками. Это разделение прямо описано в
[документации Espressif](https://developer.espressif.com/blog/2026/07/espressif-aliro-solution/).

## Готовые продукты

| Решение | Что уже предлагается | Условия и ограничения | Полезное для нашего проекта |
| --- | --- | --- | --- |
| **Aqara U400** | Замок с Apple Home Key и UWB; Samsung Wallet NFC официально объявлен 2 марта. В реестре CSA есть сертификат Aliro 1.0 от 24 марта 2026, firmware 3.1.0.4. | Для Samsung нужен совместимый SmartThings gateway и обновлённые телефон/замок. Мартовский анонс относил Samsung UWB к планам; его нельзя автоматически считать выполненным вместе с NFC. | Эталон выдачи Samsung-ключа и проверки первого/повторного tap. [Samsung NFC](https://forum.aqara.com/t/aqara-smart-lock-u400-now-supports-samsung-wallet-digital-home-key/264249), [сертификат CSA](https://csa-iot.org/csa_product/aqara-smart-lock-u400/), [Apple UWB](https://www.aqara.com/en/news/aqara-introduces-uwb-powered-smart-lock-u400-at-ces-2026/). |
| **Nuki Keypad 2 NFC** | Отдельный внешний считыватель для Matter-capable Nuki Smart Lock. Apple Home Key, Samsung Digital Home Key, fingerprint, PIN и собственный Nuki App Mode. Nuki объявляет keypad Aliro-certified. | Wallet-режим требует соответствующий Matter hub. App Mode обходится без него; права связаны с пользователем приложения. Power reserve относится к поддерживаемым Wallet-устройствам, у App Mode его нет. | Хорошая модель понятного выбора режима и проверки prerequisites. Считыватель отделён от механизма замка. [Продукт и FAQ](https://nuki.io/en-us/products/keypad-2-nfc), [анонс 24 марта](https://nuki.io/en-us/company/press-releases/keypad-2-nfc). |
| **Schlage Sense Pro** | Matter over Thread и Apple hands-free unlock. Converge использует расстояние, направление и движение для решения о намерении войти. | На текущей странице Samsung/Google Wallet ещё описаны как ожидаемые после Aliro certification. Управление из Android-приложения Schlage не равно ключу Android Wallet. | Для будущего UWB учитывать сторону двери и траекторию, а не только близость телефона. [Технологии Sense Pro](https://www.schlage.com/en/home/smart-locks/sense-pro/technologies.html). |

Для aliro.mayday iPhone был проверен на майском стенде, Samsung пока требует
испытаний с имеющимся хабом, а Google Wallet остаётся неподтверждённым. В таблице
сайта нельзя ставить всем трём «работает» на основании общей совместимости Aliro.

## SDK производителей

| SDK | Протокол и интеграция | Доступ к реализации | Применимость |
| --- | --- | --- | --- |
| **Espressif esp-aliro** | NFC Standard/Fast, LOAD CERT, lookup по keyslot, EXCHANGE/mailbox, Step-up API и extended/chained APDU. Есть связанный Matter door_lock example. BLE/UWB в официальном июльском анонсе отнесены к будущим выпускам. | Репозиторий и component LICENSE — Apache-2.0, но ядро поставляется готовыми `.a`; открыты API и примеры. В дереве есть библиотека ESP32-S3 для IDF 5.4. | Самый близкий эталон на нашей платформе. APDU callback позволяет сделать отдельный PN532-адаптер. Текущее ядро нельзя проверить по исходникам как DDK. [SDK](https://github.com/espressif/esp-aliro), [выбор бинарной библиотеки](https://github.com/espressif/esp-aliro/blob/db6bc8ea93854b730b9163d2472dfa7bff86a280/components/esp_aliro_lib/CMakeLists.txt), [API](https://github.com/espressif/esp-aliro/blob/db6bc8ea93854b730b9163d2472dfa7bff86a280/components/esp_aliro_lib/include/esp_aliro.h). |
| **Nordic Door Lock and Access Control Add-on** | Aliro, Matter или их сочетание; NFC, BLE, UWB, Thread/Wi-Fi. Есть абстракции периферии и готовая ST25R300-интеграция. | LicenseRef-Nordic-5-Clause привязывает использование к Nordic IC; бинарные части имеют дополнительные ограничения. | Изучать разделение provisioning/транзакции и тесты, либо использовать на Nordic. Перенос Nordic-кода в наш ESP32-проект не следует из публичности репозитория. [Архитектура](https://nrfconnectdocs.nordicsemi.com/addons/door_lock_and_access_control/latest/index.html), [лицензия](https://github.com/nrfconnect/ncs-door-lock-and-access-control/blob/main/LICENSE). |
| **ST X-CUBE-ALIRO** | Aliro 1.0 для STM32U5/WBA, NFC ST25R200/ST25R300, BLE RKE; Step-up Access/Revocation Documents. ST указывает сертифицированную конфигурацию с default features и совместимость с CSA test tool. | Пакет для платформ ST; UWB обозначен как ready for integration. Условия конкретных частей определяются Package_License. | Эталон потоков и работы с документами на отдельном ST-стенде. Готовый UWB-драйвер и право переноса на ESP32 из этих заявлений не следуют. [Пакет и поддерживаемые платформы](https://www.st.com/en/embedded-software/x-cube-aliro.html), [руководство UM3662](https://www.st.com/resource/en/user_manual/um3662-aliro-software-expansion-for-stm32cube-stmicroelectronics.pdf). |
| **Silicon Labs Aliro SDK 2.0.0** | Выпуск 3 августа 2026 реализует **Aliro 1.0.0**, NFC/BLE для EFR32 MG24/MG26, Matter over Thread, NFC Step-up и сценарии CSA certification. | Extension предоставляется через Sales по запросу. Используется SDK 2026.6.1 и Matter extension 2.9.1. | Полезен как функциональный эталон. Версия SDK 2.0.0 не означает Aliro 2.0. [Release notes](https://docs.silabs.com/sisdk-release-notes/latest/sisdk-aliro-release-notes/). |
| **NXP Autonomous Secure Access** | Архитектура MCX W + PN7642 + SR150 UWB + SE051S, Matter/Thread и provisioning platform. | Страница всё ещё помечена Preproduction / Aliro Technology Preview; конкретный комплект и SDK зависят от доступа NXP. | Ориентир для следующего аппаратного поколения с secure element. Это не готовая замена текущему PN532-стенду. [Решение NXP](https://www.nxp.com/design/design-center/development-boards-and-designs/AUT-SECURE-ACCESS). |

Пример esp-aliro использует M5Stack Unit NFC со ST25R3916; проверенные в нём
платы — NanoC6/NanoH2. Это существенно при переносе на PN532: наличие библиотеки
для ESP32-S3 не доказывает совместимость NFC front-end. Внешняя политика доступа
и корректный отзыв должны проверяться отдельно от успешного протокольного обмена.
[Описание примера](https://github.com/espressif/esp-aliro/blob/db6bc8ea93854b730b9163d2472dfa7bff86a280/examples/aliro_reader/README.md).

## Открытые реализации

| Проект | Практическая ценность | Существенные границы |
| --- | --- | --- |
| **rednblkx/DigitalDoorKey** | Используемая нами MIT-библиотека. Текущая upstream-ревизия совпадает с опубликованным pin `80e4a1e`. Отдельные NFC/BLE/UWB потоки и host tests. | Наш NFC-набор проходит; upstream BLE flow имеет две известные ошибки. Полная проверка права по Step-up документу в нашей интеграции не завершена; неизвестные ключи отклоняются. [Исходники](https://github.com/rednblkx/DigitalDoorKey), [наши патчи](../aliro-esp32/components/DigitalDoorKey/UPSTREAM.md). |
| **UltraWideLock** | Переносимое ядро и порты ESP32/Nordic, Apple Wallet, BLE/UWB, host tests, подписанные обновления и отдельный журнал аппаратной приёмки. Собственный код — ISC. | ESP32 evidence относится к approach unlock. PN532 имеет автоматизированные проверки, но не наследует аппаратный результат ST25R300. Сторонние Qorvo/Nordic компоненты сохраняют отдельные лицензии. [Проект](https://github.com/ultrawidelock/ultrawidelock), [hardware validation](https://github.com/ultrawidelock/ultrawidelock/blob/d6e23761e2ffbaee46db7ea4fd3807b0093e0659/docs/hardware-validation.md), [лицензии](https://github.com/ultrawidelock/ultrawidelock/blob/main/THIRD_PARTY_NOTICES). |
| **kormax/python-aliro-reader** | Python NFC reader с AUTH0/AUTH1/EXCHANGE/ENVELOPE и испытанным PN532 UART/USB-путём. Apache-2.0. Удобен для наблюдения APDU и сравнения поведения. | Сам не выдаёт wallet credentials; README прямо отмечает отсутствие issuer validation и частичное соответствие опубликованной спецификации. Использовать как исследовательский инструмент, а не готовый контроллер прав доступа. [README](https://github.com/kormax/python-aliro-reader). |
| **wqhqq1/openaliro** | Дополнительная линия кода Aliro/NFC/UWB с собственным ISC-кодом. | Более ранний fork, не независимый эталон нового UltraWideLock. Его LICENSE отдельно сохраняет ограничения Qorvo IC и Nordic IC. [Проект и условия](https://github.com/wqhqq1/openaliro/blob/main/LICENSE). |

### Проверка документов в UltraWideLock

В `ultrawidelock_stepup_verify` уже есть вычисление digest для disclosed items,
сверка DocType, проверка временного окна и validity iteration, а результат
разбит на отдельные причины отказа. Это полезная структура для закрытия нашей
текущей границы Step-up. При этом выбор сертификата/цепочки требует отдельной
проверки доверия; этот verifier нельзя переносить целиком как готовую политику
разрешения двери. [Проверенный исходник ревизии d6e2376](https://github.com/ultrawidelock/ultrawidelock/blob/d6e23761e2ffbaee46db7ea4fd3807b0093e0659/modules/ultrawidelock_cred/src/ultrawidelock_stepup.c).

UltraWideLock также явно отделяет development trust bypass от production
configuration и описывает обязательные собственные signing keys для обновлений.
Эта дисциплина полезна для нашего выпуска: показатели host tests должны стоять
отдельно от tap/reboot/отзыва на телефоне.
[Security policy](https://github.com/ultrawidelock/ultrawidelock/blob/d6e23761e2ffbaee46db7ea4fd3807b0093e0659/SECURITY.md).

## Приоритеты aliro.mayday

1. **Сначала сохранить iPhone и пройти Samsung.** Проверить старый ключ,
   перезапуск, disabled user и отзыв. Затем commission через имеющийся хаб,
   получить Samsung Wallet key и повторить ту же приёмку. Сравнить onboarding
   с официальными сценариями Aqara/Nuki, включая prerequisites.
2. **Сравнить DDK и esp-aliro на отдельном стенде.** Сохранить текущую прошивку
   и NVS без миграции. Через общий APDU-адаптер проверить выбор версии,
   ограничения PN532, Fast cache, bad signature и clear/reboot. Бинарное ядро
   Espressif не освобождает приложение от проверки разрешения доступа.
3. **Закрыть Step-up поэтапно.** Выделить проверку доверенного издателя,
   документов, времени/iteration и политики доступа; добавить негативные
   сценарии по этим границам. Использовать стандарт и открытые реализации как
   взаимную проверку, сохраняя чужие notices при переносе кода.
4. **Сделать полезный самостоятельный контроллер.** Подтвердить реальный
   исполнительный выход, понятную выдачу/отзыв и диагностику. Возможная ниша —
   подключение к существующим замкам и локальным автоматизациям. Это направление
   развития, а не уже доказанное преимущество над готовыми замками.
5. **Развивать UWB после устойчивого NFC.** UltraWideLock уже демонстрирует
   этот путь на ESP32. Для сравнения со Schlage важны намерение войти, сторона
   двери и поведение после перезапуска, а не одно измерение расстояния.

Публичный сайт нужно привести к этим статусам: iPhone — подтверждён прежним
стендом, Samsung — в проверке, Google Wallet — неподтверждён, UWB/relay protection
— отдельный будущий этап. Камеры и Prometheus следует описывать как планы до
появления соответствующей реализации и испытаний.

## Ревизии исследовательских проектов

| Проект | Ревизия на дату обзора |
| --- | --- |
| esp-aliro | `db6bc8ea93854b730b9163d2472dfa7bff86a280`, 19 августа 2026 |
| UltraWideLock | `d6e23761e2ffbaee46db7ea4fd3807b0093e0659`, 9 октября 2026 |
| Nordic add-on | `0d76535554a2dcc5438efc1c9dd1d742c78b624d`, 12 октября 2026 |
| DigitalDoorKey | `80e4a1e14eab3646b62ab3bdad6d2ed01d399375`, 18 сентября 2026 |
| Python Aliro Reader | `b6aa79dd0f4e5ffa291df6087b5d2a0ef63c9efa`, 14 марта 2026 |
| openaliro | `08089d3033ed23d641613e8ef79cebec0233fe19`, 21 августа 2026 |
