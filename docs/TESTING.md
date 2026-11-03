# Testing and Validation

D3X8 is an operating system under active development. Tests should be repeatable, scoped, and honest about what they establish. A successful build is necessary but does not prove runtime stability.

## Before testing

Record the Git commit SHA and branch, host OS and toolchain versions, emulator or machine model, guest RAM and CPU count, enabled devices, disk image versions, exact commands, and test duration.

Use disposable copies of disk images for destructive filesystem or storage tests.

## Build validation

Follow [BUILDING.md](BUILDING.md):

```sh
make deps
make all
```

Save the complete log and exit status. For CI-equivalent staged validation, use the sequential steps in the build guide.

## Boot smoke test

Boot the generated ISO in a disposable VM. Confirm and record:

- Bootloader reaches the kernel.
- Kernel initialization completes without an unexpected panic.
- Userspace init starts.
- Expected console or desktop appears.
- Keyboard and pointer input work.
- Storage devices and expected filesystems are detected.
- Serial logs contain no recurring fatal errors.

Reaching the desktop once is not proof of stability.

## Process and scheduler tests

- Launch and exit a simple userspace program repeatedly.
- Run several independent programs concurrently.
- Exercise foreground launch, background launch, wait, and termination.
- Verify a child exit does not terminate unrelated processes.
- Repeat with one CPU and multiple CPUs.
- Watch for stuck processes, scheduler stalls, zombie accumulation, and increasing memory use.

## Memory-management tests

- Repeatedly allocate and release user memory.
- Exercise `mmap`, `munmap`, `mprotect`, and `brk` where supported.
- Create and destroy processes with different mapping sizes.
- Exercise fork/exec paths and shared mappings where implemented.
- Test invalid, unaligned, overflowing, and unmapped user ranges.
- Compare free-page counts before and after repeated cycles, allowing for documented caches and persistent allocations.

A stable free-page count over a short run does not prove the absence of leaks. Test repeated cycles and record trends.

## Desktop concurrency regression

For the reported Settings/Terminal/system-information slowdown:

1. Boot from a clean test image and record baseline memory and responsiveness.
2. Open Settings and Terminal separately; exercise each for a fixed period.
3. Open both and repeat the same interactions.
4. Launch System Information while both are open.
5. Repeat with applications closed and reopened several times.
6. Repeat with different CPU counts if practical.
7. Record which application stops responding, whether input still works, memory/page counts, and the last serial/kernel log lines.

Change one variable at a time. Do not assume the last application launched caused the freeze.

## Driver and device tests

Test drivers only with the relevant virtual or physical device enabled. For USB/xHCI, test repeated input and disconnect/reset paths where supported. For storage, use disposable images and verify reads, writes, directory operations, and error handling. For networking, record adapter model and test link, address configuration, DNS, and HTTP behavior separately.

DMA resources must remain valid until the device has stopped using them. Never intentionally free active DMA memory on valuable hardware.

## Reporting results

For each test, include setup, exact steps, expected behavior, observed behavior, result (pass/fail/blocked/not run), and evidence such as logs, screenshots, counters, and reproduction frequency.

Do not label a test as passed if it was only reviewed by reading code. Distinguish static inspection, successful compilation, boot testing, and sustained runtime stress testing.
