; Count down to zero with a conditional branch.
entry _start

section .text
_start:
    mov rdi, 5
loop_again:
    dec rdi
    jne loop_again
    mov rax, 60
    syscall
