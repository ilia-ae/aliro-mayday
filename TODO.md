# TODO

> Historical bring-up/roadmap reference. Its status and assumptions predate the October document-policy update. Use the [current documentation](docs/README.md) for verified behavior, build instructions and remaining acceptance work.

Short, step-by-step plan. Tick items as they're done.

## 1. Hardware bring-up ✅ DONE

- [x] PN532: set DIP switches **SET0=H, SET1=L** (SPI mode)
      — initially flipped wrong, re-done 2026-04-23 and verified via boot log
- [x] Wire 6 required + 2 optional lines PN532 ↔ ESP32-S3 via Y65592 terminal block:
      `5V→5V`, `GND→GND`, `SCK→GPIO12`, `M→GPIO13`, `MO→GPIO11`, `NSS→GPIO10`,
      `IRQ→GPIO14`, `RST→GPIO9` (both optional, not used by stock firmware)
- [x] Connect ESP32-S3 via **left USB-C** (labelled `USB` — native USB, required for flashing)

## 2. First flash + pairing

- [x] Flash stock `rednblkx/HomeKey-ESP32` v0.8 via esptool CLI
      (`esp32s3.firmware.factory.bin` at `0x0`) — verified, boots to HomeSpan
- [x] PN532 responds on SPI (no error logs in boot; default log level is
      `ESP_LOG_ERROR` so success is silent)
- [x] `HomeSpan-Setup` WiFi AP live (SSID `HomeSpan-Setup`, password `homespan`)
- [x] Captive-portal recovery AP also observed (`HomeKey-ESP32`, password `homekey123`)
- [x] Ethernet baseline enabled on LilyGO T-ETH-Lite-ESP32S3
      (`T-ETH-Lite-ESP32S3` preset, DHCP LAN, mDNS reachable)
- [x] PN532 transport restored on the T-ETH-Lite Ethernet baseline with
      custom `SS/SCK/MISO/MOSI = 18/15/16/17`; old
      `Error establishing PN532 connection` loop no longer reproduces
- [ ] If re-provisioning the Wi-Fi-only path: join `HomeSpan-Setup` from phone and walk through captive portal:
      - Set WiFi credentials (home network)
      - Bump `logLevel` to INFO or DEBUG (to see HomeKey activity in logs)
      - NFC preset stays on `Default` (matches our wiring)
- [ ] Pair with Apple Home using the HomeKit setup code
- [ ] Add a HomeKey to the lock via the Home app, verify iPhone unlock works

## 3. Local build toolchain

- [ ] **Initialise git submodules inside `homekey-esp32/`** — subtree
      does NOT pull submodules. Without this step component dirs
      (`pn532_hal/`, `pn532_cxx/`, `DigitalDoorKey/`, `HomeSpan/upstream/`,
      `msgpack-c/upstream/`, `loggable/`, `loggable_espidf/`, `event_bus/`)
      are empty and `idf.py build` fails. Decide: (a) script that runs
      `git submodule update --init` in each, or (b) register them as
      submodules of the parent repo at `homekey-esp32/components/...`.
- [ ] `cd homekey-esp32 && nix develop` — check flake.nix brings up ESP-IDF cleanly
- [ ] Run `idf.py set-target esp32s3 && idf.py build` — confirm clean build
- [ ] `idf.py flash monitor` — flash our locally-built binary, re-test HomeKey

## 4. First local modification

- [ ] Decide what to change first (open question — list candidates below)
- [ ] Make the change under `homekey-esp32/`, commit as normal
- [ ] Build + flash + verify
- [ ] `just upstream-check` before/after to confirm subtree stays clean

## 5. Housekeeping

- [ ] Push `origin/master` (currently 2 commits ahead — subtree add)
- [ ] Remove or keep old standalone clone at `/path/to/HomeKey-ESP32`?
- [ ] Decide fate of `lock_mvp.py` / `webui.py` — retire, or keep as UID fallback?

## 6. Android support (Phase 2)

Goal: tap-to-unlock from Android phones too, not just iPhone.

**Strategy**: Path C — custom Android HCE app with our own AID. No
open-source Aliro reader stack exists yet. Reuse P-256 + AES-GCM
primitives already linked in the ESP32 firmware (libsodium + mbedTLS).

**Wire spec**: [PROTOCOL.md](PROTOCOL.md) — single source of truth
for both sides.

**Fixed values** (v1, April 2026):
- AID: `F0 53 4D 52 54 4B 45 59` ("F0SMRTKEY")
- ECP2 annotation: `6A 02 C3 02 04 FE ED FA CE 00 01`
- PLF regex: `^6a02..0204feedface0001.*$`

### Spec & design
- [x] Write wire protocol spec → [PROTOCOL.md](PROTOCOL.md)
- [x] Pick AID + ECP TCI + version byte

### Android app → [android-smartkey/](android-smartkey/)
- [x] New Android Studio project scaffold (Kotlin + Compose, compileSdk 35, minSdk 29)
- [x] Manifest: `NFC`, `NFC_HCE` feature, `USE_BIOMETRIC`, `CAMERA` (QR)
- [x] `res/xml/apduservice.xml` — AID filter + PLF pattern + `shouldDefaultToObserveMode=true`
- [x] `ApduCodec.kt` — SELECT + AUTH challenge parsers, AUTH response
      encoder; pure Kotlin with JVM unit tests (9 tests)
- [x] `crypto/Hkdf.kt` — HKDF-SHA256 via BouncyCastle; RFC 5869 vector test passes
- [x] `crypto/SecPoint.kt` — SEC1 compressed ↔ JCE `ECPublicKey` roundtrip,
      BouncyCastle decoder + JVM tests
- [x] `crypto/TapSigner.kt` — `tag()` (HMAC-SHA256 over OPEN/v1 transcript),
      `deriveLongTermMac()` (pairing HKDF); 8 JVM tests
- [x] `crypto/KeyStoreManager.kt` — P-256 keypair generation,
      StrongBox-preferred, biometric-bound, `PURPOSE_AGREE_KEY | PURPOSE_SIGN`
      (Android-only, instrumented tests needed — none written yet)
- [x] `pairing/PairPayload.kt` — 42-byte QR decoder/encoder + tests
- [x] `pairing/EndpointCredential.kt` — local credential record + alias helper
- [x] `SmartKeyHostApduService` wired to `TapSigner` (returns real
      AUTH response with stub `mk` until KeyStore + Room are connected)
- [ ] `PairingFlow.kt` — QR scan (CameraX + ML Kit), POST enroll to ESP32,
      persist `EndpointCredential` in Room
- [ ] Room DB schema + DAO for `EndpointCredential`
- [ ] Wire `BiometricPrompt` + `CryptoObject` into `handleAuth`
      (replaces the `signWithStoredCredential` stub)
- [ ] Register PLF programmatically; gate on `isObserveModeSupported()`;
      fallback `setPreferredService` for pre-Android-15 devices
- [ ] MainActivity UI: list of paired readers, "Add new reader" → QR scan

### ESP32 firmware → [smartkey-esp32-patch/](smartkey-esp32-patch/) (drafts, to be applied)
- [x] Draft `SmartKeyHandler.hpp/.cpp` — reader-side AUTH challenge/response
      for PROTOCOL v1 (HMAC-only, no per-tap ECDH)
- [x] Draft `nfcmanager-hook.patch` — branch on our AID after HomeKey's;
      calls `SmartKeyHandler::runAuthTap()`
- [x] Draft `ecp-annotation-hook.patch` — second `InCommunicateThru` in loop
- [x] Draft `cmake-add-handler.patch` — add `SmartKeyHandler.cpp` to SRCS
- [ ] Fill crypto in `verifyAuthResponse`: `mbedtls_md_hmac*` +
      `mbedtls_ct_memcmp` against recomputed tag
- [ ] Freshness check: `|ts - time(NULL)| <= 30`  (SNTP already runs at boot)
- [ ] NVS persistence: MessagePack serialisation of `EndpointRecord`
      under namespace `smartkey_endp`
- [ ] Apply patches into `homekey-esp32/` and verify local build succeeds
- [ ] Web UI: "Android keys" section — list/add/revoke; `/api/smartkey/enroll`
      POST endpoint; QR generation for pairing
- [ ] On AUTH OK → actuate via `LockManager`, publish MQTT + `events.jsonl`
      with `source: smartkey`

### Integration + hardening
- [ ] End-to-end tap test on real Android 15 device
      (Pixel 7+ / Samsung S24+ with `isObserveModeSupported()=true`)
- [ ] Fallback path for Android 10–14 devices (foreground +
      `setPreferredService`; degraded but works)
- [ ] Replay-protection check: try replaying an old APDU → must fail
- [ ] Clock-drift check: set phone +60s → must fail
- [ ] Revocation check: delete endpoint in Web UI → next tap fails

### Stretch
- [ ] Multi-reader support in one Android app (one credential per reader)
- [ ] Wear OS companion (same HCE service, smaller UI)
- [ ] Superseded by section 7 — custom-HCE becomes fallback once Aliro works

## 7. Aliro access controller (Phase 3 — main development target)

Scope reframing: **we're building a controller, not just a reader.** The
Aliro NFC transaction is the entry point; the product surface is the
controller — it opens the lock, manages credential lifecycle, emits
telemetry/events, detects tamper/attack patterns, and integrates with
the rest of the building stack (home automation, CCTV, messaging,
analytics).

Aliro 1.0 (26 Feb 2026, [spec PDF](docs/aliro-1.0-specification.pdf), 195 pp)
turns out to be doable end-to-end without CSA membership:

- **Trust:** spec §6.2 allows `reader_group_identifier` = reader's own P-256
  pubkey (self-CA mode). No global CA required.
- **Provisioning:** done through the standard **Matter DoorLock cluster**
  with the `ALIRO` feature (bit 13, Matter 1.4+). `SetAliroReaderConfig`
  (cmd 0x28) writes the reader keypair into the lock; `SetCredential`
  with type `AliroEvictableEndpointKey` / `AliroNonEvictableEndpointKey`
  adds phone pubkeys. Google Home / SmartThings / Apple Home act as the
  Matter commissioner-admin; Google/Samsung/Apple Wallet hold the phone
  private keys in SE/Knox.
- **Wire protocol:** ECC P-256 + AES-GCM + ISO7816 APDU — all in mbedtls.
- **Control plane:** local HTTP API, MQTT telemetry, Telegram bot +
  Mini App, CCTV triggers, tamper alerting, Prometheus metrics.
  Details in §7.8–§7.13.

References:
- Matter cluster: [connectedhomeip DoorLock.xml §ALIRO](https://github.com/project-chip/connectedhomeip/blob/master/data_model/1.5/clusters/DoorLock.xml)
- Nordic ref impl (nRF-only, Nordic-5-Clause license — structure only): [nrfconnect/ncs-door-lock-and-access-control](https://github.com/nrfconnect/ncs-door-lock-and-access-control)
- Protocol research: [kormax/aliro](https://github.com/kormax/aliro)

### 7.1 Platform — new `aliro-esp32/` project, zero changes to `homekey-esp32/`

- [ ] Create `aliro-esp32/` ESP-IDF project at repo root, fully separate
      from `homekey-esp32/`. Same MCU (ESP32-S3 DevKitC), same PN532 wiring,
      same GPIO map — we just swap firmware on the physical rig.
- [ ] Add `esp-matter` ([espressif/esp-matter](https://github.com/espressif/esp-matter))
      as an ESP-IDF component; it pulls upstream `connectedhomeip`
- [ ] Pin `connectedhomeip` to a tag with Matter 1.4+ DoorLock cluster
      (ALIRO feature shipped in 1.4 rev 8)
- [ ] Port PN532 SPI driver from `homekey-esp32/components/pn532_hal/` —
      **copy, don't modify the subtree**. Header note `// source:
      homekey-esp32/… @ <sha>` for later sync. Shared GPIO map
      12/13/11/10/14/9.
- [ ] `sdkconfig.defaults`: WiFi on (Matter IP transport), BLE on
      (Matter commissioning), flash encryption + NVS encryption on
      (to protect `reader_PrivK`)
- [ ] Minimal bring-up: "hello matter" boots, chip-tool pairs it via BLE,
      commissions to local WiFi
- [ ] Regression gate: flash `homekey-esp32/` back on the rig → iPhone
      HomeKey tap still works. Proves the platform swap didn't disturb
      hardware.

### 7.2 Matter DoorLock server with `ALIRO` feature — plan

Goal: any Matter commissioner-admin can configure the lock and push
phone credentials through standard cluster commands.

- [ ] Enable DoorLock cluster on main endpoint, set feature flag `ALIRO`
      (bit 13). Defer `ALBU` (bit 14, BLE+UWB) — no UWB hardware yet.
- [ ] Attribute storage in encrypted NVS namespace `aliro_cfg`:
  - `0x0080 AliroReaderVerificationKey` (65 B, reader pubkey — also
    equals `reader_group_identifier` in self-CA mode per Aliro §6.2)
  - `0x0081 AliroReaderGroupIdentifier` (16 B)
  - `0x0082 AliroReaderGroupSubIdentifier` (16 random B, set once on
    first provisioning, fixed afterwards)
  - `0x0083 AliroExpeditedTransactionSupportedProtocolVersions` →
    advertise `[0x0100]`
  - `0x0084 AliroGroupResolvingKey` (BLE privacy — store but ignore
    until BLE flow lands)
      - `0x0087 NumberOfAliroCredentialIssuerKeysSupported` = 5
      - `0x0088 NumberOfAliroEndpointKeysSupported` = 16
- [ ] Implement admin+timed commands:
  - `0x28 SetAliroReaderConfig(SigningKey, VerificationKey,
    GroupIdentifier, GroupResolvingKey)` — writes reader keypair into
    NVS; invalidates existing session state
  - `0x29 ClearAliroReaderConfig` — wipes everything (reader keys +
    all endpoint credentials)
- [ ] Extend `SetCredential` / `ClearCredential` / `GetCredentialStatus`
      for credential types 6 (`AliroCredentialIssuerKey`), 7 (evictable
      endpoint key), 8 (non-evictable endpoint key). Validate each pubkey
      is a valid P-256 point before storing.
- [ ] NVS record layout per credential: `{index, type, pubkey_65B,
      user_id, created_at}`
- [ ] Fire Matter events on unlock: `LockOperation` / `LockUserChange`
      with `OperationSource = Aliro` (enum value 10)
- [ ] chip-tool smoke test:
  - `chip-tool pairing ble-wifi <node-id> <ssid> <pass> 20202021 3840`
  - `chip-tool doorlock set-aliro-reader-config <...>`
  - `chip-tool doorlock set-credential 0 1 … aliro-evictable-endpoint-key …`
  - `chip-tool doorlock read aliro-reader-verification-key 1 1` →
    matches what we wrote

### 7.3 Aliro NFC reader transaction — plan

Goal: phone with provisioned credential taps PN532 → spec §8.3 Expedited
or Step-up path completes → endpoint pubkey matched / cached → unlock.

- [x] PN532 in initiator/reader mode, reuse SPI HAL from 7.1
- [x] Poll loop emits ECP2 annotation. Current implementation sends
      `6A 02 CB 02 06 02 11 00 <fingerprint-8B> <crc16a>` where the
      fingerprint is `reader_gid` directly for 8-byte HomeKey-style GIDs
      or `SHA-256(group_id)[0:8]` for 16-byte Aliro GIDs. Real-wallet
      validation is still pending.
- [ ] ISO7816 APDU dispatch per spec §10:
  - [x] `SELECT` currently prioritises the Aliro AID with HomeKey
        fallback, and the live iPhone path already reaches DDK auth
  - [ ] Final Aliro `CONTROL FLOW` (§10.2.2) semantics still need
        wallet validation, especially on cold-tap Step-up / deny / retry
- [ ] Expedited Phase state machine (spec §8.3):
  - [x] **AUTH0** request path is live in the current tree; iPhone cold
        taps currently answer `6984` after successful `SELECT`
  - [x] **AUTH1** / StandardAuth machinery exists in vendored DDK, and
        the current tree now feeds it the SELECT-response FCI `A5`
        template plus the full `reader_group_sub_identifier`
  - [x] Session-key schedule in the current tree includes the parsed FCI
        template needed for Aliro HKDF salt generation
  - [x] First-tap helper exists in `StandardAuth.cpp`: when a later
        payload exposes `5A devicePk`, the reader can tentatively add an
        endpoint for signature verification
  - [ ] Treat `AUTH0 = 6984` as Step-up, not terminal `cmdFlowFailed`
  - [ ] Confirm how Step-up returns endpoint ephemeral data and the
        Access Document on the cold path, then implement that APDU sequence
  - [ ] Prove warm-tap FastAuth after a successful cold-tap cache fill
  - [ ] Final **EXCHANGE** / **CONTROL FLOW** success path validated on
        real wallets
- [ ] Access decision:
  - prove the real-wallet endpoint / issuer mapping against persisted
    NVS state, not just local stubs / assumptions
  - endpoint pubkey matches an Aliro endpoint key in NVS → `AUTH_OK`
  - no match → `DENY`, logged with 4-byte pubkey prefix for forensics
  - revocation via Access/Revocation Document (§7) — **deferred to 7.6**,
    MVP is "no revocation, just allowlist"
- [x] On `AUTH_OK` the local actuator hook is already wired:
      `lockind::nfc_tap_unlock` toggles lock state with
      `OperationSource = Aliro`; real-wallet validation is still pending
- [ ] Crypto: mbedtls only — P-256 (ECDH + ECDSA), AES-GCM, HKDF-SHA256.
      No new dependencies.
- [ ] Freshness / replay protection:
  - fresh ephemeral keypair per transaction (never reused)
  - reject if > 500 ms between AUTH0 and AUTH1 (spec hint + sanity)
  - 16-byte reader-side challenge mixed into transcript
- [ ] Structured log line per transaction:
      `{ts, phase, endpoint_prefix, result, rtt_ms}` → stdout for now,
      MQTT in 7.6
- [x] Tap-focused serial-log parser exists:
      `just diag-tap` / `scripts/aliro-tap-diag.py`
- [ ] Step-up phase (§8.4) — no longer deferred. It is the current Apple
      cold-tap blocker after `AUTH0 = 6984`; see
      `docs/aliro-stdauth-finishing-roadmap.md`

### 7.4 `homekey-esp32/` — frozen as regression / hardware baseline

Anchor for "is the rig still sane?" checks. No code changes during
Aliro work.

- [ ] Before each `aliro-esp32/` flash session: flash `homekey-esp32/`
      first, confirm iPhone HomeKey tap works → wiring/PN532/power OK.
      If it fails, fix the rig before touching Aliro code.
- [ ] `just upstream-pull` keeps working — rednblkx upstream fixes
      still flow in
- [x] One-line swap scripts exist:
      `scripts/flash-homekey.sh` and `scripts/flash-aliro.sh`
- [ ] README: document the swap procedure so this isn't folklore

### 7.5 End-to-end testing on real devices (no emulator loop)

Both test devices are on hand. Skip self-emulation, go straight to
phones.

- **Samsung Fold 7** (Knox + Samsung Wallet + SmartThings):
  - [ ] Install SmartThings, add ESP32 as Matter door lock via QR code
        printed at boot (esp-matter generates it)
  - [ ] Verify SmartThings exposes the lock's `ALIRO` feature and
        prompts "Add to Samsung Wallet"
  - [ ] Complete wallet onboarding → confirm `SetCredential` with
        `AliroEvictableEndpointKey` arrives in our NVS (log attribute-
        change on cluster side)
  - [ ] Tap Fold 7 on PN532 → expected: `AUTH_OK` in log,
        actuator fires
  - [ ] Delete key from Samsung Wallet → next tap: `DENY`
- **iPhone (latest)** (SE + Apple Wallet + Home app):
  - [ ] Home app adds our Matter lock
  - [ ] If Apple Home offers "Add Home Key" for a non-HomeKey Matter
        lock (i.e. the Aliro path through Apple Wallet) — use it
  - [ ] If Apple Home instead falls back to HomeKey — record the
        observed flow, ticket it, stay on SmartThings path for now
  - [ ] Tap iPhone on PN532 → expected: `AUTH_OK`
- [ ] Cross-device: same lock, enrolled from both phones simultaneously
      → both taps authenticate against different entries in NVS
- [ ] Regression: flash homekey-esp32 back → iPhone HomeKey tap still
      works → rig proven intact end-of-day

### 7.6 Integration & hardening

- [ ] Factory reset via hardware (long-press on a GPIO button) **or**
      Matter `ClearAliroReaderConfig` + leave-fabric sequence — nukes
      Matter fabric + Aliro NVS together
- [ ] MQTT bridge mirroring homekey-esp32 topic layout:
      `{source: "aliro", endpoint_prefix, result, ts}`
- [ ] OTA updates via Matter OTA Requestor cluster (esp-matter has this
      baked in) → no more USB-C flashing for firmware bumps
- [ ] Revocation: handle step-up Access/Revocation Document flow from
      §7.6 + §8.4 once we see an ecosystem actually push one
- [ ] Security audit pass: confirm `reader_PrivK` never leaves the
      encrypted NVS partition, verify GCM nonce never repeats, check
      ECDSA nonce uses a deterministic RFC 6979 variant or good RNG

### 7.7 Stretch (deferred — not MVP)

- [ ] BLE + UWB hands-free flow (`ALBU` feature, spec §11) — needs Qorvo
      QM35 or similar UWB chip wired to ESP32; Qorvo driver is proprietary
      (see Nordic's `west.yml`)
- [ ] Multi-reader install with shared `reader_group_identifier` (spec §6.2
      allows up to 16 bound to one credential) — for multi-door setups

### 7.8 Local HTTP API — plan

Goal: on-LAN control and observation without any cloud round-trip. Lives
on the ESP32 itself via `esp_http_server`. Always available (even if
MQTT broker / bridge is down). Primary consumers: Home Assistant,
scripts, curl, the web UI later.

- [ ] Auth: Bearer token stored in encrypted NVS (`aliro_cfg/api_token`).
      Generated at first boot, printed to serial + Matter Basic
      Information attribute `ReachableChangedEvent` for commissioner
      retrieval. Rotatable via `POST /api/token/rotate` (admin).
- [ ] Transport: HTTP on port 80 by default. Optional TLS via
      `esp_https_server` (self-signed cert generated at commissioning,
      pinned by the client on first use).
- [ ] mDNS: advertise as `aliro-<node_id>._http._tcp.local` so LAN
      clients can discover without IP hardcoding.
- [ ] Endpoints (all JSON, v1 under `/api/v1/`):
  - `GET /api/v1/status` — lock state, uptime, last event ts, firmware rev
  - `POST /api/v1/unlock` — manual unlock (admin). Body: `{ reason?, duration_ms? }`
  - `POST /api/v1/lock` — force lock (admin)
  - `GET /api/v1/events?after=<ts>&limit=<n>` — paginated event log (default n=50, max 500)
  - `GET /api/v1/credentials` — list Aliro endpoint pubkeys with metadata (first 4 bytes of pubkey, user_id, created_at, last_used_at, counter). **Never exposes private keys.**
  - `DELETE /api/v1/credentials/:index` — revoke a credential (mirrors `ClearCredential`)
  - `GET /api/v1/metrics` — Prometheus text exposition (see §7.12)
  - `GET /api/v1/health` — liveness probe, unauthenticated
- [ ] WebSocket `/ws/v1/events` — real-time event stream for dashboards.
      Authenticated via query `?token=...` (bearer in query since WS
      doesn't carry Authorization header easily).
- [ ] Rate limiting: 10 req/s per client IP via sliding window in RAM.
      Blocks brute-force token guesses.
- [ ] Docs: OpenAPI 3.1 spec file alongside the code
      (`aliro-esp32/docs/openapi.yaml`) so clients can codegen.

### 7.9 Telegram integration (two variants)

Two uses of Telegram, both wanted, architected to share the same bridge
service (see §7.13). Telegram chosen because it already has push
notifications on phones the user carries every day — no extra app.

- [ ] **Variant A — Notifications only (outbound, always on).**
  - ESP32 publishes events to MQTT.
  - Bridge subscribes, filters by severity/type, sends via
    [Telegram Bot API](https://core.telegram.org/bots/api)
    `sendMessage` to allowed `chat_id`s.
  - Message types: access granted (who + time + door), denied attempt,
    tamper alert, daily digest, system health (offline/online).
  - Zero dependencies on the ESP32 side — just keeps publishing MQTT
    like today.
- [ ] **Variant B — Bot + Mini App (bi-directional, full control).**
  - Bot commands: `/status`, `/unlock`, `/lock`, `/who`, `/last 20`,
    `/revoke <index>`, `/alerts on|off`. Admin-only via allowlist.
  - [Telegram Mini App](https://core.telegram.org/bots/webapps): opens
    a WebApp inside Telegram showing lock state, live event feed,
    credentials list, one-tap unlock. Authenticates via
    `Telegram.WebApp.initData` (signed by BotFather key — no extra
    login). Served from the bridge or from Cloudflare Worker.
- [ ] Allowlist of `chat_id`s / user_ids in bridge config — no public bot.
- [ ] Bridge deployment options:
  - Docker on NAS / Raspberry Pi (Node.js, persistent)
  - Cloudflare Worker (stateless webhook — good for bot API, can serve
    the Mini App's static assets too; needs an external MQTT broker
    with HTTP webhooks or TCP-over-workers)
- [ ] Fallback: if Telegram API unreachable, queue up to 50 events
      locally on bridge, replay when connectivity returns.

### 7.10 Tamper & anomaly detection — plan

Detect attacks / physical interference, log them as first-class events,
route to Telegram as high-priority alerts.

- [ ] **Protocol-level (software on ESP32):**
  - [ ] Replay attempts: per-endpoint monotonic counter; reject
        identical nonces within TTL
  - [ ] Failed-signature tracker: 3+ failed AUTH1 ECDSA verifications
        within 60 s from same session → `alert.signature_spray`
  - [ ] Unknown-endpoint probing: same-pubkey denied tap 5+ times in
        10 min → `alert.unknown_probe` with the 4-byte pubkey prefix
        for forensics
  - [ ] Abnormal rate of granted unlocks: >N unlocks/min → `alert.rate_limit`
        (possibly forced "hold phone and walk past" attack)
  - [ ] Invalid cert presentations: reader_Cert chain validation fails
        → `alert.cert_invalid`
- [ ] **Radio-level (probabilistic relay detection):**
  - [ ] Track AUTH0→AUTH1 RTT per session; maintain P50/P99 baseline
        (first 50 successful transactions). Flag outliers beyond
        P99 × 2 as `alert.rtt_anomaly` — relay attacks add 100+ ms
        of latency per spec analysis
        ([NFC relay paper](https://vwzq.net/papers/rfidsec15-practical-nfc-relay.pdf))
  - [ ] NFC field dropout mid-transaction: state machine sees AUTH0
        completed but no AUTH1 within timeout + field-off signal from
        PN532 → `alert.session_interrupted`
  - [ ] Multiple AUTH0 starts without AUTH1: 10+ in a minute → probing
- [ ] **Physical-level (hardware, requires case support):**
  - [ ] GPIO tamper-switch input (wired to NO switch inside the case)
        → `alert.case_open`
  - [ ] Optional I²C accelerometer (LIS2DH12) for shock/drill detection
        → `alert.vibration`
  - [ ] Brown-out detector event → `alert.power_anomaly`
- [ ] **Response policy (configurable):**
  - Severity: `info` / `warn` / `critical`. All written to event log.
  - MQTT topic `aliro/<node>/alert/<severity>/<type>` always published.
  - `critical` → audible buzzer pulse + red LED flash + Telegram
    push-notification-style message (with silent=false).
  - Optional "lockdown mode": after N critical alerts within M minutes,
    reject all unlocks until admin acknowledges via API/Telegram.
- [ ] Document the full alert taxonomy in [PROTOCOL-aliro.md](PROTOCOL-aliro.md) §8.

### 7.11 CCTV / video surveillance integration — plan

Three layers, picked per deployment. All share the same event payload.

- [ ] **Layer 1 — Generic webhook (universal).**
  - Settings: up to 3 webhook URLs, configurable per-event-type.
  - ESP32 POSTs JSON on each event:
    `{ ts, event: "access.granted|access.denied|alert.*", endpoint_prefix, door_id, severity }`.
  - Works with any NVR/automation platform that accepts HTTP triggers.
- [ ] **Layer 2 — Frigate NVR** (popular open-source option).
  - On access event → bridge POSTs to
    `http://<frigate>:5000/api/events/<camera>/forced/create` with
    `{ label: "aliro-access", sub_label: "<endpoint_prefix>",
       duration: 15, include_recording: true }`
  - Creates a bookmark in Frigate timeline tagged with the
    endpoint prefix, recording window centred on the event.
  - [Reference](https://docs.frigate.video/integrations/api/).
- [ ] **Layer 3 — Home Assistant bridge** (multi-camera orchestration).
  - ESP32 MQTT → HA automation triggers → any HA-supported camera:
    UniFi Protect, Reolink, ONVIF, Hikvision ISAPI, Dahua DH-API,
    Tapo, etc.
  - HA config template ships in `aliro-esp32/docs/ha-template.yaml`.
  - [Reference](https://community.home-assistant.io/t/unifi-protect-custom-webhook-vs-home-assistant-trigger/847025).
- [ ] Pre/post-roll: webhook payload carries `pre_seconds` / `post_seconds`
      hints; receivers that support continuous recording (Frigate with
      `record.events`) create clips around the event.
- [ ] Privacy switch: allow disabling CCTV triggers per credential
      (e.g., owner's own key doesn't trigger surveillance alert).

### 7.12 Analytics & metrics — plan

- [ ] **On-device event log** (spec'd in [PROTOCOL-aliro.md](PROTOCOL-aliro.md) §6):
      NVS ring buffer, last 1000 events (~100 KB), survives reboot.
- [ ] **Prometheus exporter** at `GET /api/v1/metrics`:
  - `aliro_access_total{result,endpoint}` counter
  - `aliro_alert_total{type,severity}` counter
  - `aliro_transaction_duration_seconds` histogram (AUTH0→CONTROL FLOW)
  - `aliro_credentials_provisioned` gauge
  - `aliro_uptime_seconds` gauge
  - `aliro_nfc_taps_total` counter (includes non-Aliro taps)
- [ ] **Grafana dashboard JSON** ships in
      `aliro-esp32/docs/grafana-dashboard.json` — 6-panel default
      (taps/hour, access/deny split, RTT histogram, alert timeline,
      credential age, recent events table).
- [ ] **Long-term storage** (optional, bridge-side):
  - Bridge persists every event to SQLite / Postgres / InfluxDB.
  - Configurable retention (default 90 days).
  - SQL / Flux queries in `aliro-bridge/docs/analytics.md` for common
    reports ("most-active user", "denied attempts by hour", etc).

### 7.13 `aliro-bridge/` — new track (optional backend service)

New top-level dir `aliro-bridge/` alongside `aliro-esp32/` and
`aliro-site/`. The ESP32 works standalone for LAN use; the bridge adds
remote access, multi-controller fan-in, and smart routing.

- [ ] Stack: Node.js + TypeScript (consistent with `aliro-site/`).
      ESM, `mqtt` package, Telegram Bot API HTTPS calls, `express` for
      webhook receiver + Mini App static assets.
- [ ] Inputs:
  - MQTT subscribe `aliro/+/event/#` + `aliro/+/alert/#`
  - Telegram webhook POST receiver (Bot API)
  - REST endpoints for Mini App (`/miniapp/api/*`)
- [ ] Outputs:
  - Telegram Bot API `sendMessage` (with deeplinks to Mini App)
  - Configurable webhooks (CCTV, IFTTT, custom)
  - Optional Prometheus remote-write (for long-term metric storage)
- [ ] Config: YAML. Defines controllers (MQTT topic prefixes → device IDs),
      Telegram chats (chat_id → role: owner/viewer), webhooks
      (URL + event filter), schedules, etc.
- [ ] Deployment targets:
  - Docker Compose (bridge + mosquitto MQTT + sqlite volume) —
    drop-in for NAS / Raspberry Pi
  - Cloudflare Workers deployment (split: stateless bot-webhook Worker
    + Durable Object for MQTT session state) — for users who don't
    want self-hosted infra
- [ ] Mini App source lives under `aliro-bridge/miniapp/` (another
      Angular project? probably Lit or Svelte for smaller bundle —
      Mini Apps load inside Telegram; size matters).
- [ ] Security:
  - All admin actions require Telegram `initData` signature verification
  - No public endpoints except Telegram webhook (Telegram IP-restricted)
  - MQTT creds in env vars / secrets, never in YAML config
- [ ] Observability: bridge exposes its own `/metrics` for Prometheus
      (connection health, message rates, Telegram API errors).

## Open questions / modification candidates

*(fill in as ideas come up)*

- **Enable PN532 IRQ** — `SpiTransport` HAL supports it, but `NfcManager.cpp`
  currently hardcodes `GPIO_NUM_NC`. Line is **already wired to GPIO14**
  physically; just needs a source patch (pass `GPIO_NUM_14` instead of
  `GPIO_NUM_NC`) + rebuild. Lower CPU, faster tap-wake, better sleep.
- **Hardware-reset PN532 via GPIO 9** — line wired, firmware doesn't use
  it. Add a "reboot NFC" button in the WebUI that pulses GPIO 9 low for
  ~10ms to hard-reset the chip if it ever hangs. Nice recovery option.
- Custom actuator driver (relay / GPIO / HTTP hook on AUTH_OK)
- MQTT topic layout changes to fit existing home-automation setup
- Multi-lock support on one ESP32?
- Logging/telemetry — ship events.jsonl-style feed over MQTT?
- Visual feedback (RGB LED on auth / deny / enrol)
