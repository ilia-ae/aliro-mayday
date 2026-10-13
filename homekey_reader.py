#!/usr/bin/env python3
# apple-homekey-pm3-reader
# Console tool that drives a Proxmark3 (Iceman fork) into Apple
# Enhanced Contactless Polling — Access mode, the same framing that
# certified HomeKey locks emit. When an iPhone with Wallet/HomeKey
# comes near, it will surface the ECP annotation and may attempt
# HomeKey applet selection. Full HomeKey auth (ECDH + AES-GCM) is
# intentionally NOT implemented here — this is a research /
# triggering tool, not a functioning lock.

from __future__ import annotations

import argparse
import contextlib
import glob
import os
import signal
import subprocess
import sys
import time
from dataclasses import dataclass

PM3_BIN_CANDIDATES = [
    "/opt/homebrew/bin/proxmark3",  # macOS Apple Silicon
    "/usr/local/bin/proxmark3",  # macOS Intel
    "/usr/bin/proxmark3",  # Linux
    "proxmark3",  # PATH
]

DEFAULT_TCI = "ffff"

# Reader Identifier presets worth trying. Documented in kormax/apple-home-key
# and observed in real HomeKey-certified locks.
TCI_PRESETS: dict[str, str] = {
    "default": "ffff",
    "aqara-u200": "0007",  # observed on Aqara U200 HomeKey lock
    "schlage-encode-plus": "0006",  # Schlage Encode Plus HomeKey
    "yale-assure-lock-2": "0005",  # Yale Assure Lock 2 Touchscreen
    "level-lock-plus": "0003",
}


@dataclass
class Pm3:
    bin_path: str
    port: str
    tci: str = DEFAULT_TCI
    ecp_enabled: bool = False
    last_trace_file: str | None = None

    def run(self, commands: list[str], timeout: float = 15.0) -> tuple[str, int]:
        joined = "; ".join(commands)
        try:
            r = subprocess.run(
                [self.bin_path, self.port, "-c", joined],
                capture_output=True,
                text=True,
                timeout=timeout,
            )
            return ((r.stdout or "") + (r.stderr or ""), r.returncode)
        except subprocess.TimeoutExpired as e:
            out = e.stdout.decode() if isinstance(e.stdout, bytes) else (e.stdout or "")
            err = e.stderr.decode() if isinstance(e.stderr, bytes) else (e.stderr or "")
            return (f"[timeout after {timeout}s]\n{out}{err}", 124)

    def run_stream(self, commands: list[str]) -> int:
        # For long-running commands (continuous reader) stream output live.
        joined = "; ".join(commands)
        proc = subprocess.Popen(
            [self.bin_path, self.port, "-c", joined],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        try:
            if proc.stdout is not None:
                for line in proc.stdout:
                    print(line, end="")
        except KeyboardInterrupt:
            proc.send_signal(signal.SIGINT)
            proc.wait(timeout=5)
        return proc.returncode or 0


# --------------- helpers ---------------


def find_pm3_bin() -> str | None:
    for c in PM3_BIN_CANDIDATES:
        if os.path.isabs(c) and os.path.isfile(c) and os.access(c, os.X_OK):
            return c
        if not os.path.isabs(c):
            from shutil import which

            p = which(c)
            if p:
                return p
    return None


def find_pm3_port() -> str | None:
    # Iceman native USB enumerates as /dev/cu.usbmodemiceman1 on macOS.
    for pattern in ("/dev/cu.usbmodemiceman*", "/dev/ttyACM*"):
        hits = sorted(glob.glob(pattern))
        if hits:
            return hits[0]
    # Last resort: any usbmodem that responds to pm3 probe. Too slow here.
    return None


def is_port_busy(port: str) -> bool:
    try:
        import serial  # type: ignore[import-untyped]
    except ImportError:
        return False
    try:
        s = serial.Serial(port, 115200, timeout=0.5)
        s.close()
        return False
    except Exception:
        return True


def kill_stale_pm3() -> int:
    r = subprocess.run(["pkill", "-9", "-f", "proxmark3"], capture_output=True)
    return r.returncode


# --------------- UI primitives ---------------


def banner(pm3: Pm3) -> None:
    print("\033[2J\033[H", end="")  # clear
    print("=" * 60)
    print(" Apple HomeKey — PM3 ECP Access Reader                      ")
    print("=" * 60)
    print(f"  Binary   : {pm3.bin_path}")
    print(f"  Port     : {pm3.port}")
    print(f"  ECP TCI  : {pm3.tci}")
    print(f"  Enabled  : {'yes' if pm3.ecp_enabled else 'no'}")
    print("-" * 60)


def menu() -> str:
    print("""
  1) Set ECP TCI (reader identifier)
  2) Enable ECP Access polling
  3) Single poll (one iPhone tap)
  4) Continuous poll (Ctrl+C to stop)
  5) Sniff 14a (10 s)
  6) Show last trace
  7) Antenna tune (hw tune)
  8) Disable ECP (back to stock)
  9) Info: card on antenna
  0) Show current pm3 prefs
  q) Quit
""")
    return input("> ").strip().lower()


def choose_tci(pm3: Pm3) -> None:
    print("\nPresets:")
    for name, tci in TCI_PRESETS.items():
        print(f"  {name:25s} {tci}")
    val = input("Enter TCI (hex, 1-22 bytes) or preset name [default]: ").strip()
    if not val:
        pm3.tci = DEFAULT_TCI
        return
    if val in TCI_PRESETS:
        pm3.tci = TCI_PRESETS[val]
        return
    # Sanity-check hex
    if all(c in "0123456789abcdefABCDEF" for c in val) and len(val) % 2 == 0:
        pm3.tci = val.lower()
    else:
        print("  Not valid hex — keeping previous.")


def enable_ecp(pm3: Pm3) -> None:
    out, rc = pm3.run([f"hf 14a config --pla ecp.access.{pm3.tci}"], timeout=8)
    print(out)
    if rc == 0:
        pm3.ecp_enabled = True


def disable_ecp(pm3: Pm3) -> None:
    out, rc = pm3.run(["hf 14a config --pla off"], timeout=8)
    print(out)
    if rc == 0:
        pm3.ecp_enabled = False


def poll_once(pm3: Pm3) -> None:
    if not pm3.ecp_enabled:
        print("  ECP not enabled — enabling now ...")
        enable_ecp(pm3)
    out, _ = pm3.run(["hf 14a reader -s"], timeout=8)
    print(out)


def poll_continuous(pm3: Pm3) -> None:
    if not pm3.ecp_enabled:
        enable_ecp(pm3)
    print("\n  Holding polling loop — present iPhone near HF antenna.")
    print("  Ctrl+C to stop.\n")
    with contextlib.suppress(KeyboardInterrupt):
        pm3.run_stream(["hf 14a reader -@"])


def sniff(pm3: Pm3, seconds: float = 10.0) -> None:
    if not pm3.ecp_enabled:
        enable_ecp(pm3)
    print(f"\n  Sniffing 14a traffic for ~{seconds:.0f}s — present iPhone now ...")
    out, _ = pm3.run(["hf 14a sniff"], timeout=seconds + 5)
    print(out)
    print("  Next: option 6 to decode captured frames.")


def show_trace(pm3: Pm3) -> None:
    out, _ = pm3.run(["trace list -t 14a"], timeout=10)
    print(out)


def antenna_tune(pm3: Pm3) -> None:
    out, _ = pm3.run(["hw tune"], timeout=15)
    print(out)


def card_info(pm3: Pm3) -> None:
    out, _ = pm3.run(["hf 14a info"], timeout=8)
    print(out)


def show_prefs(pm3: Pm3) -> None:
    out, _ = pm3.run(["prefs show"], timeout=5)
    print(out)


# --------------- main loop ---------------


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--bin", help="Path to proxmark3 binary")
    ap.add_argument("--port", help="PM3 serial port, e.g. /dev/cu.usbmodemiceman1")
    ap.add_argument("--tci", default=DEFAULT_TCI, help="ECP reader identifier (hex)")
    ap.add_argument(
        "--non-interactive",
        action="store_true",
        help="Enable ECP + continuous poll, no menu (for daemon use)",
    )
    args = ap.parse_args()

    bin_path = args.bin or find_pm3_bin()
    if not bin_path:
        print("ERROR: proxmark3 binary not found. Install with:", file=sys.stderr)
        print("  brew tap RfidResearchGroup/proxmark3", file=sys.stderr)
        print("  brew install --HEAD --with-generic proxmark3", file=sys.stderr)
        return 1

    port = args.port or find_pm3_port()
    if not port:
        print(
            "ERROR: Proxmark3 not found on USB (looked for /dev/cu.usbmodemiceman*).",
            file=sys.stderr,
        )
        print("Plug in PM3 and try again.", file=sys.stderr)
        return 1

    if is_port_busy(port):
        print(f"WARN: {port} is busy — attempting to kill stale pm3 processes.")
        kill_stale_pm3()
        time.sleep(1)

    pm3 = Pm3(bin_path=bin_path, port=port, tci=args.tci)

    if args.non_interactive:
        enable_ecp(pm3)
        poll_continuous(pm3)
        return 0

    while True:
        banner(pm3)
        choice = menu()
        if choice == "1":
            choose_tci(pm3)
        elif choice == "2":
            enable_ecp(pm3)
        elif choice == "3":
            poll_once(pm3)
        elif choice == "4":
            poll_continuous(pm3)
        elif choice == "5":
            sniff(pm3)
        elif choice == "6":
            show_trace(pm3)
        elif choice == "7":
            antenna_tune(pm3)
        elif choice == "8":
            disable_ecp(pm3)
        elif choice == "9":
            card_info(pm3)
        elif choice == "0":
            show_prefs(pm3)
        elif choice in ("q", "quit", "exit"):
            print("Bye.")
            return 0
        else:
            print("  Unknown option.")
        input("\n[Press Enter to continue] ")


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\nInterrupted.")
        sys.exit(130)
