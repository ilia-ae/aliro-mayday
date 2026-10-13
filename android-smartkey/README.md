# SmartKey — Android companion key

> **Status: deprioritised (April 2026).** Superseded by the Aliro pivot
> — see [../TODO.md](../TODO.md) §7 and
> [../PROTOCOL-aliro.md](../PROTOCOL-aliro.md). Android Aliro support
> arrives natively via Samsung Wallet and Google Wallet, so this custom
> HCE path is no longer the primary Android strategy. Kept as a fallback
> for phones without wallet-side Aliro support.

Kotlin app that emulates an NFC smart-key via Host Card Emulation (HCE)
and authenticates to the ESP32 + PN532 reader built from `homekey-esp32/`
using the protocol in [../PROTOCOL.md](../PROTOCOL.md).

## Target

- **Android 10+ minimum** (`minSdk 29`) — basic HCE with
  `setPreferredService` fallback
- **Android 15+ preferred** (`compileSdk 35`) — Observe Mode + Polling
  Loop Filters for locked-screen taps without wallet collision

## Build (when ready)

```bash
cd android-smartkey
gradle wrapper                 # first time only — generates ./gradlew
./gradlew assembleDebug
./gradlew test                 # JVM unit tests (no device needed)
./gradlew installDebug         # flash to a connected Android device
./gradlew connectedAndroidTest # on-device tests (HCE needs real hardware)
```

Or open `android-smartkey/` in Android Studio — it picks up the Gradle
setup automatically.

## Structure

```
app/src/main/
├── AndroidManifest.xml                    NFC + HCE + biometric + camera perms
├── res/
│   ├── xml/apduservice.xml                AID filter + PLF registration
│   └── values/strings.xml
└── kotlin/me/razqqm/smartkey/
    ├── MainActivity.kt                    Compose entry (placeholder UI)
    └── hce/
        ├── SmartKeyHostApduService.kt     HCE service — routes APDUs
        └── ApduCodec.kt                   Wire codec (SmartKey protocol v1)

app/src/test/kotlin/me/razqqm/smartkey/hce/
└── ApduCodecTest.kt                       JVM unit tests
```

## Status

**Phase-1 scaffold.** HCE service returns stub responses (version byte
+ zeroed nonce/eph-pub for SELECT; rc=0x05 rejection for OPEN). No real
crypto, no pairing, no UI beyond a placeholder. See
[../TODO.md](../TODO.md) §6 for the road forward.

## Relationship to Aliro

[../TODO.md](../TODO.md) §7 plans a separate `aliro-esp32/` build that
speaks the CSA Aliro standard to all three major wallets. That's the
long-term target. This SmartKey stack is a usable fallback for Android
devices that aren't Aliro-capable (everything pre-Play-Services-25.11 /
Samsung Wallet rollout) and a good place to practice the mbedTLS
primitives before the Aliro push.
