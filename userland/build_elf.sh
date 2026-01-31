#!/bin/bash
set -Eeuo pipefail

VHD_PATH="ZenOS.vhd"
ZFS_MAN="./zfs_man"
FILES_DIR="userland/files"
OUT_DIR="userland/build"

die() {
    echo "[!] $*" >&2
    exit 1
}

[ -x "$ZFS_MAN" ] || die "zfs_man not found or not executable"
[ -f "$VHD_PATH" ] || die "VHD '$VHD_PATH' not found"

if [ "$#" -eq 0 ]; then
    die "No source files passed to build_elf.sh"
fi

if [ -d "$FILES_DIR" ] && ! ls "$FILES_DIR"/*.c >/dev/null 2>&1; then
    die "No .c files found in ${FILES_DIR}"
fi

mkdir -p "$OUT_DIR"

BUILD_OK=0

cleanup() {
    if [ "$BUILD_OK" -eq 1 ]; then
        echo "[*] Cleaning build directory: ${OUT_DIR}"
        rm -rf "${OUT_DIR}"
    else
        echo "[!] Build failed — preserving ${OUT_DIR} for debugging"
    fi
}

trap cleanup EXIT

for SRC in "$@"; do
    [ -f "$SRC" ] || die "Source file '$SRC' does not exist"

    case "$SRC" in
        *.c) ;;
        *) die "Unsupported file type: $SRC (expected .c)" ;;
    esac

    BASENAME="$(basename "$SRC" .c)"
    OBJ="${OUT_DIR}/${BASENAME}.o"
    ELF="${OUT_DIR}/${BASENAME}.elf"

    echo "[*] Removing old /${BASENAME} from ${VHD_PATH} (if any)"
    "$ZFS_MAN" "$VHD_PATH" delete "/${BASENAME}" || true

    echo "[*] Compiling ${SRC} -> ${OBJ}"
    clang -m64 -ffreestanding -fno-stack-protector \
          -nostdlib -fno-pie -fPIC \
          -c "$SRC" -o "$OBJ" \
          || die "Compilation failed for ${SRC}"

    echo "[*] Linking ${OBJ} -> ${ELF}"
    ld.lld -m elf_x86_64 \
           -e main \
           -T userland/userelf.ld \
           "$OBJ" -o "$ELF" \
           --warn-unresolved-symbols \
           --noinhibit-exec \
           || die "Linking failed for ${BASENAME}"

    [ -f "$ELF" ] || die "ELF not produced for ${BASENAME}"

    echo "[*] Importing ${ELF} as /${BASENAME}"
    "$ZFS_MAN" "$VHD_PATH" import "$ELF" "/${BASENAME}" \
        || die "Import failed for ${BASENAME}"

    echo "[✓] ${BASENAME} installed"
done

echo "[✓] All userland ELFs built and imported"
BUILD_OK=1
