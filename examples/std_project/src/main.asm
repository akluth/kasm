include "std/linux/io.asm"
include "std/linux/process.asm"

global _start
entry _start

section .text
_start:
    kprintln "hello from std project"
    kexit 0
