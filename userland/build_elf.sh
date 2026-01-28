#!/bin/bash
set -e

if [ $# -lt 1 ]; then
    echo "Usage: $0 <source_file.c>"
    exit 1
fi

SRC="$1"
BASENAME=$(basename "$SRC" .c)
OBJ="${BASENAME}.o"
ELF="${BASENAME}.elf"
VHD_PATH="../ZenOS.vhd"
ZFS_MAN="../zfs_man"

echo "[*] Deleting old /${BASENAME} from ${VHD_PATH}..."
${ZFS_MAN} "${VHD_PATH}" delete "/${BASENAME}" || true

echo "[*] Compiling ${SRC} -> ${OBJ}"
clang -m64 -ffreestanding -fno-stack-protector \
      -nostdlib -fno-pie -fPIC \
      -c "${SRC}" -o "${OBJ}"

echo "[*] Linking -> ${ELF}"
ld -m elf_x86_64 -e main -T userelf.ld "${OBJ}" -o "${ELF}" \
   --warn-unresolved-symbols \
   --noinhibit-exec

if [ ! -f "${ELF}" ]; then
    echo "[!] ELF build failed!"
    exit 1
fi

echo "[*] Importing ${ELF} to /${BASENAME}..."
${ZFS_MAN} "${VHD_PATH}" import "${ELF}" "/${BASENAME}"

echo "[✓] Done! ${ELF} installed as /${BASENAME} inside ${VHD_PATH}"
