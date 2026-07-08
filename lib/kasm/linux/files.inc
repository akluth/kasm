include "linux/syscalls.inc"

; openat dirfd, path, flags, mode
; Opens path relative to dirfd. Return fd in rax or negative errno.
; Clobbers: rax, rdi, rsi, rdx, r10, rcx, r11
macro openat dirfd, path, flags, mode
    syscall openat, dirfd, path, flags, mode
end

; close fd
; Closes fd. Return status in rax.
; Clobbers: rax, rdi, rcx, r11
macro close fd
    syscall close, fd
end

; read_fd fd, ptr, len
; Reads from fd into ptr.
; Clobbers: rax, rdi, rsi, rdx, rcx, r11
macro read_fd fd, ptr, len
    syscall read, fd, ptr, len
end

; write_fd fd, ptr, len
; Writes ptr/len to fd.
; Clobbers: rax, rdi, rsi, rdx, rcx, r11
macro write_fd fd, ptr, len
    syscall write, fd, ptr, len
end
