include "linux/syscalls.inc"

; kwrite fd, buf, len
; Thin alias over the Linux write syscall.
macro kwrite fd, buf, len
    syscall write, fd, buf, len
end

; kread fd, buf, len
; Thin alias over the Linux read syscall.
macro kread fd, buf, len
    syscall read, fd, buf, len
end

; kprint "literal" and kprintln "literal" are expanded by KASM's
; preprocessor into visible .rodata labels plus syscall write.
