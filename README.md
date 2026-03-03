# ZenOS&nbsp;&nbsp;[![ZenOS Build Check](https://github.com/Rishies2010/ZenOS/actions/workflows/c-cpp.yml/badge.svg)](https://github.com/Rishies2010/ZenOS/actions/workflows/c-cpp.yml)

<img width="100%" alt="ZenOS Logo" src="https://github.com/user-attachments/assets/4a3141c6-4223-465a-9b38-9f5d851f0b83" />

> [!WARNING]
> There might be dragons and bugs.

## Overview

**ZenOS** is a modern **64-bit SMP Preemptive custom operating system**, developed entirely from scratch using **C** and **x86_64 assembly**, and bootstrapped with the **Limine** bootloader.

The project focuses on clean design, correctness, and practical experimentation with real hardware concepts, while remaining lightweight and understandable. ZenOS is not a fork of an existing OS - it is a ground-up system built to explore kernel architecture, hardware interaction, and userspace execution in a controlled and extensible way.

---

## Key Features

### Kernel

- 64-bit x86_64 monolithic kernel
- Symmetric Multiprocessing (SMP) with full AP startup
- Pre-emptive round-robin scheduler
- Kernel ↔ userspace context switching
- Custom syscall ABI with assembly entry path
- ELF64 executable loading
- Userspace process support
- CPU feature detection via CPUID
- SSE and FPU initialization and management
- Spinlock-based synchronization primitives
- High-resolution timing via HPET
- ACPI-based hardware discovery and power handling

### Hardware & Drivers

- VGA output
- PC speaker support
- Serial output for debugging and logging
- Local APIC and IOAPIC interrupt handling
- ATA disk driver (PIO, 28-bit LBA)
- Socket read/write
- PCI bus enumeration
- Intel e1000 Ethernet driver (early-stage networking)
- Others include RTC, HPET, Keyboard (PS/2), Mouse (PS/2)

### Filesystem

- **FAT32 (FatFs)** port of [FatFs](https://elm-chan.org/fsw/ff/) made by CHAN
- Native host-side file management tooling (`fat_man`)
- Used as the primary medium for userspace ELF binaries

### Userspace

- [Newlib](https://sourceware.org/newlib/) C Library.
- `ELF64` userspace programs
- 60 Syscalls present.

#### Current Apps (Subject to change, this might be outdated):

- Calculator with `+`,`-`,`*`,`/`,`%`,`^` (calc.c)
- C4 : C in 4 functions compiler. (cc.c)
- Clock - Live clock (clock.c)
- Counter program (counter.c)
- Fibonacci sequence (fibonacci.c)
- Figlet - Graphical figlet style thingy (figlet.c)
- GFX Server - Graphics server for Unicode text / shapes (gfxserver.c)
- GFX Test - Program to test the above server (gfxtest.c)
- Hello, world! (hello.c)
- Init (init.c)
- Memory functions test (memtest.c)
- Prime numbers till 100 (primes.c)
- Shell - Simple Shell (shell.c)
- SmallerC compiler to Assembly (smlrc.c)
- Snake (snake.c)
- Sysinfo - Graphical + More detailed system info (sysinfo.c)
- Template program (test.c)
- Uname - System info (uname.c)
- Word counter (wc.c)
- Yes - Spam whatever you say (yes.c)

**The `ZenOS.vhd` file in the repository already usually has these programs in it.**

### Graphics & I/O

- [Flanterm](https://codeberg.org/mintsuki/flanterm) terminal rendering
- Unicode and scalable fonts support via [SSFN.](https://gitlab.com/bztsrc/scalable-font2)
- Clean logging system

---

## Building

- First try running `make help` to see the main Makefile commands. (Not all are in the help menu.)
- **Run** : `make all` at the root of the cloned git repo.
- Any issues will be reported, to which you can take necessary action, such as missing dependencies.
- For convenience, (`make funcs`) has been provided, running it will generate `funcs.txt` with all functions defined in the OS.

---

## Contributions

All good contributions are gladly welcome! It is suggested to fix an issue in the [ISSUES.md](https://github.com/Z-Proj/ZenOS/blob/main/docs/ISSUES.md), or you can add a feature, fix another unlisted bug, make ZenOS overall better!

---

## Design Goals

- Maintain a clean, minimal, and readable codebase

- Provide a solid foundation for experimentation with:

- [x] Kernel subsystems

- [x] Filesystems

- [x] Scheduling

- [x] SMP

- [x] Userspace ABI design

- [ ] Support simple dual-boot usage and lightweight utilities such as a **Calculator, Text editor, simple apps, File Manager, System Info, etc.** (Now in progress)

ZenOS is intended as a learning-oriented operating system project, prioritizing understanding the machine over chasing checklists.

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

## Third party sources

- The Limine bootloader. https://codeberg.org/Limine/Limine
- Flanterm Terminal emulator. https://codeberg.org/mintsuki/flanterm
- Scalable Screen Font 2.0. https://gitlab.com/bztsrc/scalable-font2/
- FatFs by CHAN. https://elm-chan.org/fsw/ff/
- SmallerC compiler. https://github.com/alexfru/SmallerC
- C4 Compiler: C in 4 functions. https://github.com/rswier/c4
- Newlib C library. https://sourceware.org/newlib/

---

**ZenOS**
