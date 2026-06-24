global _start

section .text
_start:
    syscall write, STDOUT, msg, msg_len
    syscall exit, 0

section .rodata
msg:
    db "Hello from a KASM project", 10
msg_len = $ - msg
