# Roadmap

KASM v1.9 keeps the small assembler identity: tiny Linux x86-64 output, no-libc programs, binary layouts, object files, project builds, debug dumps, and byte-level explainability.

Possible future work:

- additional carefully selected instruction forms
- better short-branch relaxation
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
