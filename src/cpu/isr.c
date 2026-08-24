/**
 * 
 * @file : /src/cpu/isr.c
 * @brief : Exception and IRQ handlers with register dumps and userspace fault handling.
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

#include "isr.h"
#include "idt.h"
#include "../drv/local_apic.h"
#include "../libk/ports.h"
#include "../libk/string.h"
#include "../libk/debug/log.h"
#include "../libk/debug/serial.h"
#include "../drv/vga.h"
#include "../kernel/sched.h"
#include <stdint.h>

isr_handler_t interrupt_handlers[256];

void register_interrupt_handler(uint8_t interrupt, isr_handler_t handler, const char* handler_name)
{
    (void)handler_name;
    interrupt_handlers[interrupt] = handler;
}

static const char *exception_title(uint64_t int_no)
{
    switch (int_no) {
        case DIVISION_BY_ZERO: return "Division by zero";
        case DEBUG_EXCEPTION: return "Debug exception";
        case NON_MASKABLE_INTERRUPT: return "Non-maskable interrupt";
        case BREAKPOINT_EXCEPTION: return "Breakpoint";
        case OVERFLOW_EXCEPTION: return "Overflow";
        case BOUND_RANGE_EXCEEDED: return "Bound range exceeded";
        case INVALID_OPCODE_EXCEPTION: return "Invalid opcode";
        case DEVICE_NOT_AVAILABLE: return "Device not available";
        case DOUBLE_FAULT: return "Double fault";
        case INVALID_TSS: return "Invalid TSS";
        case SEGMENT_NOT_PRESENT: return "Segment not present";
        case STACK_SEGMENT_FAULT: return "Stack segment fault";
        case GENERAL_PROTECTION_FAULT: return "General protection fault";
        case PAGE_FAULT: return "Page fault";
        case X87_FLOATING_POINT: return "x87 floating point exception";
        case ALIGNMENT_CHECK: return "Alignment check";
        case MACHINE_CHECK: return "Machine check";
        case SIMD_FLOATING_POINT: return "SIMD floating point exception";
        case VIRTUALIZATION_EXCEPTION: return "Virtualization exception";
        case SECURITY_EXCEPTION: return "Security exception";
        default: return "CPU exception";
    }
}

static const char *exception_splash(uint64_t int_no)
{
    switch (int_no) {
        case DIVISION_BY_ZERO: return "DIV0";
        case INVALID_OPCODE_EXCEPTION: return "UD";
        case DOUBLE_FAULT: return "DF";
        case INVALID_TSS: return "TSS";
        case SEGMENT_NOT_PRESENT: return "SNP";
        case STACK_SEGMENT_FAULT: return "SS";
        case GENERAL_PROTECTION_FAULT: return "GPF";
        case PAGE_FAULT: return "PF";
        case ALIGNMENT_CHECK: return "AC";
        case MACHINE_CHECK: return "MC";
        case SIMD_FLOATING_POINT: return "SIMD";
        default: return "EXCEPTION";
    }
}

static void halt_forever(void)
{
    for (;;)
        asm volatile("cli; hlt");
}

static void kernel_exception_screen(registers_t *regs)
{
    uint64_t cr2 = 0;
    if (regs->int_no == PAGE_FAULT)
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
    task_t *task = sched_current_task();
    const char *task_name = task ? task->name : "kernel";
    int pid = task ? task->pid : -1;
    uint16_t selector = (uint16_t)((regs->err_code >> 3) & 0x1fff);
    char info[1536];

    if (regs->int_no == PAGE_FAULT) {
        snprintf(info, sizeof(info),
            "Task: %s (PID %d)\n"
            "Fault address: 0x%016lx\n"
            "Error: 0x%016lx  %s  %s  %s\n"
            "RIP=0x%016lx RSP=0x%016lx RFLAGS=0x%016lx\n"
            "RAX=0x%016lx RBX=0x%016lx RCX=0x%016lx\n"
            "RDX=0x%016lx RSI=0x%016lx RDI=0x%016lx\n"
            "RBP=0x%016lx R8 =0x%016lx R9 =0x%016lx\n"
            "R10=0x%016lx R11=0x%016lx R12=0x%016lx\n"
            "R13=0x%016lx R14=0x%016lx R15=0x%016lx\n"
            "CS=0x%04lx SS=0x%04lx",
            task_name, pid, cr2, regs->err_code,
            (regs->err_code & 1) ? "protection" : "not-present",
            (regs->err_code & 2) ? "write" : "read",
            (regs->err_code & 4) ? "user" : "supervisor",
            regs->rip, regs->userrsp, regs->rflags,
            regs->rax, regs->rbx, regs->rcx, regs->rdx,
            regs->rsi, regs->rdi, regs->rbp,
            regs->r8, regs->r9, regs->r10, regs->r11,
            regs->r12, regs->r13, regs->r14, regs->r15,
            regs->cs, regs->ss);
    } else if (regs->int_no == GENERAL_PROTECTION_FAULT) {
        snprintf(info, sizeof(info),
            "Task: %s (PID %d)\n"
            "Error: 0x%016lx  selector=0x%04x  %s\n"
            "RIP=0x%016lx RSP=0x%016lx RFLAGS=0x%016lx\n"
            "RAX=0x%016lx RBX=0x%016lx RCX=0x%016lx\n"
            "RDX=0x%016lx RSI=0x%016lx RDI=0x%016lx\n"
            "RBP=0x%016lx R8 =0x%016lx R9 =0x%016lx\n"
            "R10=0x%016lx R11=0x%016lx R12=0x%016lx\n"
            "R13=0x%016lx R14=0x%016lx R15=0x%016lx\n"
            "CS=0x%04lx SS=0x%04lx",
            task_name, pid, regs->err_code, selector,
            (regs->err_code & 1) ? "external" : "internal",
            regs->rip, regs->userrsp, regs->rflags,
            regs->rax, regs->rbx, regs->rcx, regs->rdx,
            regs->rsi, regs->rdi, regs->rbp,
            regs->r8, regs->r9, regs->r10, regs->r11,
            regs->r12, regs->r13, regs->r14, regs->r15,
            regs->cs, regs->ss);
    } else {
        snprintf(info, sizeof(info),
            "Task: %s (PID %d)\n"
            "Interrupt: %lu  Error: 0x%016lx\n"
            "RIP=0x%016lx RSP=0x%016lx RFLAGS=0x%016lx\n"
            "RAX=0x%016lx RBX=0x%016lx RCX=0x%016lx\n"
            "RDX=0x%016lx RSI=0x%016lx RDI=0x%016lx\n"
            "RBP=0x%016lx R8 =0x%016lx R9 =0x%016lx\n"
            "R10=0x%016lx R11=0x%016lx R12=0x%016lx\n"
            "R13=0x%016lx R14=0x%016lx R15=0x%016lx\n"
            "CS=0x%04lx SS=0x%04lx",
            task_name, pid, regs->int_no, regs->err_code,
            regs->rip, regs->userrsp, regs->rflags,
            regs->rax, regs->rbx, regs->rcx, regs->rdx,
            regs->rsi, regs->rdi, regs->rbp,
            regs->r8, regs->r9, regs->r10, regs->r11,
            regs->r12, regs->r13, regs->r14, regs->r15,
            regs->cs, regs->ss);
    }

    vga_crash_screen(exception_splash(regs->int_no), exception_title(regs->int_no), info);
    halt_forever();
}

static void pty_fault_write(pty_buf_t *pty, const char *msg)
{
    if (!pty || !msg)
        return;

    uint64_t rflags = spinlock_acquire_irqsave(&pty->lock);
    if (!pty->master_open) {
        spinlock_release_irqrestore(&pty->lock, rflags);
        return;
    }

    for (size_t i = 0; msg[i]; i++) {
        if (pty->s2m_count >= PTY_BUF_SIZE)
            break;
        if (msg[i] == '\n') {
            pty->s2m_data[pty->s2m_write] = '\r';
            pty->s2m_write = (pty->s2m_write + 1) % PTY_BUF_SIZE;
            pty->s2m_count++;
            if (pty->s2m_count >= PTY_BUF_SIZE)
                break;
        }
        pty->s2m_data[pty->s2m_write] = (uint8_t)msg[i];
        pty->s2m_write = (pty->s2m_write + 1) % PTY_BUF_SIZE;
        pty->s2m_count++;
    }

    spinlock_release_irqrestore(&pty->lock, rflags);
}

static void userspace_exception_report(registers_t *regs)
{
    task_t *task = sched_current_task();
    if (!task) {
        sched_yield();
        return;
    }

    task->userspace_faults++;
    task->last_userspace_fault = regs->int_no;

    uint64_t cr2 = 0;
    if (regs->int_no == PAGE_FAULT)
        asm volatile("mov %%cr2, %0" : "=r"(cr2));

    char msg[768];
    if (regs->int_no == PAGE_FAULT) {
        snprintf(msg, sizeof(msg),
            "\n%s: %s in %s (pid %d)\n"
            "  fault=0x%016lx rip=0x%016lx rsp=0x%016lx error=0x%016lx\n",
            "ZenOS", exception_title(regs->int_no), task->name, task->pid,
            cr2, regs->rip, regs->userrsp, regs->err_code);
    } else {
        snprintf(msg, sizeof(msg),
            "\n%s: %s in %s (pid %d)\n"
            "  rip=0x%016lx rsp=0x%016lx error=0x%016lx\n",
            "ZenOS", exception_title(regs->int_no), task->name, task->pid,
            regs->rip, regs->userrsp, regs->err_code);
    }
    serial_write_string(msg);

    fd_entry_t *err = NULL;
    if (task->fd_table && task->fd_table->entries[2].used)
        err = &task->fd_table->entries[2];

    if (err && err->type == FD_PTY_SLAVE && err->pty) {
        pty_fault_write(err->pty, msg);
    }

    sched_abort_current(128 + (int)regs->int_no);
}

void isr_handler(registers_t* regs)
{
    if (regs->int_no < 32 && (regs->cs & 3)) {
        userspace_exception_report(regs);
        return;
    }

    if (regs->int_no < 32 && !(regs->cs & 3))
        kernel_exception_screen(regs);

    switch(regs->int_no) {
        case DIVISION_BY_ZERO:
            if (regs->cs & 3) {
                log("\n=== USERSPACE FAULT (Division by Zero) ===\n         - Task: %s (PID %d) - RIP: 0x%lx\n         - Terminating...", 2, 1,
                    sched_current_task()->name, sched_current_task()->pid, regs->rip);
                sched_current_task()->state = TASK_DEAD;
                sched_yield();
                return;
            }
            log("\n=== DIVISION BY ZERO EXCEPTION ===\n         - RIP: 0x%lx, RSP: 0x%lx\n         - RAX: 0x%lx, RBX: 0x%lx, RCX: 0x%lx, RDX: 0x%lx\n         - \n         - === REGISTER DUMP ===\n         - \n         - RAX=0x%016lx RBX=0x%016lx RCX=0x%016lx RDX=0x%016lx\n         - RSI=0x%016lx RDI=0x%016lx RBP=0x%016lx RSP=0x%016lx\n         - RIP=0x%016lx RFLAGS=0x%016lx", 3, 1,
                regs->rip, regs->userrsp,
                regs->rax, regs->rbx, regs->rcx, regs->rdx,
                regs->rax, regs->rbx, regs->rcx, regs->rdx,
                regs->rsi, regs->rdi, regs->rbp, regs->userrsp,
                regs->rip, regs->rflags);
            for(;;)asm volatile("cli; hlt");
            break;
            
        case INVALID_OPCODE_EXCEPTION: {
            if (regs->cs & 3) {
                log("\n=== USERSPACE FAULT (Invalid Opcode) ===\n         - Task: %s (PID %d) - RIP: 0x%lx\n         - Terminating...", 2, 1,
                    sched_current_task()->name, sched_current_task()->pid, regs->rip);
                sched_current_task()->state = TASK_DEAD;
                sched_yield();
                return;
            }
            uint8_t* bad_instr = (uint8_t*)regs->rip;
            log("\n=== INVALID OPCODE EXCEPTION ===\n         - RIP: 0x%lx (instruction pointer)\n         - CS: 0x%lx, RFLAGS: 0x%lx\n         - Instruction bytes: %02x %02x %02x %02x\n         - \n         - === REGISTER DUMP ===\n         - \n         - RAX=0x%016lx RBX=0x%016lx RCX=0x%016lx RDX=0x%016lx\n         - RSI=0x%016lx RDI=0x%016lx RBP=0x%016lx RSP=0x%016lx\n         - RIP=0x%016lx RFLAGS=0x%016lx", 3, 1,
                regs->rip,
                regs->cs, regs->rflags,
                bad_instr[0], bad_instr[1], bad_instr[2], bad_instr[3],
                regs->rax, regs->rbx, regs->rcx, regs->rdx,
                regs->rsi, regs->rdi, regs->rbp, regs->userrsp,
                regs->rip, regs->rflags);
            for(;;)asm volatile("cli; hlt");
            break;
        }
        
        case GENERAL_PROTECTION_FAULT: {
            uint16_t selector = (regs->err_code >> 3) & 0x1FFF;
            if (regs->cs & 3) {
                log("\n=== USERSPACE FAULT (GPF) ===\n         - Task: %s (PID %d)\n         - Error Code: 0x%lx\n         - RIP: 0x%lx\n         - RSP: 0x%lx\n         - RAX: 0x%lx\n         - RBX: 0x%lx\n         - RCX: 0x%lx\n         - RDX: 0x%lx\n         - RSI: 0x%lx\n         - RDI: 0x%lx\n         - RBP: 0x%lx\n         - FS: 0x%lx\n         - GS: 0x%lx\n         - Terminating task...", 2, 1,
                    sched_current_task()->name, sched_current_task()->pid, regs->err_code, regs->rip, regs->userrsp,
                    regs->rax, regs->rbx, regs->rcx, regs->rdx, regs->rsi, regs->rdi, regs->rbp,
                    sched_current_task()->user_fs_base, sched_current_task()->user_gs_base);
                sched_current_task()->state = TASK_DEAD;
                sched_yield();
                return;
            }
            log("\n=== GENERAL PROTECTION FAULT ===\n         - Error Code: 0x%lx\n         - %s\n         - %s\n         - RIP: 0x%lx, RSP: 0x%lx\n         - \n         - === REGISTER DUMP ===\n         - \n         - RAX=0x%016lx RBX=0x%016lx RCX=0x%016lx RDX=0x%016lx\n         - RSI=0x%016lx RDI=0x%016lx RBP=0x%016lx RSP=0x%016lx\n         - RIP=0x%016lx RFLAGS=0x%016lx", 3, 1,
                regs->err_code,
                (regs->err_code & 1) ? "External event caused fault" : "Internal event caused fault",
                selector ? "Segment selector involved" : "No segment selector",
                regs->rip, regs->userrsp,
                regs->rax, regs->rbx, regs->rcx, regs->rdx,
                regs->rsi, regs->rdi, regs->rbp, regs->userrsp,
                regs->rip, regs->rflags);
            for(;;)asm volatile("cli; hlt");
            break;
        }
        
        case PAGE_FAULT: {
            uint64_t cr2;
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
            if (regs->cs & 3) {
                log("\n=== USERSPACE FAULT (Page Fault) ===\n         - Task: %s (PID %d)\n         - Faulting address: 0x%lx\n         - RIP: 0x%lx\n         - RSP: 0x%lx\n         - RAX: 0x%lx\n         - RBX: 0x%lx\n         - RCX: 0x%lx\n         - RDX: 0x%lx\n         - RSI: 0x%lx\n         - RDI: 0x%lx\n         - RBP: 0x%lx\n         - FS: 0x%lx\n         - GS: 0x%lx\n         - %s\n         - Terminating task...", 2, 1,
                    sched_current_task()->name, sched_current_task()->pid, cr2, regs->rip, regs->userrsp,
                    regs->rax, regs->rbx, regs->rcx, regs->rdx, regs->rsi, regs->rdi, regs->rbp,
                    sched_current_task()->user_fs_base, sched_current_task()->user_gs_base,
                    (regs->err_code & 1) ? "Page protection violation" : "Page not present");
                sched_current_task()->state = TASK_DEAD;
                sched_yield();
                return;
            }
            log("\n=== PAGE FAULT ===\n         - Faulting address: 0x%lx\n         - Error code: 0x%lx\n         - %s\n         - %s\n         - %s\n         - RIP: 0x%lx\n         - \n         - === REGISTER DUMP ===\n         - \n         - RAX=0x%016lx RBX=0x%016lx RCX=0x%016lx RDX=0x%016lx\n         - RSI=0x%016lx RDI=0x%016lx RBP=0x%016lx RSP=0x%016lx\n         - RIP=0x%016lx RFLAGS=0x%016lx", 3, 1, 
                cr2, regs->err_code,
                (regs->err_code & 1) ? "Page protection violation" : "Page not present",
                (regs->err_code & 2) ? "Write operation" : "Read operation", 
                (regs->err_code & 4) ? "User mode access" : "Supervisor mode access",
                regs->rip,
                regs->rax, regs->rbx, regs->rcx, regs->rdx,
                regs->rsi, regs->rdi, regs->rbp, regs->userrsp,
                regs->rip, regs->rflags);
            for(;;)asm volatile("cli; hlt");
            break;
        }
    }

    if(interrupt_handlers[regs->int_no]) {
        interrupt_handlers[regs->int_no](regs);
    } else {
        log("Unhandled ISR. Interrupt: %i, Error Code: %d.", 3, 1, regs->int_no, regs->err_code);
    }
}

void irq_handler(registers_t* regs)
{
    if (regs->int_no == IRQ3)
        LocalApicSendEOI();

    if(interrupt_handlers[regs->int_no]) {
        interrupt_handlers[regs->int_no](regs);
    } else {
        log("Unhandled IRQ: %d", 3, 1, regs->int_no);
    }

    if (regs->int_no != IRQ3)
        LocalApicSendEOI();
}
