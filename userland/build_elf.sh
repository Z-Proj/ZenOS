#!/bin/bash
set -Eeuo pipefail

VHD_PATH="ZenOS.vhd"
fat_man="./fat_man"
FILES_DIR="userland/files"
OUT_DIR="userland/build"

die() {
    echo "[!] $*" >&2
    exit 1
}

[ -x "$fat_man" ] || die "fat_man not found or not executable"
[ -f "$VHD_PATH" ] || die "VHD '$VHD_PATH' not found"

if [ "$#" -eq 0 ]; then
    die "No source files passed to build_elf.sh"
fi

if [ -d "$FILES_DIR" ] && ! ls "$FILES_DIR"/*.c >/dev/null 2>&1; then
    die "No .c files found in ${FILES_DIR}"
fi

mkdir -p "$OUT_DIR"

CRT0_SRC="userland/crt0.asm"
CRT0_OBJ="${OUT_DIR}/crt0.o"
[ -f "$CRT0_SRC" ] || die "crt0.asm not found at ${CRT0_SRC}"
echo "[*] Assembling ${CRT0_SRC} -> ${CRT0_OBJ}"
nasm -f elf64 "$CRT0_SRC" -o "$CRT0_OBJ" || die "nasm failed for crt0.asm"

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

LIBS_DIR="userland/libs"
EXTRA_OBJS=""
if [ -d "$LIBS_DIR" ]; then
    for SFN in "$LIBS_DIR"/*.sfn; do
        [ -f "$SFN" ] || continue
        FNAME="$(basename "$SFN" .sfn)"
        FOBJ="$LIBS_DIR/${FNAME}.o"
        if [ ! -f "$FOBJ" ] || [ "$SFN" -nt "$FOBJ" ]; then
            echo "[*] Compiling font: ${SFN} -> ${FOBJ}"
            MAGIC="$(xxd -l2 "$SFN" | awk '{print $2$3}')"
            if [ "$MAGIC" = "1f8b" ]; then
                echo "[*] Detected gzip, decompressing ${SFN}..."
                cp "$SFN" "${SFN}.gz"
                gunzip -f "${SFN}.gz"
            fi
            objcopy -I binary -O elf64-x86-64 -B i386:x86-64 "$SFN" "$FOBJ" \
                || die "objcopy failed for ${SFN}"
            echo "[✓] Font compiled: $(basename "$FOBJ")"
        else
            echo "[*] Font up to date: $(basename "$FOBJ")"
        fi
    done

    for LOBJ in "$LIBS_DIR"/*.o; do
        [ -f "$LOBJ" ] || continue
        echo "[*] Including lib: $(basename "$LOBJ")"
        EXTRA_OBJS="$EXTRA_OBJS $LOBJ"
    done
fi

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
    "$fat_man" "$VHD_PATH" delete "/${BASENAME}" || true

    echo "[*] Compiling ${SRC} -> ${OBJ}"
    if [ "$BASENAME" = "gfxserver" ]; then
        PIC_FLAG="-fno-pic"
    else
        PIC_FLAG="-fPIC"
    fi
    clang -m64 -ffreestanding -fno-stack-protector -mno-red-zone \
          -nostdlib $PIC_FLAG -fno-pie -O1 \
          -I userland -I userland/libs \
          -c "$SRC" -o "$OBJ" \
          || die "Compilation failed for ${SRC}"

    if [ "$BASENAME" = "gfxserver" ]; then
        LINK_OBJS="$CRT0_OBJ $OBJ $EXTRA_OBJS"
    else
        LINK_OBJS="$CRT0_OBJ $OBJ"
    fi

    echo "[*] Linking ${OBJ} -> ${ELF}"
    ld.lld -m elf_x86_64 \
           -e _start \
           -T userland/userelf.ld \
           $LINK_OBJS -o "$ELF" \
           --warn-unresolved-symbols \
           --noinhibit-exec \
           || die "Linking failed for ${BASENAME}"

    [ -f "$ELF" ] || die "ELF not produced for ${BASENAME}"

    echo "[*] Importing ${ELF} as /${BASENAME}"
    "$fat_man" "$VHD_PATH" import "$ELF" "/${BASENAME}" \
        || die "Import failed for ${BASENAME}"

    echo "[✓] ${BASENAME} installed"
done

echo "[✓] All userland ELFs built and imported"
BUILD_OK=1
