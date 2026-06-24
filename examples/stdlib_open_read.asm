include "linux/files.inc"
include "linux/process.inc"

entry _start

section .text
_start:
    openat AT_FDCWD, path, O_RDONLY, 0
    mov r12, rax
    read_fd r12, buffer, 128
    write_fd stdout, buffer, rax
    close r12
    exit 0

section .rodata
path:
    db "README.md", 0

section .data
buffer:
    resb 128
