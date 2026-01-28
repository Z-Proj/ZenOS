#include "isr.h"
#include "idt.h"
#include "../drv/local_apic.h"
#include "../libk/ports.h"
#include "../libk/string.h"
#include "../libk/debug/log.h"
#include "../kernel/sched.h"
#include <stdint.h>

isr_handler_t interrupt_handlers[256];

void register_interrupt_handler(uint8_t interrupt, isr_handler_t handler, const char* handler_name)
{
    interrupt_handlers[interrupt] = handler;
    log("New Interrupt Handler Registered : %s.", 1, 0, handler_name);
}

void isr_handler(registers_t* regs)
{
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
                log("\n=== USERSPACE FAULT (GPF) ===\n         - Task: %s (PID %d)\n         - Error Code: 0x%lx\n         - RIP: 0x%lx\n         - Terminating task...", 2, 1,
                    sched_current_task()->name, sched_current_task()->pid, regs->err_code, regs->rip);
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
                log("\n=== USERSPACE FAULT (Page Fault) ===\n         - Task: %s (PID %d)\n         - Faulting address: 0x%lx\n         - RIP: 0x%lx\n         - %s\n         - Terminating task...", 2, 1,
                    sched_current_task()->name, sched_current_task()->pid, cr2, regs->rip,
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
    if(interrupt_handlers[regs->int_no]) {
        interrupt_handlers[regs->int_no](regs);
    } else {
        log("Unhandled IRQ: %d", 3, 1, regs->int_no);
    }
    LocalApicSendEOI();
}