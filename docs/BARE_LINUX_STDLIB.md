# Bare Linux Standard Library

KASM provides a tiny bare-Linux helper layer under `lib/kasm/std/linux`.
It is not libc, not dynamically linked, and not a hidden runtime. Helpers expand
to ordinary KASM assembly and Linux x86-64 syscalls.

## Include Files

- `std/linux/process.asm`: `kexit`, `kexit_group`
- `std/linux/io.asm`: `kwrite`, `kread`, `kprint`, `kprintln`
- `std/linux/memory.asm`: `kmmap`, `kmunmap`

KASM searches includes in this order:

1. the directory of the including source file
2. user paths from `-I` and `KASM_INCLUDE_PATH`
3. bundled and installed KASM include roots

Use `--print-std-path` to print the standard include roots. Use `--no-std` or
`--no-stdlib` to disable automatic bundled/installed include lookup.

## Process

```asm
include "std/linux/process.asm"

kexit 0
kexit_group 0
```

`kexit` expands to the Linux `exit` syscall. `kexit_group` expands to
`exit_group`.

## Output

```asm
include "std/linux/io.asm"

kprint "hello"
kprintln "hello"
kwrite STDOUT, msg, msg_len
```

`kprint` and `kprintln` accept one string literal. They generate a visible
`.rodata` label, a length constant, switch back to `.text`, and emit syscall
sugar for `write`. `kprintln` appends byte `10`.

`kwrite` is a thin macro over the existing `write_fd` helper and leaves the
syscall result in `rax`.

## Input

```asm
include "std/linux/io.asm"

kread STDIN, buffer, 64
```

`kread` expands to the Linux `read` syscall. It leaves the byte count or
negative errno in `rax`.

## Memory

```asm
include "std/linux/memory.asm"

kmmap 4096
kmunmap rax, 4096
```

`kmmap` maps anonymous private read/write memory and returns the address in
`rax`. `kmunmap` leaves the Linux syscall status in `rax`.

## Inspect Expansion

Use `--dump-expanded` to see what helpers turn into:

```sh
./kasm examples/std_hello/src/main.asm --dump-expanded
```

Teaching and explain modes operate on the expanded assembly, so bytes and
syscalls stay inspectable.

## Limitations

- No libc calls, startup code, dynamic linking, heap allocator, formatting, or
  error handling are provided.
- `kprint` and `kprintln` only accept a single string literal.
- Cross-file programs still assemble to objects first and should be linked with
  `ld` or `kasm link` where the internal linker supports the object files.
- `kstrlen` is intentionally not included yet; a transparent runtime loop would
  be larger than this helper layer needs.
