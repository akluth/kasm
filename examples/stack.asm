entry _start

section .text
_start:
    push rbp
    mov rbp, rsp
    push 0
    mov rax, 42
    mov qword ptr [rbp - 8], rax
    mov rdi, qword ptr [rbp - 8]
    mov rsp, rbp
    pop rbp
    mov rax, 60
    syscall
