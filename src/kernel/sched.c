/**
 * 
 * @file : /src/kernel/sched.c
 * @brief : Round-robin with per-CPU queues, futexes, fork, and signals.
 * 
 * This file is a part of the Zen (ZenOS)
 * Operating System, and is released under
 * the terms of the MIT Licensing : Read
 * LICENSE at the root of the repository.
 * 
 * @copyright (c) 2026
 * @author : Rishies2010
 * 
 */

#include "sched.h"
#include "../libk/core/mem.h"
#include "../libk/core/socket.h"
#include "../libk/string.h"
#include "../libk/debug/log.h"
#include "../libk/spinlock.h"
#include "../cpu/gdt.h"
#include "../cpu/smp.h"
#include "../drv/keyboard.h"
#include "signal.h"

static task_t *task_list_head = NULL;
static uint64_t next_pid = 0;
static uint64_t task_count = 0;
static spinlock_t sched_lock = {0};
static spinlock_t process_lock = {0};
static volatile int scheduler_enabled = 0;
static uint32_t next_user_cpu = 0;

#define USER_SHM_PML4_INDEX ((0x0000500000000000ULL >> 39) & 0x1FF)
#define USER_FB_PML4_INDEX  ((0x0000600000000000ULL >> 39) & 0x1FF)

static void task_entry_wrapper(void);

static inline size_t task_stack_pages(void)
{
    return (TASK_STACK_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
}

static uint64_t alloc_task_kernel_stack(void)
{
    uint64_t phys = alloc_pages(task_stack_pages());
    if (!phys)
        return 0;
    uint64_t virt = phys + KERNEL_VIRT_OFFSET;
    memset((void *)virt, 0, TASK_STACK_SIZE);
    return virt;
}

static void free_task_kernel_stack(uint64_t stack)
{
    if (!stack)
        return;
    free_pages(stack - KERNEL_VIRT_OFFSET, task_stack_pages());
}

typedef struct
{
    task_t *current_task;
    task_t *switching_from;
    registers_t bootstrap_regs;
    page_table_t *active_pml4;
} sched_cpu_state_t;

static sched_cpu_state_t sched_cpu_state[MAX_CPUS];

static sched_cpu_state_t *sched_get_cpu_state(uint32_t cpu_index)
{
    if (cpu_index >= MAX_CPUS)
        return &sched_cpu_state[0];

    return &sched_cpu_state[cpu_index];
}

static sched_cpu_state_t *sched_this_cpu_state(void)
{
    return sched_get_cpu_state(smp_current_cpu_index());
}

static int32_t sched_pick_user_cpu_locked(void)
{
    uint32_t cpu_count = smp_cpu_count();
    if (cpu_count == 0)
        return 0;

    uint32_t cpu = next_user_cpu % cpu_count;
    next_user_cpu = (cpu + 1) % cpu_count;
    return (int32_t)cpu;
}

static task_t *sched_current_task_for_cpu(uint32_t cpu_index)
{
    return sched_get_cpu_state(cpu_index)->current_task;
}

static void sched_ipi_handler(registers_t *regs)
{
    sched_tick(regs);
}

static void idle_task_loop(void)
{
    for (;;)
        asm volatile("sti; hlt" ::: "memory");
}

static page_table_t *fork_clone_user_pml4(page_table_t *src)
{
    if (!src)
        return NULL;

    page_table_t *dst = clone_page_directory(get_kernel_pml4());
    if (!dst)
        return NULL;

    for (int pml4_idx = 0; pml4_idx < 256; pml4_idx++)
    {
        uint64_t src_pml4e = src->entries[pml4_idx];
        if (!(src_pml4e & PAGE_PRESENT))
            continue;

        page_table_t *src_pdpt = (page_table_t *)((src_pml4e & 0xFFFFFFFFFFFFF000ULL) + KERNEL_VIRT_OFFSET);

        for (int pdpt_idx = 0; pdpt_idx < 512; pdpt_idx++)
        {
            uint64_t src_pdpte = src_pdpt->entries[pdpt_idx];
            if (!(src_pdpte & PAGE_PRESENT) || !(src_pdpte & PAGE_USER))
                continue;
            if (src_pdpte & (1ULL << 7))
                goto fail;

            page_table_t *src_pd = (page_table_t *)((src_pdpte & 0xFFFFFFFFFFFFF000ULL) + KERNEL_VIRT_OFFSET);

            for (int pd_idx = 0; pd_idx < 512; pd_idx++)
            {
                uint64_t src_pde = src_pd->entries[pd_idx];
                if (!(src_pde & PAGE_PRESENT) || !(src_pde & PAGE_USER))
                    continue;
                if (src_pde & (1ULL << 7))
                    goto fail;

                page_table_t *src_pt = (page_table_t *)((src_pde & 0xFFFFFFFFFFFFF000ULL) + KERNEL_VIRT_OFFSET);

                for (int pt_idx = 0; pt_idx < 512; pt_idx++)
                {
                    uint64_t src_pte = src_pt->entries[pt_idx];
                    if (!(src_pte & PAGE_PRESENT) || !(src_pte & PAGE_USER))
                        continue;

                    uint64_t virt = ((uint64_t)pml4_idx << 39)
                                  | ((uint64_t)pdpt_idx << 30)
                                  | ((uint64_t)pd_idx << 21)
                                  | ((uint64_t)pt_idx << 12);
                    uint64_t src_phys = src_pte & 0xFFFFFFFFFFFFF000ULL;
                    uint64_t flags = src_pte & ~0x000FFFFFFFFFF000ULL;

                    if (pml4_idx == USER_SHM_PML4_INDEX || pml4_idx == USER_FB_PML4_INDEX)
                    {
                        map_page(dst, virt, src_phys, flags);
                        if (virt_to_phys(dst, virt) != src_phys)
                            goto fail;
                        continue;
                    }

                    uint64_t dst_phys = alloc_page();
                    if (!dst_phys)
                        goto fail;

                    memcpy((void *)(dst_phys + KERNEL_VIRT_OFFSET),
                           (void *)(src_phys + KERNEL_VIRT_OFFSET),
                           PAGE_SIZE);
                    map_page(dst, virt, dst_phys, flags);
                    if (virt_to_phys(dst, virt) != dst_phys)
                    {
                        free_page(dst_phys);
                        goto fail;
                    }
                }
            }
        }
    }

    return dst;

fail:
    free_user_pages(dst);
    free_page_directory(dst);
    return NULL;
}

static void insert_task_locked(task_t *task)
{
    if (!task_list_head)
    {
        task_list_head = task;
        task->next = task;
    }
    else if (task_list_head->next == task_list_head)
    {
        task_list_head->next = task;
        task->next = task_list_head;
    }
    else
    {
        task_t *old_next = task_list_head->next;
        task_list_head->next = task;
        task->next = old_next;
    }
}

static task_t *create_kernel_task_locked(void (*entry)(void), const char *name, int pinned_cpu, int is_idle_task)
{
    task_t *task = (task_t *)kmalloc(sizeof(task_t));
    if (!task)
        return NULL;

    memset(task, 0, sizeof(task_t));
    fpu_init_state(task->fpu_state);
    task->pid = next_pid++;
    strncpy(task->name, name, 63);
    task->name[63] = '\0';
    task->state = TASK_READY;
    task->time_slice_remaining = TIME_SLICE;
    task->stack_size = TASK_STACK_SIZE;
    task->is_kernel_task = 1;
    task->is_idle_task = is_idle_task;
    task->owns_user_pages = 0;
    task->pml4 = get_kernel_pml4();
    task->parent_pid = TASK_NO_PARENT;
    task->wait_status = 0;
    task->wait_pid_target = -1;
    task->wait_result_pid = -1;
    task->waiting_on_pid = 0;
    task->wait_collected = 0;
    task->pinned_cpu = pinned_cpu;
    task->running_cpu = -1;
    task->last_cpu = -1;

    task->kernel_stack = alloc_task_kernel_stack();
    if (!task->kernel_stack)
    {
        kfree(task);
        return NULL;
    }
    task->user_stack = 0;

    memset(&task->regs, 0, sizeof(registers_t));
    uint64_t stack_top = task->kernel_stack + TASK_STACK_SIZE;
    stack_top &= ~0xFULL;

    task->regs.rip = (uint64_t)task_entry_wrapper;
    task->regs.rbx = (uint64_t)entry;
    task->regs.rbp = stack_top;
    task->regs.userrsp = stack_top;
    task->regs.rflags = 0x202;
    task->regs.cs = 0x08;
    task->regs.ss = 0x10;
    task->regs.ds = 0x10;

    insert_task_locked(task);
    task_count++;
    return task;
}

static task_t *find_task_by_pid_locked(uint64_t pid)
{
    if (!task_list_head)
        return NULL;

    task_t *iter = task_list_head;
    do
    {
        if (iter->pid == pid)
            return iter;
        iter = iter->next;
    } while (iter != task_list_head);

    return NULL;
}

static int task_is_switching_from_locked(task_t *task)
{
    if (!task)
        return 0;

    uint32_t cpu_count = smp_cpu_count();
    if (cpu_count == 0)
        cpu_count = 1;
    if (cpu_count > MAX_CPUS)
        cpu_count = MAX_CPUS;

    for (uint32_t i = 0; i < cpu_count; i++)
    {
        if (sched_cpu_state[i].switching_from == task)
            return 1;
    }

    return 0;
}

void task_exit(void)
{
    task_t *current = sched_current_task();
    if (!current) // this shouldnt ever happen
        log("A Task exit function returned.", 0, 1);

    __asm__ volatile("cli" ::: "memory");
    current->state = TASK_DEAD;
    log("Task %s exited.", 1, 0, current->name);
    sched_yield();
    log("A Task exit function returned.", 0, 1);
}

static void task_entry_wrapper(void)
{
    task_t *current = sched_current_task();
    if (!current)
    {
        asm volatile("cli; hlt");
        while (1)
            ;
    }
    asm volatile("sti");

    void (*entry)(void) = (void (*)(void))current->regs.rbx;
    if (!entry)
    {
        asm volatile("cli; hlt");
        while (1)
            ;
    }
    entry();
    task_exit();
}

void sched_init(void)
{
    spinlock_init(&sched_lock);
    spinlock_init(&process_lock);
    task_list_head = NULL;
    next_pid = 0;
    task_count = 0;
    scheduler_enabled = 0;
    next_user_cpu = 0;
    for (uint32_t i = 0; i < MAX_CPUS; i++)
    {
        memset(&sched_cpu_state[i], 0, sizeof(sched_cpu_state[i]));
        sched_cpu_state[i].active_pml4 = get_kernel_pml4();
        tss_t *tss = gdt_get_tss(i);
        if (tss)
            tss->rsp0 = 0;
    }
    register_interrupt_handler(SCHED_IPI_VECTOR, sched_ipi_handler, "Scheduler IPI");
    log("Scheduler initialized.", 4, 0);
}

void sched_start(void)
{
    spinlock_acquire(&sched_lock);
    for (uint32_t i = 0; i < smp_cpu_count(); i++)
    {
        int has_idle = 0;
        task_t *iter = task_list_head;
        if (iter)
        {
            do
            {
                if (iter->is_kernel_task && iter->pinned_cpu == (int32_t)i && strncmp(iter->name, "Idle", 4) == 0)
                {
                    has_idle = 1;
                    break;
                }
                iter = iter->next;
            } while (iter != task_list_head);
        }

        if (!has_idle) // Why do I check if idle already exists, when the system is literally just booted? Idk, edge cases of apocalypse.
        {
            char idle_name[16];
            snprintf(idle_name, sizeof(idle_name), "Idle-%u", i);
            if (!create_kernel_task_locked(idle_task_loop, idle_name, (int)i, 1))
            {
                spinlock_release(&sched_lock);
                return;
            }
        }
    }
    spinlock_release(&sched_lock);

    log("Scheduler enabled.", 4, 0);
    scheduler_enabled = 1;
    sched_yield();
}

void sched_ap_entry(void)
{
    for (;;)
    {
        if (scheduler_enabled && !sched_current_task())
            sched_yield();
        asm volatile("sti; hlt" ::: "memory");
    }
}

task_t *task_create_user_from_parent(void (*entry)(void), const char *name, page_table_t *pml4, uint64_t user_rsp, int argc, char **argv, char **envp, task_t *parent)
{
    spinlock_acquire(&sched_lock);
    if (task_count >= MAX_TASKS)
    {
        spinlock_release(&sched_lock);
        return NULL;
    }
    task_t *task = (task_t *)kmalloc(sizeof(task_t));
    if (!task)
    {
        spinlock_release(&sched_lock);
        return NULL;
    }
    memset(task, 0, sizeof(task_t));
    fpu_init_state(task->fpu_state);
    task->pid = next_pid++;
    strncpy(task->name, name, 63);
    task->name[63] = '\0';
    task->state = TASK_READY;
    task->time_slice_remaining = TIME_SLICE;
    task->stack_size = TASK_STACK_SIZE;
    task->is_kernel_task = 0;
    task->owns_user_pages = 1;
    task->pml4 = pml4;
    task->heap_brk = USER_HEAP_START;
    task->mmap_base = 0x0000200000000000ULL;
    task->argc = argc;
    task->argv = argv;
    task->envp = envp;
    task->parent_pid = parent ? parent->pid : TASK_NO_PARENT;
    task->wait_status = 0;
    task->wait_pid_target = -1;
    task->wait_result_pid = -1;
    task->waiting_on_pid = 0;
    task->wait_collected = 0;
    task->userspace_faults = 0;
    task->last_userspace_fault = 0;
    if (parent && parent->fd_table)
    {
        task->fd_table = fd_table_clone(parent->fd_table);
    }
    else
    {
        task->fd_table = fd_table_alloc();
    }
    if (!task->fd_table)
    {
        kfree(task);
        spinlock_release(&sched_lock);
        return NULL;
    }
    if (parent)
    {
        strncpy(task->cwd, parent->cwd, sizeof(task->cwd) - 1);
        task->cwd[sizeof(task->cwd) - 1] = '\0';
    }
    else
    {
        strncpy(task->cwd, "/mnt/drv0", sizeof(task->cwd) - 1);
        task->cwd[sizeof(task->cwd) - 1] = '\0';
    }
    memset(task->sighandlers, 0, sizeof(task->sighandlers));
    task->sig_pending = 0;
    task->sig_mask = 0;
    task->pinned_cpu = sched_pick_user_cpu_locked();
    task->running_cpu = -1;
    task->last_cpu = -1;

    task->kernel_stack = alloc_task_kernel_stack();
    if (!task->kernel_stack)
    {
        if (task->fd_table)
            fd_table_free(task->fd_table);
        kfree(task);
        spinlock_release(&sched_lock);
        return NULL;
    }
    task->user_stack = USER_STACK_BASE;

    memset(&task->regs, 0, sizeof(registers_t));
    task->regs.rip = (uint64_t)entry;
    task->regs.rbp = user_rsp;
    task->regs.userrsp = user_rsp;
    task->regs.rflags = 0x202;
    task->regs.cs = 0x23;
    task->regs.ss = 0x1B;
    task->regs.ds = 0x1B;

    insert_task_locked(task);
    log("Created user task: %s (PID %d)", 1, 0, name, task->pid);
    task_count++;

    spinlock_release(&sched_lock);
    return task;
}

task_t *task_create_user(void (*entry)(void), const char *name, page_table_t *pml4, uint64_t user_rsp, int argc, char **argv, char **envp)
{
    return task_create_user_from_parent(entry, name, pml4, user_rsp, argc, argv, envp, NULL);
}

task_t *task_create(void (*entry)(void), const char *name)
{
    spinlock_acquire(&sched_lock);
    if (task_count >= MAX_TASKS)
    {
        spinlock_release(&sched_lock);
        return NULL;
    }
    task_t *task = create_kernel_task_locked(entry, name, -1, 0);
    if (!task)
    {
        spinlock_release(&sched_lock);
        return NULL;
    }
    log("Created kernel task: %s (PID %d)", 1, 0, name, task->pid);
    spinlock_release(&sched_lock);
    return task;
}

static void reap_dead_tasks(void)
{
    if (!task_list_head)
        return;

    task_t *iter = task_list_head;
    task_t *prev = NULL;
    task_t *start = task_list_head;
    // is this function too heavy for each tick
    do
    {
        task_t *next = iter->next;

        if (iter->state == TASK_DEAD && iter->running_cpu < 0 &&
            !task_is_switching_from_locked(iter))
        {
            task_t *parent = NULL;
            if (iter->parent_pid != TASK_NO_PARENT)
                parent = find_task_by_pid_locked(iter->parent_pid);

            if (parent && parent->state != TASK_DEAD && !iter->wait_collected)
            {
                if (parent->waiting_on_pid)
                {
                    if (parent->wait_pid_target == -1 ||
                        parent->wait_pid_target == (int64_t)iter->pid)
                    {
                        parent->wait_status = (iter->exit_code & 0xff) << 8;
                        parent->wait_result_pid = (pid_t)iter->pid;
                        parent->wait_pid_target = -1;
                        parent->waiting_on_pid = 0;
                        if (parent->state == TASK_BLOCKED)
                            parent->state = TASK_READY;
                        iter->wait_collected = 1;
                    }
                }

                if (!iter->wait_collected)
                {
                    prev = iter;
                    iter = next;
                    continue;
                }
            }
            else
            {
                iter->wait_collected = 1;
            }

            if (iter->fd_table)
            {
                fd_table_free(iter->fd_table);
                iter->fd_table = NULL;
            }
            socket_close_owned_by(iter->pid);
            if (iter->kernel_stack)
            {
                free_task_kernel_stack(iter->kernel_stack);
            }

            if (!iter->is_kernel_task && iter->pml4 && iter->pml4 != get_kernel_pml4())
                syscall_release_special_user_mappings(iter->pml4);

            if (!iter->is_kernel_task && iter->owns_user_pages)
            {
                free_user_pages(iter->pml4);
            }
            else if (iter->user_stack && !iter->is_kernel_task)
            {
                size_t stack_pages = (TASK_STACK_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
                for (size_t i = 0; i < stack_pages; i++)
                {
                    uint64_t phys = virt_to_phys(iter->pml4, iter->user_stack + i * PAGE_SIZE);
                    if (phys)
                    {
                        free_page(phys);
                        unmap_page(iter->pml4, iter->user_stack + i * PAGE_SIZE);
                    }
                }
            }

            if (!iter->is_kernel_task && iter->pml4 && iter->pml4 != get_kernel_pml4())
            {
                free_page_directory(iter->pml4);
            }

            if (iter == task_list_head)
            {
                if (task_list_head->next == task_list_head)
                {
                    task_list_head = NULL;
                    if (task_count > 0)
                        task_count--;
                    kfree(iter);
                    return;
                }
                task_list_head = next;

                task_t *last = task_list_head;
                while (last->next != iter)
                    last = last->next;
                last->next = task_list_head;

                if (task_count > 0)
                    task_count--;
                kfree(iter);
                iter = task_list_head;
                start = task_list_head;
                continue;
            }

            if (prev)
                prev->next = next;

            if (task_count > 0)
                task_count--;
            kfree(iter);
            iter = next;
            continue;
        }

        prev = iter;
        iter = next;
    } while (iter != start);
}

static task_t *find_next_task_locked(uint32_t cpu_index, int allow_idle_task)
{
    if (!task_list_head)
        return NULL;

    task_t *current = sched_current_task_for_cpu(cpu_index);
    task_t *start = current && current->next ? current->next : task_list_head;
    task_t *iter = start;

    do
    {
        if (iter->state == TASK_READY &&
            (allow_idle_task || !iter->is_idle_task) &&
            (iter->pinned_cpu < 0 || iter->pinned_cpu == (int32_t)cpu_index) &&
            (iter->running_cpu < 0 || iter->running_cpu == (int32_t)cpu_index))
        {
            return iter;
        }
        iter = iter->next;
    } while (iter != start);

    return NULL;
}

static task_t *get_next_task_locked(uint32_t cpu_index)
{
    task_t *task = find_next_task_locked(cpu_index, 0);
    if (task)
        return task;

    return find_next_task_locked(cpu_index, 1);
}

static void sched_switch_locked(sched_cpu_state_t *cpu_state, task_t *old_task, task_t *new_task, uint32_t cpu_index, uint64_t rflags, registers_t *save_regs)
{
    new_task->state = TASK_RUNNING;
    new_task->running_cpu = (int32_t)cpu_index;
    new_task->last_cpu = (int32_t)cpu_index;
    new_task->time_slice_remaining = TIME_SLICE;

    tss_t *tss = gdt_get_tss(cpu_index);
    if (tss)
        tss->rsp0 = new_task->kernel_stack + TASK_STACK_SIZE;

    page_table_t *active_pml4 = cpu_state->active_pml4 ? cpu_state->active_pml4 : get_kernel_pml4();
    if (new_task->pml4 != active_pml4)
        switch_page_directory(new_task->pml4);

    cpu_state->active_pml4 = new_task->pml4;
    cpu_state->current_task = new_task;
    cpu_state->switching_from = (old_task && old_task != new_task) ? old_task : NULL;

    if (!new_task->is_kernel_task && (new_task->regs.cs & 3))
        syscall_prepare_user_return(new_task->user_gs_base);
    else if (!new_task->is_kernel_task)
        syscall_prepare_sysret_return(new_task->user_gs_base);

    if (old_task && old_task != new_task)
        fpu_save_state(old_task->fpu_state);
    fpu_restore_state(new_task->fpu_state);

    spinlock_release_noirq(&sched_lock);

    task_switch(save_regs, &new_task->regs);

    if (rflags & 0x200)
        __asm__ volatile("sti" ::: "memory");
}

void sched_yield(void)
{
    if (!scheduler_enabled)
        return;

    uint32_t cpu_index = smp_current_cpu_index();
    sched_cpu_state_t *cpu_state = sched_get_cpu_state(cpu_index);
    uint64_t rflags = spinlock_acquire_irqsave(&sched_lock);

    if (cpu_state->switching_from && cpu_state->switching_from != cpu_state->current_task)
        cpu_state->switching_from = NULL;

    reap_dead_tasks();

    task_t *old_task = cpu_state->current_task;
    if (old_task && old_task->state == TASK_RUNNING)
    {
        old_task->state = TASK_READY;
        old_task->running_cpu = -1;
        old_task->time_slice_remaining = TIME_SLICE;
    }
    else if (old_task && old_task->state != TASK_DEAD &&
             old_task->running_cpu == (int32_t)cpu_index)
    {
        old_task->running_cpu = -1;
    }

    task_t *new_task = get_next_task_locked(cpu_index);

    if (!new_task)
    {
        if (old_task && old_task->state == TASK_DEAD)
        {
            spinlock_release_irqrestore(&sched_lock, rflags);
            for (;;)
                __asm__ volatile("sti; hlt" ::: "memory");
        }
        if (old_task && old_task->state == TASK_READY)
        {
            old_task->state = TASK_RUNNING;
            old_task->running_cpu = (int32_t)cpu_index;
        }
        spinlock_release_irqrestore(&sched_lock, rflags);
        return;
    }

    if (new_task == old_task)
    {
        old_task->state = TASK_RUNNING;
        old_task->running_cpu = (int32_t)cpu_index;
        spinlock_release_irqrestore(&sched_lock, rflags);
        return;
    }
    if (old_task && old_task->state == TASK_DEAD &&
        old_task->running_cpu == (int32_t)cpu_index)
    {
        old_task->running_cpu = -1;
    }
    sched_switch_locked(cpu_state, old_task, new_task, cpu_index, rflags,
                        old_task ? &old_task->regs : &cpu_state->bootstrap_regs);
}

void sched_abort_current(int exit_code)
{
    task_t *current = sched_current_task();
    if (current)
    {
        uint64_t rflags = spinlock_acquire_irqsave(&sched_lock);
        current->exit_code = exit_code;
        current->state = TASK_DEAD;
        spinlock_release_irqrestore(&sched_lock, rflags);
    }

    for (;;)
    {
        sched_yield();
        __asm__ volatile("sti; hlt" ::: "memory");
    }
}

void sched_tick(registers_t *irq_regs)
{
    task_t *current = sched_current_task();
    if (!scheduler_enabled || !current)
        return;

    if (current->time_slice_remaining > 0)
        current->time_slice_remaining--;

    if (current->time_slice_remaining == 0)
    {
        if (!irq_regs || !current)
        {
            sched_yield();
            return;
        }

        if (!(irq_regs->cs & 3))
        {
            if (current->is_kernel_task)
            {
                sched_yield();
                return;
            }
            current->time_slice_remaining = TIME_SLICE;
            return;
        }

        if (current->is_kernel_task)
        {
            sched_yield();
            return;
        }

        uint32_t cpu_index = smp_current_cpu_index();
        sched_cpu_state_t *cpu_state = sched_get_cpu_state(cpu_index);
        uint64_t rflags = spinlock_acquire_irqsave(&sched_lock);

        if (cpu_state->switching_from && cpu_state->switching_from != cpu_state->current_task)
            cpu_state->switching_from = NULL;

        reap_dead_tasks();

        current->regs = *irq_regs;

        if (current->state == TASK_RUNNING)
        {
            current->state = TASK_READY;
            current->running_cpu = -1;
            current->time_slice_remaining = TIME_SLICE;
        }

        task_t *new_task = get_next_task_locked(cpu_index);

        if (!new_task)
        {
            if (current->state == TASK_READY)
            {
                current->state = TASK_RUNNING;
                current->running_cpu = (int32_t)cpu_index;
            }
            spinlock_release_irqrestore(&sched_lock, rflags);
            return;
        }

        if (new_task == current)
        {
            current->state = TASK_RUNNING;
            current->running_cpu = (int32_t)cpu_index;
            spinlock_release_irqrestore(&sched_lock, rflags);
            return;
        }

        sched_switch_locked(cpu_state, current, new_task, cpu_index, rflags, &cpu_state->bootstrap_regs);
    }
}

task_t *sched_current_task(void)
{
    return sched_this_cpu_state()->current_task;
}

void sched_set_active_pml4(page_table_t *pml4)
{
    sched_this_cpu_state()->active_pml4 = pml4 ? pml4 : get_kernel_pml4();
}

task_t *sched_get_task_list(void)
{
    return task_list_head;
}

uint32_t sched_list_tasks(task_info_t *infos, uint32_t max_count)
{
    if (!infos || max_count == 0)
        return 0;

    uint64_t rflags = spinlock_acquire_irqsave(&sched_lock);
    if (!task_list_head)
    {
        spinlock_release_irqrestore(&sched_lock, rflags);
        return 0;
    }

    uint32_t count = 0;
    task_t *iter = task_list_head;
    do
    {
        if (count >= max_count)
            break;

        if (iter->state != TASK_DEAD)
        {
            infos[count].pid = iter->pid;
            strncpy(infos[count].name, iter->name, sizeof(infos[count].name) - 1);
            infos[count].name[sizeof(infos[count].name) - 1] = '\0';
            count++;
        }

        iter = iter->next;
    } while (iter != task_list_head);

    spinlock_release_irqrestore(&sched_lock, rflags);
    return count;
}

int sched_futex_wait(volatile uint32_t *uaddr, uint32_t expected)
{
    if (!uaddr)
        return -22;

    if (__atomic_load_n(uaddr, __ATOMIC_SEQ_CST) != expected)
        return -11;

    task_t *current = sched_current_task();
    if (!current)
        return -22;

    uint64_t rflags = spinlock_acquire_irqsave(&sched_lock);
    if (__atomic_load_n(uaddr, __ATOMIC_SEQ_CST) != expected)
    {
        spinlock_release_irqrestore(&sched_lock, rflags);
        return -11;
    }

    current->futex_wait_addr = (uint64_t)(uintptr_t)uaddr;
    current->futex_woken = 0;
    current->state = TASK_BLOCKED;
    spinlock_release_irqrestore(&sched_lock, rflags);

    sched_yield();

    rflags = spinlock_acquire_irqsave(&sched_lock);
    int was_woken = current->futex_woken;
    current->futex_wait_addr = 0;
    current->futex_woken = 0;
    spinlock_release_irqrestore(&sched_lock, rflags);

    return was_woken ? 0 : -4;
}

uint32_t sched_futex_wake(volatile uint32_t *uaddr, uint32_t count)
{
    if (!uaddr || count == 0)
        return 0;

    uint64_t wake_addr = (uint64_t)(uintptr_t)uaddr;
    uint64_t rflags = spinlock_acquire_irqsave(&sched_lock);
    if (!task_list_head)
    {
        spinlock_release_irqrestore(&sched_lock, rflags);
        return 0;
    }

    uint32_t woken = 0;
    task_t *iter = task_list_head;
    do
    {
        if (iter->state == TASK_BLOCKED && iter->futex_wait_addr == wake_addr)
        {
            iter->futex_woken = 1;
            iter->futex_wait_addr = 0;
            iter->state = TASK_READY;
            woken++;
            if (woken >= count)
                break;
        }

        iter = iter->next;
    } while (iter != task_list_head);

    spinlock_release_irqrestore(&sched_lock, rflags);
    return woken;
}

int sched_kill(uint64_t pid)
{
    if (!task_list_head)
        return -1;

    spinlock_acquire(&sched_lock);

    task_t *t = task_list_head;
    do
    {
        if (t->pid == pid)
        {
            if (t->state == TASK_DEAD)
            {
                spinlock_release(&sched_lock);
                return -1;
            }
            t->state = TASK_DEAD;
            spinlock_release(&sched_lock);
            if (t == sched_current_task())
                sched_yield();
            return 0;
        }
        t = t->next;
    } while (t != task_list_head);

    spinlock_release(&sched_lock);
    return -1;
}

static int write_wait_status(task_t *task, int *status_ptr, int value)
{
    if (!status_ptr)
        return 0;
    if (!task || !task->pml4)
        return -1;

    uint64_t uaddr = (uint64_t)status_ptr;
    uint64_t page0 = uaddr & ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t off0 = uaddr & (PAGE_SIZE - 1);
    uint64_t phys0 = virt_to_phys(task->pml4, page0);
    if (!phys0)
        return -1;

    uint8_t bytes[sizeof(int)];
    memcpy(bytes, &value, sizeof(int));

    uint8_t *dst0 = (uint8_t *)(phys0 + KERNEL_VIRT_OFFSET + off0);
    size_t first = PAGE_SIZE - off0;
    if (first >= sizeof(int))
    {
        memcpy(dst0, bytes, sizeof(int));
        return 0;
    }

    memcpy(dst0, bytes, first);

    uint64_t page1 = page0 + PAGE_SIZE;
    uint64_t phys1 = virt_to_phys(task->pml4, page1);
    if (!phys1)
        return -1;

    uint8_t *dst1 = (uint8_t *)(phys1 + KERNEL_VIRT_OFFSET);
    memcpy(dst1, bytes + first, sizeof(int) - first);
    return 0;
}

int sched_wait_pid(int64_t pid, int *status, int options)
{
#define WNOHANG 1
    task_t *current = sched_current_task();
    if (!current)
        return -1;
    if (write_wait_status(current, status, 0) < 0)
        return -1;

    for (;;)
    {
        spinlock_acquire(&sched_lock);

        if (!task_list_head)
        {
            spinlock_release(&sched_lock);
            return -1;
        }

        task_t *dead_match = NULL;
        int has_child_match = 0;

        task_t *iter = task_list_head;
        do
        {
            if (iter == current)
            {
                iter = iter->next;
                continue;
            }

            if (iter->parent_pid != current->pid)
            {
                iter = iter->next;
                continue;
            }

            if (pid != -1 && iter->pid != (uint64_t)pid)
            {
                iter = iter->next;
                continue;
            }

            has_child_match = 1;
            if (iter->state == TASK_DEAD)
            {
                dead_match = iter;
                break;
            }

            iter = iter->next;
        } while (iter != task_list_head);

        if (dead_match)
        {
            dead_match->wait_collected = 1;
            current->wait_status = (dead_match->exit_code & 0xff) << 8;
            current->wait_result_pid = (pid_t)dead_match->pid;
            current->wait_pid_target = -1;
            current->waiting_on_pid = 0;

            int ret = (int)dead_match->pid;
            int st = current->wait_status;
            spinlock_release(&sched_lock);

            if (write_wait_status(current, status, st) < 0)
                return -1;
            return ret;
        }

        if (!has_child_match)
        {
            current->wait_pid_target = -1;
            current->wait_result_pid = -1;
            current->waiting_on_pid = 0;
            spinlock_release(&sched_lock);
            return -1;
        }

        current->wait_pid_target = pid;
        current->wait_result_pid = -1;
        current->wait_status = 0;
        current->waiting_on_pid = 1;

        if (options & WNOHANG)
        {
            current->waiting_on_pid = 0;
            spinlock_release(&sched_lock);
            return 0;
        }

        current->state = TASK_BLOCKED;

        spinlock_release(&sched_lock);
        sched_yield();

        spinlock_acquire(&sched_lock);
        if (current->wait_result_pid >= 0)
        {
            int ret = (int)current->wait_result_pid;
            int st = current->wait_status;
            current->wait_result_pid = -1;
            current->wait_pid_target = -1;
            current->waiting_on_pid = 0;
            spinlock_release(&sched_lock);

            if (write_wait_status(current, status, st) < 0)
                return -1;
            return ret;
        }
        spinlock_release(&sched_lock);
    }
}
typedef struct syscall_fork_frame
{
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t user_rflags;
    uint64_t user_rip;
    uint64_t user_rsp;
} syscall_fork_frame_t;

pid_t sched_fork(uint64_t syscall_frame_ptr)
{
    task_t *current = sched_current_task();
    spinlock_acquire_raw(&process_lock);
    if (!current || syscall_frame_ptr == 0)
    {
        spinlock_release_raw(&process_lock);
        return -1;
    }

    task_t *parent = current;
    syscall_fork_frame_t *frame = (syscall_fork_frame_t *)syscall_frame_ptr;

    task_t *child = (task_t *)kmalloc(sizeof(task_t));
    if (!child)
    {
        spinlock_release_raw(&process_lock);
        return -1;
    }
    memset(child, 0, sizeof(task_t));

    child->parent_pid = parent->pid;
    child->state = TASK_READY;
    child->time_slice_remaining = TIME_SLICE;
    child->stack_size = parent->stack_size;
    child->is_kernel_task = 0;
    child->owns_user_pages = 1;
    child->heap_brk = parent->heap_brk;
    child->mmap_base = parent->mmap_base;
    child->argc = parent->argc;
    child->argv = parent->argv;
    child->envp = parent->envp;
    child->exit_code = 0;
    child->wait_status = 0;
    child->wait_pid_target = -1;
    child->wait_result_pid = -1;
    child->waiting_on_pid = 0;
    child->wait_collected = 0;
    child->userspace_faults = 0;
    child->last_userspace_fault = 0;
    child->pinned_cpu = sched_pick_user_cpu_locked();
    child->running_cpu = -1;
    child->last_cpu = -1;
    fpu_save_state(child->fpu_state);
    memcpy(child->sighandlers, parent->sighandlers, sizeof(parent->sighandlers));
    child->sig_pending = 0;
    child->sig_mask = parent->sig_mask;
    child->sig_trampoline = parent->sig_trampoline;
    child->user_fs_base = parent->user_fs_base;
    child->user_gs_base = parent->user_gs_base;
    strncpy(child->cwd, parent->cwd, sizeof(child->cwd) - 1);
    child->cwd[sizeof(child->cwd) - 1] = '\0';

    strncpy(child->name, parent->name, 63);
    child->name[63] = '\0';

    child->pml4 = fork_clone_user_pml4(parent->pml4);
    if (!child->pml4)
    {
        kfree(child);
        spinlock_release_raw(&process_lock);
        return -1;
    }
    syscall_retain_special_user_mappings(child->pml4);

    child->kernel_stack = alloc_task_kernel_stack();
    if (!child->kernel_stack)
    {
        syscall_release_special_user_mappings(child->pml4);
        free_user_pages(child->pml4);
        free_page_directory(child->pml4);
        kfree(child);
        spinlock_release_raw(&process_lock);
        return -1;
    }

    child->fd_table = fd_table_clone(parent->fd_table);
    if (!child->fd_table)
    {
        free_task_kernel_stack(child->kernel_stack);
        syscall_release_special_user_mappings(child->pml4);
        free_user_pages(child->pml4);
        free_page_directory(child->pml4);
        kfree(child);
        spinlock_release_raw(&process_lock);
        return -1;
    }

    child->user_stack = parent->user_stack;

    child->regs = parent->regs;
    child->regs.rax = 0;
    child->regs.rbx = frame->rbx;
    child->regs.rbp = frame->rbp;
    child->regs.r12 = frame->r12;
    child->regs.r13 = frame->r13;
    child->regs.r14 = frame->r14;
    child->regs.r15 = frame->r15;
    child->regs.rip = frame->user_rip;
    child->regs.userrsp = frame->user_rsp;
    child->regs.rflags = frame->user_rflags;
    child->regs.cs = 0x23;
    child->regs.ss = 0x1B;
    child->regs.ds = 0x1B;

    spinlock_acquire(&sched_lock);
    if (task_count >= MAX_TASKS)
    {
        spinlock_release(&sched_lock);
        fd_table_free(child->fd_table);
        free_task_kernel_stack(child->kernel_stack);
        syscall_release_special_user_mappings(child->pml4);
        free_user_pages(child->pml4);
        free_page_directory(child->pml4);
        kfree(child);
        spinlock_release_raw(&process_lock);
        return -1;
    }

    child->pid = next_pid++;
    insert_task_locked(child);
    task_count++;

    pid_t child_pid = (pid_t)child->pid;
    spinlock_release(&sched_lock);
    spinlock_release_raw(&process_lock);

    return child_pid;
}

int sched_signal(uint64_t pid, int sig)
{
    if (sig <= 0 || sig >= NSIG) // Nix like signals
        return -1;
    if (!task_list_head)
        return -1;

    spinlock_acquire(&sched_lock);

    task_t *t = task_list_head;
    do
    {
        if (t->pid == pid)
        {
            if (t->state == TASK_DEAD)
            {
                spinlock_release(&sched_lock);
                return -1;
            }

            if (sig == SIGKILL)
            {
                t->state = TASK_DEAD;
                int is_self = (t == sched_current_task());
                spinlock_release(&sched_lock);
                if (is_self)
                    sched_yield();
                return 0;
            }

            if (sig == SIGSTOP)
            {
                t->state = TASK_BLOCKED;
                spinlock_release(&sched_lock);
                return 0;
            }

            if (sig == SIGCONT)
            {
                if (t->state == TASK_BLOCKED)
                    t->state = TASK_READY;
                spinlock_release(&sched_lock);
                return 0;
            }

            zen_sigaction_t *sa = &t->sighandlers[sig];
            if (sa->handler == SIG_IGN)
            {
                spinlock_release(&sched_lock);
                return 0;
            }

            t->sig_pending |= (1ull << sig);
            if (t->state == TASK_BLOCKED)
                t->state = TASK_READY;
            spinlock_release(&sched_lock);
            return 0;
        }
        t = t->next;
    } while (t != task_list_head);

    spinlock_release(&sched_lock);
    return -1;
}
