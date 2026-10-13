# SmartKey ESP32 patch (draft)

> **Status: deprioritised (April 2026).** These patches were meant to
> land in `../homekey-esp32/` to enable the custom Android HCE protocol
> from [../PROTOCOL.md](../PROTOCOL.md). With the pivot to Aliro (see
> [../TODO.md](../TODO.md) §7), Android support moves to
> [../aliro-esp32/](../aliro-esp32/) via the Matter DoorLock cluster
> with the `ALIRO` feature. `homekey-esp32/` stays frozen as the
> regression baseline — these drafts are kept for reference and as a
> fallback if Aliro proves unworkable in a specific deployment.

Draft files that will land inside `../homekey-esp32/main/` (the
rednblkx subtree) once we can build + flash + test them.  Keeping the
drafts out of the subtree avoids polluting it with untested code that
would conflict on `just upstream-pull`.

Target firmware: `v0.8` (`homekey-esp32/` subtree at commit `1e9c69b`).
Wire spec: [../PROTOCOL.md](../PROTOCOL.md).

## Files

| File | Destination (when applied) | Purpose |
|---|---|---|
| `SmartKeyHandler.hpp` | `homekey-esp32/main/SmartKeyHandler.hpp` | Handler class declaration |
| `SmartKeyHandler.cpp` | `homekey-esp32/main/SmartKeyHandler.cpp` | Handler implementation (skeleton — crypto stubs) |
| `nfcmanager-hook.patch` | patch of `homekey-esp32/main/NfcManager.cpp` | After HomeKey SELECT fails (6A82), try SmartKey AID |
| `ecp-annotation-hook.patch` | patch of `homekey-esp32/main/NfcManager.cpp` | Emit our ECP2 annotation in the polling loop next to the HomeKey one |
| `cmake-add-handler.patch` | patch of `homekey-esp32/main/CMakeLists.txt` | Add `SmartKeyHandler.cpp` to app SRCS |

## Apply order (when we're ready to test)

```bash
cd /path/to/apple-homekey-pm3-reader
# 1. Initialise git submodules inside the subtree first (TODO §3):
cd homekey-esp32
git submodule update --init --recursive
cd ..

# 2. Copy new files:
cp smartkey-esp32-patch/SmartKeyHandler.{hpp,cpp} homekey-esp32/main/

# 3. Apply patches:
cd homekey-esp32
patch -p1 < ../smartkey-esp32-patch/nfcmanager-hook.patch
patch -p1 < ../smartkey-esp32-patch/ecp-annotation-hook.patch
patch -p1 < ../smartkey-esp32-patch/cmake-add-handler.patch

# 4. Build + flash:
nix develop
idf.py set-target esp32s3
idf.py build flash monitor
```

## Status of each stub

Tracks [/PROTOCOL.md](../PROTOCOL.md) v1 (HMAC-only, no per-tap ECDH).
Reader-side flow:

1. SELECT our AID → phone returns 9000
2. AUTH APDU (reader sends nonce) → phone returns `ver || ep_id || ts || tag || 9000`
3. Reader verifies tag = `HMAC-SHA256(mk, "OPEN/v1" || nonce || ep_id || ts_be)[0..15]`

The C++ handler has the right **shape** (AUTH APDU format, response
parsing, endpoint lookup, NVS namespace) but crypto is TODO'd:

1. `mbedtls_md_hmac*()` — recompute the HMAC tag at line ~95 of
   `verifyAuthResponse()`
2. `mbedtls_ct_memcmp()` — constant-time compare the recomputed tag
   against the phone's tag
3. Freshness check: `|ts - time(nullptr)| <= 30` (requires SNTP synced
   at boot, which HomeKey-ESP32 already does when WiFi is up)
4. NVS serialisation (MessagePack, to match HomeKey's convention)
5. Pairing endpoint (`/api/smartkey/enroll` in WebServerManager) —
   accepts phone pubkey, derives `mk`, persists `EndpointRecord`

Bundled in the same flash, this adds ~5 kB to the binary (mbedtls is
already linked for HomeKey).

## Why separate from the subtree

The `homekey-esp32/` directory is a git subtree of upstream
`rednblkx/HomeKey-ESP32`.  Editing files there is normal (that's the
whole point of the fork), but **untested** edits create merge-conflict
risk when upstream publishes a new release.  Keeping the drafts here
until they're tested end-to-end means `just upstream-pull` stays
frictionless until the moment we actually ship these.
