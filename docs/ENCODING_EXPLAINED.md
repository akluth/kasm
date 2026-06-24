# Understanding x86-64 Encodings With KASM

KASM includes `--explain=deluxe`, a structured text mode for studying how supported instructions become bytes.

Run:

```sh
./kasm examples/explain_exit.asm -o explain_exit --explain=deluxe
```

Each emitted instruction keeps the traditional offset/bytes/source line, then adds:

- source location
- original source line
- normalized parsed instruction
- output offset and virtual address
- instruction length
- selected encoding form
- prefixes, including REX when present
- opcode bytes
- ModRM bit fields when present
- SIB bit fields when present
- displacement size/value when present
- immediate size/value when present
- relocation reference when present
- a short human note

The default `--explain` and existing `--explain=verbose` outputs remain supported. Deluxe mode is intentionally text-only in v1.4; `--explain-format text` is accepted for scripts that want to be explicit. JSON is deferred until the internal trace model needs it.

Example excerpt:

```text
00000000  C7 C0 3C 00 00 00    mov eax, 60
    encodes: mov r32, imm32
  location: examples/explain_exit.asm:6:5
  normalized: mov eax, 60
  offset: 0x00000000
  virtual-address: 0x401000
  bytes: c7 c0 3c 00 00 00
  length: 6
  form: MOV R32, IMM32
  opcode: c7
  ModRM: 0xc0
    mod bits: 3
    reg/opcode bits: 0
    r/m bits: 0
  immediate: size=32 value=60 bytes=3c 00 00 00 little-endian
```

KASM explains its supported encodings. It is not a general-purpose disassembler for arbitrary x86-64 byte streams.
