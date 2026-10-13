# SmartKey wire protocol — Android companion key

Wire spec shared by both sides of the custom Android key flow:

- **Reader**: ESP32 + PN532 (fork of rednblkx/HomeKey-ESP32, lives in `homekey-esp32/`)
- **Key**: Kotlin Android app (lives in `android-smartkey/`, TBD)

Runs **alongside** Apple HomeKey on the same reader — HomeKey keeps its own
AID and ECP annotation, this protocol uses its own. Both coexist in the
PN532 polling loop.

Protocol version: **`v1`** (April 2026). Values below are fixed; don't
reuse them for future incompatible revisions — bump the version byte.

---

## 1. Identifiers

### 1.1 Application ID (AID)

```
F0 53 4D 52 54 4B 45 59          (8 bytes)
^^  S  M  R  T  K  E  Y           ASCII "SMRTKEY"
|
└── 0xF0 prefix = proprietary unregistered RID (ISO/IEC 7816-5)
```

Hex string for `apduservice.xml` `<aid-filter>`: `F0534D52544B4559`.

### 1.2 ECP2 polling-loop annotation

The reader emits this frame in its polling loop (in addition to the
Apple HomeKey ECP frame). Android phones with our app installed match
it via a Polling Loop Filter; iPhones ignore it.

```
6A  02  C3  02  04  FE ED FA CE  00 01   <crc16>
│   │   │   │   │   └──────────┘  └───┘
│   │   │   │   │        │          │
│   │   │   │   │        │          └── Reader family (2 bytes, big-endian).
│   │   │   │   │        │              0x0001 = SmartKey v1.
│   │   │   │   │        │              Reserve 0x0002+ for future variants.
│   │   │   │   │        │
│   │   │   │   │        └── TCI magic (4 bytes): FE ED FA CE
│   │   │   │   │            Uniquely identifies our protocol family.
│   │   │   │   │
│   │   │   │   └── Subtype: 04 = "Home" (mirrors kormax's Apple subtype map)
│   │   │   └── Type: 02 = Access (same semantic as HomeKey ECP)
│   │   └── Configuration byte: 0xC3 (vendor, standard ECP2)
│   └── Version: 02 = ECP2
└── Header: 0x6A (ECP magic)
```

**Android PLF pattern** (lower-case hex, regex):
```
^6a02..0204feedface0001.*$
```

Register with `autoTransact=true` so Android automatically exits
Observe Mode and dispatches the subsequent SELECT APDU to our service.

### 1.3 Protocol version byte

Every message carries a 1-byte version at offset 0 of its body:

- `0x01` — this spec. Anything else → reader/phone rejects with `6A88`
  (referenced data not found).

---

## 2. Pairing (one-time, off-band)

User adds a new phone via the reader's web UI. Flow:

1. User clicks **"Add Android key"** in ESP32 web UI → reader generates:
   - `endpoint_id` — 8 random bytes (unique per phone credential)
   - Ephemeral use: reuses reader's long-lived P-256 keypair
     (one pair per device, stored in NVS on first boot)
2. Reader shows QR code encoding a base64url payload:
   ```
   ver(1)  reader_id(8)  reader_pub_compressed(33)
    01       <ESP MAC>      <compressed secp256r1 point>
   ```
   — 42 bytes total, ≈56 base64url chars. Fits comfortably in a QR.
3. Phone scans QR → parses payload → generates its own P-256 keypair
   in AndroidKeyStore with `PURPOSE_AGREE_KEY | PURPOSE_SIGN`,
   `setUserAuthenticationRequired(true)`, StrongBox-preferred.
4. Phone does ECDH: `Z = ECDH(phone_priv, reader_pub)` → 32 bytes.
5. Phone derives the long-term MAC key:
   ```
   mk = HKDF-SHA256(
       ikm   = Z,
       salt  = reader_id || endpoint_id,     // 16 bytes
       info  = "SmartKey/v1/mac",
       L     = 32
   )
   ```
6. Phone POSTs enrollment payload to reader's LAN web UI:
   ```
   POST /api/smartkey/enroll
   {
     "endpoint_id":   <8-byte hex>,
     "phone_pub":     <33-byte compressed hex>,
     "label":         "Ilya's Pixel",
     "attestation":   <optional signed blob, for KeyStore attestation>
   }
   ```
7. Reader stores `{endpoint_id, phone_pub, label, mk, created_at}` in
   NVS alongside its existing HomeKey issuers. Reader computes the same
   `mk` via ECDH on its side. User confirms on the web UI.

**Enrollment confirmation** must happen over the local network and
require the user to be physically present at the reader (manual click).
No over-the-air enrollment without user interaction.

**Revocation**: user clicks a row in the web UI → reader deletes the
`{endpoint_id, mk}` record. Next tap from that phone fails with `6982`.

---

## 3. Per-tap protocol

HCE wire direction is always **reader → phone** for commands; phone
replies in the response.  Two APDU round-trips per tap:

### 3.1 SELECT (pick our applet)

```
→ 00  A4  04  00  08  F0 53 4D 52 54 4B 45 59  00
  CLA INS P1  P2  Lc  -----AID (8 bytes)-----  Le         [reader → phone]

← 90 00                                                    [phone → reader]
```

Phone accepts silently; no payload.  If Android routing didn't land
on our service for some reason, Android itself would send `6A82`.

### 3.2 AUTH (challenge + response)

Reader issues the CHALLENGE:

```
→ 80  A0  00  00  11  01  <nonce(16)>  00                  [reader → phone]
  CLA INS P1  P2  Lc  ver ------- 17-byte body ------  Le

Lc = 17 (0x11). `nonce` is 16 cryptographically random bytes generated
by the reader on each tap.  The reader remembers `nonce` until the
phone replies (or 5 s timeout).
```

Phone reads `nonce`, looks up the stored long-term MAC key `mk` for
the matching reader, runs BiometricPrompt (if required by the KeyStore
binding), computes the tag, and responds:

```
← 01  <endpoint_id(8)>  <ts(4)>  <tag(16)>  90 00          [phone → reader]
  ver ----- 28-byte body ---------------     SW

ts   = phone's current Unix seconds, big-endian
tag  = HMAC-SHA256(mk, "OPEN/v1" || nonce || endpoint_id || ts_be)[0..15]
```

Total phone response body: 29 bytes + SW.

### 3.3 Reader verification

Reader looks up `mk` by `endpoint_id` and checks, in order:

1. `ver == 0x01`
2. `nonce` in the phone's reply matches the reader's stored nonce
   (implicit: reader recomputes tag using its own stored nonce;
    any mismatch → BAD_MAC)
3. `ts` within ±30 s of reader wall-clock (anti-replay; reader clock
   must be NTP-synced after WiFi is up)
4. Recomputed `HMAC-SHA256(mk, "OPEN/v1" || nonce || endpoint_id ||
   ts_be)[0..15] == tag`, using **constant-time compare**

If all pass → actuate lock.  Any failure → reject.

### 3.4 Result codes (phone-side logging only)

The reader logs the outcome locally (MQTT + `events.jsonl`) but the
phone only sees standard ISO 7816-4 status words on its side:

- `90 00` — phone's response accepted
- (nothing on the reader→phone rejection path — reader just doesn't
  issue a second CHALLENGE; the NFC session ends)

The phone has no way to distinguish an accepted tap from a rejected
one at the APDU layer.  That's deliberate: a fake reader should learn
as little as possible about whether `mk` is correct.  The phone's UI
surfaces tap completion via the OS's tap-success haptic, not a crypto
ack.

### 3.5 Why HMAC-only, no per-tap ECDH

Earlier drafts carried per-tap ephemeral ECDH + an HKDF-derived session
key, reasoning about forward secrecy.  Dropped because:

- **Forward secrecy protects encrypted content.**  We don't encrypt
  anything over NFC — we only authenticate.  HMAC tags reveal nothing
  about `mk`, so past tap traffic is useless to an attacker even if
  `mk` leaks later.
- **Replay is handled by `nonce` + `ts` windows.**  No need for
  ephemerals to get freshness.
- **Fewer failure modes.**  Per-tap keypair generation on Android is
  cheap but adds a BiometricPrompt-blocking `KeyAgreement` call and
  more surface area for UserNotAuthenticatedException.

`TapSigner.deriveSessionKey()` and per-tap HKDF code remain in the app
as dormant helpers — when/if we ever want to encrypt a command payload
(e.g. revoke-on-tap, rename-on-tap), they're ready.

---

## 4. Security notes

- **One tap, two secrets.** The long-term `mk` (shared at pairing time)
  authenticates the phone. The per-tap `sk` (derived from ephemeral ECDH)
  gives forward secrecy: compromising `mk` later doesn't decrypt
  historical tap traffic (though traffic itself carries no sensitive
  data — tags are short MACs over structured plaintext).

- **StrongBox / TEE.** Phone's private key is generated with
  `setIsStrongBoxBacked(true)` when available. Raw private key never
  leaves the secure element. ECDH + HMAC operations run inside the
  TEE via AndroidKeyStore.

- **Biometric gate.** Signing key has `setUserAuthenticationRequired(true)`,
  `setUserAuthenticationParameters(0, AUTH_BIOMETRIC_STRONG | AUTH_DEVICE_CREDENTIAL)`
  — one BiometricPrompt per tap. UX: phone tap → prompt → unlock in ~1s.

- **Replay windows.** 5-second nonce TTL, 30-second timestamp window.
  Both enforced by reader. Phone clock drift beyond 30s = legitimate
  taps fail — surface "check phone clock" hint in app.

- **Endpoint isolation.** Each phone has its own `endpoint_id` and `mk`.
  Compromise of one phone doesn't leak keys for others. Revocation is
  O(1): delete the row.

- **No secrets in transit on air.** The NFC link carries: nonce
  (public), ephemeral pubkeys (public), timestamp (public), endpoint ID
  (public, 8 random bytes per phone), 16-byte HMAC tag (proves knowledge
  of `mk` but reveals nothing). An eavesdropper learns only that "a
  tap happened". No UID-style persistent identifier leaked.

---

## 5. Interop with Apple HomeKey and (future) Aliro

- **HomeKey** runs on its own AID `A0000008580101` + Apple ECP
  annotation TCI `02 11 00`. Completely independent.
- **Aliro** (future Phase 3) will run on its own AID + TCI `20 42 20`
  when a public reader stack becomes available.
- This protocol occupies the `F0534D52544B4559` AID + TCI magic
  `FE ED FA CE 00 01` — reserved for our use. No collisions with either.

Reader polling loop emits all annotations it's been configured for;
each phone/wallet picks the one it understands and ignores the rest.

---

## 6. Reference implementations this borrows from

- **ECP2 frame format**: [kormax/apple-enhanced-contactless-polling](https://github.com/kormax/apple-enhanced-contactless-polling)
- **Observe Mode / PollingFrame Android reference**: [kormax/android-observe-mode-demo](https://github.com/kormax/android-observe-mode-demo)
- **Reader architecture**: [rednblkx/HomeKey-ESP32](https://github.com/rednblkx/HomeKey-ESP32) (this fork lives at `homekey-esp32/`)
