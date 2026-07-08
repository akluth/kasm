bits 16
org 0x1000

; A tiny DOS-like resident service skeleton.
; It installs INT 21h and handles AH=09h strings terminated by '$'.

start:
    cli
    xor ax, ax
    mov es, ax

    mov word [0x84], int21_handler
    mov ax, cs
    mov word [0x86], ax

    sti
    hlt

int21_handler:
    cmp ah, 0x09
    jne .done

    push ax
    push bx
    push si

    mov si, dx
.print:
    mov al, [si]
    cmp al, 0x24
    je .restore

    mov ah, 0x0E
    mov bh, 0
    int 0x10

    inc si
    jmp .print

.restore:
    pop si
    pop bx
    pop ax

.done:
    iret
