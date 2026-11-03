# Building D3X8

These instructions describe the repository's current Makefile targets. The build system is under active development, so check the Makefile if a command changes.

## Host tools

The Makefile expects a Linux-like environment with these tools available on `PATH`:

- `clang`
- `ld.lld`
- `nasm`
- `xorriso`
- `ninja` and `meson`
- `qemu-system-x86_64` for QEMU runs
- `gdb` and `socat`
- `basename`, `find`, `rm`, and standard build utilities

The mlibc build may require additional compiler, linker, and development packages depending on the host distribution. The `deps` target checks only a subset of requirements and does not guarantee every dependency is installed.

## Get the source

```sh
git clone https://github.com/jadeking5128-beep/D3X8.git
cd D3X8
git submodule update --init --recursive
```

## Check dependencies and targets

```sh
make deps
make help
```

## Build

The complete Makefile target is:

```sh
make all
```

The staged sequence used by the repository's CI workflow is:

```sh
make fat
make mlibc
make -j"$(nproc)" ZenOS.iso
make init
make user
```

Run disk-image-mutating steps sequentially. mlibc installation, initialization, and userspace installation write to `ZenOS.vhd`; running them concurrently can race on the image.

The kernel ISO target is `ZenOS.iso`. The `user` target invokes the userspace build script. The `init` target writes the init command into the system disk image.

## Run in QEMU

```sh
make qemu
```

The target expects `ZenOS.iso`, `ZenOS.vhd`, and `Storage.vhd`. It configures a 256 MiB guest, two CPUs, serial output, an e1000 network adapter, and xHCI USB keyboard/mouse devices. It enables KVM and uses the host CPU, so adjust those options on systems without KVM support.

## VirtualBox

The README documents a VirtualBox setup using xHCI, an Intel PRO/1000 MT Desktop network adapter, and HPET. The Makefile's `vbox` target expects a VM named `ZenOS` and a local serial socket at `/tmp/zenos`. Configure those separately before using the target.

## Clean generated output

```sh
make clean
```

The current clean target removes object/dependency files, the kernel build directory, ISO staging directory, ISO image, and a generated CPU assembly file. It does not delete `ZenOS.vhd` or `Storage.vhd`.

## Troubleshooting

- Run `make deps` and verify each reported tool is installed.
- Check the first failing command, not only the final Make error.
- Ensure the mlibc Meson cross file and source tree are present.
- Ensure expected disk images exist and are writable.
- For QEMU, verify KVM availability or remove KVM-specific options in a local command.
- Keep build logs and report the host OS, compiler versions, exact command, and failure output.

A successful build verifies only the build steps that completed. It does not establish that the kernel boots correctly or that runtime concurrency, drivers, and applications are stable.
