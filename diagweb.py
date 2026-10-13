"""
diagweb.py — all-in-one diagnostic dashboard for the aliro-esp32 Matter doorlock.

Tails three background log files written by the active debug session:

    /tmp/aliro-debug/serial.log              (ESP UART, written by py-serial monitor)
    /tmp/aliro-debug/mdns-operational.log    (`dns-sd -B _matter._tcp`)
    /tmp/aliro-debug/mdns-commissioning.log  (`dns-sd -B _matterc._udp`)

Parses every line into structured state (boot info, heartbeats, lock commands,
EM/IM message counters, aliro provisioning events, mDNS service appearances)
and serves a single HTML page on http://127.0.0.1:8788 that:

  * shows live snapshots refreshed every second
  * exposes "seconds since last RX from iPhone" — the single number that
    answers "is the CASE session alive right now or not"
  * keeps a rolling history of every LockDoor/UnlockDoor with timestamps
    and the controller's node-id, so a flapping pattern is obvious
  * embeds the live serial tail with grep/filter buttons and a copy-all
    button (everything is plain <pre> text — easy to paste into a bug report)

Run with:

    uv run python diagweb.py

or via `just diag`.
"""

from __future__ import annotations

import argparse
import asyncio
import re
import time
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path

from fastapi import FastAPI
from fastapi.responses import HTMLResponse, JSONResponse, PlainTextResponse

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

DEFAULT_SERIAL = Path("/tmp/aliro-debug/serial.log")
DEFAULT_MDNS_OPS = Path("/tmp/aliro-debug/mdns-operational.log")
DEFAULT_MDNS_COMM = Path("/tmp/aliro-debug/mdns-commissioning.log")
DEFAULT_PORT = 8788

# ---------------------------------------------------------------------------
# In-memory state
# ---------------------------------------------------------------------------


@dataclass
class State:
    # Raw serial tail (capped — show last 5000 lines in UI).
    serial_lines: deque[str] = field(default_factory=lambda: deque(maxlen=5000))
    serial_total: int = 0

    # Parsed lock command history.
    lock_events: list[dict] = field(default_factory=list)

    # EM (exchange manager) traffic counters — RX/TX from chip[EM] log lines.
    em_rx_total: int = 0
    em_tx_total: int = 0
    em_rx_recent: deque[float] = field(default_factory=lambda: deque(maxlen=500))  # monotonic ts
    last_em_rx_monotonic: float = 0.0
    last_em_rx_wall: str = ""

    # Latest heartbeat parse.
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
    aliro_age_s: int = -1
    nfc_count: int = 0
    nfc_last_uid: str = ""
    last_hb_a: str = ""
    last_hb_b: str = ""

    # iPhone (controller) info from the most recent lock-handler invocation.
    iphone_node_id: str = ""
    iphone_fabric_idx: str = ""

    # Aliro reader keys provisioning status.
    set_aliro_count: int = 0
    last_aliro_log: str = ""

    # Boot snapshot.
    boot_qr: str = ""
    boot_manual: str = ""
    feature_map: str = ""
    cluster_init_seen: bool = False

    # Errors.
    error_lines: list[dict] = field(default_factory=list)

    # mDNS service appearances.
    mdns_operational: list[dict] = field(default_factory=list)
    mdns_commissioning: list[dict] = field(default_factory=list)

    # Commissioning events.
    fabric_committed: int = 0
    fabric_removed: int = 0
    commissioning_complete_count: int = 0

    log_paths: dict = field(default_factory=dict)


state = State()

# ---------------------------------------------------------------------------
# Regexes
# ---------------------------------------------------------------------------

RE_LOCK = re.compile(
    r"lock-hdl: (LockDoor|UnlockDoor|UnboltDoor) ep=(\d+) from fabric=(\S+) "
    r"node=0x([0-9a-fA-F]+) pinLen=(\d+)"
)
RE_LOCK_RESULT = re.compile(r"lock-hdl: ep=(\d+) -> (LOCKED|UNLOCKED)")
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

# dns-sd output line:
#   11:44:24.408  Add        3  18 local.   _matter._tcp.   <Instance>
RE_DNSSD = re.compile(
    r"^\s*([\d:.]+)\s+(Add|Rmv)\s+(\d+)\s+(\d+)\s+(\S+)\s+(\S+)\s+(\S+)\s*$"
)


# ---------------------------------------------------------------------------
# Line parsing
# ---------------------------------------------------------------------------


def now_wall() -> str:
    return time.strftime("%H:%M:%S")


def process_serial_line(raw: str) -> None:
    line = raw.rstrip("\r\n")
    state.serial_lines.append(line)
    state.serial_total += 1

    if m := RE_LOCK.search(line):
        cmd, ep, fab, node, pin = m.groups()
        state.lock_events.append(
            {
                "wall": now_wall(),
                "ts": time.time(),
                "cmd": cmd,
                "ep": ep,
                "fabric": fab,
                "node_id": node,
                "pin_len": int(pin),
            }
        )
        # Cap history.
        if len(state.lock_events) > 500:
            state.lock_events = state.lock_events[-500:]
        state.iphone_node_id = node
        state.iphone_fabric_idx = fab

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
        state.nfc_count = int(m.group(2))
        state.aliro_age_s = int(m.group(4))
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

    if RE_FABRIC_COMMITTED.search(line):
        state.fabric_committed += 1
    if RE_FABRIC_REMOVED.search(line):
        state.fabric_removed += 1
    if RE_COMM_COMPLETE.search(line):
        state.commissioning_complete_count += 1

    if m := RE_QR.search(line):
        state.boot_qr = m.group(1)
    if m := RE_MANUAL.search(line):
        state.boot_manual = m.group(1)
    if m := RE_CLUSTER_DUMP.search(line):
        state.feature_map = f"{m.group(1)} accepted / {m.group(2)} generated"
        state.cluster_init_seen = True

    if RE_ESP_ERROR.search(line):
        state.error_lines.append({"wall": now_wall(), "line": line})
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
                {
                    "wall": now_wall(),
                    "ts_dnssd": ts,
                    "service": service,
                    "instance": instance,
                }
            )
            if len(target) > 100:
                target[:] = target[-100:]
    elif action == "Rmv":
        target[:] = [r for r in target if r.get("instance") != instance]


# ---------------------------------------------------------------------------
# Async tailers
# ---------------------------------------------------------------------------


async def tail_file(path: Path, processor) -> None:
    pos = 0
    backfilled = False
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
                backfilled = True
            else:
                if not backfilled:
                    await asyncio.sleep(1.0)
                    continue
        except Exception:
            await asyncio.sleep(2.0)
        await asyncio.sleep(0.4)


# ---------------------------------------------------------------------------
# FastAPI app
# ---------------------------------------------------------------------------

app = FastAPI(title="aliro-esp32 diagweb")


@app.on_event("startup")
async def _startup() -> None:
    asyncio.create_task(tail_file(state.log_paths["serial"], process_serial_line))
    asyncio.create_task(
        tail_file(
            state.log_paths["mdns_ops"],
            lambda l: process_mdns_line(l, state.mdns_operational),
        )
    )
    asyncio.create_task(
        tail_file(
            state.log_paths["mdns_comm"],
            lambda l: process_mdns_line(l, state.mdns_commissioning),
        )
    )


def _summarize_em_rate() -> dict:
    now = time.time()
    last_60 = sum(1 for t in state.em_rx_recent if now - t <= 60)
    last_10 = sum(1 for t in state.em_rx_recent if now - t <= 10)
    return {"em_rx_last_60s": last_60, "em_rx_last_10s": last_10}


def _session_health() -> dict:
    """The whole point of this dashboard. Tells you whether iOS is talking
    to us right now."""
    if state.last_em_rx_monotonic == 0:
        return {"label": "no controller traffic ever", "color": "gray", "age_s": None}
    age = int(time.time() - state.last_em_rx_monotonic)
    if age <= 5:
        return {"label": "active (RX in last 5s)", "color": "green", "age_s": age}
    if age <= 30:
        return {"label": "warm (RX last 30s)", "color": "olive", "age_s": age}
    if age <= 120:
        return {"label": "stale (RX 30–120s ago)", "color": "orange", "age_s": age}
    return {"label": "DEAD (no RX > 2 min)", "color": "red", "age_s": age}


@app.get("/api/snapshot")
async def snapshot() -> JSONResponse:
    out = {
        "now": now_wall(),
        "boot": {
            "qr": state.boot_qr,
            "manual_code": state.boot_manual,
            "feature_map": state.feature_map,
            "cluster_init_seen": state.cluster_init_seen,
        },
        "device": {
            "uptime_s": state.uptime_s,
            "heap_free": state.heap_free,
            "heap_min": state.heap_min,
            "wifi_up": state.wifi_up,
            "wifi_ssid": state.wifi_ssid,
            "wifi_ch": state.wifi_ch,
            "wifi_rssi": state.wifi_rssi,
            "wifi_ip": state.wifi_ip,
            "wifi_gw": state.wifi_gw,
            "fabrics": state.fabrics,
            "fabric_committed_total": state.fabric_committed,
            "fabric_removed_total": state.fabric_removed,
            "commissioning_complete_count": state.commissioning_complete_count,
        },
        "aliro": {
            "provisioned": state.aliro_provisioned,
            "set_aliro_count": state.set_aliro_count,
            "last_log": state.last_aliro_log,
            "age_s_in_hb": state.aliro_age_s,
            "last_hb_event": state.aliro_last,
        },
        "iphone": {
            "node_id_hex": state.iphone_node_id,
            "fabric_idx": state.iphone_fabric_idx,
            "session_health": _session_health(),
            "last_em_rx_wall": state.last_em_rx_wall,
            **_summarize_em_rate(),
            "em_rx_total": state.em_rx_total,
            "em_tx_total": state.em_tx_total,
        },
        "lock_events_recent": state.lock_events[-50:],
        "lock_events_total": len(state.lock_events),
        "errors_recent": state.error_lines[-30:],
        "errors_total": len(state.error_lines),
        "mdns": {
            "operational": state.mdns_operational,
            "commissioning": state.mdns_commissioning,
        },
        "last_hb_a": state.last_hb_a,
        "last_hb_b": state.last_hb_b,
        "serial_total": state.serial_total,
    }
    return JSONResponse(out)


@app.get("/api/serial")
async def serial(limit: int = 300, q: str = "", tag: str = "") -> JSONResponse:
    lines = list(state.serial_lines)
    if q:
        ql = q.lower()
        lines = [l for l in lines if ql in l.lower()]
    if tag:
        tagl = tag.lower()
        lines = [l for l in lines if tagl in l.lower()]
    return JSONResponse({"total_in_buffer": len(state.serial_lines), "lines": lines[-limit:]})


@app.get("/api/serial.txt", response_class=PlainTextResponse)
async def serial_txt(limit: int = 1000, q: str = "") -> str:
    lines = list(state.serial_lines)
    if q:
        ql = q.lower()
        lines = [l for l in lines if ql in l.lower()]
    return "\n".join(lines[-limit:])


@app.get("/", response_class=HTMLResponse)
async def index() -> str:
    return HTML_PAGE


# ---------------------------------------------------------------------------
# HTML / JS — kept as a single string so the tool is one file.
# ---------------------------------------------------------------------------

HTML_PAGE = r"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>aliro-esp32 diagweb</title>
<style>
  :root {
    --bg: #0d1117; --fg: #c9d1d9; --muted: #6e7681;
    --panel: #161b22; --border: #30363d; --accent: #58a6ff;
    --green: #3fb950; --olive: #d29922; --orange: #f78166; --red: #f85149;
    --gray: #6e7681;
  }
  * { box-sizing: border-box; }
  body { background: var(--bg); color: var(--fg); font-family: -apple-system, "SF Mono", Menlo, monospace; margin: 0; padding: 12px; font-size: 13px; }
  h2 { font-size: 13px; margin: 0 0 8px 0; color: var(--accent); text-transform: uppercase; letter-spacing: .04em; font-weight: 600; }
  h3 { font-size: 12px; color: var(--muted); margin: 12px 0 4px 0; font-weight: 500; }
  .grid { display: grid; grid-template-columns: repeat(12, 1fr); gap: 12px; }
  .card { background: var(--panel); border: 1px solid var(--border); border-radius: 6px; padding: 10px 12px; }
  .col-12 { grid-column: span 12; } .col-8 { grid-column: span 8; }
  .col-6 { grid-column: span 6; } .col-4 { grid-column: span 4; }
  .col-3 { grid-column: span 3; }
  .kv { display: grid; grid-template-columns: max-content 1fr; column-gap: 12px; row-gap: 4px; }
  .kv b { color: var(--muted); font-weight: 500; }
  .kv span { user-select: text; word-break: break-all; }
  .pill { display: inline-block; padding: 2px 8px; border-radius: 10px; font-size: 11px; font-weight: 600; }
  .green { background: rgba(63,185,80,.15); color: var(--green); }
  .olive { background: rgba(210,153,34,.15); color: var(--olive); }
  .orange { background: rgba(247,129,102,.15); color: var(--orange); }
  .red { background: rgba(248,81,73,.15); color: var(--red); }
  .gray { background: rgba(110,118,129,.15); color: var(--gray); }
  pre { background: #010409; border: 1px solid var(--border); border-radius: 4px; padding: 8px; margin: 0; overflow: auto; max-height: 60vh; white-space: pre; user-select: text; font-size: 12px; line-height: 1.4; }
  pre.short { max-height: 200px; }
  table { width: 100%; border-collapse: collapse; font-size: 12px; }
  th, td { text-align: left; padding: 4px 8px; border-bottom: 1px solid var(--border); white-space: nowrap; }
  th { color: var(--muted); font-weight: 500; font-size: 11px; text-transform: uppercase; }
  .lock { color: var(--orange); }
  .unlock { color: var(--green); }
  button, input { background: var(--panel); color: var(--fg); border: 1px solid var(--border); border-radius: 4px; padding: 4px 10px; font: inherit; cursor: pointer; }
  button:hover { border-color: var(--accent); }
  input[type="text"] { padding: 4px 8px; min-width: 180px; }
  .toolbar { display: flex; gap: 6px; align-items: center; flex-wrap: wrap; margin-bottom: 8px; }
  .ll-info { color: var(--muted); font-size: 11px; margin-left: auto; }
  .err { color: var(--red); }
  .ok { color: var(--green); }
  .hl-lock { color: #ffb86b; }
  .hl-em { color: #79c0ff; }
  .hl-attr { color: #d2a8ff; }
  .hl-aliro { color: #7ee787; }
  .hl-diag { color: var(--muted); }
  .hl-err { color: var(--red); }
</style>
</head>
<body>

<div class="grid">

<div class="card col-12">
  <div style="display:flex; align-items:center; gap:16px; flex-wrap:wrap;">
    <h2 style="margin:0">aliro-esp32 diagweb</h2>
    <span id="now" class="ll-info"></span>
    <span id="status" class="pill gray">connecting…</span>
    <button onclick="copyAll('snapshot-pre')">📋 Copy snapshot JSON</button>
    <button onclick="copyAll('serial-pre')">📋 Copy serial buffer</button>
  </div>
</div>

<!-- Headline session-health card -->
<div class="card col-6">
  <h2>iPhone session health</h2>
  <div style="display:flex; align-items:center; gap:12px; margin: 6px 0 10px 0;">
    <span id="session-pill" class="pill gray" style="font-size:14px; padding:4px 12px;">—</span>
    <span id="session-age" class="ll-info"></span>
  </div>
  <div class="kv">
    <b>last RX from iPhone</b><span id="last-em-rx">—</span>
    <b>node-id (controller)</b><span id="iphone-node">—</span>
    <b>fabric idx</b><span id="iphone-fabric">—</span>
    <b>RX last 10 s / 60 s</b><span id="em-rate">—</span>
    <b>RX total / TX total</b><span id="em-totals">—</span>
  </div>
</div>

<!-- Device snapshot -->
<div class="card col-6">
  <h2>Device snapshot</h2>
  <div class="kv">
    <b>uptime</b><span id="uptime">—</span>
    <b>Wi-Fi</b><span id="wifi">—</span>
    <b>IP / GW</b><span id="ip">—</span>
    <b>RSSI / ch</b><span id="rssi">—</span>
    <b>fabrics</b><span id="fabrics">—</span>
    <b>aliro</b><span id="aliro">—</span>
    <b>heap free / min</b><span id="heap">—</span>
  </div>
</div>

<!-- Boot info -->
<div class="card col-6">
  <h2>Boot / pairing payload</h2>
  <div class="kv">
    <b>QR string</b><span id="qr">—</span>
    <b>manual code</b><span id="manual">—</span>
    <b>feature map / cmds</b><span id="featuremap">—</span>
    <b>commissioning runs</b><span id="commrun">—</span>
    <b>fabric committed / removed</b><span id="fabchanges">—</span>
  </div>
</div>

<!-- mDNS -->
<div class="card col-6">
  <h2>mDNS services on LAN</h2>
  <h3>_matter._tcp (operational)</h3>
  <pre id="mdns-ops" class="short">—</pre>
  <h3>_matterc._udp (commissioning window open)</h3>
  <pre id="mdns-comm" class="short">—</pre>
</div>

<!-- Lock command history -->
<div class="card col-12">
  <h2>Lock command history (parsed from lock-hdl)</h2>
  <div id="lockhist-empty" class="ll-info">no lock commands seen yet</div>
  <table id="lockhist" style="display:none">
    <thead><tr><th>wall</th><th>cmd</th><th>ep</th><th>fabric</th><th>node-id</th><th>pin len</th><th>Δ from prev</th></tr></thead>
    <tbody></tbody>
  </table>
</div>

<!-- Error lines -->
<div class="card col-12">
  <h2>Errors (E (...) lines)</h2>
  <pre id="errors" class="short">—</pre>
</div>

<!-- Live serial tail -->
<div class="card col-12">
  <h2>Live serial tail</h2>
  <div class="toolbar">
    <span>filter:</span>
    <button onclick="setTag('')">all</button>
    <button onclick="setTag('lock-hdl')">lock-hdl</button>
    <button onclick="setTag('chip[EM]')">chip[EM]</button>
    <button onclick="setTag('chip[IM]')">chip[IM]</button>
    <button onclick="setTag('chip[ZCL]')">chip[ZCL]</button>
    <button onclick="setTag('chip[DMG]')">chip[DMG]</button>
    <button onclick="setTag('aliro')">aliro</button>
    <button onclick="setTag('[attr]')">[attr]</button>
    <button onclick="setTag('[ev]')">[ev]</button>
    <button onclick="setTag('diag')">diag</button>
    <button onclick="setTag('E (')">errors</button>
    <input type="text" id="grep" placeholder="grep…" oninput="renderSoon()">
    <label><input type="checkbox" id="autoscroll" checked> autoscroll</label>
    <span class="ll-info" id="serial-info">—</span>
    <button onclick="downloadSerial()">⬇️ download .txt</button>
  </div>
  <pre id="serial-pre">—</pre>
</div>

<!-- Raw heartbeat / snapshot -->
<div class="card col-12">
  <h2>Latest heartbeats &amp; raw snapshot JSON</h2>
  <h3>HB-A</h3><pre id="hba" class="short">—</pre>
  <h3>HB-B</h3><pre id="hbb" class="short">—</pre>
  <h3>snapshot JSON</h3><pre id="snapshot-pre" class="short">—</pre>
</div>

</div>

<script>
let activeTag = "";
let renderTimer = null;

function setTag(t) { activeTag = t; refreshSerial(); }
function renderSoon() {
  if (renderTimer) clearTimeout(renderTimer);
  renderTimer = setTimeout(refreshSerial, 200);
}

function copyAll(id) {
  const el = document.getElementById(id);
  if (!el) return;
  navigator.clipboard.writeText(el.innerText || el.textContent || "")
    .then(() => { el.style.outline = "2px solid var(--green)"; setTimeout(() => el.style.outline = "", 600); });
}

function downloadSerial() {
  const q = document.getElementById('grep').value || "";
  const url = `/api/serial.txt?limit=10000&q=${encodeURIComponent(q)}`;
  window.location.href = url;
}

async function refreshSnapshot() {
  try {
    const r = await fetch("/api/snapshot");
    const j = await r.json();
    document.getElementById("status").className = "pill green";
    document.getElementById("status").textContent = "connected";
    document.getElementById("now").textContent = `now=${j.now}`;

    // session health
    const h = j.iphone.session_health;
    const pill = document.getElementById("session-pill");
    pill.textContent = h.label;
    pill.className = "pill " + h.color;
    document.getElementById("session-age").textContent =
      h.age_s == null ? "" : `(${h.age_s}s ago)`;
    document.getElementById("last-em-rx").textContent =
      j.iphone.last_em_rx_wall || "—";
    document.getElementById("iphone-node").textContent =
      j.iphone.node_id_hex ? "0x" + j.iphone.node_id_hex : "—";
    document.getElementById("iphone-fabric").textContent =
      j.iphone.fabric_idx || "—";
    document.getElementById("em-rate").textContent =
      `${j.iphone.em_rx_last_10s} / ${j.iphone.em_rx_last_60s}`;
    document.getElementById("em-totals").textContent =
      `${j.iphone.em_rx_total} / ${j.iphone.em_tx_total}`;

    // device
    document.getElementById("uptime").textContent = `${j.device.uptime_s} s`;
    document.getElementById("wifi").textContent =
      (j.device.wifi_up ? "UP " : "DOWN ") + `ssid="${j.device.wifi_ssid}"`;
    document.getElementById("ip").textContent =
      `${j.device.wifi_ip} / ${j.device.wifi_gw}`;
    document.getElementById("rssi").textContent =
      `${j.device.wifi_rssi} dBm / ch ${j.device.wifi_ch}`;
    document.getElementById("fabrics").textContent = j.device.fabrics;
    document.getElementById("aliro").textContent =
      (j.aliro.provisioned ? "provisioned " : "NOT provisioned ")
      + `(set ${j.aliro.set_aliro_count}× — ${j.aliro.last_log || "no events"})`;
    document.getElementById("heap").textContent =
      `${j.device.heap_free} / ${j.device.heap_min} B`;

    // boot
    document.getElementById("qr").textContent = j.boot.qr || "—";
    document.getElementById("manual").textContent = j.boot.manual_code || "—";
    document.getElementById("featuremap").textContent = j.boot.feature_map || "—";
    document.getElementById("commrun").textContent = j.device.commissioning_complete_count;
    document.getElementById("fabchanges").textContent =
      `${j.device.fabric_committed_total} committed / ${j.device.fabric_removed_total} removed`;

    // mdns
    function rendMdns(arr) {
      if (!arr || !arr.length) return "(none)";
      return arr.map(r => `${r.wall}  ${r.service}  ${r.instance}`).join("\n");
    }
    document.getElementById("mdns-ops").textContent = rendMdns(j.mdns.operational);
    document.getElementById("mdns-comm").textContent = rendMdns(j.mdns.commissioning);

    // lock history
    const tb = document.querySelector("#lockhist tbody");
    tb.innerHTML = "";
    const events = j.lock_events_recent;
    if (!events.length) {
      document.getElementById("lockhist-empty").style.display = "";
      document.getElementById("lockhist").style.display = "none";
    } else {
      document.getElementById("lockhist-empty").style.display = "none";
      document.getElementById("lockhist").style.display = "";
      let prevTs = null;
      events.forEach(ev => {
        const tr = document.createElement("tr");
        const dt = prevTs ? `${(ev.ts - prevTs).toFixed(1)}s` : "—";
        prevTs = ev.ts;
        const cls = ev.cmd === "LockDoor" ? "lock" : "unlock";
        tr.innerHTML = `<td>${ev.wall}</td><td class="${cls}">${ev.cmd}</td><td>${ev.ep}</td><td>${ev.fabric}</td><td>0x${ev.node_id}</td><td>${ev.pin_len}</td><td>${dt}</td>`;
        tb.appendChild(tr);
      });
    }

    // errors
    document.getElementById("errors").textContent =
      (j.errors_recent || []).map(e => `${e.wall}  ${e.line}`).join("\n") || "(none)";

    // raw HBs + snapshot
    document.getElementById("hba").textContent = j.last_hb_a || "—";
    document.getElementById("hbb").textContent = j.last_hb_b || "—";
    document.getElementById("snapshot-pre").textContent = JSON.stringify(j, null, 2);
  } catch (e) {
    document.getElementById("status").className = "pill red";
    document.getElementById("status").textContent = "disconnected: " + e;
  }
}

function colorize(line) {
  // Simple span coloring — kept text-selectable.
  if (line.indexOf("lock-hdl") >= 0) return `<span class="hl-lock">${escapeHtml(line)}</span>`;
  if (line.match(/^E \(/)) return `<span class="hl-err">${escapeHtml(line)}</span>`;
  if (line.indexOf("chip[EM]") >= 0) return `<span class="hl-em">${escapeHtml(line)}</span>`;
  if (line.indexOf("[attr]") >= 0) return `<span class="hl-attr">${escapeHtml(line)}</span>`;
  if (line.indexOf("aliro:") >= 0) return `<span class="hl-aliro">${escapeHtml(line)}</span>`;
  if (line.indexOf("diag]") >= 0) return `<span class="hl-diag">${escapeHtml(line)}</span>`;
  return escapeHtml(line);
}
function escapeHtml(s) { return s.replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c])); }

async function refreshSerial() {
  try {
    const grep = document.getElementById('grep').value || "";
    const tag = activeTag;
    const r = await fetch(`/api/serial?limit=600&q=${encodeURIComponent(grep)}&tag=${encodeURIComponent(tag)}`);
    const j = await r.json();
    const pre = document.getElementById("serial-pre");
    pre.innerHTML = j.lines.map(colorize).join("\n");
    document.getElementById("serial-info").textContent =
      `tag="${tag}"  grep="${grep}"  showing ${j.lines.length}, buffer ${j.total_in_buffer}`;
    if (document.getElementById("autoscroll").checked) pre.scrollTop = pre.scrollHeight;
  } catch (e) {}
}

setInterval(refreshSnapshot, 1000);
setInterval(refreshSerial, 1500);
refreshSnapshot();
refreshSerial();
</script>

</body>
</html>
"""


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--serial", default=str(DEFAULT_SERIAL))
    ap.add_argument("--mdns-ops", default=str(DEFAULT_MDNS_OPS))
    ap.add_argument("--mdns-comm", default=str(DEFAULT_MDNS_COMM))
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--host", default="127.0.0.1")
    args = ap.parse_args()

    state.log_paths = {
        "serial": Path(args.serial),
        "mdns_ops": Path(args.mdns_ops),
        "mdns_comm": Path(args.mdns_comm),
    }

    print(f"diagweb tailing:")
    for k, p in state.log_paths.items():
        ok = "ok" if p.exists() else "MISSING (will retry)"
        print(f"  {k:10s}  {p}  [{ok}]")
    print(f"http://{args.host}:{args.port}")

    import uvicorn
    uvicorn.run(app, host=args.host, port=args.port, log_level="warning")


if __name__ == "__main__":
    main()
