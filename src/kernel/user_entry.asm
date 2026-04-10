; 
; @file : /src/kernel/user_entry.asm
; @brief : Jumps from kernel to userspace - sets up segments and IRETQ to user mode.
; 
; This file is a part of the Zen (ZenOS)
; Operating System, and is released under
; the terms of the MIT Licensing : Read
; LICENSE at the root of the repository.
; 
; @copyright (c) 2026
; @author : Rishies2010
; 

[BITS 64]
section .text
global exec_enter_user

exec_enter_user:
    mov ax, 0x1B
    mov ds, ax
    mov es, ax

    push qword 0x1B
    push rsi
    push qword 0x202
    push qword 0x23
    push rdi
    iretq
