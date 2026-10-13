#!/usr/bin/env python3
"""
aliro-tap-diag.py — pretty-print one Aliro tap cycle from /tmp/aliro-debug/serial.log.

Tails the serial log and, for each tap, prints a single coloured panel:
  - Tag detect → applet selected
  - SELECT response: was Aliro/HomeKey applet, FCI A5 parsed?
  - AUTH0: APDU sent (parsed TLVs), iPhone response status
  - AUTH1: APDU sent + response (decrypted ok or GCM fail)
  - Endpoint: cached / first-tap / not found
  - Result: SUCCESS / FAIL with the exact line that decided it

Use: just `aliro-tap-diag` (or `python scripts/aliro-tap-diag.py`).
"""
from __future__ import annotations

import re
import sys
import time
from pathlib import Path

LOG_PATH = Path("/tmp/aliro-debug/serial.log")

# ANSI colour codes — no external deps.
class C:
    R = "\033[0m"
    DIM = "\033[2m"
    BOLD = "\033[1m"
    RED = "\033[31m"
    GRN = "\033[32m"
    YEL = "\033[33m"
    BLU = "\033[34m"
    MAG = "\033[35m"
    CYN = "\033[36m"
    GRY = "\033[90m"

# Line patterns we care about (in order of appearance per tap).
PATTERNS = [
    ("tap_start", re.compile(r"NfcManager: NFC tag detected!")),
    ("aliro_sel_ok", re.compile(r"NfcManager: Aliro applet selected successfully")),
    ("homekey_sel_ok", re.compile(r"NfcManager: HomeKey applet selected successfully")),
    ("sel_fail", re.compile(r"Not a HomeKey tag, or failed to select applet")),
    ("fci_parsed", re.compile(r"NfcManager: Parsed FCI A5 template: (\d+) B = ([0-9A-F]+)")),
    ("fci_unexpected", re.compile(r"NfcManager: SELECT response unexpected format")),
    ("auth_cache_hit", re.compile(r"NfcManager: Auth cache hit")),
    ("auth_cache_miss", re.compile(r"NfcManager: Auth cache miss")),
    ("auth0_send", re.compile(r"writeChunk: len=(\d+), data=0000FF\w+D440018080")),
    ("auth0_resp", re.compile(r"readChunk: len=\d+, data=D54100(\w{4,})$")),
    ("fast_fail", re.compile(r"HKFastAuth.*FAST Flow failed")),
    ("fast_ok", re.compile(r"HKFastAuth.*FAST Flow.*SUCCESS|Endpoint Authenticated via FAST")),
    ("auth1_send", re.compile(r"writeChunk: len=(\d+), data=0000FF\w+D440018081")),
    ("ks_reader_pkx", re.compile(r"HKStdAuth: Auth1_keying_material:.* readerPublicKeyX \((\d+) bytes\): (\w+)")),
    ("ks_reader_id", re.compile(r"HKStdAuth: Auth1_keying_material:.* readerIdentifier \((\d+) bytes\): (\w+)")),
    ("ks_fci", re.compile(r"HKStdAuth: Auth1_keying_material:.* fciProprietaryTemplate \((\d+) bytes\): (\w*)")),
    ("ks_salt", re.compile(r"HKStdAuth: Auth1_keying_material:.* HKDF Salt \((\d+) bytes\): (\w+)")),
    ("gcm_fail", re.compile(r"mbedtls_gcm_auth_decrypt failed: (-\d+)")),
    ("decrypt_fail", re.compile(r"DigitalKeySC:.* GCM tag verification failed")),
    ("ep_match", re.compile(r"Found matching endpoint with public key: (\w+)")),
    ("ep_first_tap", re.compile(r"First-tap provisioning: endpoint pk not cached, tentatively adding")),
    ("ep_missing", re.compile(r"Endpoint data missing!")),
    ("sig_pass", re.compile(r"signature verification result: 0")),
    ("sig_fail_msg", re.compile(r"Signature failed verification!")),
    ("std_ok", re.compile(r"Endpoint Authenticated via STANDARD Flow|Endpoint authenticated, transaction took")),
    ("std_invalid_resp", re.compile(r"Invalid Response! STANDARD Flow failed!")),
    ("hkauth_pass", re.compile(r"HomeKey authentication successful!")),
    ("hkauth_fail", re.compile(r"HomeKey authentication failed")),
    ("nfc_unlock", re.compile(r"\[nfc\] HOMEKEY_TAP authenticated.*scheduling Aliro Unlock")),
    ("nfc_reject", re.compile(r"\[nfc\] HomeKey/Aliro auth FAILED")),
    ("lock_unlocked", re.compile(r"lock-hdl: ep=\d+ -> UNLOCKED")),
    ("tap_done", re.compile(r"NfcManager: Total processing time: (\d+) ms")),
]


def parse_tap(lines: list[str]) -> dict:
    """Reduce a sequence of log lines (one tap) into a structured dict."""
    state: dict = {"raw_count": len(lines)}
    for line in lines:
        for name, pat in PATTERNS:
            m = pat.search(line)
            if not m:
                continue
            if name in ("fci_parsed", "ks_reader_pkx", "ks_reader_id",
                        "ks_fci", "ks_salt", "ep_match", "auth0_resp",
                        "tap_done", "gcm_fail"):
                state[name] = m.groups()
            else:
                state.setdefault(name, True)
    return state


def render(tap: dict) -> str:
    """Pretty single-screen output for one tap."""
    out = []
    head = f"{C.BOLD}─── tap @ {time.strftime('%H:%M:%S')} ───────────────────────────────{C.R}"
    out.append(head)

    def row(label: str, status: str, detail: str = ""):
        out.append(f"  {C.DIM}{label:<22}{C.R} {status}  {C.GRY}{detail}{C.R}")

    # 1. detect + applet
    if tap.get("aliro_sel_ok"):
        row("PN532 detect", f"{C.GRN}✓{C.R}")
        row("Aliro AID select", f"{C.GRN}✓{C.R}", "A0 00 00 09 09 AC CE 55 01")
    elif tap.get("homekey_sel_ok"):
        row("PN532 detect", f"{C.GRN}✓{C.R}")
        row("HomeKey AID select", f"{C.YEL}fallback{C.R}", "A0 00 00 08 58 01 01")
    elif tap.get("sel_fail"):
        row("PN532 detect", f"{C.GRN}✓{C.R}")
        row("Applet select", f"{C.RED}✗ no Aliro/HomeKey{C.R}",
            "Flipper / generic 14443A")
        return "\n".join(out)
    else:
        row("PN532 detect", f"{C.RED}silent{C.R}")
        return "\n".join(out)

    # 2. FCI parse
    fci = tap.get("fci_parsed")
    if fci:
        n, hexstr = fci
        row("FCI A5 parsed", f"{C.GRN}✓{C.R}", f"{n} B = {hexstr[:24]}…")
    elif tap.get("fci_unexpected"):
        row("FCI A5 parsed", f"{C.RED}wrong fmt{C.R}", "DDK keying will diverge")

    # 3. auth cache
    if tap.get("auth_cache_hit"):
        row("Auth cache", f"{C.CYN}HIT{C.R}", "precomputed ctx reused")
    elif tap.get("auth_cache_miss"):
        row("Auth cache", f"{C.YEL}miss{C.R}", "cold init")

    # 4. AUTH0
    auth0 = tap.get("auth0_resp")
    if auth0:
        body = auth0[0]
        if body.startswith("86"):
            row("AUTH0 → iPhone", f"{C.GRN}OK{C.R}",
                f"86 41 04… (endpoint ephemeral pk + 9000)")
        elif body.startswith("9000"):
            row("AUTH0 → iPhone", f"{C.GRN}9000{C.R}", "but no payload?")
        elif body.startswith("6984"):
            row("AUTH0 → iPhone", f"{C.RED}6984{C.R}",
                "iPhone has no credential for this reader")
        else:
            row("AUTH0 → iPhone", f"{C.YEL}{body[:8]}{C.R}", "unexpected status")

    # 5. FAST flow
    if tap.get("fast_ok"):
        row("FastAuth", f"{C.GRN}SUCCESS{C.R}", "Kpersistent matched")
    elif tap.get("fast_fail"):
        row("FastAuth", f"{C.YEL}fail → STANDARD{C.R}",
            "expected for first tap (no cached Kpersistent)")

    # 6. STANDARD: keying material
    pkx = tap.get("ks_reader_pkx")
    rid = tap.get("ks_reader_id")
    fci2 = tap.get("ks_fci")
    salt = tap.get("ks_salt")
    if pkx and rid and salt:
        nrid, ridhex = rid
        row("STD: readerEphX", f"{C.GRN}{pkx[0]}B{C.R}", pkx[1][:32] + "…")
        row("STD: readerIdentifier", f"{C.GRN}{nrid}B{C.R}",
            ridhex[:32] + ("…" if len(ridhex) > 32 else ""))
        if fci2:
            n, h = fci2
            mark = f"{C.GRN}✓{C.R}" if int(n) > 0 else f"{C.RED}EMPTY{C.R}"
            row("STD: fciTemplate", f"{mark} {n}B", h[:32] + ("…" if len(h) > 32 else ""))
        row("STD: HKDF salt", f"{C.GRN}{salt[0]}B{C.R}", "(combined keying material)")

    # 7. GCM decrypt
    gcm = tap.get("gcm_fail")
    if gcm:
        row("GCM decrypt", f"{C.RED}FAIL ({gcm[0]}){C.R}",
            "AES-GCM auth tag mismatch — keying material diverges from iPhone")
    elif tap.get("decrypt_fail"):
        row("GCM decrypt", f"{C.RED}FAIL{C.R}", "tag verify failed")
    elif salt:
        # If we got past keying without gcm_fail but something later, decrypt was OK.
        if any(k in tap for k in ("ep_match", "ep_first_tap", "ep_missing")):
            row("GCM decrypt", f"{C.GRN}OK{C.R}", "AUTH1 response decrypted")

    # 8. endpoint match
    if tap.get("ep_match"):
        row("Endpoint match", f"{C.GRN}cached{C.R}", "found in readerData.issuers[]")
    elif tap.get("ep_first_tap"):
        row("Endpoint match", f"{C.CYN}first-tap{C.R}",
            "tentatively added to issuer[0] from 5A devicePk TLV")
    elif tap.get("ep_missing"):
        row("Endpoint match", f"{C.RED}MISSING{C.R}",
            "DDK has no endpoint to verify against → fail")

    # 9. ECDSA verify
    if tap.get("sig_pass"):
        row("ECDSA verify", f"{C.GRN}PASS{C.R}", "endpoint signature OK")
    elif tap.get("sig_fail_msg"):
        row("ECDSA verify", f"{C.RED}FAIL{C.R}",
            "endpoint signature didn't verify against pubkey")

    # 10. final outcome
    if tap.get("nfc_unlock") or tap.get("hkauth_pass") or tap.get("std_ok"):
        row("Result", f"{C.GRN}{C.BOLD}UNLOCK{C.R}",
            "lockind::nfc_tap_unlock(1) scheduled on chip task")
    elif tap.get("nfc_reject") or tap.get("hkauth_fail"):
        row("Result", f"{C.RED}{C.BOLD}REJECT{C.R}",
            "DDK auth flow returned kFlowFailed")

    # 11. timing
    td = tap.get("tap_done")
    if td:
        row("Total time", f"{C.GRY}{td[0]} ms{C.R}")
    return "\n".join(out)


def tail_taps():
    """Tail serial.log, segment into taps, print parsed view of each."""
    if not LOG_PATH.exists():
        print(f"{C.RED}{LOG_PATH} doesn't exist — start the serial monitor first{C.R}",
              file=sys.stderr)
        sys.exit(1)
    pos = LOG_PATH.stat().st_size  # start at end (live tail)
    buf: list[str] = []
    in_tap = False
    print(f"{C.BOLD}aliro-tap-diag{C.R} — tailing {LOG_PATH}", file=sys.stderr)
    print(f"{C.DIM}each NFC tap is parsed and rendered below{C.R}\n", file=sys.stderr)
    while True:
        try:
            cur = LOG_PATH.stat().st_size
        except FileNotFoundError:
            time.sleep(0.5)
            continue
        if cur < pos:
            # log truncated (e.g. monitor restart) — start over from end
            pos = 0
        if cur == pos:
            time.sleep(0.4)
            continue
        with LOG_PATH.open("rb") as f:
            f.seek(pos)
            chunk = f.read(cur - pos)
            pos = cur
        for raw in chunk.decode("utf-8", errors="replace").splitlines():
            if "NFC tag detected!" in raw:
                in_tap = True
                buf = [raw]
            elif in_tap:
                buf.append(raw)
                if "Total processing time:" in raw:
                    print(render(parse_tap(buf)) + "\n", flush=True)
                    in_tap = False
                    buf = []


if __name__ == "__main__":
    if "--once" in sys.argv:
        # Parse and print every tap currently in the log, then exit.
        if not LOG_PATH.exists():
            print(f"{LOG_PATH} missing", file=sys.stderr)
            sys.exit(1)
        text = LOG_PATH.read_text(errors="replace")
        in_tap = False
        buf: list[str] = []
        for raw in text.splitlines():
            if "NFC tag detected!" in raw:
                in_tap = True
                buf = [raw]
            elif in_tap:
                buf.append(raw)
                if "Total processing time:" in raw:
                    print(render(parse_tap(buf)) + "\n")
                    in_tap = False
                    buf = []
    else:
        try:
            tail_taps()
        except KeyboardInterrupt:
            sys.exit(0)
