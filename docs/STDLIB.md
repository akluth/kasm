# Linux Inline Stdlib

The stdlib is a set of KASM include files under `lib/kasm`. It is not libc, not ABI-stable, and not a linked runtime. Macros expand inline to ordinary Linux x86-64 syscalls.

## Files

- `linux/syscalls.inc`: syscall numbers and constants
- `linux/process.inc`: `exit`, `exit_group`
- `linux/io.inc`: `write_fd`, `read_fd`, `print`, `print_stdout`, `print_stderr`, `putc`
- `linux/files.inc`: `openat`, `close`, `read_fd`, `write_fd`
- `linux/memory.inc`: `mmap_raw`, `mmap_anon`, `munmap`, `brk`
- `linux/strings.inc`: placeholder constant for future string routines
- `kasm/common.inc`: small KASM-level constants

## Constants

`linux/syscalls.inc` defines `SYS_read`, `SYS_write`, `SYS_open`, `SYS_close`, `SYS_mmap`, `SYS_mprotect`, `SYS_munmap`, `SYS_brk`, `SYS_exit`, `SYS_exit_group`, `SYS_openat`, `stdin`, `stdout`, `stderr`, `AT_FDCWD`, open flags, protection flags, and mapping flags.

## Convention

Macro comments document purpose, arguments, return value, and clobbered registers. Syscall return values remain in `rax`; errors are not handled automatically.

## Include Paths

Use:

```asm
include "linux/io.inc"
include "linux/process.inc"
```

KASM searches the including file directory, `-I` paths, `KASM_INCLUDE_PATH`, development stdlib paths, and installed stdlib paths.
