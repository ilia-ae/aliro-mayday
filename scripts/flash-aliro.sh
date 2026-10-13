#!/usr/bin/env bash
# Flash the aliro-esp32 firmware onto the shared ESP32-S3 rig.
# Needs ESP-IDF + esp-matter sourced in the current shell — the
# script does not bring them in, because esp-matter lives out of tree
# and its version picks depend on what you've installed.
#
# Typical session:
#   . $IDF_PATH/export.sh
#   . $ESP_MATTER_PATH/export.sh
#   ./scripts/flash-aliro.sh

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FW_DIR="$REPO_ROOT/aliro-esp32"

if [[ -z "${IDF_PATH:-}" ]]; then
    echo "IDF_PATH is not set. Source \$IDF_PATH/export.sh first." >&2
    exit 1
fi
if [[ -z "${ESP_MATTER_PATH:-}" ]]; then
    echo "ESP_MATTER_PATH is not set. Source \$ESP_MATTER_PATH/export.sh first." >&2
    echo "See aliro-esp32/README.md for esp-matter installation." >&2
    exit 1
fi

PORT="${ESP_PORT:-$(ls /dev/cu.usbmodem* 2>/dev/null | head -n1 || true)}"
if [[ -z "$PORT" ]]; then
    echo "No /dev/cu.usbmodem* device found. Plug in ESP32-S3 via left USB-C." >&2
    echo "Or set ESP_PORT=/dev/cu.usbserial-... manually." >&2
    exit 1
fi

echo "==> Flashing aliro-esp32 firmware to $PORT"
echo "    (on first boot look for a Matter QR URL + manual pairing code)"

cd "$FW_DIR"
exec idf.py -p "$PORT" flash monitor
