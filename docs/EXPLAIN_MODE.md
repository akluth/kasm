# Explain, Map, And Listing Modes

`--explain` prints emitted bytes and a short encoding note for each source line that emits output.

```sh
./kasm examples/10_explain_demo.asm -o demo --explain
```

`--explain=verbose` adds byte-level information such as REX, opcode, ModRM, SIB, displacement, immediate, and relocation notes where the supported encoder records them.

`--explain=deluxe` adds a structured explanation with source location, normalized instruction, virtual address, instruction length, encoding form, prefixes, opcode, ModRM/SIB bit fields, displacement, immediate, relocation, and a short human note.

`--explain-format text` is accepted for explicit text output. Text is the only explain format in v1.5.

`--explain-file FILE` writes explain output to a file.

`--map FILE` writes sections, symbols, relocations, and struct summaries.

`--list FILE` writes a compact offset/bytes/source listing.

Macro-expanded stdlib calls remain transparent: listing and explain output show the expanded syscall setup instructions that KASM assembled.

See `ENCODING_EXPLAINED.md` for a guided example.
