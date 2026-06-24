entry _start
section .text
_start:
    jmp L0
L0:
    jmp L1
L1:
    jmp L2
L2:
    jmp L3
L3:
    jmp L4
L4:
    jmp L5
L5:
    jmp L6
L6:
    jmp L7
L7:
    jmp L8
L8:
    jmp L9
L9:
    syscall exit, 0
