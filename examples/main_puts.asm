global main
extern puts

section .text
main:
    push rbp
    mov rbp, rsp
    lea rdi, [rel msg]
    call puts
    xor rax, rax
    pop rbp
    ret

section .rodata
msg:
    db "Hello via puts from KASM", 0
