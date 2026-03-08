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

uint64_t spinlock_acquire_irqsave(spinlock_t *lock);

void spinlock_release_irqrestore(spinlock_t *lock, uint64_t rflags);

#endif