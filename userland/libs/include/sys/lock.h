#ifndef _ZENOS_SYS_LOCK_H
#define _ZENOS_SYS_LOCK_H

#ifdef __cplusplus
extern "C" {
#endif

struct __lock {
    volatile unsigned int futex;
    volatile int          owner;
    volatile int          depth;
};

typedef struct __lock *_LOCK_T;
#define _LOCK_RECURSIVE_T _LOCK_T

#define __LOCK_INIT(class, lock) \
    struct __lock __lock_##lock = {0, 0, 0}; \
    class _LOCK_T lock = &__lock_##lock

#define __LOCK_INIT_RECURSIVE(class, lock) __LOCK_INIT(class, lock)

void __retarget_lock_init(_LOCK_T *lock);
void __retarget_lock_init_recursive(_LOCK_T *lock);
void __retarget_lock_close(_LOCK_T lock);
void __retarget_lock_close_recursive(_LOCK_T lock);
void __retarget_lock_acquire(_LOCK_T lock);
void __retarget_lock_acquire_recursive(_LOCK_T lock);
int  __retarget_lock_try_acquire(_LOCK_T lock);
int  __retarget_lock_try_acquire_recursive(_LOCK_T lock);
void __retarget_lock_release(_LOCK_T lock);
void __retarget_lock_release_recursive(_LOCK_T lock);

#define __lock_init(lock)                   __retarget_lock_init(&(lock))
#define __lock_init_recursive(lock)         __retarget_lock_init_recursive(&(lock))
#define __lock_close(lock)                  __retarget_lock_close(lock)
#define __lock_close_recursive(lock)        __retarget_lock_close_recursive(lock)
#define __lock_acquire(lock)                __retarget_lock_acquire(lock)
#define __lock_acquire_recursive(lock)      __retarget_lock_acquire_recursive(lock)
#define __lock_try_acquire(lock)            __retarget_lock_try_acquire(lock)
#define __lock_try_acquire_recursive(lock)  __retarget_lock_try_acquire_recursive(lock)
#define __lock_release(lock)                __retarget_lock_release(lock)
#define __lock_release_recursive(lock)      __retarget_lock_release_recursive(lock)

#ifdef __cplusplus
}
#endif

#endif
