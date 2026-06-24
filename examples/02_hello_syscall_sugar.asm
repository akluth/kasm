; Same hello program using KASM's syscall sugar.
entry _start

section .text
_start:
    syscall write, stdout, msg, msg_len
    syscall exit, 0

section .rodata
msg:
    db "Hello via syscall sugar", 10
msg_len = $ - msg
