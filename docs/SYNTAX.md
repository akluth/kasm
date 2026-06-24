# KASM v1.4 Syntax

KASM uses a small Intel-like syntax. Mnemonics, directives, section names, syscall names, and register names are case-insensitive. Labels and symbol names are case-sensitive.

## Comments And Whitespace

Semicolon starts a comment outside strings:

```asm
mov rax, 60 ; exit
```

Spaces and tabs are accepted. CRLF and LF source files are accepted.

## Sections

Supported sections:

- `section .text`
- `section .rodata`
- `section .data`

There is no full `.bss` section. `resb`, `resw`, `resd`, and `resq` reserve zero-filled bytes in the current output model.

## Labels And Entry

Labels end with `:` and belong to the current section.

```asm
entry _start

section .text
_start:
    ret
```

`entry symbol` is required for direct executable output and names the ELF entry point.

## Global And Extern

`global symbol` marks a defined symbol externally visible in object output. `extern symbol` declares a linker-resolved symbol for object output.

Direct executable output cannot resolve `extern` symbols.

## Constants And Expressions

Constants use:

```asm
name = expression
define NAME expression
```

Supported expression forms are deliberately small:

- integer literals accepted by C `strtoll`, including decimal and `0x` hex
- symbol
- `$`
- `symbol + integer`
- `symbol - integer`
- `symbol - symbol`
- `$ - symbol`
- `sizeof(StructName)`
- `offsetof(StructName, field)`

KASM does not implement NASM's full expression language.

## Data Directives

Supported directives:

- `db value, ...`
- `dw value, ...`
- `dd value, ...`
- `dq value, ...`
- `times N db ...`
- `align N`
- `resb N`, `resw N`, `resd N`, `resq N`

`db` accepts quoted strings. Strings support common one-letter escapes such as `\n` and `\t`.

## Memory Operands

Supported memory forms:

```asm
[rax]
[rbp - 8]
[rax + rbx]
[rax + rbx*8 + 16]
[rel symbol]
qword ptr [rsp + 32]
dword ptr [rax]
word ptr [rax]
byte ptr [rax]
```

Supported components are a 64-bit base register, optional index register, scale `1`, `2`, `4`, or `8`, signed 32-bit displacement, and RIP-relative `[rel symbol]`.

Absolute `[symbol]` addressing is not supported. Use `[rel symbol]` for RIP-relative loads/stores where the instruction form supports it.

## Size Specifiers

KASM parses `qword ptr`, `dword ptr`, `word ptr`, and `byte ptr`. Instruction encoding is primarily 64-bit for memory operations; mismatched sizes produce diagnostics.

Memory-immediate stores such as `mov [rax], 1` are intentionally unsupported because KASM v1.4 does not infer ambiguous memory sizes for those forms.

## Includes

Includes use quoted paths:

```asm
include "linux/io.inc"
```

Search order:

1. directory of the including file
2. paths passed with `-I`
3. colon-separated `KASM_INCLUDE_PATH`
4. bundled/development stdlib paths
5. installed stdlib path

`--no-stdlib` disables bundled and installed stdlib paths.

## Defines

`define NAME expression` creates a constant in the normal symbol table. Redefinition is rejected.

## Macros

Macros are simple textual assembler macros:

```asm
macro print msg, len
    syscall write, stdout, msg, len
end
```

Arguments are substituted as whole tokens. Recursive expansion is rejected.

Macro-local labels use `%%name` and are rewritten uniquely per expansion.

## Structs

Struct definitions describe binary layout and do not emit bytes:

```asm
struct Header
    magic: bytes 4
    version: word
    entry: qword
end
```

Initializers emit bytes:

```asm
Header {
    magic = "KASM"
    version = 1
    entry = 0
}
```

Omitted fields are zero-filled. There is no implicit C ABI padding; use `align` explicitly.

## Syscall Sugar

KASM supports a pseudo-instruction for common Linux syscalls:

```asm
syscall write, stdout, msg, msg_len
syscall exit, 0
```

Supported names are `read`, `write`, `open`, `close`, `stat`, `fstat`, `mmap`, `mprotect`, `munmap`, `brk`, `exit`, `exit_group`, and `openat`. KASM checks argument counts and expands pointer labels through `lea reg, [rel label]`. For raw Linux syscall instruction emission, use plain `syscall` with no operands.

Common Linux constants such as `STDOUT`, `AT_FDCWD`, `O_RDONLY`, `PROT_READ`, and `MAP_ANONYMOUS` are built in. See `docs/SYSCALL_SUGAR.md`.

## NASM/YASM/FASM/GAS Differences

KASM is not source-compatible with NASM, YASM, FASM, or GAS. Notable differences:

- no `%define`, `%macro`, conditional assembly, or preprocessor language
- no `BITS`, `ORG`, `default rel`, or segment model
- no GAS AT&T syntax
- no automatic memory-size inference for memory-immediate stores
- limited sections and relocation types
- limited expression grammar
- no dynamic linker generation in direct executable mode
