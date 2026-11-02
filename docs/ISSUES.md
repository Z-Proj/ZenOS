# Current issues and audit notes

## Open runtime issues

- **Settings + Terminal concurrency:** Both were observed running together in the latest user test, so the previous immediate-crash behavior is not currently confirmed. Stress testing with repeated launches, closes, and concurrent I/O is still required.
- **Application-launch stalls and progressive lag:** Reported when launching additional applications. Page-table allocation, syscall range handling and scheduler reaper workload have been improved, but runtime causality and complete resolution are not proven.
- **Terminal input freeze on first launch:** Previously reported; not verified or fixed in this audit.
- **Memory growth:** Empty intermediate page tables are reclaimed. Repeated mmap/munmap, heap growth/shrink, process exit, fork/exec and shared-memory lifecycle tests are still required.
- **Remaining syscall pointer audit:** Read/write buffers, common filesystem paths and metadata, time calls, shared-memory names/results, signal actions/masks, directory entries, process names, prints and logs now use validation/copy helpers. Other handlers still need review, notably ioctl/device-specific buffers, socket object APIs, futex addresses, poll arrays, and several network/system-information calls.
- **Large-page ownership:** Teardown deliberately does not free physical frames behind large user mappings because the VM has no ownership metadata proving those frames belong to the process. This avoids freeing unrelated allocator-owned pages, but an owned large mapping can retain frames until ownership tracking is implemented.

## Fixes committed during the audit

- ea9bb58: unmap_page() reclaims empty PT, PD and PDPT structures bottom-up.
- e5e06d2: brk/sbrk arithmetic and mmap/munmap/mprotect range validation.
- 8eba383: map_page() rolls back newly allocated page-table levels after allocation failure.
- bdb3a37, f8ca9a1, a424c7a, e2e43f7: safe splitting of 2 MiB and 1 GiB mappings while preserving neighboring mappings, PAT and permissions.
- aeb79ff, 6ddfd4a, 92bc47a, 5631d70, 09d3c28, cdce5ff: page-by-page user-memory copy helpers, time syscall copyin/copyout, read/write range validation and scalar copyout.
- 14b423c: shared-memory names and result structures copied safely; failed metadata copyout rolls back mappings and allocation.
- a0cfb90, a57a99a: scheduler reaping is rate-limited and reaps at most one dead process per pass to reduce unbounded scheduler lock hold time.
- c41dc2b, 0ccb693, b5a3215, 47c9d66, d7dfe59: directory/stat/statfs/process metadata and common filesystem paths use safe user copies.
- b412495, 121b6b3, 876c4f6, 829c69f: ELF filename, argv and envp reads validate/copy userspace data; high-half pointers from user tasks are rejected.
- f4bbff1: process exit uses the scheduler's locked abort path; print/log strings are copied before use.
- 1a6d728: large-page physical frames are not freed without proof of ownership.

## Additional audit observations

- Process teardown reaps descriptor tables, sockets, user mappings, special shared-memory references, kernel stacks and page-directory structures. Stress testing remains required.
- free_user_pages() skips the shared-memory and framebuffer PML4 slots; those mappings rely on dedicated reference-counted cleanup.
- PTY reference-count increments and decrements appeared paired in the inspected clone/close paths. No confirmed PTY leak was found in that pass.
- xHCI cleanup includes slot-disable safeguards; DMA-visible pages are intentionally retained if the controller cannot safely disable a slot.

## Verification status

- A GitHub Actions kernel-build workflow was added at .github/workflows/kernel-build.yml.
- Kernel compilation passed on commit 47c9d66475cdd28e7d0e16d48453a6905dfe6521. Later user-copy and lifecycle changes are awaiting their own completed build result.
- No boot or runtime stress-test result has been observed for these changes.
- Do not mark concurrency, memory-growth, input-freeze or launch-latency issues closed until a current image boots and the stress tests pass.
