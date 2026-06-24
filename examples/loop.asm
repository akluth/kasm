entry _start

section .text
_start:
    mov rdi, 3
loop:
    dec rdi
    cmp rdi, 0
    jne loop
    mov rax, 60
    syscall
