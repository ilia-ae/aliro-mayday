# Aliro reader wire spec (our implementation)

Single source of truth for the ESP32-S3 Aliro reader in
[aliro-esp32/](aliro-esp32/). Tracks CSA Aliro 1.0 (see
`docs/aliro-1.0-specification.pdf` — local only, not committed).

This document is the *implementation contract* — it pins down every
choice the spec leaves to the vendor (NVS layout, partition names,
ECP2 annotation, Matter attribute mapping, state-machine timings).
Change this file whenever one of those choices changes.

## 1. Transport + selection

### 1.1 NFC (expedited phase, MVP)

| Layer | Value | Source |
|---|---|---|
| Carrier | 13.56 MHz ISO/IEC 14443 Type A | spec §10 |
| Expedited AID | `A0 00 00 09 09 AC CE 55 01` (9 B) | spec §10.2.1, verified in trace examples |
| SELECT APDU | `00 A4 04 00 09 A0 00 00 09 09 AC CE 55 01 00` | spec §10.2.1.1 |
| Step-up AID | **deferred** (spec §10.2.1, only needed for Access/Revocation Documents) | — |

### 1.2 ECP2 polling annotation

Emitted continuously by the PN532 via `InCommunicateThru` so Aliro-
capable phones wake up without a tap gesture.

Current implementation in `aliro-esp32/main/NfcManager.cpp` is no longer
"TCI TBD" — it builds a full 18-byte ECP payload:

```
6A 02 CB 02 06 02 11 00 <fingerprint-8B> <crc16a-2B>
```

Fingerprint selection depends on the stored reader GID:

- **HomeKey-compatible / legacy mode (8-byte `reader_gid`)**:
  fingerprint = `reader_gid` directly
- **Aliro mode (16-byte `reader_group_identifier`)**:
  fingerprint = `SHA-256(reader_group_identifier)[0:8]`

This matches the current code path used to wake Apple Wallet on the
Aliro-oriented path. What is still pending is **real-device validation**
against multiple wallet / lock combinations, not the frame-construction
logic itself.

### 1.3 BLE + UWB (hands-free, §7.7 stretch)

Deferred. Spec §11. Needs external UWB chip (Qorvo QM35 or similar).
Not in MVP.

## 2. Cryptography

| Primitive | Curve / algorithm | Source in mbedtls |
|---|---|---|
| Reader key pair | ECDSA P-256 (secp256r1) | `mbedtls_pk_setup` + `mbedtls_ecdsa_sign` |
| Access Credential key pair | ECDSA P-256 | same |
| ECDH (session establishment) | P-256, ephemeral on both sides | `mbedtls_ecdh_compute_shared` |
| Session key derivation | HKDF-SHA256, per-direction (reader→device, device→reader) | `mbedtls_hkdf` |
| Session encryption | AES-128-GCM, independent counters per direction | `mbedtls_gcm_crypt_and_tag` |
| Transcript hash | SHA-256 | `mbedtls_sha256_*` |

Credential Issuer certificate validation (spec §6.5.1) is X.509 —
`mbedtls_x509_crt_verify`. For self-CA mode (our default) there is no
Issuer cert; the Access Manager is preloaded with endpoint pubkeys
directly via Matter `SetCredential`.

Current Aliro implementation note: the DDK StandardAuth key schedule on
the ESP32 side now depends on the **FCI Proprietary Template** from the
successful `SELECT` response. `NfcManager` parses the `A5` TLV value and
passes it into `DDKAuthenticationContext::setAliroFCI()`. If that value
is missing or wrong, the reader/device HKDF salt diverges and `AUTH1`
response decryption fails GCM authentication.

## 3. Expedited Phase state machine (spec §8.3)

```
  Reader (ESP32)                        User Device (phone)
  --------------                        -------------------
  SELECT(AID) ────────────────────────▶
                          ◀──────────── SELECT response + protocol hint
  AUTH0 { reader_eph_pub, salt } ─────▶
                          ◀──────────── AUTH0 response { expedited payload
                                                          OR status requesting
                                                          Step-up }
   │
   │ if status == `6984`: do not deny; enter Step-up / StandardAuth path
   │ derive ECDH shared secret
   │ derive session keys (HKDF-SHA256)
   │ sign transcript with reader_PrivK
   ▼
  AUTH1 { reader_signature } ─────────▶
                          ◀──────────── AUTH1 response { device_signature
                                                         over transcript }
   │
   │ verify signature against
   │   stored endpoint pubkey in NVS
   │ access decision
   ▼
  EXCHANGE { encrypted result } ──────▶   (ALLOW / DENY / CHALLENGE)
  CONTROL FLOW ───────────────────────▶   (apply unlock)
```

### 3.1 Our timing budgets

| Transition | Max | Rationale |
|---|---|---|
| SELECT → AUTH0 response | 200 ms | typical ISO7816 + UI latency |
| AUTH0 response → AUTH1 | 150 ms | ESP32 compute budget (ECDH + HKDF + ECDSA-sign) |
| AUTH1 → AUTH1 response | 300 ms | phone SE crypto + biometric prompt resume |
| Total transaction | 800 ms | hard reject beyond this (replay guard) |

Fresh reader ephemeral per transaction, never reused. Salt is 32 bytes
from `esp_random()` (hardware RNG).

### 3.2 Access decision logic

```
warm / cached path:
  DDK FastAuth uses the already-cached endpoint state

cold / first-tap path:
  successful `SELECT` + `AUTH0` may return `6984`, which means
  "no cached endpoint, continue into Step-up" rather than terminal deny
  the later decrypted payload provides `5A devicePk`, which becomes the
  endpoint identity for signature verification and persistence

match on resulting endpoint_pub in persisted issuer / endpoint state:
  found, type = AliroNonEvictableEndpointKey → ALLOW
  found, type = AliroEvictableEndpointKey    → ALLOW
  not found                                   → DENY (log 4-byte prefix)

(revocation via Access/Revocation Document — deferred to §7.6)
```

The current live blocker is finishing that cold-tap `6984` → Step-up
transition in the vendored DDK path; see
[docs/aliro-stdauth-finishing-roadmap.md](docs/aliro-stdauth-finishing-roadmap.md).

## 4. NVS layout

Two encrypted partitions (see
[aliro-esp32/partitions.csv](aliro-esp32/partitions.csv)):

### 4.1 `aliro_nvs` — reader config + credentials

Namespace `aliro_cfg` (reader keypair + group identifiers):

| Key | Type | Bytes | Source |
|---|---|---|---|
| `sign_priv` | blob | 32 | `SetAliroReaderConfig.SigningKey` |
| `sign_pub` | blob | 65 | `SetAliroReaderConfig.VerificationKey` (uncompressed SEC1) |
| `group_id` | blob | 8 or 16 | `SetAliroReaderConfig.GroupIdentifier` for Aliro; 8-byte HomeKey-compatible GIDs are also accepted by the current DDK-backed path |
| `group_sub_id` | blob | 16 | generated once via `esp_random`, fixed |
| `group_rslv_key` | blob | 16 | `SetAliroReaderConfig.GroupResolvingKey` (BLE privacy, ignored until §7.7) |

`group_id` deserves an explicit note: the current `NfcManager` accepts
**both** 8-byte HomeKey-style GIDs and 16-byte Aliro
`reader_group_identifier` values. The runtime auth mode is selected by
which `DDKAuthenticationContext` path is instantiated, not by GID length
alone, which lets the current firmware bridge the HomeKey baseline and
the Aliro pivot during bring-up.

`group_sub_id` needs its own note too: for the Aliro STANDARD path the
reader-side `0x4D reader_identifier` tag is built as
`reader_group_identifier || reader_group_sub_identifier`. The current
tree now passes the **full 16-byte** `group_sub_id` into DDK, so a true
Aliro configuration produces a 32-byte `0x4D` value. The 8-byte GID
compatibility mode is still useful during bring-up, but it should not be
treated as the final Apple Wallet contract.

Namespace `aliro_creds` (endpoint allowlist, up to 16 entries per
partition sizing — see `NumberOfAliroEndpointKeysSupported` attribute):

| Key | Type | Bytes | Source |
|---|---|---|---|
| `ep_<index>_type` | u8 | 1 | Matter credential type (6/7/8) |
| `ep_<index>_pub` | blob | 65 | phone pubkey from `SetCredential` |
| `ep_<index>_user` | u32 | 4 | Matter user_id |
| `ep_<index>_ct` | i64 | 8 | created_at, UNIX ms |

`index` is the Matter credential index (0..N-1, N =
`NumberOfAliroEndpointKeysSupported`).

### 4.2 `nvs_keys` — NVS encryption keys (ESP-IDF managed)

Written by ESP-IDF at first boot if flash encryption is on (§7.6
security hardening). Never touched by app code.

## 5. Matter DoorLock cluster mapping

Feature flag: `ALIRO` (bit 13, Matter 1.4 DoorLock rev 8+).

### 5.1 Cluster attributes we implement

| ID | Name | Our storage | Notes |
|---|---|---|---|
| `0x0080` | `AliroReaderVerificationKey` | `aliro_cfg/sign_pub` | 65 B uncompressed |
| `0x0081` | `AliroReaderGroupIdentifier` | `aliro_cfg/group_id` | 16 B |
| `0x0082` | `AliroReaderGroupSubIdentifier` | `aliro_cfg/group_sub_id` | 16 B, fixed after first boot |
| `0x0083` | `AliroExpeditedTransactionSupportedProtocolVersions` | hardcoded `[0x0100]` | Aliro 1.0 only |
| `0x0084` | `AliroGroupResolvingKey` | `aliro_cfg/group_rslv_key` | ignored in MVP |
| `0x0085` | `AliroSupportedBLEUWBProtocolVersions` | empty list | no BLE+UWB yet |
| `0x0086` | `AliroBLEAdvertisingVersion` | `0` | BLE not advertised yet |
| `0x0087` | `NumberOfAliroCredentialIssuerKeysSupported` | `5` (fixed) | current implementation cap |
| `0x0088` | `NumberOfAliroEndpointKeysSupported` | `16` (fixed) | current implementation cap |

### 5.2 Cluster commands we implement

| ID | Name | Behaviour |
|---|---|---|
| `0x28` | `SetAliroReaderConfig(SigningKey, VerificationKey, GroupIdentifier, GroupResolvingKey)` | write all four to `aliro_cfg`; on first call, generate and persist `group_sub_id`; invalidate all existing credentials |
| `0x29` | `ClearAliroReaderConfig` | wipe `aliro_cfg` and `aliro_creds` both |
| (existing) | `SetCredential(credentialType, credentialIndex, credentialData, …)` | if `credentialType ∈ {6, 7, 8}` → validate as P-256 pubkey, store in `aliro_creds` |
| (existing) | `ClearCredential(credentialType, credentialIndex)` | remove entry |
| (existing) | `GetCredentialStatus(credentialType, credentialIndex)` | report slot occupancy |

Both admin-only commands (`SetAliroReaderConfig`, `ClearAliroReaderConfig`)
are `timed`-invocation per the cluster spec.

### 5.3 Events we emit

| Event | Source | When |
|---|---|---|
| `LockOperation` | `Aliro` (10) | NFC tap → ALLOW |
| `LockOperationError` | `Aliro` (10) | NFC tap → DENY (unknown endpoint, replay, timeout) |
| `LockUserChange` | `Aliro` (10) | `SetCredential` / `ClearCredential` with an Aliro type |

## 6. Log format

Single JSON line per transaction, stdout → later routed to MQTT (§7.6):

```json
{
  "ts": 1714000000000,
  "phase": "auth_ok",
  "endpoint_prefix": "04a3b2c1",
  "rtt_ms": 412,
  "source": "aliro"
}
```

`endpoint_prefix` is the first 4 bytes of the matched endpoint pubkey,
hex, no separator — enough for forensics without logging full keys.

## 7. Integration surfaces (controller features)

The device is a **controller**, not just a reader. Three surfaces expose
controller state and accept control actions. See
[TODO.md](TODO.md) §7.8–§7.13 for implementation plans.

| Surface | Transport | Direction | Purpose |
|---|---|---|---|
| Matter DoorLock cluster | Matter / IPv6 / mDNS | in + out | Provisioning (reader config + credentials), lock/unlock commands, lock state attributes. Primary ecosystem integration. |
| Local HTTP API | HTTP(S) on LAN | in + out | On-net automation, scripts, Home Assistant. Bearer-token auth. Endpoints under `/api/v1/*`. WebSocket `/ws/v1/events` for live stream. |
| MQTT | MQTT over TCP/TLS | out (mostly) | Telemetry and alert fan-out. Topics: `aliro/<node>/event/<type>`, `aliro/<node>/alert/<severity>/<type>`. |
| Webhooks (outbound) | HTTPS POST | out | CCTV / NVR / IFTTT triggers. Up to 3 URLs, per-event-type filter. |
| Prometheus scrape | HTTP GET `/api/v1/metrics` | out | `aliro_access_total`, `aliro_alert_total`, `aliro_transaction_duration_seconds` histogram, etc. |

The `aliro-bridge/` service (optional, see TODO §7.13) subscribes to
MQTT and forwards to Telegram (Bot API + Mini App) and aggregates
long-term analytics. The ESP32 doesn't talk to Telegram directly.

## 8. Alert taxonomy

All alerts are written to the event log AND published on
`aliro/<node>/alert/<severity>/<type>`. Bridge dispatches critical ones
to Telegram.

| Type | Severity | Trigger |
|---|---|---|
| `replay` | warn | Same nonce / counter value seen twice within TTL |
| `signature_spray` | warn | 3+ failed AUTH1 ECDSA verifications in 60 s from one session |
| `unknown_probe` | warn | Same unknown pubkey denied 5+ times in 10 min (logged with 4-byte prefix) |
| `rate_limit` | warn | > N successful unlocks per minute (possible "hold phone, walk past" forced access) |
| `cert_invalid` | warn | `reader_Cert` or `Issuer_Cert` validation fails |
| `rtt_anomaly` | warn | AUTH0 → AUTH1 round-trip time > P99 × 2 of baseline (relay attack tell) |
| `session_interrupted` | info | AUTH0 completed but no AUTH1 received before field dropout |
| `auth0_flood` | warn | 10+ AUTH0 starts per minute without matching completions |
| `case_open` | **critical** | Tamper switch (GPIO) triggered |
| `vibration` | warn | Accelerometer (if fitted) detects shock > threshold |
| `power_anomaly` | info | Brown-out reset or voltage glitch |
| `provisioning_conflict` | info | Second commissioner tried to write `AliroReaderConfig` while one is already present |

Event payload (JSON, both for the ring buffer and webhook transport):

```json
{
    "ts": 1714000000000,
    "type": "alert.rtt_anomaly",
    "severity": "warn",
    "source": "aliro",
    "endpoint_prefix": "04a3b2c1",
    "detail": { "rtt_ms": 320, "p99_baseline_ms": 110 },
    "node": "aliro-xyz"
}
```

## 9. References

- **Aliro 1.0 spec** — `docs/aliro-1.0-specification.pdf` (CSA, local only).
  Key sections: §6 Trust Framework, §8.3 Expedited Phase, §10 NFC.
- **Matter DoorLock cluster** — [connectedhomeip data_model/1.5/clusters/DoorLock.xml](https://github.com/project-chip/connectedhomeip/blob/master/data_model/1.5/clusters/DoorLock.xml).
  Feature `ALIRO` bit 13, attrs `0x0080`–`0x0088`, cmds `0x28`/`0x29`.
- **Reference implementation** (Nordic-licensed, structure only) —
  [nrfconnect/ncs-door-lock-and-access-control](https://github.com/nrfconnect/ncs-door-lock-and-access-control).
- **Protocol research** — [kormax/aliro](https://github.com/kormax/aliro)
  (`OBSERVATIONS.md` for TCI hints and ECP2 annotation).
- **esp-matter SDK** — [espressif/esp-matter](https://github.com/espressif/esp-matter).
