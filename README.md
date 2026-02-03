# ZenOS&nbsp;&nbsp;[![ZenOS Build Check](https://github.com/Rishies2010/ZenOS/actions/workflows/c-cpp.yml/badge.svg)](https://github.com/Rishies2010/ZenOS/actions/workflows/c-cpp.yml)

<img width="100%" alt="ZenOS Logo" src="https://github.com/user-attachments/assets/4a3141c6-4223-465a-9b38-9f5d851f0b83" />

> [!WARNING]
> Some functionality of the kernel has been temporarily disabled in the current commit, to fix an [issue.](https://github.com/Z-Proj/ZenOS/blob/main/docs/ISSUES.md)

## Overview

**ZenOS** is a modern **64-bit SMP custom operating system**, developed entirely from scratch using **C** and **x86_64 assembly**, and bootstrapped with the **Limine** bootloader.

The project focuses on clean design, correctness, and practical experimentation with real hardware concepts, while remaining lightweight and understandable. ZenOS is not a fork of an existing OS, nor is it POSIX-complete by design—it is a ground-up system built to explore kernel architecture, hardware interaction, and userspace execution in a controlled and extensible way.

---

## Key Features

### Kernel

- 64-bit x86_64 monolithic kernel
- Symmetric Multiprocessing (SMP) with AP startup
- Pre-emptive round-robin scheduler
- Kernel ↔ userspace context switching
- Custom syscall ABI with assembly entry path
- ELF64 executable loading
- Userspace process support
- CPU feature detection via CPUID
- SSE and FPU initialization and management
- Spinlock-based synchronization primitives
- High-resolution timing via HPET
- Local APIC and IOAPIC interrupt handling
- ACPI-based hardware discovery and power handling

### Hardware & Drivers

- VGA output
- PS/2 keyboard and mouse drivers
- PC speaker support
- Serial output for debugging and logging
- ATA disk driver (PIO, 28-bit LBA)
- PCI bus enumeration
- Intel e1000 Ethernet driver (early-stage networking)
- Others include RTC, HPET, Keyboard (PS/2), Mouse (PS/2)

### Filesystem

- **ZenFS (ZFS)** — a custom filesystem designed specifically for ZenOS
- Native host-side management tooling (`zfs_man`)
- Used as the primary medium for userspace ELF binaries

### Userspace

- ELF64 userspace programs
- Minimal userspace C ABI

### Graphics & I/O

- [Flanterm](https://codeberg.org/mintsuki/flanterm) terminal rendering
- Structured kernel logging system

---

## Building

- **Run** : `make all` at the root of the cloned git repo.
- Any issues will be reported, to which you can take necessary action, such as missing dependencies.

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
- [ ] Support simple dual-boot usage and lightweight utilities such as a **Calculator, Text editor, simple apps, File Manager, System Info, etc.**

ZenOS is intended as a learning-oriented operating system project, prioritizing understanding the machine over chasing checklists.

---

## Toolchain

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

---

**ZenOS**

