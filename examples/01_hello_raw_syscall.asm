; Direct Linux syscalls without stdlib macros.
; Build: ../kasm 01_hello_raw_syscall.asm -o hello
entry _start

section .text
_start:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel msg]
    mov rdx, msg_len
    syscall

    mov rax, 60
    xor rdi, rdi
    syscall

section .rodata
msg:
    db "Hello from KASM", 10
msg_len = $ - msg
