bits 16
org 0x100

start:
    mov si, message
.next:
    lodsb
    test al, al
    jnz .next
    hlt

message:
    db "KASM string walk", 0
