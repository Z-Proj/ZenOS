/**
 * 
 * @file : /src/cpu/sse_fpu.c
 * @brief : Enables SSE and FPU by setting CR0/CR4 bits and initializing FPU state.
 * 
 * This file is a part of the Zen (ZenOS)
 * Operating System, and is released under
 * the terms of the MIT Licensing : Read
 * LICENSE at the root of the repository.
 * 
 * @copyright (c) 2026
 * @author : Rishies2010
 * 
 */

#include <stdint.h>
#include "sse_fpu.h"
#include "../libk/debug/log.h"
#include "../libk/string.h"
#include "stddef.h"

static uint8_t initial_fpu_state[FPU_STATE_SIZE] __attribute__((aligned(FPU_STATE_ALIGN)));
static int initial_fpu_state_ready = 0;

static inline void load_default_mxcsr(void)
{
    uint32_t mxcsr = 0x1F80;
    asm volatile("ldmxcsr %0" : : "m"(mxcsr) : "memory");
}

void enable_sse_and_fpu(void)
{
    uint64_t cr0, cr4;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1 << 2);
    cr0 |= (1 << 1);
    asm volatile("mov %0, %%cr0" ::"r"(cr0));
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 9);
    cr4 |= (1 << 10);
    asm volatile("mov %0, %%cr4" ::"r"(cr4));
    size_t t;
    asm("clts");
    asm("mov %%cr0, %0" : "=r"(t));
    t &= ~(1 << 2);
    t |= (1 << 1);
    asm("mov %0, %%cr0" ::"r"(t));
    asm("mov %%cr4, %0" : "=r"(t));
    t |= 3 << 9;
    asm("mov %0, %%cr4" ::"r"(t));
    asm volatile("fninit");
    load_default_mxcsr();
    fpu_save_state(initial_fpu_state);
    initial_fpu_state_ready = 1;
}

void fpu_init_state(void *state)
{
    if (!state)
        return;

    if (initial_fpu_state_ready) {
        memcpy(state, initial_fpu_state, FPU_STATE_SIZE);
        return;
    }

    memset(state, 0, FPU_STATE_SIZE);
}

void fpu_save_state(void *state)
{
    if (!state)
        return;
    asm volatile("fxsave64 (%0)" : : "r"(state) : "memory");
}

void fpu_restore_state(const void *state)
{
    if (!state)
        return;
    asm volatile("fxrstor64 (%0)" : : "r"(state) : "memory");
}
