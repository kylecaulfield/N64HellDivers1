#!/usr/bin/env bash
# Upload the ROM to an EverDrive 64 X7 over USB, boot it, and stream the
# debug log (debugf output) to the terminal and to logs/usb-<timestamp>.log.
#
# Usage: tools/deploy.sh [rom.z64]
#
# Before running: console ON, sitting in the EverDrive menu, USB cable plugged
# into the cart. Needs UNFLoader (https://github.com/buu342/N64-UNFLoader);
# put it on PATH or point UNFLOADER at the binary.
#
# Flags used (from UNFLoader -help):
#   -f 3      force cart type "EverDrive 3.0 or X7" (skips autodetection)
#   -r FILE   upload ROM
#   -d FILE   debug mode: keep the connection open and log debugf output to FILE
#   -b        plain terminal output instead of the ncurses UI
# Quit with Ctrl+C.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROM="${1:-$REPO/hivebreakers.z64}"
UNFLOADER="${UNFLOADER:-UNFLoader}"

if [ ! -f "$ROM" ]; then
    echo "ROM not found: $ROM" >&2
    exit 1
fi
if ! command -v "$UNFLOADER" >/dev/null 2>&1; then
    echo "UNFLoader not found. Install it or run: UNFLOADER=/path/to/UNFLoader $0" >&2
    exit 1
fi

mkdir -p "$REPO/logs"
LOG="$REPO/logs/usb-$(date +%Y%m%d-%H%M%S).log"
echo "Uploading $ROM to EverDrive X7; USB log -> $LOG"
exec "$UNFLOADER" -f 3 -r "$ROM" -d "$LOG" -b
