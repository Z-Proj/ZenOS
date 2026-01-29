#include <stdint.h>
#include "sse_fpu.h"
#include "../libk/debug/log.h"
#include "stddef.h"
void enable_sse_and_fpu(void) {
    uint64_t cr0, cr4;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1 << 2);  // Clear EM bit
    cr0 |= (1 << 1);   // Set MP bit
    asm volatile("mov %0, %%cr0" :: "r"(cr0));
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 9);   // Set OSFXSR
    cr4 |= (1 << 10);  // Set OSXMMEXCPT
    asm volatile("mov %0, %%cr4" :: "r"(cr4));
    log("SSE instructions enabled.", 4, 0);
    size_t t;
    asm("clts");
    asm("mov %%cr0, %0" : "=r"(t));
    t &= ~(1 << 2);
    t |= (1 << 1);
    asm("mov %0, %%cr0" :: "r"(t));
    asm("mov %%cr4, %0" : "=r"(t));
    t |= 3 << 9;
    asm("mov %0, %%cr4" :: "r"(t));
    asm("fninit");
    log("Floating Point Unit enabled.", 4, 0);
}