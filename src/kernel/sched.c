#include "sched.h"
#include "../libk/core/mem.h"
#include "../libk/string.h"
#include "../libk/debug/log.h"
#include "../libk/spinlock.h"
#include "../cpu/gdt.h"
#include "../drv/keyboard.h"
#include "signal.h"

extern struct tss_struct tss;

static task_t *task_list_head = NULL;
static task_t *current_task = NULL;
static uint64_t next_pid = 0;
static uint64_t task_count = 0;
static spinlock_t sched_lock = {0};
static volatile int scheduler_enabled = 0;

extern void user_task_entry(void);

static task_t *find_task_by_pid_locked(uint64_t pid)
{
    if (!task_list_head) return NULL;

    task_t *iter = task_list_head;
    do {
        if (iter->pid == pid) return iter;
        iter = iter->next;
    } while (iter != task_list_head);

    return NULL;
}

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
    next_pid = 0;
    task_count = 0;
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

task_t *task_create_user(void (*entry)(void), const char *name, page_table_t *pml4, int argc, char **argv)
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
    task->pid = next_pid++;
    strncpy(task->name, name, 63);
    task->name[63] = '\0';
    task->state = TASK_READY;
    task->time_slice_remaining = TIME_SLICE;
    task->stack_size = TASK_STACK_SIZE;
    task->is_kernel_task = 0;
    task->pml4 = pml4;
    task->heap_brk = USER_HEAP_START;
    task->argc = argc;
    task->argv = argv;
    task->envp = NULL;
    task->parent_pid = 0;
    task->wait_status = 0;
    task->wait_pid_target = -1;
    task->wait_result_pid = -1;
    task->waiting_on_pid = 0;
    task->wait_collected = 0;
    task->fd_table = fd_table_alloc();
    strncpy(task->cwd, "/mnt/drv0", sizeof(task->cwd) - 1);
    task->cwd[sizeof(task->cwd) - 1] = '\0';
    memset(task->sighandlers, 0, sizeof(task->sighandlers));
    task->sig_pending = 0;
    task->sig_mask = 0;
    
    task->kernel_stack = (uint64_t)kmalloc(TASK_STACK_SIZE);
    if (!task->kernel_stack)
    {
        kfree(task);
        spinlock_release(&sched_lock);
        return NULL;
    }
    memset((void *)task->kernel_stack, 0, TASK_STACK_SIZE);
    
    uint64_t user_stack_base = 0x700000000000;
    size_t stack_pages = (TASK_STACK_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
    
    for (size_t i = 0; i < stack_pages; i++)
    {
        uint64_t phys = alloc_page();
        if (!phys)
        {
            for (size_t j = 0; j < i; j++)
            {
                uint64_t p = virt_to_phys(task->pml4, user_stack_base + j * PAGE_SIZE);
                if (p) free_page(p);
                unmap_page(task->pml4, user_stack_base + j * PAGE_SIZE);
            }
            kfree((void*)task->kernel_stack);
            kfree(task);
            spinlock_release(&sched_lock);
            return NULL;
        }
        map_page(task->pml4, user_stack_base + i * PAGE_SIZE, phys, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    }
    
    task->user_stack = user_stack_base;
    
    memset(&task->regs, 0, sizeof(registers_t));
    uint64_t user_stack_top = user_stack_base + TASK_STACK_SIZE;
    user_stack_top &= ~0xFULL;

    uint64_t argv_user[argc + 1];
    for (int i = 0; i < argc; i++)
    {
        size_t len = 0;
        while (argv[i][len]) len++;
        len++;
        user_stack_top -= len;
        uint64_t page_base = user_stack_top & ~(uint64_t)(PAGE_SIZE - 1);
        uint64_t page_off  = user_stack_top & (PAGE_SIZE - 1);
        uint64_t str_phys  = virt_to_phys(pml4, page_base);
        if (str_phys)
        {
            char *dest = (char *)(str_phys + KERNEL_VIRT_OFFSET + page_off);
            for (size_t j = 0; j < len; j++) dest[j] = argv[i][j];
        }
        argv_user[i] = user_stack_top;
    }
    argv_user[argc] = 0;

    user_stack_top &= ~0xFULL;

    
    
    
    if ((argc & 1) == 1)
        user_stack_top -= sizeof(uint64_t);

    for (int i = argc; i >= 0; i--)
    {
        user_stack_top -= sizeof(uint64_t);
        uint64_t phys = virt_to_phys(pml4, user_stack_top & ~(uint64_t)(PAGE_SIZE - 1));
        if (phys)
            *(uint64_t *)(phys + KERNEL_VIRT_OFFSET + (user_stack_top & (PAGE_SIZE - 1))) = argv_user[i];
    }

    user_stack_top -= sizeof(uint64_t);
    {
        uint64_t phys = virt_to_phys(pml4, user_stack_top & ~(uint64_t)(PAGE_SIZE - 1));
        if (phys)
            *(uint64_t *)(phys + KERNEL_VIRT_OFFSET + (user_stack_top & (PAGE_SIZE - 1))) = (uint64_t)argc;
    }

    user_stack_top &= ~0xFULL;

    task->regs.rip    = (uint64_t)user_task_entry;
    task->regs.rdi    = (uint64_t)entry;
    task->regs.rsi    = user_stack_top;
    task->regs.rbp    = user_stack_top;
    task->regs.userrsp = user_stack_top;
    task->regs.rflags = 0x202;
    task->regs.cs = 0x08;
    task->regs.ss = 0x10;
    task->regs.ds = 0x10;
    
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
    task_count++;
    
    spinlock_release(&sched_lock);
    return task;
}

task_t *task_create(void (*entry)(void), const char *name) 
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
    task->pid = next_pid++;
    strncpy(task->name, name, 63);
    task->name[63] = '\0';
    task->state = TASK_READY;
    task->time_slice_remaining = TIME_SLICE;
    task->stack_size = TASK_STACK_SIZE;
    task->is_kernel_task = 1;
    task->pml4 = get_kernel_pml4();
    task->parent_pid = 0;
    task->wait_status = 0;
    task->wait_pid_target = -1;
    task->wait_result_pid = -1;
    task->waiting_on_pid = 0;
    task->wait_collected = 0;
    
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
    task_count++;
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
            task_t *parent = NULL;
            if (iter->parent_pid != 0)
                parent = find_task_by_pid_locked(iter->parent_pid);

            if (parent && parent->state != TASK_DEAD && !iter->wait_collected) {
                if (parent->waiting_on_pid) {
                    if (parent->wait_pid_target == -1 ||
                        parent->wait_pid_target == (int64_t)iter->pid) {
                        parent->wait_status = (iter->exit_code & 0xff) << 8;
                        parent->wait_result_pid = (pid_t)iter->pid;
                        parent->wait_pid_target = -1;
                        parent->waiting_on_pid = 0;
                        if (parent->state == TASK_BLOCKED)
                            parent->state = TASK_READY;
                        iter->wait_collected = 1;
                    }
                }

                if (!iter->wait_collected) {
                    prev = iter;
                    iter = next;
                    continue;
                }
            } else {
                iter->wait_collected = 1;
            }

            kbd_transfer_focus(iter->pid);
            if (iter->fd_table) {
                fd_table_free(iter->fd_table);
                iter->fd_table = NULL;
            }
            if (iter->kernel_stack)
            {
                kfree((void*)iter->kernel_stack);
            }
            
            if (iter->user_stack && !iter->is_kernel_task)
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
                    if (task_count > 0) task_count--;
                    kfree(iter);
                    return;
                }
                task_list_head = next;
                
                task_t *last = task_list_head;
                while (last->next != iter)
                    last = last->next;
                last->next = task_list_head;
                
                if (task_count > 0) task_count--;
                kfree(iter);
                iter = task_list_head;
                start = task_list_head;
                continue;
            }
            
            if (prev)
                prev->next = next;
            
            if (task_count > 0) task_count--;
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
    spinlock_acquire(&sched_lock);
    
    reap_dead_tasks();
    
    if (!current_task)
    {
        spinlock_release(&sched_lock);
        if (rflags & 0x200) asm volatile("sti");
        return;
    }
    
    task_t *old_task = current_task;
    task_t *new_task = get_next_task();
    
    if (new_task == old_task)
    {
        spinlock_release(&sched_lock);
        if (rflags & 0x200) asm volatile("sti");
        return;
    }
    
    if (old_task->state == TASK_RUNNING)
        old_task->state = TASK_READY;
    
    old_task->time_slice_remaining = TIME_SLICE;
    new_task->state = TASK_RUNNING;
    new_task->time_slice_remaining = TIME_SLICE;
    
    tss.rsp0 = new_task->kernel_stack + TASK_STACK_SIZE;
    
    if (new_task->pml4 != old_task->pml4)
    {
        switch_page_directory(new_task->pml4);
    }
    
    task_t *prev_task = current_task;
    current_task = new_task;
    spinlock_release(&sched_lock);
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

task_t *sched_get_task_list(void)
{
    return task_list_head;
}

int sched_kill(uint64_t pid)
{
    if (!task_list_head) return -1;

    task_t *t = task_list_head;
    do {
        if (t->pid == pid) {
            if (t->state == TASK_DEAD) return -1;
            t->state = TASK_DEAD;
            if (t == current_task)
                sched_yield();
            return 0;
        }
        t = t->next;
    } while (t != task_list_head);

    return -1;
}

static int write_wait_status(task_t *task, int *status_ptr, int value)
{
    if (!status_ptr) return 0;
    if (!task || !task->pml4) return -1;

    uint64_t uaddr = (uint64_t)status_ptr;
    uint64_t page0 = uaddr & ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t off0 = uaddr & (PAGE_SIZE - 1);
    uint64_t phys0 = virt_to_phys(task->pml4, page0);
    if (!phys0) return -1;

    uint8_t bytes[sizeof(int)];
    memcpy(bytes, &value, sizeof(int));

    uint8_t *dst0 = (uint8_t *)(phys0 + KERNEL_VIRT_OFFSET + off0);
    size_t first = PAGE_SIZE - off0;
    if (first >= sizeof(int)) {
        memcpy(dst0, bytes, sizeof(int));
        return 0;
    }

    memcpy(dst0, bytes, first);

    uint64_t page1 = page0 + PAGE_SIZE;
    uint64_t phys1 = virt_to_phys(task->pml4, page1);
    if (!phys1) return -1;

    uint8_t *dst1 = (uint8_t *)(phys1 + KERNEL_VIRT_OFFSET);
    memcpy(dst1, bytes + first, sizeof(int) - first);
    return 0;
}

int sched_wait_pid(int64_t pid, int *status)
{
    if (!current_task) return -1;
    if (write_wait_status(current_task, status, 0) < 0) return -1;

    for (;;) {
        spinlock_acquire(&sched_lock);

        if (!task_list_head) {
            spinlock_release(&sched_lock);
            return -1;
        }

        task_t *dead_match = NULL;
        int has_child_match = 0;

        task_t *iter = task_list_head;
        do {
            if (iter == current_task) {
                iter = iter->next;
                continue;
            }

            if (iter->parent_pid != current_task->pid) {
                iter = iter->next;
                continue;
            }

            if (pid != -1 && iter->pid != (uint64_t)pid) {
                iter = iter->next;
                continue;
            }

            has_child_match = 1;
            if (iter->state == TASK_DEAD) {
                dead_match = iter;
                break;
            }

            iter = iter->next;
        } while (iter != task_list_head);

        if (dead_match) {
            dead_match->wait_collected = 1;
            current_task->wait_status = (dead_match->exit_code & 0xff) << 8;
            current_task->wait_result_pid = (pid_t)dead_match->pid;
            current_task->wait_pid_target = -1;
            current_task->waiting_on_pid = 0;

            int ret = (int)dead_match->pid;
            int st = current_task->wait_status;
            spinlock_release(&sched_lock);

            if (write_wait_status(current_task, status, st) < 0) return -1;
            return ret;
        }

        if (!has_child_match) {
            current_task->wait_pid_target = -1;
            current_task->wait_result_pid = -1;
            current_task->waiting_on_pid = 0;
            spinlock_release(&sched_lock);
            return -1;
        }

        current_task->wait_pid_target = pid;
        current_task->wait_result_pid = -1;
        current_task->wait_status = 0;
        current_task->waiting_on_pid = 1;
        current_task->state = TASK_BLOCKED;

        spinlock_release(&sched_lock);
        sched_yield();

        spinlock_acquire(&sched_lock);
        if (current_task->wait_result_pid >= 0) {
            int ret = (int)current_task->wait_result_pid;
            int st = current_task->wait_status;
            current_task->wait_result_pid = -1;
            current_task->wait_pid_target = -1;
            current_task->waiting_on_pid = 0;
            spinlock_release(&sched_lock);

            if (write_wait_status(current_task, status, st) < 0) return -1;
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
    spinlock_acquire(&sched_lock);
    if (!current_task || task_count >= MAX_TASKS || syscall_frame_ptr == 0) {
        spinlock_release(&sched_lock);
        return -1;
    }

    task_t *parent = current_task;
    syscall_fork_frame_t *frame = (syscall_fork_frame_t *)syscall_frame_ptr;

    task_t *child = (task_t *)kmalloc(sizeof(task_t));
    if (!child) { spinlock_release(&sched_lock); return -1; }
    memset(child, 0, sizeof(task_t));

    child->pid        = next_pid++;
    child->parent_pid = parent->pid;
    child->state      = TASK_READY;
    child->time_slice_remaining = TIME_SLICE;
    child->stack_size = parent->stack_size;
    child->is_kernel_task = 0;
    child->heap_brk   = parent->heap_brk;
    child->argc       = parent->argc;
    child->argv       = parent->argv;
    child->envp       = parent->envp;
    child->exit_code  = 0;
    child->wait_status = 0;
    child->wait_pid_target = -1;
    child->wait_result_pid = -1;
    child->waiting_on_pid = 0;
    child->wait_collected = 0;
    memcpy(child->sighandlers, parent->sighandlers, sizeof(parent->sighandlers));
    child->sig_pending = 0;
    child->sig_mask = parent->sig_mask;
    child->sig_trampoline = parent->sig_trampoline;
    strncpy(child->cwd, parent->cwd, sizeof(child->cwd) - 1);
    child->cwd[sizeof(child->cwd) - 1] = '\0';

    strncpy(child->name, parent->name, 63);
    child->name[63] = '\0';

    child->pml4 = clone_page_directory(parent->pml4);
    if (!child->pml4) { kfree(child); spinlock_release(&sched_lock); return -1; }

    size_t stack_pages = (TASK_STACK_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t new_stack_phys[64];
    if (stack_pages > 64) {
        free_page_directory(child->pml4);
        kfree(child);
        spinlock_release(&sched_lock);
        return -1;
    }
    memset(new_stack_phys, 0, sizeof(new_stack_phys));

    for (size_t i = 0; i < stack_pages; i++) {
        uint64_t virt = parent->user_stack + i * PAGE_SIZE;
        uint64_t src_phys = virt_to_phys(parent->pml4, virt);
        if (!src_phys) continue;

        uint64_t dst_phys = alloc_page();
        if (!dst_phys) {
            for (size_t j = 0; j < stack_pages; j++) {
                if (!new_stack_phys[j]) continue;
                uint64_t v = parent->user_stack + j * PAGE_SIZE;
                unmap_page(child->pml4, v);
                free_page(new_stack_phys[j]);
            }
            free_page_directory(child->pml4);
            kfree(child);
            spinlock_release(&sched_lock);
            return -1;
        }

        memcpy((void *)(dst_phys + KERNEL_VIRT_OFFSET),
               (void *)(src_phys + KERNEL_VIRT_OFFSET),
               PAGE_SIZE);
        map_page(child->pml4, virt, dst_phys, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        new_stack_phys[i] = dst_phys;
    }

    child->kernel_stack = (uint64_t)kmalloc(TASK_STACK_SIZE);
    if (!child->kernel_stack) {
        free_page_directory(child->pml4);
        kfree(child);
        spinlock_release(&sched_lock);
        return -1;
    }
    memset((void *)child->kernel_stack, 0, TASK_STACK_SIZE);

    child->fd_table = fd_table_clone(parent->fd_table);
    if (!child->fd_table) {
        kfree((void *)child->kernel_stack);
        free_page_directory(child->pml4);
        kfree(child);
        spinlock_release(&sched_lock);
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

    task_t *old_next = task_list_head->next;
    task_list_head->next = child;
    child->next = old_next;
    task_count++;

    pid_t child_pid = (pid_t)child->pid;
    spinlock_release(&sched_lock);

    return child_pid;
}

int sched_signal(uint64_t pid, int sig)
{
    if (sig <= 0 || sig >= NSIG) return -1;
    if (!task_list_head) return -1;

    task_t *t = task_list_head;
    do {
        if (t->pid == pid) {
            if (t->state == TASK_DEAD) return -1;

            if (sig == SIGKILL) {
                t->state = TASK_DEAD;
                if (t == current_task) sched_yield();
                return 0;
            }

            if (sig == SIGSTOP) {
                t->state = TASK_BLOCKED;
                return 0;
            }

            if (sig == SIGCONT) {
                if (t->state == TASK_BLOCKED) t->state = TASK_READY;
                return 0;
            }

            zen_sigaction_t *sa = &t->sighandlers[sig];
            if (sa->handler == SIG_IGN) return 0;

            t->sig_pending |= (1u << sig);
            if (t->state == TASK_BLOCKED) t->state = TASK_READY;
            return 0;
        }
        t = t->next;
    } while (t != task_list_head);

    return -1;
}
