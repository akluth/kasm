# Architecture Notes

KASM keeps one parser, symbol table, data-directive path, listing path, and raw
binary writer. The target bitness and CPU live on the central `Assembler`
context.

The legacy x86-64 encoder remains the default. The 8086 encoder is selected
only when the target bitness is 16, normally through `-f bin16`, `--bits 16`, or
`bits 16`.

The 16-bit path uses exact instruction sizes. This is required for boot-sector
layout expressions such as `times 510 - ($ - $$) db 0`.
