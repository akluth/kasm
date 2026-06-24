entry _start

section .text
_start:
    mov rax, 60
    mov rdi, 42
    syscall
