# Real Mode

The 16-bit target is intended for Intel 8086/8088-compatible real-mode raw
binaries: boot sectors, BIOS experiments, DOS-style COM files, and early kernel
stubs.

KASM emits bytes directly. It does not call NASM, YASM, FASM, GAS, LLVM, or GNU
binutils for instruction encoding.

Use `org 0x7C00` for BIOS boot sectors and `org 0x100` for COM programs.
Labels evaluate to logical offsets that include the origin.
