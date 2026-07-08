include "linux/syscalls.inc"

; mmap_raw addr, len, prot, flags, fd, offset
; Performs mmap syscall. Return address in rax or negative errno.
; Clobbers: rax, rdi, rsi, rdx, r10, r8, r9, rcx, r11
macro mmap_raw addr, len, prot, flags, fd, offset
    syscall mmap, addr, len, prot, flags, fd, offset
end

; mmap_anon size, prot
; Maps anonymous private memory. Return address in rax or negative errno.
; Clobbers: rax, rdi, rsi, rdx, r10, r8, r9, rcx, r11
macro mmap_anon size, prot
    syscall mmap, 0, size, prot, MAP_PRIVATE + MAP_ANONYMOUS, -1, 0
end

; munmap addr, len
; Unmaps memory. Return status in rax.
; Clobbers: rax, rdi, rsi, rcx, r11
macro munmap addr, len
    syscall munmap, addr, len
end

; brk addr
; Sets program break. Return new break in rax.
; Clobbers: rax, rdi, rcx, r11
macro brk addr
    syscall brk, addr
end
