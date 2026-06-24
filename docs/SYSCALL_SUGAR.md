# Linux Syscall Sugar

KASM supports named Linux x86-64 syscall pseudo-instructions:

```asm
syscall exit, 0
syscall write, STDOUT, msg, msg_len
syscall read, STDIN, buf, 64
syscall openat, AT_FDCWD, path, O_RDONLY, 0
```

The pseudo-instruction expands during parsing to normal register setup followed by the raw `syscall` instruction. Explain mode, listings, maps, and dumps see the expanded instructions.

For example:

```asm
syscall write, STDOUT, msg, msg_len
```

expands as:

```asm
mov rax, 1
mov rdi, STDOUT
lea rsi, [rel msg]
mov rdx, msg_len
syscall
```

## ABI

Linux x86-64 syscall arguments use:

| Role | Register |
| --- | --- |
| syscall number | `rax` |
| arg 1 | `rdi` |
| arg 2 | `rsi` |
| arg 3 | `rdx` |
| arg 4 | `r10` |
| arg 5 | `r8` |
| arg 6 | `r9` |

## Supported Names

| Name | Number | Arguments |
| --- | ---: | ---: |
| `read` | 0 | 3 |
| `write` | 1 | 3 |
| `open` | 2 | 3 |
| `close` | 3 | 1 |
| `stat` | 4 | 2 |
| `fstat` | 5 | 2 |
| `mmap` | 9 | 6 |
| `mprotect` | 10 | 3 |
| `munmap` | 11 | 2 |
| `brk` | 12 | 1 |
| `exit` | 60 | 1 |
| `exit_group` | 231 | 1 |
| `openat` | 257 | 4 |

Pointer arguments that are simple labels expand through `lea reg, [rel label]`. Registers, integer addresses, and built-in constants are moved directly. More complex pointer expressions should be written out manually with raw register setup.

## Built-In Constants

These constants are available without an include:

- `STDIN`, `STDOUT`, `STDERR`
- `AT_FDCWD`
- `O_RDONLY`, `O_WRONLY`, `O_RDWR`, `O_CREAT`, `O_TRUNC`
- `PROT_READ`, `PROT_WRITE`, `PROT_EXEC`
- `MAP_PRIVATE`, `MAP_ANONYMOUS`

Lowercase `stdin`, `stdout`, and `stderr` remain supported for existing code.

## Disabling Sugar

Use `--no-syscall-sugar` to reject named syscall pseudo-instructions. Plain raw `syscall` with no operands is still accepted.
