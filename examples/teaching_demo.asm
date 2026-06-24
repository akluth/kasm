; Teaching mode demo alias.
; Run: ./kasm --teach examples/teaching_demo.asm -o teaching_demo

entry _start

section .text
_start:
    syscall write, STDOUT, msg, msg_len
    syscall exit, 0

section .rodata
msg:
    db "teaching demo", 10
msg_len = $ - msg
