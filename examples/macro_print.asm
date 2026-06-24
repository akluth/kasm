macro print msg, len
    syscall write, stdout, msg, len
end

entry _start

section .text
_start:
    print hello, hello_len
    syscall exit, 0

section .rodata
hello:
    db "Hello macro", 10
hello_len = $ - hello
