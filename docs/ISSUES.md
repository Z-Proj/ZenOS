# Current issues and audit notes

## Open runtime issues

- **Settings + Terminal concurrency crash:** Still reproducible according to the reported behavior, but the exact faulting component has not been isolated. Harp IPC, shared-memory registration, terminal/shell process lifecycle, and kernel memory management remain areas to test together.
- **Terminal input freeze on first launch:** Reported previously; not verified or fixed in this audit.
- **Memory growth while opening apps:** Some growth is expected because each live process has its own address space, kernel stack, file descriptors, and application buffers. Memory should be reclaimed when a process exits and is reaped. A runtime measurement is still needed to distinguish normal per-process allocation from a leak.

## Changes from the static audit

- Harp now rejects invalid window dimensions, duplicate registrations from the same PID, missing/undersized shared-memory buffers, and failed event-socket opens before activating a window.
- Harp closes a successfully opened shared-memory reference when registration validation fails.
- The compositor renderer no longer performs pointer arithmetic on a null shared-memory buffer.
- xHCI device cleanup now routes failed enumeration and disconnect cleanup through a slot-disable-before-free helper. If the controller refuses to disable a slot, DMA-visible pages are deliberately retained rather than reused unsafely.
- The Nuklear Harp initialization font-load failure path frees its back buffer before returning.
- PTY descriptor cloning and closing use matching reference-count increments/decrements in the current implementation; no clear PTY refcount leak was found by static inspection.

## Verification status

These changes have **not** been compiled or runtime-tested yet. The GitHub Actions workflow currently has no recorded workflow runs, so there is no CI build result to rely on. The concurrency crash and memory-growth behavior remain open until tested on the OS.
