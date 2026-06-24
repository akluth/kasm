; Labels and conditional jumps.

entry _start

section .text
_start:
    mov rax, 2
again:
    dec rax
    cmp rax, 0
    jne again
    syscall exit, 0
