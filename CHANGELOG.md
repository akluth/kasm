# Changelog

## 1.9.0

KASM v1.9 improves debuggability and introspection.

- Added `--dump-all` for stable symbol, section, and relocation dumps in one command.
- Extended dump tables with source locations, section alignment, file offset fields, and stable headings.
- Added `kasm inspect file.o` with `--symbols`, `--sections`, and `--relocs` for ELF64 relocatable object files.
- Added project build dump flags such as `kasm build --dump-symbols`.
- Improved include and macro diagnostics with hints and origin lines.
- Added debugging/introspection documentation and tests.

## 1.8.0

KASM v1.8 adds a small real-project build mode.

- Added `kasm build` for strict `kasm.toml` project files.
- Added `--config`, `--verbose`, and `--no-link` build options.
- Assembled project sources into `out_dir/*.o` and linked executable projects through `ld` by default.
- Added diagnostics for missing/malformed configs, missing fields, missing sources, duplicate object outputs, assembler failures, and linker failures.
- Added `examples/project_hello/` and project build documentation.

## 1.7.0

KASM v1.7 hardens diagnostics, release checks, examples, and smoke coverage.

- Added stable hint text for common diagnostic mistakes.
- Added `tests/corpus/` with valid, invalid, encoding, ELF, CLI, and teach/explain fixtures.
- Added deterministic invalid-input fuzz smoke tests.
- Added missing release-pack examples: `read_write.asm`, `labels_and_jumps.asm`, `data_struct_layout.asm`, `explain_demo.asm`, and `teaching_demo.asm`.
- Added `make ubsan` and CI sanitizer build steps.
- Added troubleshooting and release documentation.

## 1.6.0

KASM v1.6 adds Teaching Mode for guided assembly, syscall, encoding, and ELF walkthroughs.

- Added `--teach`.
- Added `--teach-level beginner|intermediate|deep`.
- Added program overview, source walkthrough, register/ABI notes, ELF overview, and final run note output.
- Added syscall sugar expansion notes and label reference explanations.
- Added `examples/teach_hello.asm` and `docs/TEACHING_MODE.md`.

## 1.5.0

KASM v1.5 adds ELF Surgeon Mode for inspecting generated ELF64 output.

- Added `--elf-info`.
- Added ELF header, program header, section, symbol, relocation, and entry point explanation output.
- Covered both direct ELF64 executables and ELF64 relocatable object files.
- Added byte-preservation tests for `--elf-info`.
- Added `docs/ELF_SURGEON_MODE.md`.

## 1.4.5

KASM v1.4.5 adds conservative multi-input assembly before v1.5.

- Added support for `kasm main.asm util.asm`, assembling each input independently as `main.o`, `util.o`, and so on.
- Preserved single-input output modes and added default output derivation for plain `kasm main.asm`.
- Rejected ambiguous multi-input `-o` usage and duplicate derived output names.
- Recognized `--combine` but return `error: --combine is not implemented yet`.
- Updated diagnostics to start with `file:line:column: error:`.

## 1.4.0

KASM v1.4 improves Linux syscall sugar and makes it more predictable in tests, explain output, and tiny mode.

- Added checked syscall sugar for `read`, `write`, `open`, `close`, `stat`, `fstat`, `mmap`, `mprotect`, `munmap`, `brk`, `exit`, `exit_group`, and `openat`.
- Added argument count diagnostics for named syscalls.
- Added `--no-syscall-sugar`.
- Added built-in constants `STDIN`, `STDOUT`, `STDERR`, `AT_FDCWD`, `O_*`, `PROT_*`, and `MAP_*`.
- Added `examples/syscall_sugar_hello.asm` and `docs/SYSCALL_SUGAR.md`.

## 1.3.0

KASM v1.3 adds optional educational microarchitecture and ABI hints.

- Added `--hints`.
- Added `--hints=perf,abi,size,all`.
- Added `--hints-cpu generic|intel|amd|zen4|skylake`.
- Added stable hint IDs for zeroing idioms, syscall ABI notes, partial-register hazards, stack alignment, size opportunities, and addressing complexity.
- Added minimal 8/16-bit immediate move support for partial-register hint examples.
- Added `examples/hints.asm` and `docs/HINTS.md`.

## 1.2.0

KASM v1.2 adds conservative Tiny Mode for smaller binaries.

- Added `--tiny` and `-Oz`.
- Added `--tiny-report`.
- Added short jump relaxation for in-range `jmp` and conditional branches.
- Added automatic near-jump fallback when a branch is out of range.
- Added imm8 encodings for supported same-instruction arithmetic/logic/compare forms.
- Added shorter explicit `mov r32, imm32` encoding in tiny mode.
- Added compact single-segment direct ELF output for `--tiny`.
- Added `examples/tiny_exit.asm`, `examples/tiny_hello.asm`, and `docs/TINY_MODE.md`.

## 1.1.0

KASM v1.1 turns explain mode into a more serious encoding study tool while preserving the existing `--explain` and `--explain=verbose` outputs.

- Added `--explain=deluxe` with structured source location, normalized instruction, virtual address, byte length, encoding form, prefixes, opcode, ModRM/SIB breakdowns, displacement, immediate, relocation, and note fields.
- Added `--explain-format text` as an explicit text-format selector.
- Added `examples/explain_exit.asm`.
- Added `docs/ENCODING_EXPLAINED.md`.
- Expanded regression tests for representative deluxe explanations.

## 1.0.0

KASM v1.0 stabilizes the project as a tiny Linux x86-64 assembler focused on direct ELF64 output, no-libc programs, binary layout work, object files, and transparent explain output.

Headline features:

- Frozen v1.0 syntax documentation and instruction-form table.
- Curated numbered examples for raw syscalls, syscall sugar, stdlib, object output, structs, explain mode, and read/echo.
- Restructured documentation under `docs/`.
- Finalized man page for the v1.0 CLI.
- Release archive now creates `dist/kasm-1.0.0.tar.gz`.
- Additional regression coverage for CLI help grouping, numbered examples, stdlib include files, NASM-style unsupported syntax, and release artifacts.

Supported platform:

- Linux x86-64.

Known limitations:

- KASM is not a full NASM replacement.
- Limited instruction subset and relocation types.
- Direct executable mode does not generate dynamic linker metadata.
- No debug info/DWARF, full expression language, full C preprocessor, or libc.
- Stdlib is macro/include-based and does not handle syscall errors automatically.

Migration notes from v0.9:

- No intentional source-breaking changes.
- `kasm --version` now reports `KASM 1.0.0`.
- Distribution archive name changed from `kasm-0.9.0.tar.gz` to `kasm-1.0.0.tar.gz`.

## 0.9.0

- Added release, debug, size, bench, install, uninstall, dist, and asan Makefile targets.
- Added benchmark fixtures and shell benchmark runner.
- Added GitHub Actions CI workflow.
- Added man page and installable stdlib path support.
- Added `KASM_INCLUDE_PATH`, `--print-include-paths`, and `--no-stdlib`.

## 0.8.0

- Added tiny Linux inline stdlib under `lib/kasm`.
- Added stdlib examples for stdout, stderr, and read/echo.

## 0.7.0

- Added verbose explain mode, explain files, map files, and listing files.

## 0.6.0

- Added structs, typed binary fields, `sizeof`, `offsetof`, `align`, and layout examples.

## 0.5.0

- Added includes, defines, macros, macro-local labels, and dump-expanded output.

## 0.4.0

- Added arithmetic/bitwise instructions, more branches, and basic memory operands.

## 0.3.0

- Added ELF64 relocatable object output, global/extern, symbol tables, and relocations.

## 0.2.0

- Improved diagnostics, parser robustness, dump modes, tests, and documentation.

## 0.1.0

- Initial tiny Linux x86-64 assembler with direct ELF64 output, raw binary output, syscall sugar, and explain mode.
