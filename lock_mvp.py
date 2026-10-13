#!/usr/bin/env python3
# lock_mvp.py — "lock from scratch" MVP
#
# Poll for RFID/NFC cards via Proxmark3, compare UID against an
# allowlist, toggle an open/closed state on a valid tap. All events
# logged to stdout and an append-only JSONL log.
#
# Intentionally pared-down: no relay GPIO, no HomeKey crypto, no
# network. Just "someone tapped their card — is it allowed?" with
# deterministic UID matching. Good enough to prove the concept and
# wire a real actuator in later.
#
# Supports:
#   - ISO14443-A (Mifare Classic, DESFire, NTAG, EMV — UID only)
#   - LF EM 410x (common EU/RU building fobs)
#
# Does NOT support (MVP scope):
#   - iPhone HomeKey / Express Mode auth cryptogram
#   - Mifare Classic sector-based access validation
#   - DESFire authenticated AID selection
#   - Sensitive-data sniffing


from __future__ import annotations

import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path

PM3_BIN_CANDIDATES = [
    "/opt/homebrew/bin/proxmark3",
    "/usr/local/bin/proxmark3",
    "/usr/bin/proxmark3",
    "proxmark3",
]


def utcnow() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def find_pm3_bin() -> str | None:
    for c in PM3_BIN_CANDIDATES:
        if os.path.isabs(c):
            if os.path.isfile(c) and os.access(c, os.X_OK):
                return c
        else:
            p = shutil.which(c)
            if p:
                return p
    return None


def find_pm3_port() -> str | None:
    for pattern in ("/dev/cu.usbmodemiceman*", "/dev/ttyACM*"):
        hits = sorted(glob.glob(pattern))
        if hits:
            return hits[0]
    return None


# --------- PM3 subprocess wrapper ---------


@dataclass
class Pm3:
    bin_path: str
    port: str

    def run(self, commands: list[str], timeout: float = 12.0) -> str:
        joined = "; ".join(commands)
        try:
            r = subprocess.run(
                [self.bin_path, self.port, "-c", joined],
                capture_output=True,
                text=True,
                timeout=timeout,
            )
            return (r.stdout or "") + (r.stderr or "")
        except subprocess.TimeoutExpired as e:
            out = e.stdout.decode() if isinstance(e.stdout, bytes) else (e.stdout or "")
            err = e.stderr.decode() if isinstance(e.stderr, bytes) else (e.stderr or "")
            return out + err


# --------- Card readers ---------

UID_RE_14A = re.compile(r"\bUID:\s+((?:[0-9A-F]{2}\s){3,9}[0-9A-F]{2})\b", re.I)
ID_RE_EM = re.compile(r"EM 410x ID\s+([0-9A-F]{10})", re.I)


def normalize_uid(raw: str) -> str:
    # "04 53 FE B2 87 10 90" -> "0453FEB2871090"
    return raw.replace(" ", "").upper()


def read_hf_14a(pm3: Pm3) -> str | None:
    out = pm3.run(["hf 14a info"], timeout=5)
    m = UID_RE_14A.search(out)
    return normalize_uid(m.group(1)) if m else None


def read_lf_em(pm3: Pm3) -> str | None:
    out = pm3.run(["lf em 410x reader"], timeout=6)
    m = ID_RE_EM.search(out)
    return m.group(1).upper() if m else None


def poll_any(pm3: Pm3) -> tuple[str, str] | None:
    # Try HF first (fast, most common for offices), then LF
    uid = read_hf_14a(pm3)
    if uid:
        return ("HF14A", uid)
    uid = read_lf_em(pm3)
    if uid:
        return ("EM410X", uid)
    return None


# --------- Allowlist & state ---------


@dataclass
class Allowlist:
    path: Path
    entries: dict[str, dict] = field(default_factory=dict)

    def load(self) -> None:
        if not self.path.exists():
            self.entries = {}
            return
        data = json.loads(self.path.read_text())
        # normalize keys
        self.entries = {k.upper(): v for k, v in data.items()}

    def save(self) -> None:
        self.path.write_text(json.dumps(self.entries, indent=2, sort_keys=True))

    def contains(self, uid: str) -> bool:
        return uid.upper() in self.entries

    def describe(self, uid: str) -> str:
        e = self.entries.get(uid.upper())
        if not e:
            return "<unknown>"
        return e.get("name") or e.get("owner") or "<named>"

    def add(self, uid: str, name: str, proto: str) -> None:
        self.entries[uid.upper()] = {
            "name": name,
            "proto": proto,
            "added_at": utcnow(),
        }
        self.save()


@dataclass
class LockState:
    state: str = "CLOSED"  # OR "OPEN"
    last_change: str = field(default_factory=utcnow)
    last_uid: str | None = None

    def toggle(self, uid: str) -> None:
        self.state = "OPEN" if self.state == "CLOSED" else "CLOSED"
        self.last_change = utcnow()
        self.last_uid = uid


# --------- Event log (JSONL, append-only) ---------


def log_event(log_path: Path, event: dict) -> None:
    event["ts"] = utcnow()
    with log_path.open("a") as f:
        f.write(json.dumps(event) + "\n")
    # human-readable mirror
    banner_state = event.get("state") or event.get("result", "")
    print(
        f"[{event['ts']}] {event['type']:10s} proto={event.get('proto', '-'):7s} "
        f"uid={event.get('uid', '-'):14s} name={event.get('name', '-'):20s} "
        f"state={banner_state}"
    )


# --------- State snapshot (read by webui) ---------


def write_state(state_path: Path, lock: LockState, allow_size: int, enrol_armed: bool) -> None:
    state_path.write_text(
        json.dumps(
            {
                "state": lock.state,
                "last_uid": lock.last_uid,
                "last_change": lock.last_change,
                "allowlist_size": allow_size,
                "enrol_armed": enrol_armed,
                "updated_at": utcnow(),
            },
            indent=2,
        )
    )


# --------- Control channel (written by webui, read by lock) ---------


def read_control(control_path: Path) -> dict | None:
    # If present, consume it (read + delete) and return the action.
    if not control_path.exists():
        return None
    try:
        data: dict = json.loads(control_path.read_text())
    except json.JSONDecodeError:
        control_path.unlink(missing_ok=True)
        return None
    control_path.unlink(missing_ok=True)
    return data if isinstance(data, dict) else None


# --------- Enrollment mode ---------


def enroll_next(pm3: Pm3, allow: Allowlist) -> None:
    print("\n[enroll] Present a card to enrol (Ctrl+C to abort).")
    while True:
        time.sleep(0.3)
        hit = poll_any(pm3)
        if not hit:
            continue
        proto, uid = hit
        if allow.contains(uid):
            print(f"  UID {uid} already enrolled as '{allow.describe(uid)}'. Skipping.")
            return
        name = input(f"  Got {proto} UID={uid}. Name/owner: ").strip() or "unnamed"
        allow.add(uid, name, proto)
        print(f"  Enrolled '{name}' -> {uid} ({proto}).")
        return


# --------- Non-blocking keyboard input (for live enrol) ---------


def _stdin_ready() -> bool:
    import select

    return bool(select.select([sys.stdin], [], [], 0)[0])


def _read_key() -> str:
    # Read a single char without needing Enter. termios cbreak on POSIX.
    import termios
    import tty

    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    try:
        tty.setcbreak(fd)
        ch = sys.stdin.read(1)
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)
    return ch


# --------- Main loop ---------

HELP = """\
  [e] arm live-enrol (next unknown tap gets added)
  [l] list allowlist
  [s] print state
  [h] show help
  [q] quit
"""


def run_loop(
    pm3: Pm3,
    allow: Allowlist,
    log_path: Path,
    poll_interval: float,
    state_path: Path,
    control_path: Path,
) -> None:
    lock = LockState()
    last_uid_seen: str | None = None
    last_seen_ts: float = 0.0
    debounce_s = 2.0  # same UID won't retrigger within this window
    enrol_armed = False  # flipped by pressing 'e' or via webui

    print(
        f"[start] lock MVP | allowlist={len(allow.entries)} entries "
        f"| poll={poll_interval:.1f}s | log={log_path}"
    )
    print(f"[start] initial state: {lock.state}")
    print(f"[start] state file  : {state_path}")
    print(f"[start] control     : {control_path}")
    print(HELP)
    log_event(
        log_path, {"type": "start", "state": lock.state, "allowlist_size": len(allow.entries)}
    )
    write_state(state_path, lock, len(allow.entries), enrol_armed)

    try:
        while True:
            # -- webui control file? ----------------------------------
            ctrl = read_control(control_path)
            if ctrl:
                action = ctrl.get("action")
                if action == "arm_enrol":
                    enrol_armed = True
                    print("[webui] enrol armed")
                elif action == "disarm_enrol":
                    enrol_armed = False
                    print("[webui] enrol disarmed")
                elif action == "toggle":
                    lock.toggle(ctrl.get("uid") or "manual")
                    log_event(
                        log_path,
                        {
                            "type": "MANUAL_TOGGLE",
                            "proto": "-",
                            "uid": "manual",
                            "name": ctrl.get("who", "webui"),
                            "state": lock.state,
                        },
                    )
                elif action == "reload_allowlist":
                    allow.load()
                    print(f"[webui] allowlist reloaded ({len(allow.entries)} entries)")
                write_state(state_path, lock, len(allow.entries), enrol_armed)

            # -- handle any keypress between polls ---------------------
            while _stdin_ready():
                ch = _read_key().lower()
                if ch == "e":
                    enrol_armed = not enrol_armed
                    print(
                        f"[enrol] {'ARMED — next unknown card will be added' if enrol_armed else 'disarmed'}"
                    )
                elif ch == "l":
                    if not allow.entries:
                        print("[list] (empty)")
                    for uid, m in sorted(allow.entries.items()):
                        print(f"[list] {uid:14s}  {m.get('proto', '-'):7s}  {m.get('name', '-')}")
                elif ch == "s":
                    print(
                        f"[state] {lock.state}  last_uid={lock.last_uid}  "
                        f"changed={lock.last_change}  enrol_armed={enrol_armed}"
                    )
                elif ch == "h":
                    print(HELP)
                elif ch == "q":
                    raise KeyboardInterrupt
                # any other key: ignore silently
                write_state(state_path, lock, len(allow.entries), enrol_armed)

            # -- poll the reader ----------------------------------------
            time.sleep(poll_interval)
            hit = poll_any(pm3)
            if not hit:
                continue
            proto, uid = hit
            now = time.time()
            if uid == last_uid_seen and (now - last_seen_ts) < debounce_s:
                continue  # ignore repeated reads of same stationary card
            last_uid_seen, last_seen_ts = uid, now

            if allow.contains(uid):
                name = allow.describe(uid)
                lock.toggle(uid)
                log_event(
                    log_path,
                    {
                        "type": "AUTH_OK",
                        "proto": proto,
                        "uid": uid,
                        "name": name,
                        "state": lock.state,
                    },
                )
                write_state(state_path, lock, len(allow.entries), enrol_armed)
                continue

            # Unknown card. If enrol armed, add it. Otherwise deny.
            if enrol_armed:
                auto_name = f"card-{datetime.now().strftime('%Y%m%d-%H%M%S')}"
                allow.add(uid, auto_name, proto)
                enrol_armed = False
                log_event(
                    log_path,
                    {
                        "type": "ENROLLED",
                        "proto": proto,
                        "uid": uid,
                        "name": auto_name,
                        "state": lock.state,
                    },
                )
                print(
                    f"[enrol] added '{auto_name}' -> {uid} ({proto}). "
                    f"Rename later by editing {allow.path}."
                )
            else:
                log_event(
                    log_path,
                    {
                        "type": "DENY",
                        "proto": proto,
                        "uid": uid,
                        "name": "-",
                        "result": "unknown_uid",
                        "state": lock.state,
                    },
                )
            write_state(state_path, lock, len(allow.entries), enrol_armed)
    except KeyboardInterrupt:
        print("\n[stop] interrupted")
        log_event(log_path, {"type": "stop", "state": lock.state})


# --------- CLI ---------


def main() -> int:
    ap = argparse.ArgumentParser(description="PM3-driven DIY lock MVP")
    ap.add_argument("--bin", help="Proxmark3 binary path")
    ap.add_argument("--port", help="PM3 serial port")
    ap.add_argument(
        "--allowlist",
        type=Path,
        default=Path("allowlist.json"),
        help="Path to allowlist JSON (default: ./allowlist.json)",
    )
    ap.add_argument(
        "--log",
        type=Path,
        default=Path("events.jsonl"),
        help="Append-only event log (default: ./events.jsonl)",
    )
    ap.add_argument(
        "--state",
        type=Path,
        default=Path("state.json"),
        help="Current-state snapshot (read by webui, default: ./state.json)",
    )
    ap.add_argument(
        "--control",
        type=Path,
        default=Path("control.json"),
        help="Control file (written by webui, consumed by lock)",
    )
    ap.add_argument(
        "--poll-interval", type=float, default=0.8, help="Seconds between card polls (default: 0.8)"
    )
    ap.add_argument(
        "--enroll",
        action="store_true",
        help="Enrol the next presented card into the allowlist, then exit",
    )
    ap.add_argument("--list", action="store_true", help="Print current allowlist and exit")
    args = ap.parse_args()

    bin_path = args.bin or find_pm3_bin()
    port = args.port or find_pm3_port()
    if not bin_path:
        print("ERROR: proxmark3 binary not found.", file=sys.stderr)
        return 1
    if not port:
        print("ERROR: PM3 USB not detected.", file=sys.stderr)
        return 1

    pm3 = Pm3(bin_path=bin_path, port=port)
    allow = Allowlist(path=args.allowlist)
    allow.load()

    if args.list:
        print(f"Allowlist ({args.allowlist}):")
        for uid, meta in sorted(allow.entries.items()):
            print(
                f"  {uid:14s}  {meta.get('proto', '-'):7s}  "
                f"{meta.get('name', '-')}  "
                f"(added {meta.get('added_at', '?')})"
            )
        return 0

    if args.enroll:
        enroll_next(pm3, allow)
        return 0

    run_loop(pm3, allow, args.log, args.poll_interval, args.state, args.control)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
