# D3X8

<p align="center">
  <img width="100%" alt="D3X8 operating system" src="https://github.com/user-attachments/assets/4a3141c6-4223-465a-9b38-9f5d851f0b83" />
</p>

<p align="center">
  <strong>A 64-bit x86_64 operating system built from the ground up.</strong>
  <br />
  <a href="https://github.com/jadeking5128-beep/D3X8/actions/workflows/c-cpp.yml">
    <img alt="Build status" src="https://github.com/jadeking5128-beep/D3X8/actions/workflows/c-cpp.yml/badge.svg" />
  </a>
  <a href="https://github.com/jadeking5128-beep/D3X8/blob/main/LICENSE">
    <img alt="MIT License" src="https://img.shields.io/badge/license-MIT-blue.svg" />
  </a>
  <a href="https://github.com/jadeking5128-beep/D3X8">
    <img alt="Platform" src="https://img.shields.io/badge/platform-x86__64-informational.svg" />
  </a>
</p>

D3X8 is an actively developed, 64-bit, SMP-capable operating system written primarily in **C** and **x86_64 assembly**, and booted with the **Limine** bootloader. The project brings together a custom kernel, userspace environment, C library, device drivers, networking stack, filesystem support, and a graphical desktop.

The goal is to build and understand a complete operating system, from low-level hardware and memory management to interactive applications.

---

## Live desktop preview

<p align="center">
  <a href="https://jadeking5128-beep.github.io/D3X8/">
    <img width="100%" alt="Interactive D3X8 desktop preview" src="preview/preview.svg" />
  </a>
  <br />
  <a href="https://jadeking5128-beep.github.io/D3X8/"><strong>Launch the interactive GUI preview →</strong></a>
</p>

The browser preview demonstrates the desktop's visual direction and interaction model. It includes a floating dock, translucent windows, an application launcher, Control Center, and sample application interfaces.

**Preview scope:** this is a browser-based GUI demonstration. It is not an emulator, does not boot the kernel, and does not execute native D3X8 applications or shell commands.

The preview's theme tokens are generated from the native Harp GUI source using `preview/sync-theme.py`. The GitHub Actions workflow in `.github/workflows/gui-preview.yml` builds and publishes the preview to GitHub Pages when the relevant preview or GUI source files change. GitHub Pages must be enabled for the repository with **Source: GitHub Actions**.

---

## Highlights

- **64-bit kernel:** x86_64 architecture with symmetric multiprocessing and preemptive scheduling.
- **Userspace execution:** ELF64 programs, dynamic loading, process creation, and a Unix-style process model.
- **Hardware support:** ACPI discovery, interrupt controllers, PCI enumeration, USB input, storage, display, and networking devices.
- **Networking:** TCP/IP, DNS, HTTP downloads, and a network-based package manager.
- **Storage:** a virtual filesystem with FAT32 support and disk-image tooling.
- **Desktop environment:** Harp compositor and window server, native GUI widgets, terminal, settings, system information, and other applications.
- **Development tools:** native C compilers, Lua, command-line utilities, and debugging support.

---

## Kernel and system architecture

### Core kernel

- 64-bit x86_64 monolithic kernel.
- Symmetric Multiprocessing (SMP).
- Preemptive round-robin SMP scheduler.
- ELF64 userspace program loading.
- Dynamic ELF loader.
- Unix-style process model.
- System-call interface with 100+ POSIX-oriented and project-specific calls.
- High-resolution timing using HPET.
- ACPI-based hardware discovery and power-management interfaces.
- Local APIC and IOAPIC interrupt handling.

### Memory and processes

D3X8 provides the kernel and userspace foundations required to run independent programs, load ELF executables, and manage process execution. Process isolation, memory mapping, page-table management, and resource reclamation remain active areas of development and testing.

### System calls

The system-call interface contains more than 100 POSIX-oriented and custom calls covering core process, file, memory, timing, and userspace runtime requirements. The exact supported behavior can differ from a full POSIX implementation.

---

## Graphics and desktop environment

### Harp

**Harp** is D3X8's custom graphical compositor and desktop server. It provides the foundation for application windows and the desktop interface.

Current GUI work includes:

- A native C UI tree with row, column, stack, container, text, button, glass, and spacer elements.
- Window and desktop composition.
- A bounded ARGB8888 glass-rendering path.
- Background snapshots used by glass surfaces.
- Refraction, softening, tint, and highlight effects.
- Glass styling for desktop dock groups and window titlebars.

The GUI is being developed alongside the kernel and userspace. Rendering capabilities and application stability are still being tested on the target system.

### Browser GUI preview

The separate browser preview is designed to make the desktop interface easy to inspect without building or booting the operating system. It currently demonstrates:

- A desktop workspace and top menu bar.
- A translucent floating application dock.
- Draggable application windows with close, minimize, and maximize controls.
- Application launcher with search.
- Control Center with example connectivity and appearance toggles.
- Display brightness and sound sliders.
- Sample interfaces for Files, Settings, Terminal, System Monitor, Notes, and Gallery.
- Responsive layout for desktop and mobile screens.

The preview is illustrative. Its sample controls and applications do not operate the underlying OS.

---

## Hardware and drivers

Current hardware and platform support includes:

- Framebuffer display output.
- Flanterm for early kernel console output.
- PS/2 keyboard and mouse.
- USB HID keyboard and mouse through xHCI.
- PCI bus enumeration.
- ATA storage driver with DMA/PIO support and 28-bit LBA addressing.
- Intel e1000 Ethernet driver.
- PC speaker.
- Serial port output for debugging and kernel logs.
- RTC and HPET.
- ACPI hardware discovery and power-management support.
- Local APIC and IOAPIC interrupt routing.

Driver coverage depends on the hardware and virtual-machine configuration. Some devices and edge cases remain under active development.

---

## Networking

- TCP/IP networking stack.
- DNS resolution.
- HTTP/1.0 file downloads through `wget`.
- **zen** package manager for fetching and installing packages over HTTP.
- Intel e1000 network interface support.

---

## Filesystem and storage

- Virtual filesystem (VFS) layer.
- Multiple mountpoint support.
- FAT32 filesystem support through [FatFs](http://elm-chan.org/fsw/ff/).
- `fat_man` host-side disk-image tooling.
- Separate `ZenOS.vhd` and `Storage.vhd` disk images are included in the repository where provided.

---

## Userspace and applications

D3X8 includes a growing userspace environment built around ELF64 programs and [mlibc](https://github.com/managarm/mlibc).

| Area | Included software |
| --- | --- |
| Shell and core utilities | Shell, BusyBox, `echo`, `yes`, `sleep`, `cat`, `ls`, `touch`, `rm`, `stat`, `wc`, `mkdir`, `rmdir`, `pwd`, `mv`, `cp`, `rename` |
| Process management | `ps`, `kill` |
| System information | `uname`, `time`, `sysinfo`, Settings |
| Terminal and desktop | Harp, terminal (PTY terminal emulator), clock, image viewer, Nuklear demo |
| Productivity and graphics | Notepad, Paint |
| Development | TinyCC, SmallerC, Lua 5.5.0 |
| Utilities and demos | Calculator, primes, Fibonacci, counter, hello, beep |
| Additional applications | FIGlet, Kilo, DOOM, ClassiCube |
| Networking | `wget`, `zen` package manager |

Third-party applications and libraries are documented in [CREDITS.md](docs/CREDITS.md). Availability may vary by disk image and build configuration.

---

## Building

Start by listing the available build targets:

```sh
make help
```

Build the complete project:

```sh
make all
```

The build system reports missing dependencies when required tools are unavailable. The repository also provides:

```sh
make funcs
```

This generates `funcs.txt`, a listing of kernel functions.

### Toolchain and prerequisites

The build environment uses the following tools:

- Clang
- LLD (`ld.lld`)
- NASM
- Ninja
- Meson
- xorriso
- QEMU for x86_64
- GDB
- socat
- Standard command-line utilities, including `basename`

Install the toolchain packages appropriate for your host distribution before building.

### Running in VirtualBox

For a more representative virtual-machine setup:

- Allocate sufficient video memory.
- Enable the xHCI USB controller.
- Use **Intel PRO/1000 MT Desktop** as the network adapter.
- Enable HPET:

```sh
VBoxManage modifyvm "D3X8" --hpet on
```

PS/2 and USB tablet pointer modes can both be used, depending on the VM configuration.

---

## Development status

D3X8 is a work in progress and is not presented as a production-ready operating system. Kernel subsystems, drivers, GUI behavior, and userspace compatibility are continuously evolving.

In particular, concurrent application stability, process resource cleanup, and memory-management behavior require ongoing validation under load. A successful compile does not by itself guarantee runtime stability on every machine.

For current known problems and investigation notes, see [the issue tracker](https://github.com/jadeking5128-beep/D3X8/issues) and the repository's [ISSUES.md](docs/ISSUES.md), when present.

---

## Contributing

Contributions are welcome. Useful areas include:

- Kernel and memory-management correctness.
- Scheduler and process stability.
- Device drivers and hardware compatibility.
- Filesystem and networking support.
- GUI rendering, widgets, and desktop applications.
- Build-system improvements and documentation.

Before making a change, review the existing code and open issues. Keep changes focused, document important behavior, and include build or runtime test results where possible.

---

## Project goals

D3X8 is developed as a practical systems-programming project. Its ongoing goals include:

- Building a maintainable kernel and userspace foundation.
- Improving process, memory, and SMP reliability.
- Expanding hardware and filesystem support.
- Strengthening networking and package management.
- Developing a cohesive native graphical desktop.
- Improving everyday userspace utilities.
- Exploring dual-boot support.

---

## Credits and third-party software

D3X8 builds on work from several open-source projects, libraries, and application authors. See [CREDITS.md](docs/CREDITS.md) for attribution and license information.

---

## License

D3X8 is released under the **MIT License**. See [LICENSE](LICENSE) for the complete license text.

---

<p align="center">
  <strong>D3X8</strong><br />
  <sub>Built from the ground up, one subsystem at a time.</sub>
</p>
