[BITS 64]
section .text
global _start
extern main

_start:
    mov rdi, [rsp]
    lea rsi, [rsp + 8]
    and rsp, ~0xF
    call main
    mov rdi, rax
    mov rax, 1
    syscall
.hang:
    hlt
    jmp .hang