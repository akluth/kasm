# Changelog

## 0.2.1

- Expanded the 16-bit 8086 backend toward small DOS-like kernels and monitors.
- Added native encodings for immediate far `jmp`/`call`, `in`, `out`, `lea`,
  `lds`, `les`, `xchg`, memory ALU forms, memory `inc`/`dec`, and unary memory
  forms such as `not word [addr]`.
- Added support for segment-register stores to memory, useful for installing
  real-mode interrupt vectors.
- Added regression coverage for INT-21h-style handler setup, IVT writes, port
  I/O, far transfers, and DOS-like kernel skeleton code.

## 0.2.0

- Added an additive 16-bit 8086 raw-binary target via `-f bin16` and
  `-f bin --bits 16`.
- Added `bits 16`, `org`, `$`, `$$`, scoped local labels, and simple
  compile-time `assert` support for raw binary layouts.
- Added a native 8086 encoder path for boot-sector and COM-program essentials,
  including register/immediate moves, segment moves, ModR/M addressing, short
  conditional jumps, `int`, string instructions, and core flag/control
  instructions.
- Added 16-bit examples, boot-sector documentation, and regression tests for
  exact bytes, `[BP]`, boot signatures, and COM layout.

## 0.1.0

Official initial release.

- Added the KASM assembler for Linux x86-64 with Intel-like syntax.
- Supports direct ELF64 executables, ELF64 relocatable objects, and raw binary output.
- Supports multiple input files, project builds, and a minimal built-in ELF64 linker.
- Includes macros, defines, includes, structs, syscall sugar, and bare Linux std helpers.
- Includes explain mode, teaching mode, performance/ABI hints, maps, listings, and dump commands.
- Includes tiny mode for conservative size-oriented encodings.
- Includes ELF inspection and lightweight disassembly tools for KASM-generated files.
