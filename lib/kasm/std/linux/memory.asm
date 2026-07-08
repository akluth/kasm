include "linux/syscalls.inc"

; kmmap size
; Maps anonymous private read/write memory. Return pointer in rax.
macro kmmap size
    syscall mmap, 0, size, PROT_READ + PROT_WRITE, MAP_PRIVATE + MAP_ANONYMOUS, -1, 0
end

; kmunmap addr, size
; Unmaps memory. Return status in rax.
macro kmunmap addr, size
    syscall munmap, addr, size
end
