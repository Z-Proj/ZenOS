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
