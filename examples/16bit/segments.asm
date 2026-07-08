bits 16
org 0x100

start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ax, [es:bx]
    hlt
