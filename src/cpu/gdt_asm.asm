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