[BITS 64]
section .text
global user_task_entry

user_task_entry:
    ; Clear all registers first
    xor rax, rax
    xor rbx, rbx
    xor rdx, rdx
    xor r8, r8
    xor r9, r9
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15
    
    ; Setup for sysret
    mov rcx, rdi        ; User RIP (from RDI)
    mov rsp, rsi        ; User stack (from RSI)
    mov r11, 0x202      ; RFLAGS (IF set)
    
    ; Set user data segments
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Clear remaining registers
    xor rax, rax
    xor rbx, rbx
    xor rdx, rdx
    xor rsi, rsi
    xor rdi, rdi
    xor rbp, rbp
    xor r8, r8
    xor r9, r9
    xor r10, r10
    
    ; Jump to userspace
    o64 sysret