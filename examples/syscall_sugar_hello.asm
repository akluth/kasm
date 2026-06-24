; Linux syscall sugar example with v1.4 built-in constants.

entry _start

section .text
_start:
    syscall write, STDOUT, msg, msg_len
    syscall exit, 0

section .rodata
msg:
    db "Hello via v1.4 syscall sugar", 10
msg_len = $ - msg
