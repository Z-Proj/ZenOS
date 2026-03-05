#!/bin/bash
set -Eeuo pipefail

FILES_DIR="userland/files"
VHD_PATH="ZenOS.vhd"
FATMAN="./fat_man"

[ -x "$FATMAN" ]   || { echo "[!] fat_man not found"; exit 1; }
[ -f "$VHD_PATH" ] || { echo "[!] VHD not found"; exit 1; }

$FATMAN "$VHD_PATH" mkdir /bin || true

FAILED=0

for app_dir in "$FILES_DIR"/*/; do
    app=$(basename "$app_dir")
    mf="$app_dir/makefile"

    [ -f "$mf" ] || { echo "[!] No makefile in $app_dir, skipping"; continue; }

    echo "[*] Building $app"
    if ! make -C "$app_dir" install -j1 --no-print-directory; then
        echo "[!] Failed: $app"
        FAILED=1
    else
        echo "[✓] $app installed"
    fi
done

[ "$FAILED" -eq 0 ] && echo "[✓] All apps built and imported" || { echo "[!] Some apps failed"; exit 1; }
