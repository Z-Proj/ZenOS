# ZenOS&nbsp;&nbsp;[![ZenOS Build Check](https://github.com/Rishies2010/ZenOS/actions/workflows/c-cpp.yml/badge.svg)](https://github.com/Rishies2010/ZenOS/actions/workflows/c-cpp.yml)

<img width="100%" alt="ZenOS Logo" src="https://github.com/user-attachments/assets/4a3141c6-4223-465a-9b38-9f5d851f0b83" />

> [!WARNING]
> There might be dragons and bugs.

## Overview

**ZenOS** is a 64-bit SMP preemptive operating system built from scratch in **C** and **x86_64 assembly**, bootstrapped with the **Limine** bootloader.

The project focuses on clean design, correctness, and real hardware interaction.

---

## Key Features

### Kernel

- 64-bit x86_64 monolithic kernel
- Symmetric Multiprocessing (SMP)
- Preemptive round-robin SMP scheduler
- ELF64 userspace execution
- Dynamic loader
- Unix-style process model
- High-resolution timing via HPET
- ACPI-based hardware discovery and power management

### Syscalls

100+ POSIX-compatible / custom syscalls covering major app and C library needs.

### Hardware & Drivers

- Framebuffer output
- PS/2 keyboard and mouse
- USB HID keyboard and mouse (xHCI)
- PC speaker
- Serial port for debugging and logging
- Local APIC and IOAPIC interrupt handling
- ATA disk driver (DMA/PIO, 28-bit LBA)
- PCI bus
- Intel e1000 Ethernet driver
- RTC, HPET

### Networking

- TCP/IP stack
- DNS resolution
- **zen** — a package manager that fetches and installs packages over HTTP
- **wget** — downloads files over HTTP/1.0

### Filesystem

- VFS layer with support for multiple mountpoints
- FAT32 via a port of [FatFs](https://elm-chan.org/fsw/ff/) by CHAN
- Native host-side disk image tooling (`fat_man`)

### Userspace & Libc

- [mlibc](https://github.com/managarm/mlibc) C library.
- ELF64 userspace programs.
- Dynamic ELF loading.

### Applications

- **Shell & Core Utils** — shell, echo, yes, sleep, cat, ls, touch, rm, stat, wc, mkdir, rmdir, pwd, mv, cp, rename, settings (Settings GUI)
- **Process Utils** — ps, kill
- **System Info** — uname, time, sysinfo (System About)
- **Math** — calc, primes, fibonacci, counter
- **Text & UI** — [Harp](https://github.com/Z-Proj/ZenOS/blob/main/userland/files/harp/harp_main.c) (Custom compositor and server for Zen), terminal (PTY terminal emulator), clock, beep, imgview (Image Viewer), nk_widgets (Nuklear Demo), notepad, paint, [FIGlet](https://github.com/cmatsuoka/figlet), [Kilo](https://github.com/antirez/kilo), [DOOM](https://github.com/ozkl/doomgeneric), [ClassiCube](https://www.classicube.net).
- **Compilers** — [TinyCC](https://github.com/TinyCC/tinycc), [SmallerC](https://github.com/alexfru/SmallerC)
- **Scripting** — [Lua 5.5.0](https://www.lua.org/)
- **Networking** — wget, zen package manager
- **Misc** — hello, init, busybox


**The `ZenOS.vhd` in the repository usually already has these compiled and ready.**
A separate `Storage.vhd` is present for storage.

### I/O & Display

- USB HID devices like keyboard and mouse via xHCI
- [Flanterm](https://codeberg.org/mintsuki/flanterm) for early kernel output
- Clean font rendering via the [FreeType Engine](https://freetype.org/)
- Structured kernel logging with log levels and serial output

---

## Building

Run `make help` first to see available build commands.

- **Build everything:** `make all` from the repo root
- Missing dependencies will be reported with clear errors
- `make funcs` generates `funcs.txt` listing all defined functions in the codebase (kernel)
- **Note:** When using VBox, I recommend you allocate max video memory, enable xHCI in USB, network adapter as `Intel PRO/1000 MT Desktop` and run `VBoxManage modifyvm "ZenOS" --hpet on`. You can also use both PS/2 / USB Tablet pointer mode.

---

## Contributions

All good contributions are welcome. Check [ISSUES.md](https://github.com/Z-Proj/ZenOS/blob/main/docs/ISSUES.md) for open problems, or add a feature, fix a bug, or improve the codebase.

---

## Design Goals

- Clean, minimal, readable codebase
- Solid foundation for experimenting with:
  - [x] Kernel subsystems
  - [x] Filesystems
  - [x] Scheduling and SMP
  - [x] Userspace ABI design
  - [x] Unix-compatible process model (fork/exec/signals/pipes/PTY)
  - [x] Networking (TCP/IP, DNS, HTTP)
  - [x] Native C compilation on the OS itself
  - [ ] Lightweight everyday utilities
  - [ ] Dual-boot support

ZenOS is a learning-oriented project. Understanding the machine comes first for me.

---

## Toolchain / Prerequisites

```bash
clang
ld.lld
nasm
basename
ninja
meson
xorriso
qemu-system-x86_64
gdb
socat
```

---

## Third Party

- Check **[CREDITS.md](https://github.com/Z-Proj/ZenOS/blob/main/docs/CREDITS.md)** for the list of **third party apps and libraries**.

---

## License

- **ZenOS** is released under the terms of the **MIT License** : Read **[LICENSE](https://github.com/Z-Proj/ZenOS/blob/main/LICENSE)** for the full document.

---

## Showcase (may be outdated)

<table>
  <tr>
    <td><img alt="1" src="https://github.com/user-attachments/assets/58002186-1309-447e-8458-cca25bee3892" /></td>
    <td><img alt="2" src="https://github.com/user-attachments/assets/859e58b7-56ec-42ae-8ed1-a48f8fbb70e9" /></td>
  </tr>
  <tr>
    <td><img alt="3" src="https://github.com/user-attachments/assets/168cc50b-74b9-4515-8008-37ed2da0d0b9" /></td>
    <td><img alt="4" src="https://github.com/user-attachments/assets/444320f9-3d0e-4b46-bafb-b275b6b882e0" /></td>
  </tr>
</table>

---

<p align="center">
  <b>ZenOS</b>
</p>
