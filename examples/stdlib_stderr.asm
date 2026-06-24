include "linux/io.inc"
include "linux/process.inc"

entry _start

section .text
_start:
    print_stderr msg, msg_len
    exit 0

section .rodata
msg:
    db "Hello stderr from KASM stdlib", 10
msg_len = $ - msg
