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

75+ POSIX-compatible / custom syscalls covering major app and C library needs.

### Hardware & Drivers

- Framebuffer output
- PS/2 keyboard and mouse
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
- Dynamic ELF loader : `ld.so`.

### Applications

- **Shell & Core Utils** — shell, echo, yes, sleep, cat, ls, touch, rm, stat, wc, mkdir, rmdir, pwd
- **Process Utils** — ps, kill
- **System Info** — uname, time
- **Math** — calc, primes, fibonacci, counter
- **Text & UI** — terminal (PTY terminal emulator), edit (text editor), clock, beep, mouse, [FIGlet](https://github.com/cmatsuoka/figlet).
- **Compilers** — [TinyCC](https://github.com/TinyCC/tinycc), [SmallerC](https://github.com/alexfru/SmallerC)
- **Scripting** — [Lua 5.5.0](https://www.lua.org/)
- **Networking** — wget, zen package manager, nettest
- **Misc** — hello, init


**The `ZenOS.vhd` in the repository usually already has these compiled and ready.**
A separate `Storage.vhd` is present for storage.

### I/O & Display

- [Flanterm](https://codeberg.org/mintsuki/flanterm) for early kernel output
- Scalable font rendering via [SSFN](https://gitlab.com/bztsrc/scalable-font2)
- Structured kernel logging with log levels and serial output

---

## Building

Run `make help` first to see available build commands.

- **Build everything:** `make all` from the repo root
- Missing dependencies will be reported with clear errors
- `make funcs` generates `funcs.txt` listing all defined functions in the codebase

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
xorriso
qemu-system-x86_64
gdb
socat
```

---

## Third Party

- [Limine](https://codeberg.org/Limine/Limine) — bootloader
- [Flanterm](https://codeberg.org/mintsuki/flanterm) — early kernel terminal renderer
- [SSFN 2](https://gitlab.com/bztsrc/scalable-font2/) — scalable screen fonts
- [FatFs](https://elm-chan.org/fsw/ff/) — FAT32 library by CHAN
- [mlibc](https://github.com/managarm/mlibc) — C standard library
- [Lua](https://www.lua.org/) — lightweight scripting language
- [TinyCC](https://github.com/TinyCC/tinycc) — Tiny C Compiler, ported to ZenOS
- [SmallerC](https://github.com/alexfru/SmallerC) — C to assembly compiler
- [FIGlet](https://github.com/cmatsuoka/figlet) — Large ASCII text renderer
- [Blend2D](https://blend2d.com/) — 2D Vector Graphics

---

## Showcase (may be outdated)

<table>
  <tr>
    <td><img src="https://github.com/user-attachments/assets/bffcf748-2ba0-4a47-bb6c-4824b77675fa" /></td>
    <td><img src="https://github.com/user-attachments/assets/b8fa20f5-1e42-426b-bfd3-7ee8b2017b61" /></td>
  </tr>
  <tr>
    <td><img src="https://github.com/user-attachments/assets/f7872e91-188e-427e-974f-0870fd437118" /></td>
    <td><img src="https://github.com/user-attachments/assets/a6e08d37-b8b6-45b8-b87b-b8f948e9d6a4" /></td>
  </tr>
</table>

---

<p align="center">
  <b>ZenOS</b>
</p>
