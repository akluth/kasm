; Tiny-mode exit example.
; Build: ./kasm --tiny --tiny-report examples/tiny_exit.asm -o tiny_exit
entry _start

section .text
_start:
    mov eax, 60
    mov edi, 42
    syscall
