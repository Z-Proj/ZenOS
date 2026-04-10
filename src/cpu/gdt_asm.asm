; 
; @file : /src/cpu/gdt_asm.asm
; @brief : Loads GDT and sets up segment registers for 64-bit mode.
; 
; This file is a part of the Zen (ZenOS)
; Operating System, and is released under
; the terms of the MIT Licensing : Read
; LICENSE at the root of the repository.
; 
; @copyright (c) 2026
; @author : Rishies2010
; 

[bits 64]

section .text
global load_gdt

load_gdt:
    lgdt [rdi]
    push 0x10
    pop rax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    push 0x08
    lea rax, [rel .flush]
    push rax
    retfq
    
.flush:
    ret