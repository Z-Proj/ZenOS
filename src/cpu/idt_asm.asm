[bits 64]

section .text
global load_idt

load_idt:
    lidt [rdi]
    ret

%macro isr_noerr 1
global isr%1
isr%1:
    push 0
    push %1
    jmp isr_stub
%endmacro

%macro isr_err 1
global isr%1
isr%1:
    push %1
    jmp isr_stub
%endmacro

; Define all ISRs
isr_noerr 0     ; Divide by zero
isr_noerr 1     ; Debug
isr_noerr 2     ; NMI
isr_noerr 3     ; Breakpoint
isr_noerr 4     ; Overflow
isr_noerr 5     ; Bound range exceeded
isr_noerr 6     ; Invalid opcode
isr_noerr 7     ; Device not available
isr_err   8     ; Double fault
isr_noerr 9     ; Coprocessor segment overrun
isr_err   10    ; Invalid TSS
isr_err   11    ; Segment not present
isr_err   12    ; Stack-segment fault
isr_err   13    ; General protection fault
isr_err   14    ; Page fault
isr_noerr 15    ; Reserved
isr_noerr 16    ; x87 floating-point exception
isr_err   17    ; Alignment check
isr_noerr 18    ; Machine check
isr_noerr 19    ; SIMD floating-point exception
isr_err   20    ; Virtualization exception
isr_err   21    ; Control protection exception
isr_noerr 22    ; Reserved
isr_noerr 23    ; Reserved
isr_noerr 24    ; Reserved
isr_noerr 25    ; Reserved
isr_noerr 26    ; Reserved
isr_noerr 27    ; Reserved
isr_noerr 28    ; Reserved
isr_noerr 29    ; Reserved
isr_noerr 30    ; Reserved
isr_noerr 31    ; Reserved

extern isr_handler
isr_stub:
    push rax
    push rbx  
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov ax, ds
    push rax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov rdi, rsp
    call isr_handler
    pop rax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    iretq

%macro irq 2
global irq%1
irq%1:
    push 0
    push %2
    jmp irq_stub
%endmacro

irq 0,  32      ; PIT Timer
irq 1,  33      ; Keyboard
irq 2,  34      ; Cascade
irq 3,  35      ; COM2
irq 4,  36      ; COM1
irq 5,  37      ; LPT2
irq 6,  38      ; Floppy disk
irq 7,  39      ; LPT1
irq 8,  40      ; CMOS RTC
irq 9,  41      ; Free for peripherals
irq 10, 42      ; Free for peripherals
irq 11, 43      ; Free for peripherals
irq 12, 44      ; PS2 mouse
irq 13, 45      ; FPU / coprocessor
irq 14, 46      ; Primary ATA
irq 15, 47      ; Secondary ATA

extern irq_handler
irq_stub:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov ax, ds
    push rax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov rdi, rsp
    call irq_handler
    pop rax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    iretq