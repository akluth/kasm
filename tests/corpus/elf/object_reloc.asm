global _start
section .text
_start:
    lea rsi, [rel msg]
    ret
section .rodata
msg:
    db "reloc", 0
