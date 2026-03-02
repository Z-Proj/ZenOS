[BITS 64]
section .text
global _start
global __libc_init_array
global __libc_fini_array
extern main
extern _exit

_start:
    ; Save argc/argv before aligning stack
    mov rbx, [rsp]          ; rbx = argc
    lea r12, [rsp + 8]      ; r12 = argv

    ; Align stack to 16 bytes
    and rsp, ~0xF

    ; Call newlib's init (sets up malloc, stdio, ctors)
    call __libc_init_array

    ; Call main(argc, argv)
    mov rdi, rbx
    mov rsi, r12
    call main

    ; Exit via newlib (flushes stdio buffers etc.)
    mov rdi, rax
    call _exit

.hang:
    hlt
    jmp .hang

__libc_init_array:
    ret

__libc_fini_array:
    ret
