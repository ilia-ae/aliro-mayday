"""
webui.py — small FastAPI dashboard for the PM3 DIY lock.

Runs alongside `lock_mvp.py`. They communicate via JSON files in cwd:

    state.json      (written by lock, read by webui)
    control.json    (written by webui, consumed by lock)
    allowlist.json  (edited by either, re-read by lock on demand)
    events.jsonl    (append-only, tailed by webui)

No shared memory, no threading — just files. Perfect for an MVP
on localhost. Serve with `uvicorn` (or `just webui`).
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from fastapi import FastAPI, HTTPException
from fastapi.responses import HTMLResponse, JSONResponse
from pydantic import BaseModel

# ------------- config -------------


class Paths:
    state: Path = Path("state.json")
    control: Path = Path("control.json")
    allowlist: Path = Path("allowlist.json")
    events: Path = Path("events.jsonl")


# ------------- helpers -------------


def read_json(p: Path, default: Any) -> Any:
    if not p.exists():
        return default
    try:
        return json.loads(p.read_text())
    except json.JSONDecodeError:
        return default


def tail_jsonl(p: Path, n: int = 50) -> list[dict]:
    if not p.exists():
        return []
    # Simple tail for MVP; fine for <1MB files.
    lines = p.read_text().splitlines()[-n:]
    out = []
    for line in lines:
        try:
            out.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    return out


def write_control(action: str, **extra: Any) -> None:
    payload = {"action": action, **extra}
    Paths.control.write_text(json.dumps(payload))


# ------------- schemas -------------


class RenamePayload(BaseModel):
    uid: str
    name: str


class RevokePayload(BaseModel):
    uid: str


# ------------- app -------------

app = FastAPI(title="PM3 DIY Lock", version="0.1.0")


@app.get("/api/status")
def status() -> JSONResponse:
    st = read_json(
        Paths.state,
        {
            "state": "UNKNOWN",
            "last_uid": None,
            "last_change": None,
            "allowlist_size": 0,
            "enrol_armed": False,
            "updated_at": None,
        },
    )
    return JSONResponse(st)


@app.get("/api/allowlist")
def allowlist() -> JSONResponse:
    data = read_json(Paths.allowlist, {})
    items = [{"uid": uid, **meta} for uid, meta in sorted(data.items())]
    return JSONResponse(items)


@app.get("/api/events")
def events(limit: int = 50) -> JSONResponse:
    return JSONResponse(list(reversed(tail_jsonl(Paths.events, n=limit))))


@app.post("/api/enrol/arm")
def enrol_arm() -> JSONResponse:
    write_control("arm_enrol")
    return JSONResponse({"ok": True})


@app.post("/api/enrol/disarm")
def enrol_disarm() -> JSONResponse:
    write_control("disarm_enrol")
    return JSONResponse({"ok": True})


@app.post("/api/toggle")
def toggle() -> JSONResponse:
    write_control("toggle", who="webui")
    return JSONResponse({"ok": True})


@app.post("/api/rename")
def rename(p: RenamePayload) -> JSONResponse:
    al = read_json(Paths.allowlist, {})
    key = p.uid.upper()
    if key not in al:
        raise HTTPException(status_code=404, detail="UID not found")
    al[key]["name"] = p.name
    Paths.allowlist.write_text(json.dumps(al, indent=2, sort_keys=True))
    write_control("reload_allowlist")
    return JSONResponse({"ok": True})


@app.post("/api/revoke")
def revoke(p: RevokePayload) -> JSONResponse:
    al = read_json(Paths.allowlist, {})
    key = p.uid.upper()
    if key not in al:
        raise HTTPException(status_code=404, detail="UID not found")
    del al[key]
    Paths.allowlist.write_text(json.dumps(al, indent=2, sort_keys=True))
    write_control("reload_allowlist")
    return JSONResponse({"ok": True})


# ------------- HTML dashboard -------------

INDEX_HTML = """
<!DOCTYPE html>
<html lang="en"><head>
<meta charset="utf-8">
<title>PM3 DIY Lock</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
  :root { color-scheme: light dark; }
  body { font: 14px/1.4 -apple-system,Segoe UI,Roboto,sans-serif; margin: 0; padding: 1rem;
         max-width: 900px; margin-inline: auto; }
  h1 { margin: 0 0 .5rem; }
  .card { background: color-mix(in srgb, Canvas 92%, CanvasText); border-radius: 8px;
          padding: 1rem; margin-bottom: 1rem; border: 1px solid #8884; }
  .state-OPEN   { color: #0a0; font-weight: bold; }
  .state-CLOSED { color: #a00; font-weight: bold; }
  .state-UNKNOWN{ color: #888; font-style: italic; }
  .enrol-ARMED  { color: #a60; font-weight: bold; }
  .enrol-idle   { color: #888; }
  button { padding: .5rem 1rem; margin-right: .5rem; border-radius: 4px; border: 1px solid #8886;
           background: color-mix(in srgb, Canvas 85%, CanvasText); cursor: pointer; font-size: 1em; }
  button:hover { background: color-mix(in srgb, Canvas 75%, CanvasText); }
  button.danger { color: #c00; border-color: #c006; }
  table { width: 100%; border-collapse: collapse; }
  th, td { padding: .35rem .5rem; text-align: left; border-bottom: 1px solid #8883; }
  code { font-family: ui-monospace, Menlo, monospace; font-size: .9em; }
  input[type=text] { padding: .3rem; border: 1px solid #8886; border-radius: 4px; width: 10rem; font-size: 1em; }
  .row { display: flex; gap: 1rem; flex-wrap: wrap; align-items: center; }
  .kv { display: grid; grid-template-columns: max-content 1fr; gap: .25rem 1rem; }
  .muted { color: #888; font-size: .9em; }
  .evt-AUTH_OK { color: #0a0; }
  .evt-DENY    { color: #c00; }
  .evt-ENROLLED{ color: #06c; }
  .evt-MANUAL_TOGGLE { color: #a60; }
  .evt-start, .evt-stop { color: #888; }
</style>
</head><body>

<h1>PM3 DIY Lock</h1>

<div class="card">
  <div class="row">
    <div class="kv">
      <span>State:</span>            <span id="state" class="state-UNKNOWN">—</span>
      <span>Last UID:</span>          <code id="last_uid">—</code>
      <span>Last change:</span>       <span id="last_change" class="muted">—</span>
      <span>Allowlist:</span>         <span id="allowlist_size">—</span>
      <span>Enrol mode:</span>        <span id="enrol_armed" class="enrol-idle">off</span>
      <span>Updated:</span>           <span id="updated_at" class="muted">—</span>
    </div>
  </div>
  <hr>
  <div class="row">
    <button onclick="call('/api/toggle')">Toggle state</button>
    <button onclick="call('/api/enrol/arm')">Arm enrol (next tap)</button>
    <button onclick="call('/api/enrol/disarm')">Disarm enrol</button>
  </div>
</div>

<div class="card">
  <h2>Allowlist</h2>
  <table id="al">
    <thead><tr><th>UID</th><th>Proto</th><th>Name</th><th>Added</th><th></th></tr></thead>
    <tbody></tbody>
  </table>
</div>

<div class="card">
  <h2>Recent events</h2>
  <table id="evt">
    <thead><tr><th>When</th><th>Type</th><th>Proto</th><th>UID</th><th>Name</th><th>State</th></tr></thead>
    <tbody></tbody>
  </table>
</div>

<script>
async function call(path, opts) {
  const r = await fetch(path, {method: 'POST', ...opts});
  const j = await r.json();
  if (!r.ok) alert('Error: ' + JSON.stringify(j));
  await refresh();
}

async function rename(uid) {
  const nm = prompt('New name for ' + uid + ':');
  if (!nm) return;
  await call('/api/rename', {
    headers: {'Content-Type':'application/json'},
    body: JSON.stringify({uid, name: nm}),
  });
}

async function revoke(uid) {
  if (!confirm('Revoke ' + uid + '?')) return;
  await call('/api/revoke', {
    headers: {'Content-Type':'application/json'},
    body: JSON.stringify({uid}),
  });
}

async function refresh() {
  const [s, al, ev] = await Promise.all([
    fetch('/api/status').then(r=>r.json()),
    fetch('/api/allowlist').then(r=>r.json()),
    fetch('/api/events?limit=30').then(r=>r.json()),
  ]);

  const se = document.getElementById('state');
  se.textContent = s.state;
  se.className = 'state-' + s.state;

  document.getElementById('last_uid').textContent = s.last_uid || '—';
  document.getElementById('last_change').textContent = s.last_change || '—';
  document.getElementById('allowlist_size').textContent = s.allowlist_size;
  document.getElementById('updated_at').textContent = s.updated_at || '—';

  const ea = document.getElementById('enrol_armed');
  ea.textContent = s.enrol_armed ? 'ARMED' : 'off';
  ea.className = s.enrol_armed ? 'enrol-ARMED' : 'enrol-idle';

  const altb = document.querySelector('#al tbody');
  altb.innerHTML = '';
  for (const e of al) {
    const tr = document.createElement('tr');
    tr.innerHTML = `<td><code>${e.uid}</code></td>
                    <td>${e.proto || '—'}</td>
                    <td>${e.name || '—'}</td>
                    <td class="muted">${e.added_at || '—'}</td>
                    <td><button onclick="rename('${e.uid}')">rename</button>
                        <button class="danger" onclick="revoke('${e.uid}')">revoke</button></td>`;
    altb.appendChild(tr);
  }

  const evtb = document.querySelector('#evt tbody');
  evtb.innerHTML = '';
  for (const e of ev) {
    const tr = document.createElement('tr');
    tr.innerHTML = `<td class="muted">${(e.ts||'').slice(11,19)}</td>
                    <td class="evt-${e.type}">${e.type}</td>
                    <td>${e.proto||'—'}</td>
                    <td><code>${e.uid||'—'}</code></td>
                    <td>${e.name||'—'}</td>
                    <td>${e.state||e.result||'—'}</td>`;
    evtb.appendChild(tr);
  }
}

refresh();
setInterval(refresh, 1500);
</script>

</body></html>
"""


@app.get("/", response_class=HTMLResponse)
def index() -> HTMLResponse:
    return HTMLResponse(INDEX_HTML)


# ------------- CLI -------------


def main() -> int:
    import uvicorn

    ap = argparse.ArgumentParser(description="PM3 DIY Lock — Web UI")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8787)
    ap.add_argument("--dir", type=Path, help="Working directory with state/allowlist/events files")
    args = ap.parse_args()

    if args.dir:
        import os

        os.chdir(args.dir)

    print(f"Open http://{args.host}:{args.port} in your browser.")
    uvicorn.run(app, host=args.host, port=args.port, log_level="warning")
    return 0


if __name__ == "__main__":
    import sys

    sys.exit(main())
