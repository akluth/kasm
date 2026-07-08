bits 16
org 0x100

start:
    mov dx, message
    mov ah, 0x09
    int 0x21

    mov ax, 0x4C00
    int 0x21

message:
    db "Hello from a KASM-built COM program!$"
