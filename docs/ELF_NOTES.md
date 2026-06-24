# ELF Notes

Direct executable mode writes small Linux x86-64 ELF64 executables. KASM lays out `.text`, `.rodata`, and `.data` with simple page alignment and uses the `entry` declaration for the executable entry point.

Object mode writes ELF64 `ET_REL` files with symbols, string tables, section headers, and Rela relocation sections as needed.

KASM does not generate:

- dynamic linker program headers
- PLT/GOT
- DWARF/debug information
- relocation relaxation
- full relocation coverage

Use object mode and a system linker when external symbols are needed.
