# KASM v1.4 Instruction Subset

KASM v1.4 intentionally supports a small x86-64 subset. Registers include `rax`, `rcx`, `rdx`, `rbx`, `rsp`, `rbp`, `rsi`, `rdi`, `r8` through `r15`, 32-bit aliases `eax`, `ecx`, `edx`, `ebx`, `esp`, `ebp`, `esi`, `edi`, and limited 16/8-bit aliases for immediate moves: `ax`, `bx`, `cx`, `dx`, `si`, `di`, `bp`, `sp`, `al`, `bl`, `cl`, `dl`.

`r64` means a supported 64-bit register. `r32` means a supported 32-bit alias. `r16` and `r8` are only documented for immediate `mov`. `[mem]` means a supported memory operand from `SYNTAX.md`.

| Instruction | Supported forms |
| --- | --- |
| `mov` | `mov r64, imm32`; `mov r64, imm64`; `mov r64, r64`; `mov r64, [mem]`; `mov [mem], r64`; `mov r32, imm32`; `mov r32, r32`; `mov r16, imm16`; `mov r8, imm8` |
| `lea` | `lea r64, [rel symbol]`; `lea r64, [mem]` |
| `add` | `add r64, imm32`; `add r64, r64`; `add r64, [mem]`; `add [mem], r64` |
| `sub` | `sub r64, imm32`; `sub r64, r64`; `sub r64, [mem]`; `sub [mem], r64` |
| `and` | `and r64, imm32`; `and r64, r64`; `and r64, [mem]`; `and [mem], r64` |
| `or` | `or r64, imm32`; `or r64, r64`; `or r64, [mem]`; `or [mem], r64` |
| `xor` | `xor r64, imm32`; `xor r64, r64`; `xor r64, [mem]`; `xor [mem], r64` |
| `cmp` | `cmp r64, imm32`; `cmp r64, r64`; `cmp r64, [mem]`; `cmp [mem], r64` |
| `test` | `test r64, imm32`; `test r64, r64` |
| `inc` | `inc r64` |
| `dec` | `dec r64` |
| `neg` | `neg r64` |
| `not` | `not r64` |
| `push` | `push r64`; `push imm32` |
| `pop` | `pop r64` |
| `jmp` | `jmp symbol` |
| `call` | `call symbol` |
| `je`, `jz` | `je symbol`; `jz symbol` |
| `jne`, `jnz` | `jne symbol`; `jnz symbol` |
| `jg`, `jge`, `jl`, `jle` | signed conditional branches to `symbol` |
| `ja`, `jae`, `jb`, `jbe` | unsigned conditional branches to `symbol` |
| `syscall` | raw Linux syscall instruction |
| `ret` | return |

Named syscall sugar such as `syscall write, STDOUT, msg, msg_len` is documented separately in `SYSCALL_SUGAR.md`; it expands to supported `mov`, `lea`, and raw `syscall` instructions.

Branches and calls use near `rel32` encodings. KASM pads fixed internal instruction slots with `NOP` bytes to keep two-pass assembly stable.

Unsupported instruction forms are errors rather than silently rewritten.
