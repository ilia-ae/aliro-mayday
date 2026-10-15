# Aliro competitors and implementations for aliro.mayday

[Русский](aliro-competitors-2026-10-13.ru.md) · [Documentation index](README.md)

Research dated **13 October 2026**; our implementation status is updated on
14 October. The most useful references are Espressif's esp-aliro as a second
protocol reference on ESP32 and UltraWideLock for portable architecture and
signed-document validation. Nuki and Aqara already offer Apple/Samsung Wallet
paths. This project needs proven Samsung support, reliable revocation and an
actual output to an existing lock to offer a useful alternative.

Aliro handles the local transaction with a phone; Matter handles commissioning,
configuration and lock control. A product supporting Matter does not prove
credential provisioning by every wallet. [Espressif explains the distinction](https://developer.espressif.com/blog/2026/07/espressif-aliro-solution/).

## Commercial products

| Product | Offered features | Conditions and limits | Useful comparison |
| --- | --- | --- | --- |
| **Aqara U400** | Apple Home Key and UWB; Samsung Wallet NFC announced on 2 March. CSA lists Aliro 1.0 certification dated 24 March 2026, firmware 3.1.0.4. | Samsung requires a compatible SmartThings gateway and updated phone/lock. The March announcement described Samsung UWB as future work; NFC support does not prove it shipped. | Samsung credential provisioning and first/repeated taps. [Samsung NFC](https://forum.aqara.com/t/aqara-smart-lock-u400-now-supports-samsung-wallet-digital-home-key/264249), [CSA record](https://csa-iot.org/csa_product/aqara-smart-lock-u400/), [Apple UWB](https://www.aqara.com/en/news/aqara-introduces-uwb-powered-smart-lock-u400-at-ces-2026/). |
| **Nuki Keypad 2 NFC** | External reader for a Matter-capable Nuki Smart Lock; Apple Home Key, Samsung Digital Home Key, fingerprint, PIN and Nuki App Mode. Nuki describes the keypad as Aliro-certified. | Wallet Mode requires the relevant Matter hub. App Mode works without one and follows app-user permissions. Power reserve is available on supported Wallet devices, not in App Mode. | Clear mode selection and prerequisites; reader separate from the lock mechanism. [Product/FAQ](https://nuki.io/en-us/products/keypad-2-nfc), [24 March announcement](https://nuki.io/en-us/company/press-releases/keypad-2-nfc). |
| **Schlage Sense Pro** | Matter over Thread and Apple hands-free unlock. Converge considers distance, direction and motion to infer entry intent. | At the review date, the page describes Samsung/Google Wallet as expected after Aliro certification. Android control in Schlage's app is not an Android Wallet key. | Future UWB should consider door side and trajectory, not proximity alone. [Technologies](https://www.schlage.com/en/home/smart-locks/sense-pro/technologies.html). |

Our iPhone path was demonstrated in May; Samsung needs testing through the
available hub and Google Wallet remains unproven. Generic Aliro compatibility
cannot justify marking all three as working on the website.

## Manufacturer SDKs

| SDK | Protocol/integration | Implementation access | Relevance |
| --- | --- | --- | --- |
| **Espressif esp-aliro** | NFC Standard/Fast, LOAD CERT, keyslot lookup, EXCHANGE/mailbox, Step-up API and extended/chained APDU; linked Matter door_lock example. The July announcement puts BLE/UWB in future releases. | Repository/component LICENSE is Apache-2.0; core is supplied as compiled `.a`, with public API/examples. ESP32-S3/IDF 5.4 binary is present. | Closest platform reference; APDU callback permits a separate PN532 adapter. Core cannot be audited from source like DDK. [SDK](https://github.com/espressif/esp-aliro), [library selection](https://github.com/espressif/esp-aliro/blob/db6bc8ea93854b730b9163d2472dfa7bff86a280/components/esp_aliro_lib/CMakeLists.txt), [API](https://github.com/espressif/esp-aliro/blob/db6bc8ea93854b730b9163d2472dfa7bff86a280/components/esp_aliro_lib/include/esp_aliro.h). |
| **Nordic Door Lock and Access Control Add-on** | Aliro, Matter or both; NFC, BLE, UWB, Thread/Wi-Fi; peripheral abstractions and ST25R300 integration. | LicenseRef-Nordic-5-Clause restricts use to Nordic IC; binary portions have further terms. | Study provisioning/transaction boundaries and tests, or use on Nordic. Public source does not authorize an ESP32 port. [Architecture](https://nrfconnectdocs.nordicsemi.com/addons/door_lock_and_access_control/latest/index.html), [license](https://github.com/nrfconnect/ncs-door-lock-and-access-control/blob/main/LICENSE). |
| **ST X-CUBE-ALIRO** | Aliro 1.0 on STM32U5/WBA, ST25R200/ST25R300 NFC, BLE RKE, Step-up Access/Revocation Documents. ST identifies a certified default-feature configuration and CSA test-tool compatibility. | ST platform package; UWB described as ready for integration. Package_License defines individual component terms. | Reference for document flows on an ST bench; claims do not establish a ready UWB driver or permission to port to ESP32. [Package](https://www.st.com/en/embedded-software/x-cube-aliro.html), [UM3662](https://www.st.com/resource/en/user_manual/um3662-aliro-software-expansion-for-stm32cube-stmicroelectronics.pdf). |
| **Silicon Labs Aliro SDK 2.0.0** | 3 August 2026 release implements **Aliro 1.0.0**; NFC/BLE on EFR32 MG24/MG26, Matter over Thread, NFC Step-up and certification scenarios. | Extension obtained through Sales; SDK 2026.6.1 and Matter extension 2.9.1. | Functional reference. SDK version 2.0.0 does not mean Aliro 2.0. [Release notes](https://docs.silabs.com/sisdk-release-notes/latest/sisdk-aliro-release-notes/). |
| **NXP Autonomous Secure Access** | MCX W + PN7642 + SR150 UWB + SE051S, Matter/Thread and provisioning platform. | Review-date page is marked Preproduction / Aliro Technology Preview; kit/SDK depends on NXP access. | Later secure-element hardware architecture, not a ready PN532 replacement. [Solution](https://www.nxp.com/design/design-center/development-boards-and-designs/AUT-SECURE-ACCESS). |

The esp-aliro example uses an M5Stack Unit NFC with ST25R3916 and names
NanoC6/NanoH2 as tested boards. A library for ESP32-S3 does not prove PN532
front-end compatibility. External access policy and revocation still require
independent checks. [Example](https://github.com/espressif/esp-aliro/blob/db6bc8ea93854b730b9163d2472dfa7bff86a280/examples/aliro_reader/README.md).

## Public implementations

| Project | Practical value | Significant limits |
| --- | --- | --- |
| **rednblkx/DigitalDoorKey** | Our MIT protocol library, pinned to `80e4a1e`; separate NFC/BLE/UWB flows and host tests. | Our supported NFC suite passes; upstream BLE-flow has two known failures. Our 14 October branch implements a documented subset of access policy and signed revocation; this is not full standard coverage or real-wallet proof. [Source](https://github.com/rednblkx/DigitalDoorKey), [patches](../aliro-esp32/components/DigitalDoorKey/UPSTREAM.md), [current policy](aliro-access-policy-2026-10-14.md). |
| **UltraWideLock** | Portable core with ESP32/Nordic ports, Apple Wallet, BLE/UWB, host tests, signed updates and hardware acceptance records; original code is ISC. | ESP32 evidence concerns approach unlock. PN532 automated tests do not inherit ST25R300 hardware evidence. Qorvo/Nordic dependencies retain separate terms. [Project](https://github.com/ultrawidelock/ultrawidelock), [hardware validation](https://github.com/ultrawidelock/ultrawidelock/blob/d6e23761e2ffbaee46db7ea4fd3807b0093e0659/docs/hardware-validation.md), [notices](https://github.com/ultrawidelock/ultrawidelock/blob/main/THIRD_PARTY_NOTICES). |
| **kormax/python-aliro-reader** | Python AUTH0/AUTH1/EXCHANGE/ENVELOPE reader, tested PN532 UART/USB path, Apache-2.0. Useful for APDU observation and comparison. | Does not issue wallet credentials; README states absent issuer validation and partial conformance. Research tool, not a completed access-policy controller. [README](https://github.com/kormax/python-aliro-reader). |
| **wqhqq1/openaliro** | Additional Aliro/NFC/UWB code line with original ISC code. | Earlier fork, not an independent reference for new UltraWideLock. LICENSE preserves Qorvo/Nordic IC restrictions. [Project/license](https://github.com/wqhqq1/openaliro/blob/main/LICENSE). |

### UltraWideLock document verification

`ultrawidelock_stepup_verify` computes disclosed-item digests, checks DocType,
time window and validity iteration and reports separate denial reasons. That
structure informed our verifier. Certificate/chain trust still needs its own
audit; the entire verifier cannot be copied as a finished door-access policy.
[Reviewed source](https://github.com/ultrawidelock/ultrawidelock/blob/d6e23761e2ffbaee46db7ea4fd3807b0093e0659/modules/ultrawidelock_cred/src/ultrawidelock_stepup.c).

UltraWideLock separates development trust bypass from production configuration
and requires private signing keys for updates. Its [security policy](https://github.com/ultrawidelock/ultrawidelock/blob/d6e23761e2ffbaee46db7ea4fd3807b0093e0659/SECURITY.md)
reinforces recording host tests separately from real tap/reboot/revocation tests.

## Priorities for aliro.mayday

1. Preserve iPhone compatibility and prove Samsung: old key, reboot, disabled
   user and revocation, then provision Samsung through the hub and repeat.
   Compare Aqara/Nuki onboarding, including prerequisites.
2. Compare DDK and esp-aliro on a separate bench, preserving firmware/NVS.
   Test version selection, PN532 limits, Fast cache, bad signatures and
   clear/reboot via a common APDU adapter. A binary core does not replace policy.
3. Validate our new document-policy subset on hardware; add optional capabilities
   separately with negative tests and retained upstream notices. Host implementation
   of rights/revocation is now complete within its documented scope.
4. Prove a physical output, clear provisioning/revocation and diagnostics.
   Integrating existing locks and local automations is a possible direction,
   not an already established advantage over commercial products.
5. Develop UWB after reliable NFC, with separate radio hardware. Compare entry
   intent, door side and reboot behavior, not just measured distance.

The live website still needs status corrections: iPhone refers to prior bench
proof, Samsung is pending, Google Wallet is unproven, UWB/relay protection is a
future stage. Cameras/Prometheus remain plans until implemented and tested.

## Reviewed revisions

| Project | Revision at review date |
| --- | --- |
| esp-aliro | `db6bc8ea93854b730b9163d2472dfa7bff86a280`, 19 August 2026 |
| UltraWideLock | `d6e23761e2ffbaee46db7ea4fd3807b0093e0659`, 9 October 2026 |
| Nordic add-on | `0d76535554a2dcc5438efc1c9dd1d742c78b624d`, 12 October 2026 |
| DigitalDoorKey | `80e4a1e14eab3646b62ab3bdad6d2ed01d399375`, 18 September 2026 |
| Python Aliro Reader | `b6aa79dd0f4e5ffa291df6087b5d2a0ef63c9efa`, 14 March 2026 |
| openaliro | `08089d3033ed23d641613e8ef79cebec0233fe19`, 21 August 2026 |
