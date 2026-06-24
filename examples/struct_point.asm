struct Point
    x: qword
    y: qword
end

entry _start

section .text
_start:
    mov rdi, sizeof(Point)
    mov rax, 60
    syscall

section .rodata
p1:
    Point {
        x = 10
        y = 20
    }
