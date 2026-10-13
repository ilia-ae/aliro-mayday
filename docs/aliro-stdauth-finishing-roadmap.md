# Aliro StandardAuth — finishing the kAliro path

Working roadmap for completing the Aliro Step-up authentication flow in
`components/DigitalDoorKey`. Generated 2026-04-28 from a live tap-debug
session against iPhone 16 Pro Max + Apple Wallet HomeKey-as-Aliro, then
updated after landing the FCI parse, full reader-identifier sizing, and
first-tap helper patches in this repo.

The goal: take a cold (no cached endpoint) iPhone tap from
`SELECT applet → 9000` to `lock-hdl: ep=1 -> UNLOCKED (src=10)`.

---

## 1. What already works today

Verified live, 2026-04-28:

| Layer | Status | Evidence |
|---|---|---|
| Matter pair (BLE → CASE → fabric) | ✅ | `fabric committed to persistent storage` |
| `SetAliroReaderConfig` Matter command | ✅ | `aliro: SetAliroReaderConfig OK — reader provisioned` |
| `SetCredential(kAliroCredentialIssuerKey)` | ✅ | `Aliro issuer key idx=1 synced (pk=65B, id=08F92772..) — total issuers=1` |
| Reader keys + issuer key persisted in NVS | ✅ | `Aliro credentials replayed: 1 issuer key(s)` after reboot |
| ECP wakeup with **Aliro TCI `20 42 20`** | ✅ | iPhone Wallet wakes Aliro applet (was waking HomeKey applet on TCI `02 11 00`) |
| SELECT AID `A0 00 00 09 09 AC CE 55 01` | ✅ | `Aliro applet selected successfully.` |
| FCI `A5` template extracted from SELECT response | ✅ | `Parsed FCI A5 template: ...` in live logs |
| AUTH0 APDU sent to iPhone | ✅ | full TLV (`41/42/5C/87/4C/4D`) on the wire |
| iPhone responds | ✅ | `D5 41 00 6984` |
| Full 16-byte `reader_group_sub_identifier` now reaches DDK | ✅ | `AliroDelegate::syncToReaderDataManager()` now mirrors all 16 bytes into `readerData.reader_id` |
| First-tap provisional endpoint helper exists | ✅ | `StandardAuth.cpp` can tentatively add `5A devicePk` when the endpoint is not cached |
| DoorLock cluster wired to NFC tap subscriber | ✅ | `lockind::nfc_tap_unlock` → `OperationSourceEnum::kAliro` |
| Chip stack lock taken when DoorLockServer fires | ✅ | `PlatformMgr().ScheduleWork(...)` (no more `chipDie/abort`) |

## 2. Where it dies

Current failure on the wire (from `serial.log` uptime 667266 onwards):

```
SELECT  → 9000 (FCI: A000000909ACCE5501, version 0009)
AUTH0   → 6984          ← Apple: "no cached endpoint, please step up"
DDK     → 803C00000641010042010 1   ← cmdFlowFailed
Apple   → 9000
DDK     → "Response not valid, something went wrong!"
                                lockind: red 1s reject
```

The `6984` is **not** an error — it's Apple's signal that the
**Expedited Phase had no cached endpoint** and the reader should
proceed to the **Step-up Phase**. DDK currently reads `6984` as
"failure", sends `cmdFlowFailed`, and exits.

### Code site

`components/DigitalDoorKey/src/auth/AuthContext.cpp:172`

```cpp
nfc(apdu, response, false);
// ...
if (response.size() > 64 && response[0] == 0x86) {       // ← guard
    // Path that runs FastAuth, then StdAuth, then ATTESTATION
    ...
} else {
    // Falls through to commandFlow(kCmdFlowFailed) at line 269
}
```

A `6984` response is 2 bytes long → fails `response.size() > 64` →
StandardAuth never gets called → `cmdFlowFailed` is sent → DDK returns
`kFlowFailed`.

That means the current blocker is now narrower than it was in the first
trace: the support fixes around keying material and endpoint handling are
already in this repo, but the `6984` → Step-up transition itself is still
missing in the vendored DDK control flow.

### What the StandardAuth.cpp kAliro path already does (lines 125–368)

- ✅ Builds STD-TLV (`4D readerId`, `86 endpointEphX`, `87 readerEphX`,
  `4C txnId`, `93 readerCtx`)
- ✅ Signs the TLV with `reader_sk`, wraps in `9E sig`, prefixes
  `41 01 01` for Aliro
- ✅ Sends AUTH1 APDU `80 81 00 00 [Lc] [41 01 01] [9E sig]`
- ✅ Derives `skReader` / `skDevice` via X9.63 KDF + HKDF (`Volatile****`)
- ✅ Decrypts the response with `DigitalKeySecureContext`
- ✅ Looks up endpoint by **public key match** (`5A devicePk` TLV)
  in `readerData.issuers[].endpoints[]`
- ✅ Verifies signature against found endpoint's pubkey

But there are still **two** unfinished pieces on this path for
"first-tap-and-also-cold-cache":

1. **`endpointEphPubKey` is required as input to StdAuth** but on a
   `6984` AUTH0 we never received it. AUTH1 can't be signed without it.
   The Aliro spec must specify a way to receive the endpoint ephemeral
   pubkey separately during step-up — likely via an additional Step-up
   Initiation APDU between AUTH0 and AUTH1.

2. **Endpoint not in `readerData.issuers[].endpoints[]`** — first tap
   still needs a real Access Document / issuer-verification flow. The
   current tree already has a provisional helper that can tentatively add
   a `5A devicePk` for signature verification, but the spec-backed path
   should still:
   - extract endpoint pubkey from the Access Document,
   - verify the issuer signature using `issuer.issuer_pk`,
   - **persist the endpoint** in `readerData.issuers[matching_issuer].endpoints[]`,
   - then verify the challenge signature using the (now known) endpoint pubkey.

## 3. Wire trace from one failed tap (reference)

For comparison against the spec when reading byte-by-byte.

```
# SELECT AID
TX: 00 A4 04 00 09 A0 00 00 09 09 AC CE 55 01 00
RX: 6F 20 84 09 A0 00 00 09 09 AC CE 55 01
       A5 13 80 02 00 00 5C 02 00 09
              7F 66 08 02 02 0E 00 02 02 0E 00
       90 00

# AUTH0
TX: 80 80 00 00 79
       41 01 01                                          # tx flags
       42 01 01                                          # tx code
       5C 02 00 09                                       # protocol version
       87 41 04 DB34EEE5BBDF7E61 54A46F8B7D72DF12        # reader ephemeral
              BE2D1CCA997C3B43 DCA61A48066D54CB          #   uncompressed
              0BC4724DBABC0BD3 B5EBB7D78AA69858          #   65 B = 0x04 + X + Y
              B9CF72240CB8E623 9AD79E39910BED38
       4C 10 A231736CA1B3F6AA 9DF921B203D61838           # transaction ID (16 B)
       4D 18 B54E5CCF8BB1E650 D5E4B64903515380           # ?? 24 B. Likely
              058CA7FCB013F44D                           #   readerIdentifier
                                                          #   (HomeKey is 8 B)
RX: 69 84

# Control flow (kCmdFlowFailed) — what we shouldn't be sending
TX: 80 3C 00 00 06 41 01 00 42 01 01
RX: 90 00
```

The `4D 18 ...` in this trace is now **historical root-cause evidence**,
not the target shape. It was captured before `AliroDelegate` started
passing the full 16-byte `reader_group_sub_identifier` into DDK. In the
current tree, a true Aliro config should produce a 32-byte
`reader_identifier = group_id(16) || group_sub_id(16)`. Keep the old
24-byte trace here as history; do not treat it as the desired format.

## 4. What to read in the spec

`docs/aliro-1.0-specification.pdf` (download from CSA-IoT, gitignored
per their license).

Priority sections (numbering may shift between revisions):

| § | What | Looking for |
|---|---|---|
| 7 | Two-Phase Access Protocol | exact list of APDUs in order, esp. Step-up trigger codes |
| 7.x | Step-up Initiation | how `endpointEphPubKey` is delivered after a `6984` AUTH0 — there has to be a second AUTH0/AUTH1 round |
| 8 | NFC tap-to-unlock state machine | Wallet-side decision tree on cached vs. cold |
| 8.x | Access Document format | TLV layout, signature algorithm, what to feed into ECDSA verify |
| 9 | Reader credential lifecycle | how reader proves identity to Wallet across the *first* tap (when Wallet has only the issuer key + groupId) |

## 5. Concrete implementation plan

Five steps, each isolatable + testable.

1. **Hop the guard** in `AuthContext.cpp:172`. Add an `else if` for
   `kAliro && status==6984`. Don't re-use the existing FastAuth-prefix
   path — just enter Step-up directly.

2. **Step-up Initiation APDU** — new method on `DDKStdAuth` (or a
   dedicated `DDKStepUp`). The exact APDU is the §7 question. Likely
   `80 81 00 00 [Lc] [41 01 02 / similar step-up sub-cmd] [reader sig
   over recent state]`. The response should include the endpoint
   ephemeral pubkey + Access Document.

3. **Access Document parser** — new function. TLV layout per §8.x.
   Out: endpoint pubkey + endpoint id + issuer-signed cert.

4. **Issuer-cert verification + real persistence** —
   `mbedtls_ecdsa_verify` against `issuer.issuer_pk` (already loaded via
   `SetCredential` and persisted in NVS). The current tree already has a
   provisional `5A devicePk` insertion helper; replace that temporary
   path with a real issuer-matched persistence flow that calls
   `readerData.issuers[i].endpoints.push_back(...)` and
   `ReaderDataManager.saveData()`.

5. **Endpoint signature verify** — already exists at
   `StandardAuth.cpp:338`. Just gets the now-known endpoint pubkey and
   verifies challenge signature (the existing kAliro lookup at line 280
   then matches and the success branch at 347-353 fires).

After step 5, the next-tap path is the FastAuth Expedited Phase
(already implemented), reusing the persistent key cached in
`endpoint_prst_k` (line 222-224). So **only the cold-tap path has
implementation work; warm-tap should work for free** once cold-tap
populates the endpoint.

## 6. Test methodology

For each iteration:

1. Apple Home → tile → ⚙ → "Add Home Key" (forces a fresh provisioning
   round so the issuer key is current in NVS).
2. iPhone Wallet: swipe up the Home Key card, ⋯ → "Remove Card", to
   force cold-cache state on the next tap.
3. Tap iPhone to PN532, watch `/tmp/aliro-debug/serial.log` filtered
   to `HKAuthCtx` + `aliro` + `lock-hdl`, or run `just diag-tap` from the
   repo root for a one-screen-per-tap summary.
4. The success log:
   - `Auth0 Response Length: 2, DATA: 6984`
   - **`Step-up entered`** (new log we'd add)
   - `Auth1 APDU Length: ...`
   - `Auth1 Response Length: ...`
   - `Decrypted ...`
   - `Found matching endpoint with public key: ...`
   - `signature verification result: 0`
   - `Endpoint authenticated, transaction took ...`
   - `[nfc] HOMEKEY_TAP authenticated`
   - `lock-hdl: ep=1 -> UNLOCKED (src=10)`
   - LED: red blink → cyan blink → white flash → green

## 7. Changes already landed in this repo (so the spec work has a working
baseline to patch on top of)

- ECP TCI flipped to Aliro `20 42 20`
  ([NfcManager.cpp](../aliro-esp32/main/NfcManager.cpp))
- SELECT-AID prioritises Aliro AID
  `A0 00 00 09 09 AC CE 55 01` with HomeKey AID fallback
  ([NfcManager.cpp](../aliro-esp32/main/NfcManager.cpp))
- SELECT-response FCI `A5` template is parsed and injected into
   `DDKAuthenticationContext::setAliroFCI()` for the HKDF salt
   ([NfcManager.cpp](../aliro-esp32/main/NfcManager.cpp))
- DDKAuthenticationContext type flipped to `kAliro`
- AliroDelegate ↔ ReaderDataManager bridge: reader keys, issuer keys,
  endpoint keys all sync into `readerData_t`
  ([AliroDelegate.cpp](../aliro-esp32/main/AliroDelegate.cpp))
- Full 16-byte `reader_group_sub_identifier` now reaches
   `readerData.reader_id`, so true Aliro mode can emit a 32-byte
   `0x4D reader_identifier`
   ([AliroDelegate.cpp](../aliro-esp32/main/AliroDelegate.cpp))
- StandardAuth now has a provisional first-tap helper that tentatively
   inserts `5A devicePk` when the endpoint is not cached
   ([StandardAuth.cpp](../aliro-esp32/components/DigitalDoorKey/src/auth/StandardAuth.cpp))
- DoorLock cluster Set/Get User+Credential persistence to NVS, with
  boot-time replay of Aliro credentials so DDK starts each session
  with `issuers[]` already populated
  ([DoorLockUserStubs.cpp](../aliro-esp32/main/DoorLockUserStubs.cpp))
- `OperationSourceEnum::kAliro` (0x0A) wired to NFC tap path
- LED state machine reflects auth phase: red blink (NFC detect) →
  cyan blink (DDK auth in flight) → white flash → green (success), or
  red 1 s (reject)
  ([LockHandlers.cpp](../aliro-esp32/main/LockHandlers.cpp))
- All Aliro / DDK / chip log levels at `VERBOSE` so the spec-vs-code
  comparison shows wire bytes for free
- `scripts/aliro-tap-diag.py` + `just diag-tap` provide a one-screen
   parser for each tap while iterating on the auth flow

## 8. What to file upstream once this works

`rednblkx/DigitalDoorKey` is the home of the actual auth code. The
patch in §5 above will produce a PR that's a finishing of commit
`e592f5f3` ("feat: experimental Aliro support", 2026-02-11). Note in
the PR body: tested against iPhone 16 Pro Max iOS 26 + Apple Wallet
+ Matter ALIRO provisioning path on a custom ESP32 reader; mention the
Samsung Fold 7 path is a follow-up since Samsung Wallet's Aliro applet
behaves slightly differently per kormax.
