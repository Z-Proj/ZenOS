#ifndef _ZENOS_SEMAPHORE_H
#define _ZENOS_SEMAPHORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <errno.h>
#include <stdint.h>

typedef volatile int sem_t;

#define SEM_FAILED ((sem_t *)-1)

int    sem_init    (sem_t *sem, int pshared, unsigned int value);
int    sem_destroy (sem_t *sem);
int    sem_wait    (sem_t *sem);
int    sem_trywait (sem_t *sem);
int    sem_post    (sem_t *sem);
int    sem_getvalue(sem_t *sem, int *val);
sem_t *sem_open    (const char *name, int oflag, ...);
int    sem_close   (sem_t *sem);
int    sem_unlink  (const char *name);

#ifdef __cplusplus
}
#endif

#endif
