#include "spinlock.h"
#include <stdint.h>

void spinlock_init(spinlock_t *lock)
{
    lock->locked = 0;
    lock->saved_rflags = 0;
}

static inline uint64_t _flags_cli(void)
{
    uint64_t rflags;
    __asm__ volatile(
        "pushfq\n\t"
        "pop %0\n\t"
        "cli"
        : "=r"(rflags)
        :
        : "memory");
    return rflags;
}

static inline void _flags_restore(uint64_t rflags)
{
    if (rflags & 0x200)
        __asm__ volatile("sti" ::: "memory");
}

static inline void _spin_wait(spinlock_t *lock)
{
    while (__sync_lock_test_and_set(&lock->locked, 1))
    {
        while (lock->locked)
            __asm__ volatile("pause" ::: "memory");
    }
}

void spinlock_acquire(spinlock_t *lock)
{
    uint64_t rflags = _flags_cli();
    while (__sync_lock_test_and_set(&lock->locked, 1))
    {
        _flags_restore(rflags);
        while (lock->locked)
            __asm__ volatile("pause" ::: "memory");
        rflags = _flags_cli();
    }
    lock->saved_rflags = rflags;
}

void spinlock_release(spinlock_t *lock)
{
    uint64_t rflags = lock->saved_rflags;
    __sync_lock_release(&lock->locked);
    _flags_restore(rflags);
}

void spinlock_release_noirq(spinlock_t *lock)
{
    __sync_lock_release(&lock->locked);
}

uint64_t spinlock_acquire_irqsave(spinlock_t *lock)
{
    uint64_t rflags = _flags_cli();
    _spin_wait(lock);
    return rflags;
}

void spinlock_release_irqrestore(spinlock_t *lock, uint64_t rflags)
{
    __sync_lock_release(&lock->locked);
    _flags_restore(rflags);
}
