# Debugging and Introspection

KASM v1.9 adds stable text dumps for understanding what an assembly unit produced.

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

`kasm inspect` reads ELF64 relocatable object files:

```sh
./kasm inspect object_start.o
./kasm inspect object_start.o --symbols
./kasm inspect object_start.o --sections
./kasm inspect object_start.o --relocs
```

The inspector is intentionally small. It reads section headers, section names, symbol tables, string tables, and RELA relocation sections. It is not a replacement for `readelf` or `objdump`.

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

KASM v1.9 does not emit DWARF, line tables, or debugger-ready metadata. The new functionality is text introspection and ELF64 object inspection groundwork.
