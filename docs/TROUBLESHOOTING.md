# Troubleshooting

KASM diagnostics start with `file:line:column: error:` and then print the source line with a caret.

Common fixes:

- `unknown register`: check spelling such as `rax`, `rdi`, `rsp`, or `r8` through `r15`.
- `unknown instruction`: check the mnemonic and compare with `docs/INSTRUCTIONS.md`.
- `undefined symbol`: define the label or use `extern` with `-f obj`/`-f elf64-obj` and link later.
- `duplicate symbol`: labels and `define` names must be unique in one assembly unit.
- `ambiguous memory operand size`: load the immediate into a register first, then store the register.
- `invalid memory operand`: use supported forms such as `[rax]`, `[rax + 8]`, `[rax + rbx*2]`, or `[rel label]`.
- `wrong argument count for syscall`: see `docs/SYSCALL_SUGAR.md`.

For release checks, run:

```sh
make clean
make test
make release
make size
make bench
make dist
```

`make asan` and `make ubsan` build sanitizer variants when the local compiler supports them.
