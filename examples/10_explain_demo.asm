; Demonstrates REX, ModRM/SIB, RIP-relative addressing, and explain output.
entry _start

section .text
_start:
    push 0
    mov r8, 40
    add r8, 2
    mov qword ptr [rsp], r8
    mov rdi, qword ptr [rsp]
    lea rsi, [rel msg]
    mov rax, 60
    syscall

section .rodata
msg:
    db "explain", 10
