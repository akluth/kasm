# Tiny Mode

KASM includes `--tiny` and `-Oz`, a conservative size-oriented assembly
mode. Tiny mode is an assembler-level encoding selector, not a compiler
optimizer.

Tiny mode keeps source semantics intact. It only chooses a shorter encoding for
the same instruction and operands when KASM can prove that the shorter encoding
is valid.

## CLI

```sh
./kasm --tiny examples/tiny_exit.asm -o tiny_exit
./kasm -Oz --tiny-report examples/tiny_hello.asm -o tiny_hello
./kasm --tiny --no-tiny examples/hello.asm -o hello
```

`--tiny-report` enables tiny mode and prints:

- jumps shortened
- imm8 encodings used
- push imm8 encodings used
- accumulator encodings used
- disp8 and zero-displacement memory encodings used
- near jumps required
- estimated bytes saved
- final output size

## Optimizations

- short `jmp rel8` and `jcc rel8` when the target is in range
- automatic fallback to near jumps when the target is out of range
- forward and backward label relaxation to a safe fixpoint
- sign-extended imm8 encodings for supported arithmetic/logic/compare forms
- accumulator immediate forms such as `add rax, imm32` where shorter
- `push imm8` when the value fits in signed 8 bits
- shorter memory operands with no displacement or disp8 where x86-64 permits it
- correct disp8 `0` for RBP/R13 base forms that cannot encode no displacement
- shorter `mov r32, imm32` when the source explicitly uses a 32-bit register
- compact single-segment direct ELF layout for `--tiny` executables

## Explain Output

Explain modes show tiny decisions in the encoding form:

```text
encodes: tiny: selected short jump because target distance is 7 bytes
encodes: tiny: selected push imm8
encodes: tiny: mov r64, qword ptr [mem]; ModRM/SIB memory operand with disp8 displacement
```

Use `--dump-expanded` first when includes or macros are involved, then
`--explain=deluxe` to inspect final byte choices.

## Safety Rules

Tiny mode does not:

- replace `mov reg, 0` with `xor reg, reg`, because that changes flags
- silently rewrite `rax` to `eax`
- reorder instructions
- remove dead code
- allocate registers
- optimize across data boundaries
- perform control-flow or value analysis beyond branch-size relaxation

Object output keeps relocatable jumps in near form when a relocation is needed.
