include "std/linux/io.asm"
include "std/linux/process.asm"

global _start
entry _start

section .text
_start:
    kread STDIN, buffer, 64
    mov r15, rax
    kwrite STDOUT, buffer, r15
    kexit 0

section .data
buffer:
    resb 64
