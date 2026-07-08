# 8086 Instruction Subset

KASM 0.2.1 implements the 16-bit forms needed for small boot sectors, COM
programs, and DOS-like kernel experiments.

Supported forms include:

- `mov` register/immediate, register/register, register/memory,
  memory/register, memory/immediate, segment-register moves, and segment stores
  to memory.
- `xchg` register/register forms.
- `lea`, `lds`, and `les`.
- `add`, `adc`, `sub`, `sbb`, `and`, `or`, `xor`, `cmp`, and `test` for
  register/register, register/memory, memory/register, register/immediate, and
  memory/immediate forms.
- `jmp`, `call`, immediate far `jmp`/`call`, `ret`, `retf`, `int`, `iret`,
  8086 short conditional jumps, `loop`, `loope`, `loopne`, and `jcxz`.
- `push`, `pop`, `pushf`, `popf`.
- `in` and `out` for `AL`/`AX` with immediate 8-bit ports or `DX`.
- `movsb`, `movsw`, `lodsb`, `lodsw`, `stosb`, `stosw`, `cmpsb`, `cmpsw`,
  `scasb`, `scasw`, with `rep`, `repe`, `repz`, `repne`, and `repnz`.
- `clc`, `stc`, `cmc`, `cld`, `std`, `cli`, `sti`, `lahf`, `sahf`, `nop`,
  `hlt`, `wait`, `cbw`, and `cwd`.
- One-operand `mul`, `imul`, `div`, `idiv`, `neg`, and `not` on registers.
- 8086 shifts and rotates with count `1` or `CL`.

Not yet complete: indirect far calls/jumps, 80186+ immediate shift counts,
32-bit real mode prefixes, floating point, and the full 8086 instruction matrix.
