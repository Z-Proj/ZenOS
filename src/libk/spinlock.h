/**
 * 
 * @file : /src/libk/spinlock.h
 * @brief : Spinlock primitives with IRQ save/restore for SMP synchronization.
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

#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <stdint.h>

typedef struct
{
    volatile int locked;
    uint64_t saved_rflags;
} spinlock_t;

void spinlock_init(spinlock_t *lock);
void spinlock_acquire(spinlock_t *lock);
void spinlock_release(spinlock_t *lock);
void spinlock_release_noirq(spinlock_t *lock);
void spinlock_acquire_raw(spinlock_t *lock);
void spinlock_release_raw(spinlock_t *lock);

uint64_t spinlock_acquire_irqsave(spinlock_t *lock);

void spinlock_release_irqrestore(spinlock_t *lock, uint64_t rflags);

#endif
