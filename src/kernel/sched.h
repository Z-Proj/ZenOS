#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>
typedef int64_t pid_t;
#include "../cpu/isr.h"
#include "../libk/core/mem.h"
#include "../libk/core/fd.h"
#include "../libk/core/syscall.h"
#include "signal.h"

#define TASK_STACK_SIZE (256 * 1024)
#define TIME_SLICE 2
#define MAX_TASKS 2048
#define SCHED_IPI_VECTOR IRQ3
#define TASK_NO_PARENT UINT64_MAX

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
    int is_idle_task;
    page_table_t *pml4;
    uint64_t heap_brk;
    uint64_t mmap_base;
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

    uint64_t futex_wait_addr;
    int futex_woken;
    zen_sigaction_t sighandlers[NSIG];
    uint32_t sig_pending;
    uint32_t sig_mask;
    uint64_t sig_trampoline;
    uint64_t user_fs_base;
    uint64_t user_gs_base;
    int32_t pinned_cpu;
    int32_t running_cpu;
    int32_t last_cpu;
    char cwd[256];
    struct task *next;
} task_t;

void sched_init(void);
void sched_start(void);
void sched_ap_entry(void);
task_t *task_create(void (*entry)(void), const char *name);
task_t *task_create_user(void (*entry)(void), const char *name, page_table_t *pml4, uint64_t user_rsp, int argc, char **argv, char **envp);
void sched_yield(void);
void sched_tick(registers_t *irq_regs);
task_t *sched_current_task(void);
task_t *sched_get_task_list(void);
uint32_t sched_list_tasks(task_info_t *infos, uint32_t max_count);
int sched_futex_wait(volatile uint32_t *uaddr, uint32_t expected);
uint32_t sched_futex_wake(volatile uint32_t *uaddr, uint32_t count);
int sched_kill(uint64_t pid);
int sched_wait_pid(int64_t pid, int *status, int options);
pid_t sched_fork(uint64_t syscall_frame_ptr);
int sched_signal(uint64_t pid, int sig);
void sched_set_active_pml4(page_table_t *pml4);
extern void task_switch(registers_t *old_regs, registers_t *new_regs);

#endif
