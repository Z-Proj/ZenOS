# ZenOS  [![ZenOS Build Check](https://github.com/Rishies2010/ZenOS/actions/workflows/c-cpp.yml/badge.svg)](https://github.com/Rishies2010/ZenOS/actions/workflows/c-cpp.yml)

<img width="100%" alt="ZenOS Logo" src="https://github.com/user-attachments/assets/4a3141c6-4223-465a-9b38-9f5d851f0b83" />

## Overview

**ZenOS** is a modern **64-bit SMP custom operating system**, developed entirely from scratch using **C** and **x86_64 assembly**, and bootstrapped with the **Limine** bootloader.

The project focuses on clean design, correctness, and practical experimentation with real hardware concepts, while remaining lightweight and understandable. ZenOS is not a fork of an existing OS, nor is it POSIX-complete by design—it is a ground-up system built to explore kernel architecture, hardware interaction, and userspace execution in a controlled and extensible way.

---

## Key Features

### Kernel

- 64-bit x86_64 architecture  
- Symmetric Multiprocessing (SMP)  
- Pre-emptive round-robin scheduler  
- Custom syscall interface  
- ELF64 executable loading  
- Userspace process support  
- SSE and FPU support 
- [Limine](https://codeberg.org/Limine/Limine) bootloader
- High-resolution timing via HPET  
- LAPIC and IOAPIC interrupt handling  
- ACPI-based hardware discovery  
- PCI bus enumeration  

### Hardware & Drivers

- PS/2 keyboard and mouse  
- PC speaker  
- Real-Time Clock (RTC)  
- Serial output for debugging and logging  
- ATA disk driver (PIO, 28-bit LBA)  
- Early-stage network subsystem  

### Filesystem

- **ZenFS (ZFS)** — a custom filesystem designed specifically for ZenOS  
- Native tooling for integration with Linux / Unix hosts  
- Simple, deterministic, and OS-focused design  

### Graphics & I/O

- [Flanterm](https://codeberg.org/mintsuki/flanterm) terminal rendering
- TGA image rendering  
- Structured kernel logging system  

---

## Building

- **Run** : `make all` at the root of the cloned git repo.
- Any issues will be reported, to which you can take necessary action, such as missing dependencies.

---

## Design Goals

- Maintain a clean, minimal, and readable codebase  
- Avoid unnecessary legacy constraints  
- Favor correctness and explicitness over convenience  
- Provide a solid foundation for experimentation with:
  - Kernel subsystems  
  - Filesystems  
  - Scheduling  
  - Userspace ABI design  
- Support simple dual-boot usage and lightweight utilities such as:
  - Calculator  
  - Text editor  
  - File manager  
  - System inspection tools  

ZenOS is intended as a learning-oriented operating system project, prioritizing understanding the machine over chasing checklists.

---

## Toolchain

```bash
`clang`
`ld.lld`
`nasm`
`xorriso`
`qemu-system-x86_64`
`gdb`
`socat`
```

---

## Third party sources

- The Limine bootloader. https://codeberg.org/Limine/Limine
- Flanterm Terminal emulator. https://codeberg.org/mintsuki/flanterm

---

**ZenOS**  
Made by **Rishies2010**.
