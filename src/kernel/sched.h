#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>
#include "../cpu/isr.h"

#define TASK_STACK_SIZE 8192
#define TIME_SLICE 4

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
    struct task *next;
} task_t;

void sched_init(void);
void sched_start(void);
task_t *task_create(void (*entry)(void), const char *name);
task_t *task_create_user(void (*entry)(void), const char *name);
void sched_yield(void);
void sched_tick(void);
task_t *sched_current_task(void);
extern void task_switch(registers_t *old_regs, registers_t *new_regs);

#endif