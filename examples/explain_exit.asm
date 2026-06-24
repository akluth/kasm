; Small deluxe explain-mode fixture.
; Run: ./kasm examples/explain_exit.asm -o explain_exit --explain=deluxe
entry _start

section .text
_start:
    mov eax, 60
    mov rdi, 1
    syscall
