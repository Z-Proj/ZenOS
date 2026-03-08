#include "ff.h"

#if FF_USE_LFN == 3
#include <stdlib.h>
void* ff_memalloc(UINT msize) { return malloc((size_t)msize); }
void  ff_memfree(void* mblock) { free(mblock); }
#endif

#if FF_FS_REENTRANT

#include <stdint.h>
#include "../../../kernel/sched.h"

static volatile int _ff_mutex[FF_VOLUMES + 1];

int ff_mutex_create(int vol)
{
    _ff_mutex[vol] = 0;
    return 1;
}

void ff_mutex_delete(int vol)
{
    (void)vol;
}

int ff_mutex_take(int vol)
{
    while (__sync_lock_test_and_set(&_ff_mutex[vol], 1))
        sched_yield();
    return 1;
}

void ff_mutex_give(int vol)
{
    __sync_lock_release(&_ff_mutex[vol]);
}

#endif