include "linux/syscalls.inc"

; exit code
; Terminates the current process with code.
; Args: code
; Returns: does not return
; Clobbers: rax, rdi, rcx, r11
macro exit code
    syscall exit, code
end

; exit_group code
; Terminates all threads in the process.
; Args: code
; Returns: does not return
; Clobbers: rax, rdi, rcx, r11
macro exit_group code
    syscall exit_group, code
end
