include "linux/syscalls.inc"

; write_fd fd, ptr, len
; Writes len bytes from ptr to fd.
; Args: fd, ptr, len
; Returns: rax = bytes written or negative errno
; Clobbers: rax, rdi, rsi, rdx, rcx, r11
macro write_fd fd, ptr, len
    syscall write, fd, ptr, len
end

; read_fd fd, ptr, len
; Reads up to len bytes from fd into ptr.
; Args: fd, ptr, len
; Returns: rax = bytes read or negative errno
; Clobbers: rax, rdi, rsi, rdx, rcx, r11
macro read_fd fd, ptr, len
    syscall read, fd, ptr, len
end

; print ptr, len
; Writes len bytes from ptr to stdout.
; Args: ptr, len
; Returns: rax = bytes written or negative errno
; Clobbers: rax, rdi, rsi, rdx, rcx, r11
macro print ptr, len
    syscall write, stdout, ptr, len
end

; print_stdout ptr, len
; Alias for print.
macro print_stdout ptr, len
    syscall write, stdout, ptr, len
end

; print_stderr ptr, len
; Writes len bytes from ptr to stderr.
; Clobbers: rax, rdi, rsi, rdx, rcx, r11
macro print_stderr ptr, len
    syscall write, stderr, ptr, len
end

; putc char
; Writes one static byte value to stdout by storing it on the stack.
; Clobbers: rax, rdi, rsi, rdx, rcx, r11
macro putc char
    push char
    syscall write, stdout, rsp, 1
    pop rax
end
