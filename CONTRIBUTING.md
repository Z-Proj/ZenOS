# Contributing to D3X8

Thank you for considering a contribution. D3X8 is an operating-system project, so correctness, reproducibility, and careful testing matter more than patch size.

## Before you start

1. Read the [README](README.md), [architecture overview](docs/ARCHITECTURE.md), [build guide](docs/BUILDING.md), and [known issues](docs/ISSUES.md).
2. Search existing issues and pull requests.
3. Discuss large design changes in an issue before implementing them.
4. Report suspected vulnerabilities privately using [SECURITY.md](SECURITY.md), never in a public issue.

## Development principles

- Keep changes focused and explain the problem they solve.
- Follow existing conventions unless deliberately improving them.
- Treat userspace pointers, lengths, hardware values, and DMA buffers as untrusted.
- Check allocation failures, integer overflow, ownership, and cleanup paths.
- Avoid unbounded work while holding scheduler, VM, filesystem, or driver locks.
- Document non-obvious invariants and resource lifetimes near the code.
- Do not commit credentials, unrelated build output, or generated disk images unless required.

## Build and test

Follow [docs/BUILDING.md](docs/BUILDING.md). Run the narrowest relevant build or test and report exact commands and results. A successful compile is not proof of runtime correctness. For QEMU or VirtualBox tests, include the emulator configuration, RAM, CPU count, devices, and observed behavior. Clearly identify tests not run.

## Pull requests

Include the problem, proposed approach, affected subsystems, build/test results, known limitations, and useful logs or screenshots. Keep unrelated formatting changes out of functional patches. Do not claim a bug is fixed unless it was reproduced before the change and verified afterwards.

## Commit messages

Use concise imperative messages, for example:

- `mm: reclaim empty page-table levels`
- `sched: bound dead-process reaping work`
- `docs: explain QEMU test setup`

D3X8 is a work in progress and does not promise full POSIX compatibility or stable interfaces. Document changes to syscall behavior, userspace ABI, disk formats, and boot configuration.