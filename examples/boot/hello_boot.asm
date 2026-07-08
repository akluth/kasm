bits 16
org 0x7C00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld

    mov si, message
.print:
    lodsb
    test al, al
    jz .halt
    mov ah, 0x0E
    mov bh, 0
    int 0x10
    jmp .print

.halt:
    cli
    hlt
    jmp .halt

message:
    db "Booted with KASM!", 13, 10, 0

assert ($ - $$) <= 510, "boot sector exceeds 510 bytes"
times 510 - ($ - $$) db 0
dw 0xAA55
