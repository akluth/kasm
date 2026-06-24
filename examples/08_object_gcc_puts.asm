; Optional object example using libc through gcc -no-pie.
; Build: ../kasm 08_object_gcc_puts.asm -f obj -o puts.o && gcc -no-pie puts.o -o puts
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
