# Limitations

KASM v1.4 is stable within a deliberately small scope.

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
