# Debugging and Introspection

KASM includes stable text dumps for understanding what an assembly unit produced.

## Assembly Dumps

Dump symbols while assembling:

```sh
./kasm examples/hello.asm --dump-symbols
```

Dump sections:

```sh
./kasm examples/hello.asm --dump-sections
```

Dump relocations for object output:

```sh
./kasm examples/object_start.asm -f obj -o object_start.o --dump-relocs
```

Dump all core views in one stable report:

```sh
./kasm examples/object_start.asm -f obj -o object_start.o --dump-all
```

The dump tables use tab-separated columns and stable headings so tests and scripts can grep them.

## Inspecting Object Files

`kasm inspect` reads ELF64 objects and executables:

```sh
./kasm inspect object_start.o
./kasm inspect --headers object_start.o
./kasm inspect object_start.o --symbols
./kasm inspect object_start.o --sections
./kasm inspect object_start.o --relocs
./kasm inspect --segments hello
./kasm disasm hello
```

The inspector and disassembler are intentionally small. They read ELF64 headers,
program headers, section headers, symbol tables, string tables, RELA relocation
sections, and KASM-supported x86-64 encodings. They are not replacements for
`readelf` or `objdump`.

## Project Builds

Project mode can print per-source dumps:

```sh
./kasm build --dump-symbols
./kasm build --dump-all --no-link
```

KASM does not yet merge project symbol tables after linking. Use `ld`, `readelf`, or `kasm inspect` on the generated `.o` files for cross-file inspection.

## Common Diagnostics

Undefined symbol:

```text
src/main.asm:42:13: error: undefined symbol 'print_line'
```

For direct executable output, define the symbol in the same file. For cross-file code, assemble objects and link them with `ld`.

Missing include:

```text
src/app.asm:3:1: error: include file not found 'linux/io.inc'
  hint: add an -I path or fix the include filename
```

Macro argument mismatch:

```text
src/app.asm:9:1: error: macro argument count mismatch for 'print'
  expanded from macro call at src/app.asm:9
  hint: check the macro definition parameter count
```

## Limits

KASM does not emit DWARF, line tables, or debugger-ready metadata. The debugging functionality is text introspection, ELF64 inspection, and lightweight disassembly groundwork.
