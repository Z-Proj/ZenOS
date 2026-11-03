# D3X8 Architecture Overview

## System layers

D3X8 is a 64-bit x86_64 operating system. The repository contains a custom kernel, hardware drivers, userspace programs, a C library build, bootloader assets, and disk-image tooling.

The main layers are:

1. **Boot and platform:** Limine boot protocol, early console, firmware tables, CPU setup, interrupt controllers, timers, and platform discovery.
2. **Kernel core:** memory management, scheduling, process and thread lifecycle, system calls, synchronization, and kernel services.
3. **Drivers:** PCI and device discovery, display, input, storage, USB/xHCI, and networking hardware.
4. **Kernel interfaces:** virtual filesystem, file descriptors, process interfaces, memory mapping, PTYs, sockets, and syscall-facing facilities.
5. **Userspace runtime:** ELF64 loading, dynamic linking, mlibc, initialization, shell, utilities, and applications.
6. **Desktop:** Harp compositor/window server and graphical applications.

This is a conceptual map, not a guarantee that every subsystem is complete or fully POSIX-compatible.

## Boot and execution

The build produces a kernel image and a bootable ISO using Limine assets. The kernel initializes platform facilities and starts userspace through the configured init program. The current Makefile initialization rule writes `/mnt/drv0/bin/harp` as the first init command in `init.run`.

Userspace programs are ELF64 executables. The kernel provides process creation and system-call facilities, while mlibc supplies much of the userspace C/POSIX runtime. Compatibility is project-specific and should be checked against the implementation.

## Memory management

The kernel manages physical pages and x86_64 page tables, and provides user mappings and memory-related system calls. Important correctness properties include:

- Every physical page has a clear owner or an explicit shared/reference-counted lifetime.
- User virtual addresses and lengths are validated before kernel access.
- Mapping and unmapping handle overflow, alignment, and partial failure.
- Page-table structures are reclaimed only when empty and safe to release.
- Process teardown must not free frames still mapped by another process or subsystem.

Fork-like address-space cloning, shared memory, framebuffer mappings, large pages, and DMA memory require special ownership rules. These areas need continued validation; the existence of an API alone does not establish correctness.

## Scheduling and processes

D3X8 has an SMP-capable preemptive scheduler and process lifecycle management. Concurrent correctness depends on lock ordering, safe transitions between runnable/running/dead states, bounded reaping work, and cleanup that does not run while another CPU can still access a resource.

When investigating hangs or progressive lag, record CPU count, process state, lock ownership where available, allocation growth, and whether the issue reproduces with one CPU. A single application crash may expose a kernel-wide lifetime or synchronization defect.

## System calls and userspace boundary

System calls form an untrusted boundary. Kernel handlers must validate pointers, lengths, arithmetic, and object handles. Copy data into kernel-owned buffers before parsing or retaining it; copy results back only after validating the destination range. Avoid directly dereferencing user pointers.

A syscall name or POSIX-like signature does not imply complete standards compatibility. Consult the implementation and tests for actual behavior.

## Drivers and DMA

Drivers interact with hardware through memory-mapped registers, interrupts, and DMA. DMA buffer lifetimes may extend beyond the initiating syscall or process. A buffer must not be freed until the device is known to have stopped accessing it. Device reset and teardown paths need explicit timeout and failure handling.

## Storage and desktop

The repository includes a VFS layer and FAT filesystem support, plus host-side disk-image tooling. The root Makefile mutates `ZenOS.vhd` during initialization and userspace installation. Treat disk images as build/runtime data, not source files to overwrite casually.

Harp provides the graphical desktop/composition path. The browser-based preview described in the main README is illustrative and is not the operating system itself; preview controls do not imply that the corresponding kernel or userspace service is operational.

## Further reading

- [Build instructions](BUILDING.md)
- [Testing guide](TESTING.md)
- [Known issues and verification status](ISSUES.md)
- [Third-party components](CREDITS.md)
