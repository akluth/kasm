include "linux/io.inc"
include "linux/process.inc"

entry _start

section .text
_start:
    print msg, msg_len
    exit 0

section .rodata
msg:
    db "Hello from KASM includes", 10
msg_len = $ - msg
