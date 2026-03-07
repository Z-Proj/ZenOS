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

66 POSIX + Custom Syscalls covering process management, file I/O, memory, signals, pipes, directories, sockets, time, graphics, and networking.

### Hardware & Drivers

- VGA framebuffer output
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
- **wget** — downloads files over HTTP from the command line

### Filesystem

- **VFS** support with multiple drives mountable.
- **FAT32** via a port of [FatFs](https://elm-chan.org/fsw/ff/) by CHAN
- Native host-side disk image tooling (`fat_man`)

### Userspace & Libc

- [Newlib](https://sourceware.org/newlib/) C library, fully compiled and integrated for the ZenOS target.
- ELF64 userspace programs.
- Currently **70+** Syscalls present.

#### Current Apps (subject to change):

- **Shell & Core Utils** - shell, echo, yes, sleep (shell.c, echo.c, yes.c, sleep.c)
- **File Utils** - cat, ls, touch, rm, stat, wc (cat.c, ls.c, touch.c, rm.c, stat.c, wc.c)
- **Directory Utils** - mkdir, rmdir, pwd (mkdir.c, rmdir.c, pwd.c)
- **Process Utils** - ps, kill (ps.c, kill.c)
- **System Info** - uname, sysinfo (uname.c, sysinfo.c)
- **Math & Science** - calc, primes, fibonacci, counter (calc.c, primes.c, fibonacci.c, counter.c)
- **Graphics** - GFX Server, GFX Test, edit, figlet, clock, mouse (gfxserver.c, gfxtest.c, edit.c, figlet.c, clock.c, mouse.c)
- **Compilers** - [TinyCC](https://github.com/TinyCC/tinycc), [SmallerC](https://github.com/alexfru/SmallerC) (cc.c, smlrc.c)
- **Scripting** - [Lua 5.5.0](https://www.lua.org/) interpreter
- **Networking** - wget, zen package manager (wget.c, zen.c)
- **Misc** - hello, beep, snake, init (hello.c, beep.c, snake.c, init.c)

**The `ZenOS.vhd` in the repository usually already has these compiled and ready.**
A separate `Storage.vhd` is present for uhh... storage.

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
  - [x] Networking (TCP/IP, DNS, HTTP)
  - [x] Native C compilation on the OS itself
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
- [TinyCC](https://github.com/TinyCC/tinycc) - Tiny C Compiler, ported to ZenOS
- [SmallerC](https://github.com/alexfru/SmallerC) - C to assembly compiler

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