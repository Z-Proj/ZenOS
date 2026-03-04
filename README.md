# ZenOS&nbsp;&nbsp;[![ZenOS Build Check](https://github.com/Rishies2010/ZenOS/actions/workflows/c-cpp.yml/badge.svg)](https://github.com/Rishies2010/ZenOS/actions/workflows/c-cpp.yml)

<img width="100%" alt="ZenOS Logo" src="https://github.com/user-attachments/assets/4a3141c6-4223-465a-9b38-9f5d851f0b83" />

> [!WARNING]
> There might be dragons and bugs.

## Overview

**ZenOS** is a modern **64-bit SMP preemptive operating system**, developed entirely from scratch in **C** and **x86_64 assembly**, bootstrapped with the **Limine** bootloader.

The project focuses on clean design, correctness, and real hardware interaction. Everything from the scheduler to the filesystem to the userspace process model is built from the ground up.

---

## Key Features

### Kernel

- 64-bit x86_64 monolithic kernel
- Symmetric Multiprocessing (SMP)
- Preemptive round-robin scheduler
- ELF64 userspace execution
- Unix-style process model
- CPU feature detection via CPUID
- SSE and FPU initialization
- High-resolution timing via HPET
- ACPI-based hardware discovery and power management

### Syscalls

Over 60 syscalls covering process management, file I/O, memory, signals, pipes, directories, sockets, time, and graphics.

### Hardware & Drivers

- VGA framebuffer output
- PS/2 keyboard and mouse
- PC speaker
- Serial port for debugging and logging
- Local APIC and IOAPIC interrupt handling
- ATA disk driver (DMA/PIO, 28-bit LBA)
- PCI bus enumeration
- Intel e1000 Ethernet driver
- RTC, HPET

### Filesystem

- **FAT32** via a port of [FatFs](https://elm-chan.org/fsw/ff/) by CHAN
- Native host-side disk image tooling (`fat_man`)
- Primary storage medium for userspace ELF binaries

### Userspace & Libc

- [Newlib](https://sourceware.org/newlib/) C library, fully compiled and integrated for the ZenOS target.
- ELF64 userspace programs.
- Currently **60** Syscalls present.

#### Current Apps (subject to change):

- Calculator - `+`, `-`, `*`, `/`, `%`, `^` (calc.c)
- [C4](https://github.com/rswier/c4) - C in 4 functions compiler (cc.c)
- Clock - live clock display (clock.c)
- Counter (counter.c)
- Edit - graphical text editor (edit.c)
- Fibonacci sequence (fibonacci.c)
- Figlet - graphical text art (figlet.c)
- GFX Server - Unicode text and shapes graphics server (gfxserver.c)
- GFX Test - test client for the graphics server (gfxtest.c)
- Hello, world! (hello.c)
- Init - Boot process manager (init.c)
- The [Lua](https://www.lua.org/) Interpreter (lua-5.5.0-zenos/)
- Primes - prime numbers up to 100 (primes.c)
- Shell - Quite a good and simple shell (shell.c)
- [SmallerC](https://github.com/alexfru/SmallerC) - C to assembly compiler (smlrc.c)
- Snake (snake.c)
- Sysinfo - graphical system information (sysinfo.c)
- Uname - system info (uname.c)
- Word counter (wc.c)
- Yes - repeat output indefinitely (yes.c)

**The `ZenOS.vhd` in the repository usually already has these compiled and ready.**

### Graphics & I/O

- [Flanterm](https://codeberg.org/mintsuki/flanterm) terminal renderer
- Unicode and scalable font support via [SSFN](https://gitlab.com/bztsrc/scalable-font2)
- Structured kernel logging with log levels and serial output

---

## Building

Run `make help` first to see available build commands.

- **Build everything:** `make all` from the repo root
- Missing dependencies will be reported with clear errors
- `make funcs` generates `funcs.txt` listing all defined functions in the codebase

---

## Contributions

All good contributions are welcome! Check [ISSUES.md](https://github.com/Z-Proj/ZenOS/blob/main/docs/ISSUES.md) for open problems, or add a feature, fix a bug, or improve the codebase in any way.

---

## Design Goals

- Maintain a clean, minimal, and readable codebase
- Provide a solid foundation for experimenting with:
  - [x] Kernel subsystems
  - [x] Filesystems
  - [x] Scheduling
  - [x] SMP
  - [x] Userspace ABI design
  - [x] Unix-compatible process model (fork/exec/signals/pipes)
  - [ ] Lightweight dual-boot support and everyday utilities

ZenOS is a learning-oriented project. Understanding the machine comes first.

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

- [Limine](https://codeberg.org/Limine/Limine) - bootloader
- [Flanterm](https://codeberg.org/mintsuki/flanterm) - terminal emulator
- [SSFN 2.0](https://gitlab.com/bztsrc/scalable-font2/) - scalable screen fonts
- [FatFs](https://elm-chan.org/fsw/ff/) - FAT32 filesystem library by CHAN
- [Newlib](https://sourceware.org/newlib/) - C standard library
- [Lua](https://www.lua.org/) - A powerful lightweight scripting language
- [SmallerC](https://github.com/alexfru/SmallerC) - C to assembly compiler
- [C4](https://github.com/rswier/c4) - C in 4 functions

---

## Showcase (Can be outdated)

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
