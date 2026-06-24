macro count_down reg
%%loop:
    dec reg
    jne %%loop
end

entry _start

section .text
_start:
    mov rdi, 3
    count_down rdi
    mov rax, 60
    syscall
