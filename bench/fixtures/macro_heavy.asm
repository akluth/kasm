macro emit_exit
    mov rax, 60
    xor rdi, rdi
end
entry _start
section .text
_start:
    emit_exit
