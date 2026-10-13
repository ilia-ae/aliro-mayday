"""
diagbot.py — Telegram bot for live diagnostics of the aliro-esp32 doorlock.

Tails the same three log files as `diagweb.py`:

    /tmp/aliro-debug/serial.log              (ESP UART — pyserial monitor)
    /tmp/aliro-debug/mdns-operational.log    (`dns-sd -B _matter._tcp`)
    /tmp/aliro-debug/mdns-commissioning.log  (`dns-sd -B _matterc._udp`)

Authorizes only the `TELEGRAM_SUPER_ADMIN_ID` from `.env`. Any other
chat is silently ignored — bot replies only to the owner. Add more
trusted ids by setting `TELEGRAM_TRUSTED_IDS=id1,id2,...`.

Commands (all gated to authorized users):

    /start /help     — list commands
    /status          — full snapshot card
    /health          — iPhone session health pill (active/warm/stale/dead)
    /locks [N]       — last N lock command invocations (default 10, max 50)
    /qr              — current pairing QR/manual code
    /wifi            — wifi state
    /errors [N]      — last N error lines (default 10)
    /tail [N]        — last N raw serial lines (default 30, max 200)
    /grep <q>        — last 30 lines matching <q>
    /mdns            — mDNS services seen on LAN
    /alerts on|off   — toggle push alerts
    /whoami          — print your Telegram chat-id (handy for adding trusted ids)

Push alerts (when /alerts is on, default on):

    🔓 / 🔒        — every Lock/Unlock command (with controller node-id)
    🟢 fabric committed
    ⚠️  fabric removed
    🟢 commissioning complete
    🔑 SetAliroReaderConfig OK
    ❌ N errors in last 30s
    💤 iPhone session DEAD (no RX > 2 min)
    💚 iPhone session active again

Run with:

    uv run python diagbot.py

or `just diag-bot`.
"""

from __future__ import annotations

import asyncio
import io
import os
import re
import time
from collections import deque
from dataclasses import dataclass, field
from html import escape as h
from pathlib import Path

import segno
from dotenv import load_dotenv
from telegram import (
    InlineKeyboardButton,
    InlineKeyboardMarkup,
    KeyboardButton,
    ReplyKeyboardMarkup,
    Update,
)
from telegram.constants import ParseMode
from telegram.ext import (
    Application,
    CallbackQueryHandler,
    CommandHandler,
    ContextTypes,
    MessageHandler,
    filters,
)

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

DEFAULT_SERIAL = Path("/tmp/aliro-debug/serial.log")
DEFAULT_MDNS_OPS = Path("/tmp/aliro-debug/mdns-operational.log")
DEFAULT_MDNS_COMM = Path("/tmp/aliro-debug/mdns-commissioning.log")

load_dotenv()

TOKEN = os.getenv("TELEGRAM_BOT_TOKEN", "").strip()
SUPER_ADMIN = int(os.getenv("TELEGRAM_SUPER_ADMIN_ID", "0") or 0)
TRUSTED_IDS_RAW = os.getenv("TELEGRAM_TRUSTED_IDS", "").strip()
TRUSTED: set[int] = {SUPER_ADMIN} if SUPER_ADMIN else set()
if TRUSTED_IDS_RAW:
    for part in TRUSTED_IDS_RAW.split(","):
        part = part.strip()
        if part.isdigit():
            TRUSTED.add(int(part))

if not TOKEN:
    raise SystemExit("TELEGRAM_BOT_TOKEN missing in .env")
if not SUPER_ADMIN:
    raise SystemExit("TELEGRAM_SUPER_ADMIN_ID missing in .env")


# ---------------------------------------------------------------------------
# Shared state (mirrors diagweb.py — kept here so this file is self-sufficient)
# ---------------------------------------------------------------------------


@dataclass
class State:
    serial_lines: deque[str] = field(default_factory=lambda: deque(maxlen=5000))
    serial_total: int = 0

    lock_events: list[dict] = field(default_factory=list)

    em_rx_total: int = 0
    em_tx_total: int = 0
    em_rx_recent: deque[float] = field(default_factory=lambda: deque(maxlen=500))
    last_em_rx_monotonic: float = 0.0
    last_em_rx_wall: str = ""

    uptime_s: int = 0
    heap_free: int = 0
    heap_min: int = 0
    wifi_up: bool = False
    wifi_ssid: str = "-"
    wifi_ch: int = 0
    wifi_rssi: int = 0
    wifi_ip: str = "-"
    wifi_gw: str = "-"
    fabrics: int = 0
    aliro_provisioned: bool = False
    aliro_last: str = ""
    last_hb_a: str = ""
    last_hb_b: str = ""

    iphone_node_id: str = ""
    iphone_fabric_idx: str = ""

    # Current physical door state — None until first LOCKED/UNLOCKED line is
    # seen. Updated from the `ep=N -> LOCKED|UNLOCKED` log emitted by both
    # NFC-tap and Matter-cluster paths (LockHandlers.cpp:apply_lock_state_change).
    door_locked: bool | None = None
    door_last_change_ts: float = 0.0
    door_last_change_wall: str = ""
    door_last_source: str = ""

    set_aliro_count: int = 0
    last_aliro_log: str = ""

    boot_qr: str = ""
    boot_manual: str = ""
    feature_map: str = ""

    error_lines: list[dict] = field(default_factory=list)

    mdns_operational: list[dict] = field(default_factory=list)
    mdns_commissioning: list[dict] = field(default_factory=list)

    fabric_committed: int = 0
    fabric_removed: int = 0
    commissioning_complete_count: int = 0

    log_paths: dict = field(default_factory=dict)

    # Alert subscribers — by default super-admin gets alerts.
    alert_chats: set[int] = field(default_factory=set)

    # Last alert-relevant snapshot, for transition-edge detection.
    last_session_label: str = ""
    last_error_alert_ts: float = 0.0
    error_count_since_alert: int = 0


state = State()


# ---------------------------------------------------------------------------
# Regexes — same as diagweb.py
# ---------------------------------------------------------------------------

RE_LOCK = re.compile(
    r"lock-hdl: (LockDoor|UnlockDoor|UnboltDoor) ep=(\d+) from fabric=(\S+) "
    r"node=0x([0-9a-fA-F]+) pinLen=(\d+)"
)
# NFC tap path — fired by the reader, not by the Matter Door Lock cluster.
# Example: `I (946253) lock-hdl: ep=1 -> LOCKED (src=10)`
RE_LOCK_NFC = re.compile(
    r"lock-hdl: ep=(\d+) -> (LOCKED|UNLOCKED) \(src=(\d+)\)"
)
RE_HB_A = re.compile(
    r"\[diag\] HB-A \| up=(\d+)s heap=(\d+)/(\d+) B  wifi=(UP|DOWN) "
    r'ssid="([^"]*)" ch=(\d+) rssi=(-?\d+) phy=\S+ bssid=\S+ ip=(\S+) gw=(\S+)'
)
RE_HB_B = re.compile(
    r"\[diag\] HB-B \| .*?fabrics=(\d+).*?nfc=(\d+)/(-?\d+)s.*?aliro=(-?\d+)s\(([^)]*)\)"
)
RE_EM_RX = re.compile(r"chip\[EM\]: >>>")
RE_EM_TX = re.compile(r"chip\[EM\]: <<<")
RE_ALIRO_DELEGATE = re.compile(r"aliro: \[delegate\] (.+)")
RE_SET_ALIRO_OK = re.compile(r"SetAliroReaderConfig OK")
RE_FABRIC_COMMITTED = re.compile(r"\[ev\] fabric committed")
RE_FABRIC_REMOVED = re.compile(r"\[ev\] fabric REMOVED")
RE_COMM_COMPLETE = re.compile(r"\[ev\] commissioning COMPLETE")
RE_QR = re.compile(r"SetupQRCode: \[(MT:[A-Z0-9-]+)\]")
RE_MANUAL = re.compile(r"Manual pairing code: \[(\d+)\]")
RE_CLUSTER_DUMP = re.compile(r"DoorLock cluster has (\d+) accepted / (\d+) generated")
RE_ESP_ERROR = re.compile(r"^E \(\d+\) ")

RE_DNSSD = re.compile(
    r"^\s*([\d:.]+)\s+(Add|Rmv)\s+(\d+)\s+(\d+)\s+(\S+)\s+(\S+)\s+(\S+)\s*$"
)


def now_wall() -> str:
    return time.strftime("%H:%M:%S")


# ---------------------------------------------------------------------------
# Async event bus — when parsing emits an "interesting" event, it goes onto
# a queue that the alert task drains and broadcasts to subscribed chats.
# ---------------------------------------------------------------------------

alert_queue: asyncio.Queue[dict] = asyncio.Queue()


def emit(kind: str, **payload) -> None:
    """Schedule an alert. Called from sync parsing code."""
    try:
        alert_queue.put_nowait({"kind": kind, **payload})
    except asyncio.QueueFull:
        pass


# ---------------------------------------------------------------------------
# Line parsing (same shape as diagweb.py — duplicated by design so each
# script can run on its own without a shared module)
# ---------------------------------------------------------------------------


def process_serial_line(raw: str) -> None:
    line = raw.rstrip("\r\n")
    state.serial_lines.append(line)
    state.serial_total += 1

    if m := RE_LOCK.search(line):
        cmd, ep, fab, node, pin = m.groups()
        ev = {
            "wall": now_wall(),
            "ts": time.time(),
            "cmd": cmd,
            "ep": ep,
            "fabric": fab,
            "node_id": node,
            "pin_len": int(pin),
        }
        state.lock_events.append(ev)
        if len(state.lock_events) > 500:
            state.lock_events = state.lock_events[-500:]
        state.iphone_node_id = node
        state.iphone_fabric_idx = fab
        emit("lock", **ev)
    elif m := RE_LOCK_NFC.search(line):
        ep, result, src = m.groups()
        cmd = "LockDoor" if result == "LOCKED" else "UnlockDoor"
        ev = {
            "wall": now_wall(),
            "ts": time.time(),
            "cmd": cmd,
            "ep": ep,
            "fabric": f"src={src}",
            "node_id": "",
            "pin_len": 0,
        }
        state.lock_events.append(ev)
        if len(state.lock_events) > 500:
            state.lock_events = state.lock_events[-500:]
        # Track the current door state — both NFC and Matter paths emit
        # this line, so it's authoritative.
        prev_locked = state.door_locked
        state.door_locked = (result == "LOCKED")
        state.door_last_change_ts = ev["ts"]
        state.door_last_change_wall = ev["wall"]
        state.door_last_source = f"src={src}"
        if prev_locked != state.door_locked:
            emit("door_state", locked=state.door_locked, src=src, wall=ev["wall"])
        emit("lock", **ev)

    if m := RE_HB_A.search(line):
        state.uptime_s = int(m.group(1))
        state.heap_free = int(m.group(2))
        state.heap_min = int(m.group(3))
        state.wifi_up = m.group(4) == "UP"
        state.wifi_ssid = m.group(5)
        state.wifi_ch = int(m.group(6))
        state.wifi_rssi = int(m.group(7))
        state.wifi_ip = m.group(8)
        state.wifi_gw = m.group(9)
        state.last_hb_a = line

    if m := RE_HB_B.search(line):
        state.fabrics = int(m.group(1))
        state.aliro_last = m.group(5)
        state.aliro_provisioned = "SetReaderConfig" in state.aliro_last
        state.last_hb_b = line

    if RE_EM_RX.search(line):
        state.em_rx_total += 1
        t = time.time()
        state.em_rx_recent.append(t)
        state.last_em_rx_monotonic = t
        state.last_em_rx_wall = now_wall()

    if RE_EM_TX.search(line):
        state.em_tx_total += 1

    if m := RE_ALIRO_DELEGATE.search(line):
        state.last_aliro_log = m.group(1)
        if RE_SET_ALIRO_OK.search(line):
            state.set_aliro_count += 1
            emit("aliro_provisioned", text=m.group(1))

    if RE_FABRIC_COMMITTED.search(line):
        state.fabric_committed += 1
        emit("fabric_committed", count=state.fabric_committed)
    if RE_FABRIC_REMOVED.search(line):
        state.fabric_removed += 1
        emit("fabric_removed", count=state.fabric_removed)
    if RE_COMM_COMPLETE.search(line):
        state.commissioning_complete_count += 1
        emit("commissioning_complete", count=state.commissioning_complete_count)

    if m := RE_QR.search(line):
        state.boot_qr = m.group(1)
    if m := RE_MANUAL.search(line):
        state.boot_manual = m.group(1)
    if m := RE_CLUSTER_DUMP.search(line):
        state.feature_map = f"{m.group(1)} accepted / {m.group(2)} generated"

    if RE_ESP_ERROR.search(line):
        state.error_lines.append({"wall": now_wall(), "line": line})
        state.error_count_since_alert += 1
        if len(state.error_lines) > 200:
            state.error_lines = state.error_lines[-200:]


def process_mdns_line(raw: str, target: list[dict]) -> None:
    line = raw.rstrip("\r\n")
    m = RE_DNSSD.match(line)
    if not m:
        return
    ts, action, _, _, _, service, instance = m.groups()
    if action == "Add":
        if not any(r.get("instance") == instance for r in target):
            target.append(
                {"wall": now_wall(), "ts_dnssd": ts, "service": service, "instance": instance}
            )
            if len(target) > 100:
                target[:] = target[-100:]
    elif action == "Rmv":
        target[:] = [r for r in target if r.get("instance") != instance]


# ---------------------------------------------------------------------------
# Tailer task
# ---------------------------------------------------------------------------


async def tail_file(path: Path, processor) -> None:
    pos = 0
    while True:
        try:
            if path.exists():
                with open(path, "rb") as f:
                    f.seek(pos)
                    chunk = f.read()
                if chunk:
                    pos += len(chunk)
                    text = chunk.decode("utf-8", errors="replace")
                    for line in text.split("\n"):
                        if line:
                            processor(line)
        except Exception:
            await asyncio.sleep(2.0)
        await asyncio.sleep(0.4)


# ---------------------------------------------------------------------------
# Session-health computation
# ---------------------------------------------------------------------------


def session_health() -> tuple[str, str, int | None]:
    """Returns (label, emoji, age_s)."""
    if state.last_em_rx_monotonic == 0:
        return "no controller traffic ever", "⚪️", None
    age = int(time.time() - state.last_em_rx_monotonic)
    if age <= 5:
        return "active", "🟢", age
    if age <= 30:
        return "warm", "🟡", age
    if age <= 120:
        return "stale", "🟠", age
    return "DEAD", "🔴", age


# ---------------------------------------------------------------------------
# Auth helper
# ---------------------------------------------------------------------------


def authorized(update: Update) -> bool:
    cid = update.effective_chat.id if update.effective_chat else 0
    return cid in TRUSTED


async def gate(update: Update) -> bool:
    """Reply with a polite refusal to non-trusted chats so the bot is silent
    to outsiders but the owner can see they tried."""
    if authorized(update):
        return True
    cid = update.effective_chat.id if update.effective_chat else 0
    # Tell super-admin who tried, once per minute per chat.
    return False


# ---------------------------------------------------------------------------
# Command handlers
# ---------------------------------------------------------------------------


HELP_TEXT = (
    "<b>aliro-esp32 diagbot</b>\n\n"
    "<b>door</b>:\n"
    "/door — current state + Lock/Unlock/Toggle/History buttons\n"
    "  ⚠️ Lock/Unlock buttons are <i>bot-local</i> for now — they update "
    "the bot's state and post an alert, but do not drive the ESP yet.\n\n"
    "<b>state</b>:\n"
    "/status — full snapshot\n"
    "/health — iPhone session health\n"
    "/wifi — Wi-Fi state\n"
    "/qr — current pairing payload (PNG)\n"
    "/mdns — mDNS services on LAN\n\n"
    "<b>logs</b>:\n"
    "/tail [N] — last N raw serial lines\n"
    "/grep &lt;q&gt; — search log for &lt;q&gt;\n"
    "/errors [N] — last N error lines\n\n"
    "<b>misc</b>:\n"
    "/alerts on|off — toggle push alerts\n"
    "/whoami — show your Telegram chat-id"
)


# ---------------------------------------------------------------------------
# Menus
# ---------------------------------------------------------------------------

# Persistent bottom keyboard. Each label maps 1:1 to a command in
# BOTTOM_MENU_ROUTES below — Telegram sends the label as a plain text
# message and the MessageHandler dispatches it.
BOTTOM_MENU = ReplyKeyboardMarkup(
    [
        [KeyboardButton("🚪 Door"), KeyboardButton("📊 Status"), KeyboardButton("❤️ Health")],
        [KeyboardButton("📱 QR"), KeyboardButton("🌐 mDNS"), KeyboardButton("📡 Wi-Fi")],
        [KeyboardButton("⚠️ Errors"), KeyboardButton("📜 Tail"), KeyboardButton("🔔 Alerts")],
    ],
    resize_keyboard=True,
    is_persistent=True,
    input_field_placeholder="pick an action or type /help",
)


def status_inline_kb() -> InlineKeyboardMarkup:
    """Inline action row attached under /status snapshots."""
    return InlineKeyboardMarkup(
        [
            [
                InlineKeyboardButton("🔄 Refresh", callback_data="refresh_status"),
                InlineKeyboardButton("❤️ Health", callback_data="show_health"),
            ],
            [
                InlineKeyboardButton("🚪 Door", callback_data="show_door"),
                InlineKeyboardButton("📱 QR", callback_data="show_qr"),
            ],
            [
                InlineKeyboardButton("⚠️ Errors", callback_data="show_errors"),
                InlineKeyboardButton("📜 Tail 30", callback_data="show_tail"),
            ],
        ]
    )


def door_inline_kb() -> InlineKeyboardMarkup:
    """Lock/Unlock/Toggle + history. The Lock/Unlock/Toggle buttons are
    bot-local (no firmware control channel yet) — they update the bot's
    notion of the door state and broadcast an alert. When real control
    lands (HTTP on ESP), swap the callback bodies."""
    return InlineKeyboardMarkup(
        [
            [
                InlineKeyboardButton("🔓 Unlock", callback_data="door_unlock"),
                InlineKeyboardButton("🔒 Lock", callback_data="door_lock"),
            ],
            [
                InlineKeyboardButton("🔄 Toggle", callback_data="door_toggle"),
                InlineKeyboardButton("📜 History 10", callback_data="door_history"),
            ],
            [InlineKeyboardButton("🔄 Refresh", callback_data="door_refresh")],
        ]
    )


def alerts_inline_kb(on: bool) -> InlineKeyboardMarkup:
    return InlineKeyboardMarkup(
        [
            [
                InlineKeyboardButton(
                    ("🔔 Alerts: ON" if on else "🔕 Alerts: OFF"),
                    callback_data=("alerts_off" if on else "alerts_on"),
                )
            ]
        ]
    )


async def cmd_help(update: Update, ctx: ContextTypes.DEFAULT_TYPE) -> None:
    if not await gate(update):
        return
    await update.message.reply_html(
        HELP_TEXT, disable_web_page_preview=True, reply_markup=BOTTOM_MENU
    )


async def cmd_whoami(update: Update, ctx: ContextTypes.DEFAULT_TYPE) -> None:
    cid = update.effective_chat.id
    uid = update.effective_user.id if update.effective_user else 0
    is_trusted = cid in TRUSTED
    await update.message.reply_html(
        f"chat_id = <code>{cid}</code>\nuser_id = <code>{uid}</code>\n"
        f"trusted = {'✅' if is_trusted else '❌'}\n"
        f"super_admin = <code>{SUPER_ADMIN}</code>"
    )


def _door_pill() -> str:
    if state.door_locked is None:
        return "🚪 <b>Door</b>: ⚪️ unknown (no lock event yet)"
    if state.door_locked:
        body = "🔒 <b>LOCKED</b>"
    else:
        body = "🔓 <b>UNLOCKED</b>"
    age = int(time.time() - state.door_last_change_ts) if state.door_last_change_ts else None
    age_str = f", changed {age}s ago" if age is not None else ""
    src = f" via {state.door_last_source}" if state.door_last_source else ""
    return f"🚪 <b>Door</b>: {body}{src}{age_str}"


def _fmt_status() -> str:
    label, emoji, age = session_health()
    age_str = f"{age}s ago" if age is not None else "never"
    lines = []
    lines.append(_door_pill())
    lines.append("")
    lines.append(f"<b>iPhone session</b>: {emoji} {label} ({age_str})")
    if state.iphone_node_id:
        lines.append(f"  controller node = <code>0x{state.iphone_node_id}</code>  fabric={state.iphone_fabric_idx}")
    lines.append(f"  RX/TX totals = {state.em_rx_total} / {state.em_tx_total}")
    last_60 = sum(1 for t in state.em_rx_recent if time.time() - t <= 60)
    last_10 = sum(1 for t in state.em_rx_recent if time.time() - t <= 10)
    lines.append(f"  RX last 10 s / 60 s = {last_10} / {last_60}")
    lines.append("")

    wifi = f"{'UP' if state.wifi_up else 'DOWN'} ssid=<code>{h(state.wifi_ssid)}</code>"
    lines.append(f"<b>Device</b>: uptime {state.uptime_s}s, heap free {state.heap_free} B")
    lines.append(f"  Wi-Fi {wifi}, ch={state.wifi_ch}, rssi={state.wifi_rssi}")
    lines.append(f"  IP={state.wifi_ip}  GW={state.wifi_gw}")
    lines.append(f"  fabrics={state.fabrics}  aliro={'✅' if state.aliro_provisioned else '❌'}")
    lines.append(f"  pairing: committed={state.fabric_committed}, removed={state.fabric_removed}, comm-complete={state.commissioning_complete_count}")
    lines.append("")

    if state.boot_qr:
        lines.append(f"<b>QR</b>: <code>{state.boot_qr}</code>")
    if state.boot_manual:
        lines.append(f"<b>Manual</b>: <code>{state.boot_manual}</code>")
    if state.feature_map:
        lines.append(f"<b>FeatureMap</b>: {state.feature_map}")

    if state.lock_events:
        last = state.lock_events[-1]
        lines.append("")
        lines.append(f"<b>Last lock cmd</b>: {last['wall']}  {last['cmd']}  fabric={last['fabric']}  node=0x{last['node_id']}")
    return "\n".join(lines)


async def cmd_status(update: Update, ctx: ContextTypes.DEFAULT_TYPE) -> None:
    if not await gate(update):
        return
    await update.message.reply_html(_fmt_status(), reply_markup=status_inline_kb())


async def cmd_door(update: Update, ctx: ContextTypes.DEFAULT_TYPE) -> None:
    if not await gate(update):
        return
    await update.message.reply_html(_door_pill(), reply_markup=door_inline_kb())


def _apply_telegram_door_change(target_locked: bool, who: str) -> dict:
    """Bot-local door-state change driven by a Telegram tap. Updates the
    in-memory state, appends a synthetic lock event with fabric=tg, and
    emits an alert. Returns the synthesised event dict."""
    cmd = "LockDoor" if target_locked else "UnlockDoor"
    ev = {
        "wall": now_wall(),
        "ts": time.time(),
        "cmd": cmd,
        "ep": "1",
        "fabric": f"tg:{who}",
        "node_id": "",
        "pin_len": 0,
    }
    state.lock_events.append(ev)
    if len(state.lock_events) > 500:
        state.lock_events = state.lock_events[-500:]
    prev = state.door_locked
    state.door_locked = target_locked
    state.door_last_change_ts = ev["ts"]
    state.door_last_change_wall = ev["wall"]
    state.door_last_source = f"tg:{who}"
    if prev != target_locked:
        emit("door_state", locked=target_locked, src=f"tg:{who}", wall=ev["wall"])
    emit("lock", **ev)
    return ev


def _fmt_lock_history(n: int = 10) -> str:
    events = state.lock_events[-n:]
    if not events:
        return "🔐 <b>No lock events yet</b>"
    lines = [f"<b>Last {len(events)} lock events</b>:"]
    prev = None
    for ev in events:
        emoji = "🔒" if ev["cmd"] == "LockDoor" else "🔓"
        delta = f" (+{ev['ts']-prev:.1f}s)" if prev else ""
        prev = ev["ts"]
        node_str = f" node=<code>0x{ev['node_id']}</code>" if ev.get("node_id") else ""
        lines.append(
            f"{ev['wall']}  {emoji} {ev['cmd']}  fab={ev['fabric']}{node_str}{delta}"
        )
    return "\n".join(lines)


async def cmd_health(update: Update, ctx: ContextTypes.DEFAULT_TYPE) -> None:
    if not await gate(update):
        return
    label, emoji, age = session_health()
    age_str = f"{age}s ago" if age is not None else "never"
    txt = f"{emoji} <b>{label}</b>  (last RX from iPhone: {age_str})"
    if state.last_em_rx_wall:
        txt += f"\nlast RX wall = {state.last_em_rx_wall}"
    await update.message.reply_html(txt)


async def cmd_wifi(update: Update, ctx: ContextTypes.DEFAULT_TYPE) -> None:
    if not await gate(update):
        return
    txt = (
        f"<b>{'UP' if state.wifi_up else 'DOWN'}</b>\n"
        f"ssid = <code>{h(state.wifi_ssid)}</code>\n"
        f"ch = {state.wifi_ch}\n"
        f"rssi = {state.wifi_rssi} dBm\n"
        f"ip = <code>{state.wifi_ip}</code>\n"
        f"gw = <code>{state.wifi_gw}</code>"
    )
    await update.message.reply_html(txt)


async def cmd_locks(update: Update, ctx: ContextTypes.DEFAULT_TYPE) -> None:
    """Kept for backwards-compat with /locks slash command — same data
    is now reachable via the 🚪 Door button → 📜 History."""
    if not await gate(update):
        return
    n = 10
    if ctx.args and ctx.args[0].isdigit():
        n = max(1, min(50, int(ctx.args[0])))
    await update.message.reply_html(_fmt_lock_history(n))


def _render_qr_png(payload: str) -> bytes:
    buf = io.BytesIO()
    segno.make(payload, error="m").save(buf, kind="png", scale=10, border=4)
    return buf.getvalue()


async def cmd_qr(update: Update, ctx: ContextTypes.DEFAULT_TYPE) -> None:
    if not await gate(update):
        return
    if not state.boot_qr and not state.boot_manual:
        await update.message.reply_text("no pairing code seen yet — has ESP booted?")
        return

    caption_parts = []
    if state.boot_qr:
        caption_parts.append(f"<b>QR</b>: <code>{state.boot_qr}</code>")
    if state.boot_manual:
        caption_parts.append(f"<b>Manual</b>: <code>{state.boot_manual}</code>")
    caption = "\n".join(caption_parts)

    if state.boot_qr:
        png = _render_qr_png(state.boot_qr)
        await update.message.reply_photo(
            photo=png,
            caption=caption,
            parse_mode=ParseMode.HTML,
            filename="pairing.png",
        )
    else:
        await update.message.reply_html(caption)


async def cmd_errors(update: Update, ctx: ContextTypes.DEFAULT_TYPE) -> None:
    if not await gate(update):
        return
    n = 10
    if ctx.args and ctx.args[0].isdigit():
        n = max(1, min(50, int(ctx.args[0])))
    errs = state.error_lines[-n:]
    if not errs:
        await update.message.reply_text("no errors")
        return
    body = "\n".join(f"{e['wall']}  {e['line']}" for e in errs)
    await update.message.reply_text(
        f"last {len(errs)} errors (total {len(state.error_lines)}):\n\n{body}"
    )


async def cmd_tail(update: Update, ctx: ContextTypes.DEFAULT_TYPE) -> None:
    if not await gate(update):
        return
    n = 30
    if ctx.args and ctx.args[0].isdigit():
        n = max(1, min(200, int(ctx.args[0])))
    lines = list(state.serial_lines)[-n:]
    if not lines:
        await update.message.reply_text("serial buffer empty")
        return
    body = "\n".join(lines)
    # Telegram messages cap at 4096 chars — chunk if needed.
    await _send_long_text(update, body, prefix=f"tail {n} (buffer={len(state.serial_lines)})")


async def cmd_grep(update: Update, ctx: ContextTypes.DEFAULT_TYPE) -> None:
    if not await gate(update):
        return
    if not ctx.args:
        await update.message.reply_text("usage: /grep <query>")
        return
    q = " ".join(ctx.args).lower()
    matches = [l for l in state.serial_lines if q in l.lower()]
    if not matches:
        await update.message.reply_text(f"no matches for {q!r} in {len(state.serial_lines)} lines")
        return
    matches = matches[-30:]
    body = "\n".join(matches)
    await _send_long_text(
        update, body, prefix=f"grep {q!r}: {len(matches)} of {len(state.serial_lines)}"
    )


async def cmd_mdns(update: Update, ctx: ContextTypes.DEFAULT_TYPE) -> None:
    if not await gate(update):
        return
    lines = ["<b>_matter._tcp (operational)</b>:"]
    if state.mdns_operational:
        for r in state.mdns_operational:
            lines.append(f"  {r['wall']}  <code>{h(r['instance'])}</code>")
    else:
        lines.append("  (none)")
    lines.append("")
    lines.append("<b>_matterc._udp (commissioning window open)</b>:")
    if state.mdns_commissioning:
        for r in state.mdns_commissioning:
            lines.append(f"  {r['wall']}  <code>{h(r['instance'])}</code>")
    else:
        lines.append("  (none)")
    await update.message.reply_html("\n".join(lines))


async def cmd_alerts(update: Update, ctx: ContextTypes.DEFAULT_TYPE) -> None:
    if not await gate(update):
        return
    cid = update.effective_chat.id
    if not ctx.args:
        on = cid in state.alert_chats
        await update.message.reply_text(
            f"alerts for this chat: {'ON' if on else 'OFF'}",
            reply_markup=alerts_inline_kb(on),
        )
        return
    flag = ctx.args[0].lower()
    if flag in ("on", "1", "true", "yes"):
        state.alert_chats.add(cid)
        await update.message.reply_text(
            "alerts ON for this chat", reply_markup=alerts_inline_kb(True)
        )
    elif flag in ("off", "0", "false", "no"):
        state.alert_chats.discard(cid)
        await update.message.reply_text(
            "alerts OFF for this chat", reply_markup=alerts_inline_kb(False)
        )
    else:
        await update.message.reply_text("usage: /alerts on|off")


async def _send_long_text(update: Update, body: str, prefix: str = "") -> None:
    """Telegram caps messages at 4096 chars. Chunk over <pre> blocks."""
    MAX = 3800  # leave room for <pre> wrappers
    if prefix:
        await update.message.reply_text(prefix)
    chunks = []
    cur = ""
    for line in body.split("\n"):
        if len(cur) + len(line) + 1 > MAX:
            chunks.append(cur)
            cur = ""
        cur += line + "\n"
    if cur:
        chunks.append(cur)
    for c in chunks:
        await update.message.reply_html(f"<pre>{h(c)}</pre>")


# ---------------------------------------------------------------------------
# Bottom-menu text router + inline-button callback router
# ---------------------------------------------------------------------------


# Map bottom-keyboard labels → command handlers above.
BOTTOM_MENU_ROUTES: dict[str, object] = {
    "🚪 Door": cmd_door,
    "📊 Status": cmd_status,
    "❤️ Health": cmd_health,
    "📡 Wi-Fi": cmd_wifi,
    "📱 QR": cmd_qr,
    "🌐 mDNS": cmd_mdns,
    "⚠️ Errors": cmd_errors,
    "📜 Tail": cmd_tail,
    "🔔 Alerts": cmd_alerts,
}


async def on_menu_text(update: Update, ctx: ContextTypes.DEFAULT_TYPE) -> None:
    if not await gate(update):
        return
    txt = (update.message.text or "").strip() if update.message else ""
    handler = BOTTOM_MENU_ROUTES.get(txt)
    if handler is None:
        return
    # Bottom-menu taps don't carry args; clear any leftovers.
    ctx.args = []
    await handler(update, ctx)


async def on_callback(update: Update, ctx: ContextTypes.DEFAULT_TYPE) -> None:
    q = update.callback_query
    if q is None:
        return
    cid = q.message.chat.id if q.message else 0
    if cid not in TRUSTED:
        await q.answer("not authorized", show_alert=False)
        return

    data = q.data or ""
    await q.answer()  # ack the spinner

    if data == "refresh_status":
        try:
            await q.edit_message_text(
                _fmt_status(),
                parse_mode=ParseMode.HTML,
                reply_markup=status_inline_kb(),
            )
        except Exception:
            # message unchanged — just reply with a fresh card
            await ctx.bot.send_message(
                chat_id=cid,
                text=_fmt_status(),
                parse_mode=ParseMode.HTML,
                reply_markup=status_inline_kb(),
            )
        return

    if data == "show_health":
        label, emoji, age = session_health()
        age_str = f"{age}s ago" if age is not None else "never"
        await ctx.bot.send_message(
            chat_id=cid,
            text=f"{emoji} <b>{label}</b>  (last RX from iPhone: {age_str})",
            parse_mode=ParseMode.HTML,
        )
        return

    if data == "show_qr":
        if not state.boot_qr and not state.boot_manual:
            await ctx.bot.send_message(chat_id=cid, text="no pairing code seen yet — has ESP booted?")
            return
        caption_parts = []
        if state.boot_qr:
            caption_parts.append(f"<b>QR</b>: <code>{state.boot_qr}</code>")
        if state.boot_manual:
            caption_parts.append(f"<b>Manual</b>: <code>{state.boot_manual}</code>")
        caption = "\n".join(caption_parts)
        if state.boot_qr:
            png = _render_qr_png(state.boot_qr)
            await ctx.bot.send_photo(
                chat_id=cid,
                photo=png,
                caption=caption,
                parse_mode=ParseMode.HTML,
                filename="pairing.png",
            )
        else:
            await ctx.bot.send_message(chat_id=cid, text=caption, parse_mode=ParseMode.HTML)
        return

    if data == "show_door":
        await ctx.bot.send_message(
            chat_id=cid,
            text=_door_pill(),
            parse_mode=ParseMode.HTML,
            reply_markup=door_inline_kb(),
        )
        return

    if data == "show_errors":
        errs = state.error_lines[-10:]
        if not errs:
            await ctx.bot.send_message(chat_id=cid, text="no errors")
            return
        body = "\n".join(f"{e['wall']}  {e['line']}" for e in errs)
        await ctx.bot.send_message(
            chat_id=cid, text=f"last {len(errs)} errors:\n\n{body}"
        )
        return

    if data == "show_tail":
        lines = list(state.serial_lines)[-30:]
        if not lines:
            await ctx.bot.send_message(chat_id=cid, text="serial buffer empty")
            return
        body = "\n".join(lines)
        # _send_long_text needs an Update; inline its chunking here.
        MAX = 3800
        chunks: list[str] = []
        cur = ""
        for line in body.split("\n"):
            if len(cur) + len(line) + 1 > MAX:
                chunks.append(cur)
                cur = ""
            cur += line + "\n"
        if cur:
            chunks.append(cur)
        for c in chunks:
            await ctx.bot.send_message(
                chat_id=cid, text=f"<pre>{h(c)}</pre>", parse_mode=ParseMode.HTML
            )
        return

    if data in ("door_lock", "door_unlock", "door_toggle"):
        if data == "door_lock":
            target = True
        elif data == "door_unlock":
            target = False
        else:
            target = not bool(state.door_locked)  # None → True (lock)
        who_uid = update.effective_user.id if update.effective_user else cid
        _apply_telegram_door_change(target_locked=target, who=str(who_uid))
        try:
            await q.edit_message_text(
                _door_pill(),
                parse_mode=ParseMode.HTML,
                reply_markup=door_inline_kb(),
            )
        except Exception:
            await ctx.bot.send_message(
                chat_id=cid,
                text=_door_pill(),
                parse_mode=ParseMode.HTML,
                reply_markup=door_inline_kb(),
            )
        return

    if data == "door_refresh":
        try:
            await q.edit_message_text(
                _door_pill(),
                parse_mode=ParseMode.HTML,
                reply_markup=door_inline_kb(),
            )
        except Exception:
            pass
        return

    if data == "door_history":
        await ctx.bot.send_message(
            chat_id=cid,
            text=_fmt_lock_history(10),
            parse_mode=ParseMode.HTML,
        )
        return

    if data in ("alerts_on", "alerts_off"):
        if data == "alerts_on":
            state.alert_chats.add(cid)
        else:
            state.alert_chats.discard(cid)
        on = cid in state.alert_chats
        try:
            await q.edit_message_reply_markup(reply_markup=alerts_inline_kb(on))
        except Exception:
            pass
        await ctx.bot.send_message(
            chat_id=cid, text=f"alerts {'ON' if on else 'OFF'} for this chat"
        )
        return


# ---------------------------------------------------------------------------
# Push-alert pump
# ---------------------------------------------------------------------------


async def alert_pump(app: Application) -> None:
    """Drains the alert queue and broadcasts to subscribed chats. Also runs
    transition-edge checks (session health) on a 10-sec heartbeat."""
    last_check = 0.0
    while True:
        try:
            ev = await asyncio.wait_for(alert_queue.get(), timeout=5.0)
            await broadcast_alert(app, ev)
        except asyncio.TimeoutError:
            pass

        now = time.time()
        if now - last_check >= 10.0:
            last_check = now
            await check_session_transition(app)
            await check_error_burst(app)


async def broadcast_alert(app: Application, ev: dict) -> None:
    text = format_alert(ev)
    if not text:
        return
    for cid in list(state.alert_chats):
        try:
            await app.bot.send_message(
                chat_id=cid, text=text, parse_mode=ParseMode.HTML
            )
        except Exception:
            pass


def format_alert(ev: dict) -> str | None:
    k = ev.get("kind")
    if k == "lock":
        emoji = "🔒" if ev["cmd"] == "LockDoor" else "🔓"
        node_str = f"  node=<code>0x{ev['node_id']}</code>" if ev.get("node_id") else ""
        return (
            f"{emoji} <b>{ev['cmd']}</b>  ep={ev['ep']}  fab={ev['fabric']}{node_str}"
            f"  ({ev['wall']})"
        )
    if k == "door_state":
        return (
            f"🚪 Door is now <b>{'LOCKED 🔒' if ev['locked'] else 'UNLOCKED 🔓'}</b>"
            f"  (src={ev['src']}, {ev['wall']})"
        )
    if k == "fabric_committed":
        return f"🟢 fabric committed (total now {ev['count']})"
    if k == "fabric_removed":
        return f"⚠️ fabric REMOVED (total {ev['count']})"
    if k == "commissioning_complete":
        return f"🟢 commissioning COMPLETE (#{ev['count']})"
    if k == "aliro_provisioned":
        return f"🔑 Aliro: {h(ev.get('text',''))}"
    if k == "session_dead":
        return f"💤 iPhone session DEAD — no RX from controller for {ev['age_s']}s"
    if k == "session_alive":
        return f"💚 iPhone session active again (RX after {ev['gap_s']}s)"
    if k == "error_burst":
        return f"❌ {ev['count']} new ESP errors in last 30 s"
    return None


async def check_session_transition(app: Application) -> None:
    label, emoji, age = session_health()
    prev = state.last_session_label
    state.last_session_label = label

    # Transitions worth alerting on:
    #   active/warm → DEAD       (subscription died — main bug we're chasing)
    #   DEAD → active            (recovered)
    if not prev:
        return
    if label == "DEAD" and prev != "DEAD":
        emit("session_dead", age_s=age)
    elif label == "active" and prev == "DEAD":
        emit("session_alive", gap_s=age or 0)


async def check_error_burst(app: Application) -> None:
    now = time.time()
    if state.error_count_since_alert >= 5 and now - state.last_error_alert_ts > 30:
        emit("error_burst", count=state.error_count_since_alert)
        state.error_count_since_alert = 0
        state.last_error_alert_ts = now


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


async def post_init(app: Application) -> None:
    state.log_paths = {
        "serial": DEFAULT_SERIAL,
        "mdns_ops": DEFAULT_MDNS_OPS,
        "mdns_comm": DEFAULT_MDNS_COMM,
    }
    # Default alert subscription: super-admin gets push alerts.
    state.alert_chats.add(SUPER_ADMIN)

    asyncio.create_task(tail_file(DEFAULT_SERIAL, process_serial_line))
    asyncio.create_task(
        tail_file(DEFAULT_MDNS_OPS, lambda l: process_mdns_line(l, state.mdns_operational))
    )
    asyncio.create_task(
        tail_file(
            DEFAULT_MDNS_COMM, lambda l: process_mdns_line(l, state.mdns_commissioning)
        )
    )
    asyncio.create_task(alert_pump(app))

    # Boot ping to super-admin so they know the bot is up.
    try:
        await app.bot.send_message(
            chat_id=SUPER_ADMIN,
            text=f"🤖 diagbot online\ntailing: {DEFAULT_SERIAL}\ntrusted: {sorted(TRUSTED)}",
        )
    except Exception:
        pass


async def on_error(update: object, ctx: ContextTypes.DEFAULT_TYPE) -> None:
    import traceback

    err = ctx.error
    print(f"[diagbot] handler error: {err!r}", flush=True)
    traceback.print_exception(type(err), err, err.__traceback__)


def main() -> None:
    app = (
        Application.builder()
        .token(TOKEN)
        .post_init(post_init)
        .build()
    )

    app.add_error_handler(on_error)

    app.add_handler(CommandHandler(["start", "help"], cmd_help))
    app.add_handler(CommandHandler("whoami", cmd_whoami))
    app.add_handler(CommandHandler("status", cmd_status))
    app.add_handler(CommandHandler("door", cmd_door))
    app.add_handler(CommandHandler("health", cmd_health))
    app.add_handler(CommandHandler("wifi", cmd_wifi))
    app.add_handler(CommandHandler("locks", cmd_locks))
    app.add_handler(CommandHandler("qr", cmd_qr))
    app.add_handler(CommandHandler("errors", cmd_errors))
    app.add_handler(CommandHandler("tail", cmd_tail))
    app.add_handler(CommandHandler("grep", cmd_grep))
    app.add_handler(CommandHandler("mdns", cmd_mdns))
    app.add_handler(CommandHandler("alerts", cmd_alerts))

    # Inline-button callbacks (refresh, qr, locks, alerts toggle, ...)
    app.add_handler(CallbackQueryHandler(on_callback))

    # Bottom-keyboard taps arrive as plain text messages — route to commands.
    app.add_handler(
        MessageHandler(filters.TEXT & ~filters.COMMAND, on_menu_text)
    )

    print(f"diagbot starting (super_admin={SUPER_ADMIN}, trusted={sorted(TRUSTED)})")
    app.run_polling(close_loop=False)


if __name__ == "__main__":
    main()
