# pm3-diy-lock — dev commands
# Run `just` with no args to see the list.

# Default target: show the list of commands
default:
    @just --list

# ------------------- Setup -------------------

# Install all deps incl. dev tools into .venv
install:
    uv sync --all-extras

# Run BOTH lock and webui (split in two Terminal windows on macOS)
up:
    #!/usr/bin/env bash
    osascript -e 'tell application "Terminal" to do script "cd {{justfile_directory()}} && just lock"'
    osascript -e 'tell application "Terminal" to do script "cd {{justfile_directory()}} && just webui"'
    echo "Opened two Terminal windows: lock + webui. Browser: http://127.0.0.1:8787"

# Start only the Web UI on http://127.0.0.1:8787
webui:
    uv run python webui.py

# Upgrade lockfile to latest compatible versions
upgrade:
    uv lock --upgrade
    uv sync --all-extras

# Wipe venv and reinstall
clean-install:
    rm -rf .venv uv.lock
    just install

# ------------------- Run -------------------

# Start the lock (normal monitoring mode)
lock *FLAGS:
    uv run python lock_mvp.py {{FLAGS}}

# Enroll next card presented
enroll:
    uv run python lock_mvp.py --enroll

# Print current allowlist
list:
    uv run python lock_mvp.py --list

# Drive PM3 into Apple HomeKey ECP Access polling
homekey:
    uv run python homekey_reader.py

# ------------------- aliro-esp32 diagnostics -------------------

# Web dashboard for the active aliro-esp32 debug session (port 8788)
diag:
    uv run python diagweb.py

# Telegram bot for the active aliro-esp32 debug session (creds in .env)
diag-bot:
    uv run python diagbot.py

# Live one-screen-per-tap parser for /tmp/aliro-debug/serial.log
# Shows each Aliro NFC tap as: detect → AID select → FCI parse → AUTH0 →
# AUTH1 → GCM decrypt → endpoint match → ECDSA verify → result. Use this
# instead of grepping serial.log by hand when iterating on DDK auth.
diag-tap:
    python3 scripts/aliro-tap-diag.py

# ------------------- Quality -------------------

# Lint (check only)
lint:
    uv run ruff check .

# Auto-fix + format
fix:
    uv run ruff check --fix .
    uv run ruff format .

# Static type check
typecheck:
    uv run mypy lock_mvp.py homekey_reader.py

# Run tests
test:
    uv run pytest

# Full CI-style check: lint + typecheck + test
check: lint typecheck test

# ------------------- HomeKey-ESP32 subtree -------------------

# Check if rednblkx/HomeKey-ESP32 has new upstream commits
upstream-check:
    #!/usr/bin/env bash
    set -eu
    git fetch homekey-upstream --quiet
    last_synced=$(git log --grep='git-subtree-dir: homekey-esp32' -n1 --format=%b \
                  | awk '/^git-subtree-split:/ {print $2; exit}')
    upstream_head=$(git rev-parse homekey-upstream/main)
    if [ "$last_synced" = "$upstream_head" ]; then
        echo "up to date (at $(git rev-parse --short $upstream_head))"
    else
        behind=$(git rev-list --count ${last_synced}..${upstream_head})
        echo "behind by $behind commit(s):"
        echo "  last synced: $(git rev-parse --short $last_synced)"
        echo "  upstream:    $(git rev-parse --short $upstream_head)"
        echo ""
        git log --oneline ${last_synced}..${upstream_head}
    fi

# Pull upstream changes into homekey-esp32/ (squashed merge)
upstream-pull:
    git fetch homekey-upstream
    git subtree pull --prefix=homekey-esp32 homekey-upstream main --squash

# Push local homekey-esp32/ changes back to razqqm/HomeKey-ESP32
upstream-push:
    git subtree push --prefix=homekey-esp32 homekey-fork main

# ------------------- aliro-esp32 web UI -------------------
#
# aliro-esp32/data/ is a Svelte app vendored from rednblkx. The build output
# under aliro-esp32/data/dist/ is flashed to the device's `spiffs` LittleFS
# partition by `idf.py flash` (driven by littlefs_create_partition_image
# in components/admin_panel/CMakeLists.txt).

# Build Svelte UI (data/dist/) — run after editing aliro-esp32/data/src/
webui-build:
    cd aliro-esp32/data && bun install && bun run build

# Hot-reload dev server for the Svelte UI on http://localhost:5173 —
# proxies /api and /ws to the device's IP. Set ALIRO_HOST to override.
webui-dev:
    cd aliro-esp32/data && bun install && bun run dev

# ------------------- aliro-site subtree -------------------
#
# aliro-site/ is a subtree of razqqm/aliro-mayday-software (private).
# The private repo is what Cloudflare Workers Build clones — keeping it
# flat and free of homekey-esp32 submodule noise. All day-to-day edits
# happen here in the monorepo; `just site-push` mirrors them out.

# Status between local aliro-site/ and the private mirror repo
site-check:
    #!/usr/bin/env bash
    set -eu
    git fetch aliro-site-remote --quiet
    last_synced=$(git log --grep='git-subtree-dir: aliro-site' -n1 --format=%b \
                  | awk '/^git-subtree-split:/ {print $2; exit}')
    remote_head=$(git rev-parse aliro-site-remote/main)
    if [ "$last_synced" = "$remote_head" ]; then
        echo "up to date (at $(git rev-parse --short $remote_head))"
    else
        echo "out of sync"
        echo "  last synced: $(git rev-parse --short $last_synced)"
        echo "  remote head: $(git rev-parse --short $remote_head)"
    fi

# Pull changes from the private mirror into aliro-site/ (if someone
# edited the private repo directly, e.g. via GitHub UI — rare)
site-pull:
    git fetch aliro-site-remote
    git subtree pull --prefix=aliro-site aliro-site-remote main --squash

# Push local aliro-site/ changes to the private mirror → triggers CF build
site-push:
    git subtree push --prefix=aliro-site aliro-site-remote main

# ------------------- aliro-esp32 OTA / telemetry over WiFi -------------------
#
# Once the lock is on the network, USB cable is no longer required: build
# locally, push the firmware over WiFi to admin_panel's /ota/firmware and
# tail logs via the WebSocket /ws sink. Default ALIRO_HOST is the lock's
# current LAN IP — override per-shot: `just ota 192.0.2.10`.

ALIRO_HOST := env_var_or_default('ALIRO_HOST', '192.0.2.10')

# Build aliro-esp32 firmware (idf.py build). Run from anywhere.
# Sources both ESP-IDF and esp-matter; sets _PW_ACTUAL_ENVIRONMENT_ROOT
# to connectedhomeip's vendored .environment because the chip GN build
# imports build_overrides/pigweed_environment.gni from there.
aliro-build:
    #!/usr/bin/env bash
    set -eu
    cd aliro-esp32
    export _PW_ACTUAL_ENVIRONMENT_ROOT=$HOME/CODE/esp-matter/connectedhomeip/connectedhomeip/.environment
    source $HOME/esp/esp-idf/export.sh > /dev/null
    source $HOME/CODE/esp-matter/export.sh > /dev/null
    idf.py build

# Flash the latest firmware to the lock over WiFi. Pass HOST=ip for a
# non-default address. Skips webAuth header — turn off webAuthEnabled in
# admin panel (default) or set ALIRO_USER/ALIRO_PASS to add basic auth.
ota host=ALIRO_HOST:
    #!/usr/bin/env bash
    set -eu
    BIN=aliro-esp32/build/aliro_esp32.bin
    [ -f "$BIN" ] || { echo "no $BIN — run \`just aliro-build\` first"; exit 1; }
    BYTES=$(stat -f%z "$BIN")
    echo "POST $BIN ($BYTES bytes) → http://{{host}}/ota/firmware"
    AUTH=""
    if [ -n "${ALIRO_USER:-}" ] && [ -n "${ALIRO_PASS:-}" ]; then
        AUTH="-u $ALIRO_USER:$ALIRO_PASS"
    fi
    curl --fail --max-time 120 $AUTH \
        -H "Content-Type: application/octet-stream" \
        --data-binary @"$BIN" \
        "http://{{host}}/ota/firmware"
    echo
    echo "OTA done — device will reboot. Wait ~15s, then \`just ota-logs\`."

# Tail ESP_LOG output from the lock over HTTP. Polls /api/logs?since=N
# every 300ms, prints new lines as they appear, surfaces drops if the
# poller falls behind the device's ring buffer. Pure curl — no Python
# / npm deps. Ctrl-C to exit.
ota-logs host=ALIRO_HOST:
    #!/usr/bin/env bash
    set -u
    SINCE=0
    AUTH=""
    if [ -n "${ALIRO_USER:-}" ] && [ -n "${ALIRO_PASS:-}" ]; then
        AUTH="-u $ALIRO_USER:$ALIRO_PASS"
    fi
    echo "tailing http://{{host}}/api/logs (Ctrl-C to exit)"
    while :; do
        # -D - dumps headers to stdout, separated from body by a blank
        # line. We pull X-Log-Next (cursor) and X-Log-Dropped (overflow
        # count) out of the headers without depending on jq.
        RESP=$(curl -sS --max-time 3 $AUTH -D - "http://{{host}}/api/logs?since=$SINCE&max=32768" || true)
        if [ -z "$RESP" ]; then sleep 1; continue; fi
        NEXT=$(echo "$RESP" | awk -F': ' 'tolower($1)=="x-log-next"{gsub(/\r/,"",$2); print $2}')
        DROP=$(echo "$RESP" | awk -F': ' 'tolower($1)=="x-log-dropped"{gsub(/\r/,"",$2); print $2}')
        BODY=$(echo "$RESP" | awk 'BEGIN{p=0} /^\r?$/{if(!p){p=1; next}} p{print}')
        if [ -n "${DROP:-}" ] && [ "$DROP" != "0" ]; then
            echo "[ota-logs] dropped $DROP lines (poller lagged behind ring buffer)" >&2
        fi
        if [ -n "$BODY" ]; then printf "%s" "$BODY"; fi
        if [ -n "${NEXT:-}" ]; then SINCE=$NEXT; fi
        sleep 0.3
    done

# ------------------- Diagnostics -------------------

# Print PM3 detected port + binary
doctor:
    @echo "== proxmark3 binary =="
    @which proxmark3 || echo "NOT FOUND — brew install --HEAD --with-generic proxmark3"
    @echo ""
    @echo "== USB ports (should see cu.usbmodemiceman*) =="
    @ls /dev/cu.usbmodem* 2>/dev/null || echo "no usbmodem ports"
    @echo ""
    @echo "== python version =="
    @uv run python --version
