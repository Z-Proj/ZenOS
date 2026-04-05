#!/bin/bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SYSROOT="$ROOT/userland/sysroot"
VHD="$ROOT/ZenOS.vhd"
FATMAN="$ROOT/fat_man"
SHARED_BUILD="$ROOT/mlibc-build-zenos-shared"
STATIC_BUILD="$ROOT/mlibc-build-zenos-static"

for dir in "$SHARED_BUILD" "$STATIC_BUILD"; do
    meson configure "$dir" -Dprefix=/usr >/dev/null
done

rm -rf "$SYSROOT"
mkdir -p "$SYSROOT"

echo "Building shared mlibc library..."
meson install -C "$SHARED_BUILD" --no-rebuild --destdir "$SYSROOT"
echo "Building static mlibc library..."
meson install -C "$STATIC_BUILD" --no-rebuild --destdir "$SYSROOT"

if [ -d "$SYSROOT/usr/local" ]; then
    mkdir -p "$SYSROOT/usr"
    cp -a "$SYSROOT/usr/local/." "$SYSROOT/usr/"
    rm -rf "$SYSROOT/usr/local"
fi

mkdir -p "$SYSROOT/lib"
cp -f "$SYSROOT/usr/lib/ld.so" "$SYSROOT/lib/ld.so"

mkdir_in_vhd() {
    "$FATMAN" "$VHD" mkdir "$1" >/dev/null 2>&1 || true
}

import_tree() {
    local src_root="$1"
    local dst_root="$2"
    local path rel

    mkdir_in_vhd "$dst_root"
    while IFS= read -r path; do
        rel="${path#"$src_root"/}"
        mkdir_in_vhd "$dst_root/$rel"
    done < <(find "$src_root" -type d | sort)

    while IFS= read -r path; do
        rel="${path#"$src_root"/}"
        "$FATMAN" "$VHD" import "$path" "$dst_root/$rel" >/dev/null
    done < <(find "$src_root" -type f | sort)
}

mkdir_in_vhd /usr
mkdir_in_vhd /usr/include
mkdir_in_vhd /usr/lib
mkdir_in_vhd /lib

import_tree "$SYSROOT/usr/include" /usr/include
import_tree "$SYSROOT/usr/lib" /usr/lib
"$FATMAN" "$VHD" import "$SYSROOT/lib/ld.so" /lib/ld.so >/dev/null
