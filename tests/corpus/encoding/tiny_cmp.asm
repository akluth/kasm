entry _start
section .text
_start:
    cmp rax, 1
    syscall
