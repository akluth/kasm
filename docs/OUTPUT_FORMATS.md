# Output Formats

KASM v1.9 supports three output families.

## `-f elf64`

Default. Writes a directly executable Linux x86-64 ELF file with program headers for supported sections.

Requirements:

- source must declare `entry symbol`
- entry must resolve to a defined label
- unresolved `extern` symbols are rejected

This mode does not generate dynamic linker metadata and cannot link libc.

## `-f elf64-obj`

Writes an ELF64 relocatable object file suitable for `ld` or `gcc -no-pie`.

Object files include `.text`, `.rodata`, `.data`, `.symtab`, `.strtab`, `.shstrtab`, and relocation sections such as `.rela.text` when needed.

Supported relocation types:

- `R_X86_64_PC32` for supported PC-relative calls, jumps, and RIP-relative references
- `R_X86_64_64` for simple `dq symbol` data references

## `-f obj`

Alias for `elf64-obj`.

## `-f bin`

Writes raw bytes in this order:

1. `.text`
2. `.rodata`
3. `.data`

This is useful for binary headers and layout checks. No ELF headers are written.

## Multiple Inputs

Multiple input files are assembled independently as ELF64 relocatable objects:

```sh
kasm main.asm util.asm syscalls.asm
ld main.o util.o syscalls.o -o app
```

Each output filename is derived from the input basename. `src/main.asm` and `main.asm` would both derive `main.o`, so KASM rejects that duplicate output conflict.

Use `ld` for cross-file linking. `--combine` is recognized for future work but returns `error: --combine is not implemented yet`.
