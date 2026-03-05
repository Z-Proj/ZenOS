#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>
typedef int64_t pid_t;
#include "../cpu/isr.h"
#include "../libk/core/mem.h"
#include "../libk/core/fd.h"
#include "signal.h"

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
    fd_table_t *fd_table;
    char **envp;
    uint64_t parent_pid;
    int wait_status;
    int64_t wait_pid_target;
    pid_t wait_result_pid;
    uint8_t waiting_on_pid;
    uint8_t wait_collected;
    zen_sigaction_t sighandlers[NSIG];
    uint32_t sig_pending;
    uint32_t sig_mask;
    uint64_t sig_trampoline;
    char cwd[256];
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
int sched_wait_pid(int64_t pid, int *status);
pid_t sched_fork(uint64_t syscall_frame_ptr);
int sched_signal(uint64_t pid, int sig);
extern void task_switch(registers_t *old_regs, registers_t *new_regs);

#endif
