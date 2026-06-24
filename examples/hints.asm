; Demonstrates KASM educational hints.
; Run: ./kasm examples/hints.asm -o hints --hints
entry _start

section .text
_start:
    mov rax, 0
    mov ax, 1
    mov rdi, rax
    cmp rax, 1
    lea rsi, [rax + rbx*4 + 8]
    mov rax, 60
    syscall
