; Read bytes from stdin and write them back to stdout.
include "linux/io.inc"
include "linux/process.inc"

entry _start

section .text
_start:
    read_fd stdin, buffer, 64
    mov r15, rax
    write_fd stdout, buffer, r15
    exit 0

section .data
buffer:
    resb 64
