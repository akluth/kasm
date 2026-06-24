; Explain mode demo alias.
; Run: ./kasm examples/explain_demo.asm -o explain_demo --explain=deluxe

entry _start

section .text
_start:
    mov rax, 60
    xor rdi, rdi
    syscall
