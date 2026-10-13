# Documentation Changelog

Separate changelog for **project-authored documentation** in this repo.

Scope:

- root docs such as `README.md`, `TODO.md`, `PROTOCOL-aliro.md`, and files under `docs/`
- project docs under `aliro-esp32/`
- excludes vendored / upstream docs under `homekey-esp32/` and third-party component READMEs

Maintenance rule:

- append a new dated entry for every meaningful documentation-only update
- keep newest entries first
- group each dated entry by subsystem
- record the reason for the doc change, the facts captured, and the touched files

Subsystem buckets:

- **Root / repo contract** — repo overview, status, protocol contract, TODO, cross-project docs
- **Hardware / platform docs** — board maps, wiring, field-service notes, hardware decision docs
- **`aliro-esp32/` docs** — firmware-local README, debugging notes, bring-up guidance

## Entry Template

Use this template for every new entry. Omit empty subsystem sections only
when that keeps the entry clearer than an explicit "no delta" note.

```md
## YYYY-MM-DD

### Summary

- one short paragraph or 1-3 bullets describing the doc revision as a whole

### Root / repo contract

Why:
- why these docs changed

Captured:
- concrete facts now reflected in docs

Files:
- `README.md`
- `TODO.md`

### Hardware / platform docs

Why:
- why the hardware docs changed

Captured:
- board / wiring / LAN / RF facts now recorded

Files:
- `docs/hardware-reference.md`

### `aliro-esp32/` docs

Why:
- why the firmware-local docs changed

Captured:
- bring-up / debug / wallet-validation facts now recorded

Files:
- `aliro-esp32/README.md`
- `aliro-esp32/DEBUGGING.md`
```

## 2026-04-29

### Summary

- Aliro bring-up docs were brought up to the current live auth state.
  This revision records the real iPhone `SELECT` / `AUTH0` progress,
  documents the cold-tap `6984` blocker explicitly, and makes the new
  tap-debug and Step-up notes discoverable from the main docs.

### Root / repo contract

Why:

- the repo-level docs still described Aliro too loosely and did not
  expose the actual current blocker in a way that matched the code and
  live traces

Captured:

- live iPhone traces now reach Aliro `SELECT` and `AUTH0`
- current main blocker is cold-tap `AUTH0 = 6984` and the missing Step-up transition
- SELECT-response FCI `A5` is now part of the DDK key schedule / HKDF salt
- Aliro `reader_identifier` docs now reflect the full 16-byte `reader_group_sub_identifier`
- the StandardAuth finishing notes distinguish historical traces from the current code state

Files:

- `README.md`
- `PROTOCOL-aliro.md`
- `TODO.md`
- `docs/project-gap-analysis.md`
- `docs/execution-plan.md`
- `docs/aliro-stdauth-finishing-roadmap.md`

### Hardware / platform docs

Why:

- no hardware-specific facts were changed in this pass

Captured:

- no hardware-doc delta in this entry

Files:

- none

### `aliro-esp32/` docs

Why:

- firmware-local docs needed to match the real auth state and expose the
  new tap-debug workflow directly where bring-up happens

Captured:

- provisional first-tap endpoint helper is documented as landed, but not treated as final Step-up completion
- `just diag-tap` / `scripts/aliro-tap-diag.py` are now part of the documented debug workflow
- current auth state is described as `SELECT` / `AUTH0` reached, with cold-tap Step-up still pending

Files:

- `aliro-esp32/README.md`
- `aliro-esp32/DEBUGGING.md`

## 2026-04-28

### Summary

- Main repo documentation was realigned with the post-pivot state of the
  project: `homekey-esp32/` as the frozen baseline, T-ETH-Lite as the LAN
  service host, and `aliro-esp32/` as the active path with a real PN532 /
  DDK runtime already in tree.

### Root / repo contract

Why:

- root docs had drifted behind both the real hardware baseline and the
  then-current Aliro code state

Captured:

- `homekey-esp32/` is documented as the frozen regression baseline, including the field-service T-ETH-Lite LAN baseline
- real setup AP names were documented (`HomeSpan-Setup` / `homespan`, with recovery `HomeKey-ESP32` / `homekey123`)
- `aliro-esp32/` docs stopped claiming PN532 / `NfcManager` were “not wired yet”
- ECP payload behavior and 8-byte vs 16-byte GID handling were documented from the live code path

Files:

- `README.md`
- `TODO.md`
- `PROTOCOL-aliro.md`
- `docs/project-gap-analysis.md`
- `docs/execution-plan.md`

### Hardware / platform docs

Why:

- hardware guidance needed to reflect the actual T-ETH-Lite LAN baseline
  and to repair the broken hardware-reference state left by an earlier
  partial patch

Captured:

- the T-ETH-Lite Ethernet preset and verified PN532 remap were added to the hardware docs
- the hardware reference was repaired after an intermediate malformed patch state

Files:

- `docs/hardware-reference.md`

### `aliro-esp32/` docs

Why:

- firmware-local docs still contained stale claims about PN532 state and
  pointed users at debug surfaces that did not exist in this tree

Captured:

- `aliro-esp32/` local docs were updated from “not wired yet” to “early runtime exists, validation pending”
- stale debug guidance about nonexistent on-device HTTP / WebSocket logging in `aliro-esp32/` was removed

Files:

- `aliro-esp32/README.md`
- `aliro-esp32/DEBUGGING.md`