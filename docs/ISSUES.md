# Current issues and audit notes

## Open runtime issues

- **Settings + Terminal concurrency:** Both were observed running together in the latest user test, so the previous immediate-crash behavior is not currently confirmed. Stress testing with repeated launches, closes, and concurrent I/O is still required.
- **Application-launch stalls and progressive lag:** Reported when launching additional applications. The static audit found page-table allocation and user-range handling defects and committed fixes, but runtime causality and complete resolution are not yet proven.
- **Terminal input freeze on first launch:** Previously reported; not verified or fixed in this audit.
- **Memory growth:** The original empty-page-table leak is addressed by bottom-up PT/PD/PDPT reclamation. Repeated mmap/munmap, heap growth/shrink, process exit, and shared-memory lifetime tests are still required to verify that physical memory returns to baseline.
- **Syscall user-pointer safety:** Many syscall handlers still dereference user pointers directly in kernel mode after checking only for null. Invalid or unmapped user addresses can fault while the CPU is executing kernel code. A consistent copyin/copyout and range-validation layer is still needed.

## Fixes committed during the static audit

- ea9bb58: unmap_page() now reclaims empty PT, PD, and PDPT levels bottom-up and avoids treating large-page entries as page-table pointers.
- e5e06d2: brk/sbrk arithmetic is bounded against overflow and the mmap region; mmap, munmap, and mprotect validate rounded lengths and user mapping bounds. This also prevents generic munmap from freeing shared-memory or framebuffer mappings.
- 8eba383: map_page() rolls back page-table levels allocated by a failed mapping attempt instead of leaking partially built hierarchies.
- bdb3a37, f8ca9a1, a424c7a, e2e43f7: 2 MiB and 1 GiB large mappings are split into smaller mappings without discarding neighboring mappings; PAT and user/supervisor permission propagation are preserved.

## Additional audit observations

- Process teardown reaps dead tasks and releases descriptor tables, owned user pages, special shared-memory references, kernel stacks, and page-directory structures. This path still needs repeated lifecycle tests under load.
- free_user_pages() deliberately skips the shared-memory and framebuffer PML4 slots; those mappings rely on their dedicated reference-counted cleanup path.
- Harp validates window dimensions and shared-memory registration before activating windows. Its close/reap lifecycle still needs stress testing with applications exiting unexpectedly.
- PTY reference-count increments and decrements appeared paired in the inspected clone/close paths. No confirmed PTY leak was found in this pass.
- xHCI cleanup includes slot-disable safeguards; DMA-visible pages are intentionally retained if the controller cannot safely disable a slot.

## Verification status

These are static source changes only. No successful kernel build, boot, or runtime stress-test result has been observed for the commits above. GitHub Actions currently reports no workflow runs for this repository, so CI cannot yet be treated as a passing signal. Do not mark the concurrency, memory-growth, input-freeze, or launch-latency issues closed until the tests pass on a booted image.
