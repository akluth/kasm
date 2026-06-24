entry _start

section .text
_start:
    push 0
    mov r8, 42
    mov qword ptr [rsp], r8
    mov rdi, qword ptr [rsp]
    mov rax, 60
    syscall
