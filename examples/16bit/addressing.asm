bits 16
org 0x100

start:
    mov ax, [bx + si]
    mov ax, [bp]
    mov ax, [0x1234]
    mov word [buffer], 0x1234
    hlt

buffer:
    dw 0
