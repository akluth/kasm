entry _start

section .text
_start:
    mov rdi, 5
    add rdi, 7
    sub rdi, 2
    and rdi, 15
    or rdi, 32
    test rdi, rdi
    cmp rdi, 42
    jne bad
    mov rax, 60
    syscall
bad:
    mov rdi, 1
    mov rax, 60
    syscall
