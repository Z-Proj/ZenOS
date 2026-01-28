[BITS 64]
section .text
global task_switch

task_switch:
    mov [rdi + 0], ds
    mov [rdi + 8], r15
    mov [rdi + 16], r14
    mov [rdi + 24], r13
    mov [rdi + 32], r12
    mov [rdi + 40], r11
    mov [rdi + 48], r10
    mov [rdi + 56], r9
    mov [rdi + 64], r8
    mov [rdi + 72], rbp
    mov [rdi + 80], rdi
    mov [rdi + 88], rsi
    mov [rdi + 96], rdx
    mov [rdi + 104], rcx
    mov [rdi + 112], rbx
    mov [rdi + 120], rax
    mov rax, [rsp]
    mov [rdi + 144], rax
    mov ax, cs
    mov [rdi + 152], rax
    pushfq
    pop rax
    mov [rdi + 160], rax
    lea rax, [rsp + 8]
    mov [rdi + 168], rax
    mov ax, ss
    mov [rdi + 176], rax
    mov rax, [rsi + 152]
    test rax, 3
    jnz .user_mode
    
.kernel_mode:
    mov ax, [rsi + 0]
    mov ds, ax
    mov es, ax
    
    mov r15, [rsi + 8]
    mov r14, [rsi + 16]
    mov r13, [rsi + 24]
    mov r12, [rsi + 32]
    mov r11, [rsi + 40]
    mov r10, [rsi + 48]
    mov r9,  [rsi + 56]
    mov r8,  [rsi + 64]
    mov rbp, [rsi + 72]
    mov rdx, [rsi + 96]
    mov rcx, [rsi + 104]
    mov rbx, [rsi + 112]
    mov rax, [rsi + 120]
    mov rsp, [rsi + 168]
    push qword [rsi + 160]
    popfq
    mov rdi, [rsi + 80]
    push qword [rsi + 88]
    push qword [rsi + 144]
    pop rax
    pop rsi
    jmp rax

.user_mode:
    push qword [rsi + 176]      ; SS
    push qword [rsi + 168]      ; RSP
    push qword [rsi + 160]      ; RFLAGS  
    push qword [rsi + 152]      ; CS
    push qword [rsi + 144]      ; RIP
    mov ax, [rsi + 0]
    mov ds, ax
    mov es, ax
    mov r15, [rsi + 8]
    mov r14, [rsi + 16]
    mov r13, [rsi + 24]
    mov r12, [rsi + 32]
    mov r11, [rsi + 40]
    mov r10, [rsi + 48]
    mov r9,  [rsi + 56]
    mov r8,  [rsi + 64]
    mov rbp, [rsi + 72]
    mov rdx, [rsi + 96]
    mov rcx, [rsi + 104]
    mov rbx, [rsi + 112]
    mov rax, [rsi + 120]
    mov rdi, [rsi + 80]
    mov rsi, [rsi + 88]
    
    ; Jump to user mode
    iretq