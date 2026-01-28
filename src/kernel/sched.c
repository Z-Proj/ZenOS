#include "sched.h"
#include "../libk/core/mem.h"
#include "../libk/string.h"
#include "../libk/debug/log.h"
#include "../libk/spinlock.h"
#include "../cpu/gdt.h"

extern struct tss_struct tss;

static task_t *task_list_head = NULL;
static task_t *current_task = NULL;
static uint64_t next_pid = 0;
static spinlock_t sched_lock = {0};
static volatile int scheduler_enabled = 0;

extern void user_task_entry(void);

void task_exit(void)
{
    asm volatile("cli");
    current_task->state = TASK_DEAD;
    asm volatile("sti");
    log("Task %s exited.", 1, 0, current_task->name);
    sched_yield();
    log("A Task exit function returned.", 0, 1);
}

static void task_entry_wrapper(void)
{
    if (!current_task)
    {
        asm volatile("cli; hlt");
        while (1);
    }
    asm volatile("sti");

    void (*entry)(void) = (void (*)(void))current_task->regs.rbx;
    if (!entry)
    {
        asm volatile("cli; hlt");
        while (1);
    }
    entry();
    task_exit();
}

void sched_init(void)
{
    spinlock_init(&sched_lock);
    task_list_head = NULL;
    current_task = NULL;
    scheduler_enabled = 0;
    extern struct tss_struct tss;
    tss.rsp0 = 0;
    log("Scheduler initialized.", 4, 0);
}

void sched_start(void)
{
    if (!current_task) return;
    log("Scheduler enabled.", 4, 0);
    scheduler_enabled = 1;
}

// At the top, add extern
extern void user_task_entry(void);

// In task_create_user, change the register setup:
task_t *task_create_user(void (*entry)(void), const char *name)
{
    spinlock_acquire(&sched_lock);
    task_t *task = (task_t *)kmalloc(sizeof(task_t));
    if (!task)
    {
        spinlock_release(&sched_lock);
        return NULL;
    }
    memset(task, 0, sizeof(task_t));
    task->pid = next_pid++;
    strncpy(task->name, name, 63);
    task->name[63] = '\0';
    task->state = TASK_READY;
    task->time_slice_remaining = TIME_SLICE;
    task->stack_size = TASK_STACK_SIZE;
    task->is_kernel_task = 0;
    
    task->kernel_stack = (uint64_t)kmalloc(TASK_STACK_SIZE);
    if (!task->kernel_stack)
    {
        kfree(task);
        spinlock_release(&sched_lock);
        return NULL;
    }
    memset((void *)task->kernel_stack, 0, TASK_STACK_SIZE);
    
    uint64_t user_stack_base = 0x700000000000;
    page_table_t *pml4 = get_kernel_pml4();
    size_t stack_pages = (TASK_STACK_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
    
    for (size_t i = 0; i < stack_pages; i++)
    {
        uint64_t phys = alloc_page();
        if (!phys)
        {
            for (size_t j = 0; j < i; j++)
            {
                uint64_t p = virt_to_phys(pml4, user_stack_base + j * PAGE_SIZE);
                if (p) free_page(p);
                unmap_page(pml4, user_stack_base + j * PAGE_SIZE);
            }
            kfree((void*)task->kernel_stack);
            kfree(task);
            spinlock_release(&sched_lock);
            return NULL;
        }
        map_page(pml4, user_stack_base + i * PAGE_SIZE, phys, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    }
    
    __asm__ volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");
    task->user_stack = user_stack_base;
    
    memset(&task->regs, 0, sizeof(registers_t));
    uint64_t user_stack_top = user_stack_base + TASK_STACK_SIZE;
    user_stack_top &= ~0xFULL;
    user_stack_top -= 8;  // ABI alignment
    
    // Setup for user_task_entry trampoline
    task->regs.rip = (uint64_t)user_task_entry;
    task->regs.rdi = (uint64_t)entry;      // arg1: user entry point
    task->regs.rsi = user_stack_top;       // arg2: user stack
    task->regs.rbp = task->kernel_stack + TASK_STACK_SIZE - 16;
    task->regs.userrsp = task->kernel_stack + TASK_STACK_SIZE - 16;
    task->regs.rflags = 0x202;
    task->regs.cs = 0x08;   // Kernel code (trampoline runs in kernel)
    task->regs.ss = 0x10;   // Kernel stack
    task->regs.ds = 0x10;
    
    // ... rest of existing code (adding to list, etc)
    
    if (!task_list_head)
    {
        task_list_head = task;
        task->next = task;
        task->state = TASK_RUNNING;
        current_task = task;
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
    log("Created user task: %s (PID %d)", 1, 0, name, task->pid);
    
    spinlock_release(&sched_lock);
    return task;
}

task_t *task_create(void (*entry)(void), const char *name)
{
    spinlock_acquire(&sched_lock);
    task_t *task = (task_t *)kmalloc(sizeof(task_t));
    if (!task)
    {
        spinlock_release(&sched_lock);
        return NULL;
    }
    memset(task, 0, sizeof(task_t));
    task->pid = next_pid++;
    strncpy(task->name, name, 63);
    task->name[63] = '\0';
    task->state = TASK_READY;
    task->time_slice_remaining = TIME_SLICE;
    task->stack_size = TASK_STACK_SIZE;
    task->is_kernel_task = 1;
    
    task->kernel_stack = (uint64_t)kmalloc(TASK_STACK_SIZE);
    if (!task->kernel_stack)
    {
        kfree(task);
        spinlock_release(&sched_lock);
        return NULL;
    }
    memset((void *)task->kernel_stack, 0, TASK_STACK_SIZE);
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
    task->regs.rcx = 0;
    task->regs.rdx = 0;
    task->regs.rsi = 0;
    task->regs.rdi = 0;
    task->regs.r8 = 0;
    task->regs.r9 = 0;
    task->regs.r10 = 0;
    task->regs.r11 = 0;
    task->regs.r12 = 0;
    task->regs.r13 = 0;
    task->regs.r14 = 0;
    task->regs.r15 = 0;
    
    if (!task_list_head)
    {
        task_list_head = task;
        task->next = task;
        task->state = TASK_RUNNING;
        current_task = task;
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
    log("Created kernel task: %s (PID %d)", 1, 0, name, task->pid);
    spinlock_release(&sched_lock);
    return task;
}

static void reap_dead_tasks(void)
{
    if (!task_list_head) return;
    
    task_t *iter = task_list_head;
    task_t *prev = NULL;
    task_t *start = task_list_head;
    
    do
    {
        task_t *next = iter->next;
        
        if (iter->state == TASK_DEAD && iter != current_task)
        {
            if (iter->kernel_stack)
            {
                kfree((void*)iter->kernel_stack);
            }
            
            if (iter->user_stack && !iter->is_kernel_task)
            {
                page_table_t *pml4 = get_kernel_pml4();
                size_t stack_pages = (TASK_STACK_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
                for (size_t i = 0; i < stack_pages; i++)
                {
                    uint64_t phys = virt_to_phys(pml4, iter->user_stack + i * PAGE_SIZE);
                    if (phys)
                    {
                        free_page(phys);
                        unmap_page(pml4, iter->user_stack + i * PAGE_SIZE);
                    }
                }
            }
            
            if (iter == task_list_head)
            {
                if (task_list_head->next == task_list_head)
                {
                    task_list_head = NULL;
                    kfree(iter);
                    return;
                }
                task_list_head = next;
                
                task_t *last = task_list_head;
                while (last->next != iter)
                    last = last->next;
                last->next = task_list_head;
                
                kfree(iter);
                iter = task_list_head;
                start = task_list_head;
                continue;
            }
            
            if (prev)
                prev->next = next;
            
            kfree(iter);
            iter = next;
            continue;
        }
        
        prev = iter;
        iter = next;
    } while (iter != start);
}

static task_t *get_next_task(void)
{
    if (!current_task || !current_task->next)
        return NULL;
    
    task_t *start = current_task->next;
    task_t *iter = start;
    
    do
    {
        if (iter->state == TASK_READY || iter->state == TASK_RUNNING)
            return iter;
        iter = iter->next;
    } while (iter != start);
    
    return current_task;
}

void sched_yield(void)
{
    if (!scheduler_enabled) return;
    
    uint64_t rflags;
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags));
    
    reap_dead_tasks();
    
    if (!current_task)
    {
        if (rflags & 0x200) asm volatile("sti");
        return;
    }
    
    task_t *old_task = current_task;
    task_t *new_task = get_next_task();
    
    if (new_task == old_task)
    {
        if (rflags & 0x200) asm volatile("sti");
        return;
    }
    
    if (old_task->state == TASK_RUNNING)
        old_task->state = TASK_READY;
    
    old_task->time_slice_remaining = TIME_SLICE;
    new_task->state = TASK_RUNNING;
    new_task->time_slice_remaining = TIME_SLICE;
    
    tss.rsp0 = new_task->kernel_stack + TASK_STACK_SIZE;
    
    task_t *prev_task = current_task;
    current_task = new_task;
    task_switch(&prev_task->regs, &new_task->regs);
}

void sched_tick(void)
{
    if (!scheduler_enabled || !current_task) return;
    
    if (current_task->time_slice_remaining > 0)
        current_task->time_slice_remaining--;
    
    if (current_task->time_slice_remaining == 0)
        sched_yield();
}

task_t *sched_current_task(void)
{
    return current_task;
}