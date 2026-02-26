[BITS 64]
section .text
global syscall_entry
extern syscall_handler
extern tss

syscall_entry:
    swapgs
    mov r15, qword [rel tss + 4]
    xchg rsp, r15
    push r15
    push rcx
    push r11
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
    mov r9, r8
    mov r8, r10
    mov rcx, rdx
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, rax
    
    call syscall_handler
    
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
    add rsp, 8
    pop r11
    pop rcx
    pop r15
    
    mov rsp, r15
    swapgs
    
    o64 sysret
