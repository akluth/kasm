; Tiny-mode hello example. The direct ELF uses compact layout with --tiny.
; Build: ./kasm --tiny --tiny-report examples/tiny_hello.asm -o tiny_hello
entry _start

section .text
_start:
    mov eax, 1
    mov edi, 1
    lea rsi, [rel msg]
    mov edx, msg_len
    syscall
    mov eax, 60
    xor edi, edi
    syscall

section .rodata
msg:
    db "tiny hello", 10
msg_len = $ - msg
