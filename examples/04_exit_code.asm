; Exit with status 42.
entry _start

section .text
_start:
    mov rdi, 42
    mov rax, 60
    syscall
