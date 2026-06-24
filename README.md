# KASM

KASM is a tiny Linux x86-64 assembler with Intel-like syntax. It focuses on direct ELF64 output, small no-libc binaries, binary layout work, and educational byte-level transparency through its explain mode.

KASM is intentionally not a complete NASM/YASM/FASM/GAS replacement. It is a small, dependency-free C11 project with a frozen syntax and instruction subset.

## Why It Exists

KASM is useful when you want to see exactly what a small Linux program emits:

- direct ELF64 executables without invoking `ld`
- ELF64 relocatable object files for `ld` or `gcc -no-pie`
- raw binary output for headers and layout experiments
- transparent macro/include expansion
- verbose explain, map, and listing output

## Quick Start

```sh
make
./kasm examples/01_hello_raw_syscall.asm -o hello
./hello
```

Tiny hello:

```asm
entry _start

section .text
_start:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel msg]
    mov rdx, msg_len
    syscall
    mov rax, 60
    xor rdi, rdi
    syscall

section .rodata
msg:
    db "Hello from KASM", 10
msg_len = $ - msg
```

Explain mode:

```sh
./kasm examples/10_explain_demo.asm -o explain_demo --explain=deluxe
./kasm examples/10_explain_demo.asm -o explain_demo --map explain_demo.map --list explain_demo.lst
```

## Build, Test, Install

```sh
make
make test
make release
make size
make bench
make dist
```

Optional sanitizer builds:

```sh
make asan
make ubsan
```

Install to a custom prefix:

```sh
make install PREFIX=/tmp/kasm-test
/tmp/kasm-test/bin/kasm examples/03_hello_stdlib.asm -o hello_stdlib
```

`make dist` creates `dist/kasm-1.9.0.tar.gz`.

## Command Line

```sh
kasm [options] input.asm
kasm build [--config FILE] [--verbose] [--no-link]
kasm inspect file.o [--symbols] [--sections] [--relocs]
```

Common options:

- `-o FILE`
- `-f elf64|elf64-obj|obj|bin`
- `--combine` is recognized but not implemented yet
- `--tiny`, `-Oz`
- `--tiny-report`
- `--hints`, `--hints=perf,abi,size`, `--hints-cpu CPU`
- `-I PATH`
- `--explain`, `--explain=normal`, `--explain=verbose`, `--explain=deluxe`
- `--explain-format text`
- `--explain-file FILE`
- `--elf-info`
- `--teach`, `--teach-level beginner|intermediate|deep`
- `--map FILE`
- `--list FILE`
- `--dump-symbols`, `--dump-sections`, `--dump-relocs`, `--dump-all`, `--dump-ir`, `--dump-structs`, `--dump-expanded`, `--dump-tokens`
- `--print-include-paths`
- `--no-stdlib`
- `--no-syscall-sugar`
- `--help`
- `--version`

## Project Build Mode

`kasm build` reads a strict `kasm.toml` project file, assembles each source into `out_dir/*.o`, and links executable projects with `ld` by default.

```toml
[project]
name = "hello"
type = "executable"

[build]
sources = ["src/main.asm", "src/util.asm"]
out_dir = "build"
output = "hello"
linker = "ld"
entry = "_start"
```

```sh
cd examples/project_hello
../../kasm build --verbose
./build/project_hello
```

Use `--no-link` to generate only object files. Cross-file linking is handled by the system linker unless a future KASM version adds a safe internal combiner/linker.

## Debugging And Introspection

KASM can print stable assembly-unit dumps while assembling:

```sh
./kasm examples/hello.asm --dump-symbols
./kasm examples/hello.asm --dump-sections
./kasm examples/object_start.asm -f obj --dump-all -o object_start.o
```

ELF64 object files can also be inspected after they are written:

```sh
./kasm inspect object_start.o
./kasm inspect object_start.o --symbols
```

## Output Formats

- `elf64`: direct Linux x86-64 executable, default
- `elf64-obj`: ELF64 relocatable object
- `obj`: alias for `elf64-obj`
- `bin`: raw bytes in `.text`, `.rodata`, `.data` order

Multiple input files are assembled independently as relocatable objects:

```sh
./kasm main.asm util.asm syscalls.asm
ld main.o util.o syscalls.o -o app
```

With multiple inputs, each output name is derived from the input basename, for example `main.asm` becomes `main.o`. Cross-file linking should be done with `ld`; `--combine` is reserved for a future safe combiner and currently returns an error.

## Inline Stdlib

The bundled stdlib lives under `lib/kasm`. It is not libc and not a runtime library. It is a set of include files and macros that expand to normal Linux x86-64 syscalls.

```asm
include "linux/io.inc"
include "linux/process.inc"

entry _start

section .text
_start:
    print msg, msg_len
    exit 0

section .rodata
msg:
    db "Hello from KASM stdlib", 10
msg_len = $ - msg
```

Include search order is: including file directory, `-I` paths, `KASM_INCLUDE_PATH`, bundled/development stdlib paths, installed stdlib path.

## Linux Syscall Sugar

Named Linux syscall pseudo-instructions expand to the x86-64 syscall ABI register setup:

```asm
syscall write, STDOUT, msg, msg_len
syscall exit, 0
```

Supported names include `read`, `write`, `open`, `close`, `stat`, `fstat`, `mmap`, `mprotect`, `munmap`, `brk`, `exit`, `exit_group`, and `openat`. Use `--no-syscall-sugar` to require explicit raw register setup.

## Tiny Mode

`--tiny` enables conservative size-oriented encoding. It can shorten in-range jumps, use sign-extended imm8 forms where valid, use shorter 32-bit immediate moves when the source explicitly names a 32-bit register, and pack direct ELF output more tightly.

```sh
./kasm --tiny --tiny-report examples/tiny_exit.asm -o tiny_exit
```

Tiny mode does not perform semantic instruction substitution. For example, it does not rewrite `mov reg, 0` to `xor reg, reg`, because flags differ.

## ELF Surgeon Mode

`--elf-info` prints a compact ELF64 anatomy report for the generated executable or object file without changing output bytes.

```sh
./kasm --elf-info examples/hello.asm -o hello
./kasm --elf-info -f obj examples/object_start.asm -o object_start.o
```

It reports ELF headers, program headers where applicable, sections, symbols, relocations, and the entry point explanation.

## Learn Assembly With KASM

`--teach` prints a guided walkthrough for curious learners. It combines program overview, source walkthrough, generated bytes, syscall ABI notes, ELF context, and a final run note.

```sh
./kasm --teach examples/teach_hello.asm -o teach_hello
./kasm --teach-level deep examples/teach_hello.asm -o teach_hello
```

Use `--teach-level beginner` for fewer encoding details, or `--teach-level deep` for source offsets and embedded ELF anatomy.

## Hints

`--hints` prints optional educational performance and ABI notes without changing output bytes.

```sh
./kasm examples/hints.asm -o hints --hints
```

Hints cover common zeroing idioms, conservative partial-register hazards, Linux syscall ABI notes, simple stack-alignment cases, memory addressing complexity, and places where `--tiny` may choose shorter encodings.

## Documentation

- [Syntax](docs/SYNTAX.md)
- [Instruction Subset](docs/INSTRUCTIONS.md)
- [Output Formats](docs/OUTPUT_FORMATS.md)
- [Explain Mode](docs/EXPLAIN_MODE.md)
- [ELF Surgeon Mode](docs/ELF_SURGEON_MODE.md)
- [Teaching Mode Tutorial](docs/TEACHING_MODE.md)
- [Project Build Mode](docs/PROJECTS.md)
- [Debugging and Introspection](docs/DEBUGGING.md)
- [Understanding x86-64 Encodings](docs/ENCODING_EXPLAINED.md)
- [Tiny Mode](docs/TINY_MODE.md)
- [Linux Syscall Sugar](docs/SYSCALL_SUGAR.md)
- [Performance And ABI Hints](docs/HINTS.md)
- [Stdlib](docs/STDLIB.md)
- [Structs](docs/STRUCTS.md)
- [Macros](docs/MACROS.md)
- [ELF Notes](docs/ELF_NOTES.md)
- [Limitations](docs/LIMITATIONS.md)
- [Performance](docs/PERFORMANCE.md)
- [Troubleshooting](docs/TROUBLESHOOTING.md)
- [Release Notes](docs/RELEASE.md)
- [Roadmap](docs/ROADMAP.md)
- [Examples](examples/README.md)

## Current Limitations

KASM is Linux x86-64 only. It has a limited instruction subset, limited relocation types, no dynamic linker generation for direct executable output, no debug info/DWARF, no full expression language, no full C preprocessor, and no libc. Explain mode explains KASM's supported encodings; it is not a full disassembler.

## Exit Codes

- `0`: success
- `1`: assembly/source error
- `2`: CLI usage error
- `3`: file/system error, reserved for future finer-grained handling
