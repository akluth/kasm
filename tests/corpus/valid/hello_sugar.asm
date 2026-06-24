entry _start
section .text
_start:
    syscall write, STDOUT, msg, msg_len
    syscall exit, 0
section .rodata
msg:
    db "corpus hello", 10
msg_len = $ - msg
