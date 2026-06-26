# Roadmap

KASM keeps the small assembler identity: tiny Linux x86-64 output, no-libc programs, binary layouts, object files, internal linking, project builds, debug dumps, bare Linux std helpers, improved tiny encodings, binary inspection, disassembler-light output, and byte-level explainability.

Possible future work:

- additional carefully selected instruction forms
- additional tiny-mode encodings where semantics are identical
- more relocation forms
- optional byte-load/string helpers for stdlib
- stronger fixture organization
- more precise file/system exit code use
- symbol lookup performance improvements for very large files

Non-goals unless the project direction changes:

- becoming a full NASM clone
- implementing a full C preprocessor
- hiding syscalls behind an opaque runtime
- adding third-party runtime dependencies
