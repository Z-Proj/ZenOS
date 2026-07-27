/**
 * 
 * @file : /src/cpu/sse_fpu.h
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

#ifndef SSE_FPU_H
#define SSE_FPU_H

#include <stddef.h>

#define FPU_STATE_SIZE 512
#define FPU_STATE_ALIGN 16

void enable_sse_and_fpu(void);
void fpu_init_state(void *state);
void fpu_save_state(void *state);
void fpu_restore_state(const void *state);

#endif
