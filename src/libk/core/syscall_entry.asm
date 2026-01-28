[BITS 64]
section .text
global syscall_entry
extern syscall_handler
extern tss

syscall_entry:
    ; Swap to kernel stack
    swapgs                      ; Get kernel GS (if you're using it)
    mov r15, qword [rel tss + 4]  ; Load full 64-bit RSP0
    xchg rsp, r15               ; Swap to kernel stack
    
    ; Save user context
    push r15        ; User RSP
    push rcx        ; User RIP (sysret expects this)
    push r11        ; User RFLAGS (sysret expects this)
    
    ; Save ALL callee-saved registers
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
    
    ; Syscall args: rax=num, rdi=arg1, rsi=arg2, rdx=arg3, r10=arg4, r8=arg5
    ; C calling convention: rdi, rsi, rdx, rcx, r8, r9
    mov r9, r8      ; arg5
    mov r8, r10     ; arg4
    mov rcx, rdx    ; arg3
    mov rdx, rsi    ; arg2
    mov rsi, rdi    ; arg1
    mov rdi, rax    ; syscall number
    
    call syscall_handler
    
    ; Restore registers
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
    add rsp, 8      ; Skip rax (it has return value)
    
    pop r11         ; RFLAGS
    pop rcx         ; RIP
    pop r15         ; User RSP
    
    mov rsp, r15    ; Restore user stack
    swapgs          ; Restore user GS
    
    o64 sysret