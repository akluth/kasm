; Relocatable object with global _start, link with ld.
; Build: ../kasm 07_object_ld.asm -f obj -o object.o && ld object.o -o object
global _start

section .text
_start:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel msg]
    mov rdx, msg_len
    syscall
    mov rax, 60
    xor rdi, rdi
    syscall

section .rodata
msg:
    db "Hello from KASM object", 10
msg_len = $ - msg
