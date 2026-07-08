include "linux/syscalls.inc"

; kexit code
; Thin alias over the Linux exit syscall.
macro kexit code
    syscall exit, code
end

; kexit_group code
; Thin alias over the Linux exit_group syscall.
macro kexit_group code
    syscall exit_group, code
end
