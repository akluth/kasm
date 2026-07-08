# 16-Bit Real Mode

KASM 0.2.1 provides an additive 8086-oriented raw-binary mode.

Enable it with:

```sh
./kasm file.asm -f bin16 -o file.bin
./kasm file.asm -f bin --bits 16 -o file.bin
./kasm file.asm --arch x86 --bits 16 -f bin -o file.bin
```

Source may also request the mode with `bits 16`. A conflicting CLI `--bits`
value is rejected. ELF64 output cannot be combined with `--bits 16`.

Supported registers are AX, CX, DX, BX, SP, BP, SI, DI, AL, CL, DL, BL, AH,
CH, DH, BH, and segment registers ES, CS, SS, DS. `MOV CS, ...` is rejected.

Memory operands support `byte [addr]`, `word [addr]`, `byte ptr [addr]`, and
`word ptr [addr]`. The 8086 addressing forms are `[BX+SI]`, `[BX+DI]`,
`[BP+SI]`, `[BP+DI]`, `[SI]`, `[DI]`, `[BP]`, `[BX]`, optional displacement,
and direct 16-bit addresses. `[BP]` is encoded with an explicit zero disp8.

Segment overrides use NASM-like syntax inside brackets: `[es:bx]`,
`[cs:label]`, `[ss:bp + 2]`, and `[ds:si]`.

`org` sets the logical address without writing padding. `$` is the current
logical address, and `$$` is the start of the current raw binary unit, so
`times 510 - ($ - $$) db 0` works for boot sectors.

Local labels begin with `.` and are scoped under the last nonlocal label.
