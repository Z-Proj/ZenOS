#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>
#include "../cpu/isr.h"
#include "../libk/core/mem.h"

#define TASK_STACK_SIZE (256*1024)
#define TIME_SLICE 2
#define MAX_TASKS 32

typedef enum
{
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_DEAD
} task_state_t;

typedef struct task
{
    uint64_t pid;
    char name[64];
    task_state_t state;
    registers_t regs;
    uint64_t kernel_stack;
    uint64_t user_stack;
    uint64_t stack_size;
    uint64_t time_slice_remaining;
    int is_kernel_task;
    page_table_t *pml4;
    uint64_t heap_brk;
    int argc;
    char **argv;
    int exit_code;
    struct task *next;
} task_t;

void sched_init(void);
void sched_start(void);
task_t *task_create(void (*entry)(void), const char *name);
task_t *task_create_user(void (*entry)(void), const char *name, page_table_t *pml4, int argc, char **argv);
void sched_yield(void);
void sched_tick(void);
task_t *sched_current_task(void);
task_t *sched_get_task_list(void);
int sched_kill(uint64_t pid);
int sched_wait_pid(uint64_t pid);
extern void task_switch(registers_t *old_regs, registers_t *new_regs);

#endif
