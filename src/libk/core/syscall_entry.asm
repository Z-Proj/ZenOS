[BITS 64]
section .text
global syscall_entry
extern syscall_handler

syscall_entry:
    swapgs
    mov r9, rsp
    mov rsp, qword [gs:4]
    sub rsp, 8

    push r9
    push rcx
    push r11
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    mov r9, rsp
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
    pop rbp
    pop rbx
    pop r11
    pop rcx
    pop r9
    add rsp, 8

    mov rsp, r9
    swapgs

    o64 sysret
