# Limitations

KASM 0.2.1 is stable within a deliberately small scope.

The 16-bit mode is focused on 8086 real-mode raw binaries. It does not yet
implement the full 8086 instruction matrix, indirect far transfers, ELF32, OMF,
MZ EXE generation, 32-bit protected mode, floating point, MMX, SSE, or
automatic jump relaxation. KASM can assemble small DOS-like kernels, monitors,
and boot-time services, but it does not provide DOS filesystem, process, or MZ
loader semantics.

- not a full NASM/YASM/FASM/GAS replacement
- Linux x86-64 only
- limited instruction subset
- limited memory operand forms
- limited relocation types: `R_X86_64_PC32` and simple `R_X86_64_64`
- no dynamic linker generation for direct executable mode
- no debug info or DWARF output
- no full expression language
- no full C preprocessor
- no conditional assembly
- no libc and no automatic syscall error handling
- stdlib is macro/include-based, not ABI-stable
- no C ABI struct layout or implicit padding
- explain mode only explains supported encodings
