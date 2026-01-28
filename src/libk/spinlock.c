#include "spinlock.h"
#include "stdint.h"
#include "stddef.h"
#include "debug/serial.h"

void spinlock_init(spinlock_t *lock)
{
    lock->locked = 0;
}

void spinlock_acquire(spinlock_t *lock)
{
    while (__sync_lock_test_and_set(&lock->locked, 1))
    {
        while (lock->locked)
        {
            __asm__ volatile("pause" ::: "memory");
        }
    }
}

void spinlock_release(spinlock_t *lock)
{
    __sync_lock_release(&lock->locked);
}