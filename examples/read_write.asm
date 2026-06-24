; Read from stdin and write the same bytes to stdout.
; Build: ./kasm examples/read_write.asm -o read_write

entry _start

section .text
_start:
    syscall read, STDIN, buf, 64
    syscall write, STDOUT, buf, rax
    syscall exit, 0

section .data
buf:
    resb 64
