; Teaching mode hello example.
; Run: ./kasm --teach examples/teach_hello.asm -o teach_hello

entry _start

section .text
_start:
    syscall write, STDOUT, msg, msg_len
    syscall exit, 0

section .rodata
msg:
    db "learn KASM", 10
msg_len = $ - msg
