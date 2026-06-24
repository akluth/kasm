# Tiny Mode

KASM v1.4 includes `--tiny` and `-Oz`, a conservative size-oriented assembly mode.

Tiny mode keeps source semantics intact. It only chooses shorter encodings for the same instruction form when KASM can prove the shorter encoding is valid.

## CLI

```sh
./kasm --tiny examples/tiny_exit.asm -o tiny_exit
./kasm -Oz --tiny-report examples/tiny_hello.asm -o tiny_hello
```

`--tiny-report` enables tiny mode and prints:

- jumps shortened
- imm8 encodings used
- near jumps required
- estimated bytes saved
- final output size

## Optimizations

- short `jmp rel8` and `jcc rel8` when the target is in range
- automatic fallback to near jumps when the target is out of range
- forward and backward label relaxation
- sign-extended imm8 encodings for supported arithmetic/logic/compare forms
- shorter `mov r32, imm32` when the source explicitly uses a 32-bit register
- compact single-segment direct ELF layout for `--tiny` executables

## Safety Rules

Tiny mode does not replace `mov reg, 0` with `xor reg, reg`, because that changes flags. It does not silently rewrite `rax` to `eax`. It does not optimize across data boundaries or perform compiler-style control-flow analysis.

Object output keeps relocatable jumps in near form when a relocation is needed.
