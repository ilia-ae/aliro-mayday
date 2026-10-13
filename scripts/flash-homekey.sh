#!/usr/bin/env bash
# Flash the frozen homekey-esp32 firmware onto the shared ESP32-S3 rig.
# Use this before each Aliro dev session to confirm the hardware is
# healthy (iPhone tap must unlock). See TODO.md §7.4.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FW_DIR="$REPO_ROOT/homekey-esp32"

# Autodetect port (ESP32-S3 native USB enumerates as /dev/cu.usbmodem* on macOS)
PORT="${ESP_PORT:-$(ls /dev/cu.usbmodem* 2>/dev/null | head -n1 || true)}"
if [[ -z "$PORT" ]]; then
    echo "No /dev/cu.usbmodem* device found. Plug in ESP32-S3 via left USB-C." >&2
    echo "Or set ESP_PORT=/dev/cu.usbserial-... manually." >&2
    exit 1
fi

echo "==> Flashing homekey-esp32 firmware to $PORT"
echo "    (if iPhone tap works after this, the rig is healthy)"

cd "$FW_DIR"

# homekey-esp32 uses a nix flake for its toolchain.
if command -v nix >/dev/null 2>&1 && [[ -f "flake.nix" ]]; then
    exec nix develop --command bash -c "idf.py -p $PORT flash monitor"
fi

# Fallback: assume ESP-IDF is already exported in current shell.
if [[ -z "${IDF_PATH:-}" ]]; then
    echo "No nix and no IDF_PATH — source \$IDF_PATH/export.sh first." >&2
    exit 1
fi
exec idf.py -p "$PORT" flash monitor
