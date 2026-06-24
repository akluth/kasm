#!/bin/sh
set -eu

tmp=tests/tmp
rm -rf "$tmp"
mkdir -p "$tmp"

pass() {
    printf 'PASS %s\n' "$1"
}

fail() {
    printf 'FAIL %s\n' "$1" >&2
    exit 1
}

expect_fail() {
    name=$1
    asm=$2
    pattern=$3
    if ./kasm "$asm" -o "$tmp/$name.out" 2> "$tmp/$name.err"; then
        fail "$name should fail"
    fi
    grep -q "$pattern" "$tmp/$name.err" || {
        cat "$tmp/$name.err" >&2
        fail "$name missing pattern: $pattern"
    }
    grep -q "$asm" "$tmp/$name.err" || fail "$name missing filename"
    grep -Eq "$asm:[0-9]+:[0-9]+: error:" "$tmp/$name.err" || fail "$name missing primary diagnostic location"
    grep -q " --> " "$tmp/$name.err" || fail "$name missing diagnostic location"
    pass "$name"
}

./kasm --version | grep -q "KASM 1.9.0"
./kasm --help | grep -q -- "--dump-symbols"
./kasm --help | grep -q -- "--dump-all"
./kasm --help | grep -q -- "kasm inspect"
./kasm --help | grep -q -- "elf64-obj"
./kasm --help | grep -q -- "--dump-expanded"
./kasm --help | grep -q -- "--explain=deluxe"
./kasm --help | grep -q -- "--explain-format text"
./kasm --help | grep -q -- "--elf-info"
./kasm --help | grep -q -- "--teach"
./kasm --help | grep -q -- "--teach-level"
./kasm --help | grep -q -- "--tiny"
./kasm --help | grep -q -- "--tiny-report"
./kasm --help | grep -q -- "--combine"
./kasm --help | grep -q -- "--hints"
./kasm --help | grep -q -- "--hints-cpu"
./kasm --help | grep -q -- "--no-syscall-sugar"
./kasm --help | grep -q -- "kasm build"
./kasm --help | grep -q -- "output:"
./kasm --help | grep -q -- "project build:"
./kasm --help | grep -q -- "include/preprocessor:"
./kasm --help | grep -q -- "syscalls:"
./kasm --help | grep -q -- "explain/list/map:"
./kasm --help | grep -q -- "debug/dump:"
./kasm --help | grep -q -- "general:"
./kasm --print-include-paths | grep -q "lib/kasm"
pass "help/version"

./kasm examples/hello.asm -o "$tmp/hello"
out="$("$tmp/hello")"
[ "$out" = "Hello from KASM" ] || fail "hello output"
pass "hello"

./kasm examples/exit.asm -o "$tmp/exit"
set +e
"$tmp/exit"
code=$?
set -e
[ "$code" -eq 42 ] || fail "exit code"
pass "exit"

./kasm examples/syscall_sugar.asm -o "$tmp/sugar"
out="$("$tmp/sugar")"
[ "$out" = "Hello via syscall sugar" ] || fail "syscall sugar"
pass "syscall sugar"

cat > "$tmp/syscall_sugar_v14.asm" <<'ASM'
entry _start
section .text
_start:
    syscall write, STDOUT, msg, msg_len
    syscall openat, AT_FDCWD, path, O_RDONLY, 0
    syscall close, rax
    syscall exit, 0
section .rodata
msg:
    db "v1.4 syscall sugar", 10
msg_len = $ - msg
path:
    db "/dev/null", 0
ASM
./kasm "$tmp/syscall_sugar_v14.asm" -o "$tmp/syscall_sugar_v14"
out="$("$tmp/syscall_sugar_v14")"
[ "$out" = "v1.4 syscall sugar" ] || fail "syscall sugar v1.4 output"
pass "syscall sugar v1.4"

cat > "$tmp/syscall_sugar_exit.asm" <<'ASM'
entry _start
section .text
_start:
    syscall exit, 0
ASM
./kasm "$tmp/syscall_sugar_exit.asm" -o "$tmp/syscall_sugar_exit"
"$tmp/syscall_sugar_exit"
pass "syscall sugar exit"

./kasm --tiny "$tmp/syscall_sugar_v14.asm" -o "$tmp/syscall_sugar_v14_tiny"
out="$("$tmp/syscall_sugar_v14_tiny")"
[ "$out" = "v1.4 syscall sugar" ] || fail "syscall sugar tiny output"
pass "syscall sugar tiny"

./kasm "$tmp/syscall_sugar_v14.asm" -o "$tmp/syscall_sugar_explain" --explain=deluxe > "$tmp/syscall_sugar_explain.txt"
grep -q "normalized: mov rax, 1" "$tmp/syscall_sugar_explain.txt" || fail "syscall sugar explain rax"
grep -q "normalized: mov rdi, STDOUT" "$tmp/syscall_sugar_explain.txt" || fail "syscall sugar explain fd"
grep -q "normalized: lea rsi, \\[rel msg\\]" "$tmp/syscall_sugar_explain.txt" || fail "syscall sugar explain label pointer"
grep -q "normalized: syscall" "$tmp/syscall_sugar_explain.txt" || fail "syscall sugar explain syscall"
pass "syscall sugar explain"

cat > "$tmp/bad_syscall_count.asm" <<'ASM'
entry _start
section .text
_start:
    syscall write, STDOUT, msg
section .rodata
msg:
    db "x"
ASM
expect_fail "syscall wrong count" "$tmp/bad_syscall_count.asm" "wrong argument count for syscall 'write': expected 3, got 2"
grep -q "hint: see docs/SYSCALL_SUGAR.md" "$tmp/syscall wrong count.err" || fail "syscall wrong count hint"

cat > "$tmp/bad_syscall_unknown.asm" <<'ASM'
entry _start
section .text
_start:
    syscall frobnicate, 0
ASM
expect_fail "syscall unknown" "$tmp/bad_syscall_unknown.asm" "unknown syscall"

cat > "$tmp/bad_syscall_disabled.asm" <<'ASM'
entry _start
section .text
_start:
    syscall exit, 0
ASM
if ./kasm --no-syscall-sugar "$tmp/bad_syscall_disabled.asm" -o "$tmp/bad_syscall_disabled" 2> "$tmp/bad_syscall_disabled.err"; then
    fail "disabled syscall sugar should fail"
fi
grep -q "syscall sugar is disabled" "$tmp/bad_syscall_disabled.err" || fail "disabled syscall sugar diagnostic"
pass "no syscall sugar"

cat > "$tmp/bad_syscall_pointer.asm" <<'ASM'
entry _start
section .text
_start:
    syscall write, STDOUT, msg + 1, 1
section .rodata
msg:
    db "x"
ASM
expect_fail "syscall pointer expression" "$tmp/bad_syscall_pointer.asm" "unsupported pointer argument"

cat > "$tmp/single_default.asm" <<'ASM'
entry _start
section .text
_start:
    syscall exit, 0
ASM
(cd "$tmp" && ../../kasm single_default.asm)
[ -x "$tmp/single_default" ] || fail "single input default output"
"$tmp/single_default"
pass "single input default output"

mkdir -p "$tmp/multi"
cat > "$tmp/multi/main.asm" <<'ASM'
global _start
extern util_exit
section .text
_start:
    call util_exit
ASM
cat > "$tmp/multi/util.asm" <<'ASM'
global util_exit
section .text
util_exit:
    syscall exit, 0
ASM
./kasm "$tmp/multi/main.asm" "$tmp/multi/util.asm"
[ -s main.o ] || fail "multi main.o missing"
[ -s util.o ] || fail "multi util.o missing"
ld main.o util.o -o "$tmp/multi/app"
"$tmp/multi/app"
rm -f main.o util.o
pass "multiple inputs produce objects"

cat > "$tmp/multi/raw.asm" <<'ASM'
global raw_exit
section .text
raw_exit:
    mov rax, 60
    xor rdi, rdi
    syscall
ASM
cat > "$tmp/multi/sugar.asm" <<'ASM'
global sugar_exit
section .text
sugar_exit:
    syscall exit, 0
ASM
./kasm "$tmp/multi/raw.asm" "$tmp/multi/sugar.asm"
[ -s raw.o ] || fail "multi raw syscall object"
[ -s sugar.o ] || fail "multi sugar syscall object"
rm -f raw.o sugar.o
pass "multi raw syscall and sugar"

if ./kasm "$tmp/multi/main.asm" "$tmp/multi/util.asm" -o "$tmp/multi/app.o" 2> "$tmp/multi_one_output.err"; then
    fail "multi input one -o should fail"
fi
grep -q "multiple input files with one -o output are ambiguous" "$tmp/multi_one_output.err" || fail "multi one output diagnostic"
pass "multi one output error"

mkdir -p "$tmp/multi/a" "$tmp/multi/b"
cat > "$tmp/multi/a/util.asm" <<'ASM'
section .text
global a_util
a_util:
    ret
ASM
cat > "$tmp/multi/b/util.asm" <<'ASM'
section .text
global b_util
b_util:
    ret
ASM
if ./kasm "$tmp/multi/a/util.asm" "$tmp/multi/b/util.asm" 2> "$tmp/multi_dup.err"; then
    fail "duplicate multi output should fail"
fi
grep -q "duplicate output filename 'util.o'" "$tmp/multi_dup.err" || fail "duplicate output diagnostic"
pass "duplicate output error"

if ./kasm --combine "$tmp/multi/main.asm" "$tmp/multi/util.asm" 2> "$tmp/combine.err"; then
    fail "--combine should fail"
fi
grep -q -- "--combine is not implemented yet" "$tmp/combine.err" || fail "--combine diagnostic"
if ./kasm --combine "$tmp/multi/main.asm" "$tmp/multi/util.asm" -o "$tmp/combine.o" 2> "$tmp/combine_o.err"; then
    fail "--combine with -o should fail"
fi
grep -q -- "--combine is not implemented yet" "$tmp/combine_o.err" || fail "--combine -o diagnostic"
pass "combine not implemented"

if ./kasm -f bin "$tmp/multi/main.asm" "$tmp/multi/util.asm" 2> "$tmp/multi_bin.err"; then
    fail "multi input bin format should fail"
fi
grep -q "multiple input files only support object output" "$tmp/multi_bin.err" || fail "multi bin diagnostic"
pass "multi invalid format"

mkdir -p "$tmp/project/src"
cat > "$tmp/project/kasm.toml" <<'TOML'
[project]
name = "hello"
type = "executable"

[build]
sources = ["src/main.asm", "src/util.asm"]
out_dir = "build"
output = "hello"
linker = "ld"
entry = "_start"
TOML
cat > "$tmp/project/src/main.asm" <<'ASM'
global _start
section .text
_start:
    syscall write, STDOUT, msg, msg_len
    syscall exit, 0
section .rodata
msg:
    db "project hello", 10
msg_len = $ - msg
ASM
cat > "$tmp/project/src/util.asm" <<'ASM'
global project_marker
section .rodata
project_marker:
    db "project-data", 0
ASM
./kasm build --config "$tmp/project/kasm.toml" --no-link --verbose > "$tmp/project_no_link.out"
[ -s "$tmp/project/build/main.o" ] || fail "project main.o missing"
[ -s "$tmp/project/build/util.o" ] || fail "project util.o missing"
grep -q "assemble:" "$tmp/project_no_link.out" || fail "project verbose assemble"
grep -q "link: skipped (--no-link)" "$tmp/project_no_link.out" || fail "project no-link verbose"
pass "project build no-link"

./kasm build --config "$tmp/project/kasm.toml" --verbose > "$tmp/project_link.out"
[ -x "$tmp/project/build/hello" ] || fail "project executable missing"
out="$("$tmp/project/build/hello")"
[ "$out" = "project hello" ] || fail "project executable output"
grep -q "link:" "$tmp/project_link.out" || fail "project verbose link"
pass "project build link"

./kasm build --config "$tmp/project/kasm.toml" --no-link --dump-symbols > "$tmp/project_dump_symbols.out"
grep -q "Symbols:" "$tmp/project_dump_symbols.out" || fail "project dump symbols header"
grep -q "project_marker" "$tmp/project_dump_symbols.out" || fail "project dump symbols per file"
pass "project build dump symbols"

mkdir -p "$tmp/project_default"
cp "$tmp/project/kasm.toml" "$tmp/project_default/kasm.toml"
mkdir -p "$tmp/project_default/src"
cp "$tmp/project/src/main.asm" "$tmp/project_default/src/main.asm"
cp "$tmp/project/src/util.asm" "$tmp/project_default/src/util.asm"
(cd "$tmp/project_default" && ../../../kasm build --no-link)
[ -s "$tmp/project_default/build/main.o" ] || fail "project default config"
pass "project default config"

mkdir -p "$tmp/project_bad"
if (cd "$tmp/project_bad" && ../../../kasm build 2> "$PWD/missing.err"); then
    fail "project missing config should fail"
fi
grep -q "missing config 'kasm.toml'" "$tmp/project_bad/missing.err" || fail "project missing config diagnostic"

cat > "$tmp/project_bad/malformed.toml" <<'TOML'
[project]
name = "bad"
type = "executable"

[build]
sources "src/main.asm"
TOML
if ./kasm build --config "$tmp/project_bad/malformed.toml" 2> "$tmp/project_bad/malformed.err"; then
    fail "project malformed config should fail"
fi
grep -q "malformed config" "$tmp/project_bad/malformed.err" || fail "project malformed diagnostic"

cat > "$tmp/project_bad/missing_fields.toml" <<'TOML'
[project]
name = "bad"
type = "executable"

[build]
sources = ["src/main.asm"]
out_dir = "build"
TOML
if ./kasm build --config "$tmp/project_bad/missing_fields.toml" 2> "$tmp/project_bad/missing_fields.err"; then
    fail "project missing fields should fail"
fi
grep -q "missing required fields" "$tmp/project_bad/missing_fields.err" || fail "project missing fields diagnostic"

cat > "$tmp/project_bad/missing_source.toml" <<'TOML'
[project]
name = "bad"
type = "executable"

[build]
sources = ["src/missing.asm"]
out_dir = "build"
output = "bad"
TOML
if ./kasm build --config "$tmp/project_bad/missing_source.toml" 2> "$tmp/project_bad/missing_source.err"; then
    fail "project missing source should fail"
fi
grep -q "missing source file" "$tmp/project_bad/missing_source.err" || fail "project missing source diagnostic"

mkdir -p "$tmp/project_dup/a" "$tmp/project_dup/b"
cat > "$tmp/project_dup/kasm.toml" <<'TOML'
[project]
name = "dup"
type = "executable"

[build]
sources = ["a/util.asm", "b/util.asm"]
out_dir = "build"
output = "dup"
TOML
printf 'section .text\na:\n    ret\n' > "$tmp/project_dup/a/util.asm"
printf 'section .text\nb:\n    ret\n' > "$tmp/project_dup/b/util.asm"
if ./kasm build --config "$tmp/project_dup/kasm.toml" --no-link 2> "$tmp/project_dup/dup.err"; then
    fail "project duplicate outputs should fail"
fi
grep -q "duplicate source output" "$tmp/project_dup/dup.err" || fail "project duplicate output diagnostic"

mkdir -p "$tmp/project_asm_fail/src"
cat > "$tmp/project_asm_fail/kasm.toml" <<'TOML'
[project]
name = "badasm"
type = "executable"

[build]
sources = ["src/main.asm"]
out_dir = "build"
output = "badasm"
TOML
printf 'entry _start\nsection .text\n_start:\n    frobnicate rax\n' > "$tmp/project_asm_fail/src/main.asm"
if ./kasm build --config "$tmp/project_asm_fail/kasm.toml" --no-link 2> "$tmp/project_asm_fail/asm.err"; then
    fail "project assembler failure should fail"
fi
grep -q "assembler failure" "$tmp/project_asm_fail/asm.err" || fail "project assembler failure diagnostic"

mkdir -p "$tmp/project_link_fail/src"
cat > "$tmp/project_link_fail/kasm.toml" <<'TOML'
[project]
name = "badlink"
type = "executable"

[build]
sources = ["src/main.asm"]
out_dir = "build"
output = "badlink"
linker = "false"
TOML
printf 'global _start\nsection .text\n_start:\n    syscall exit, 0\n' > "$tmp/project_link_fail/src/main.asm"
if ./kasm build --config "$tmp/project_link_fail/kasm.toml" 2> "$tmp/project_link_fail/link.err"; then
    fail "project linker failure should fail"
fi
grep -q "linker failure" "$tmp/project_link_fail/link.err" || fail "project linker failure diagnostic"
pass "project build errors"

./kasm examples/exit.asm -o "$tmp/exit_normal_size"
./kasm --tiny examples/exit.asm -o "$tmp/exit_tiny_size"
normal_size=$(wc -c < "$tmp/exit_normal_size")
tiny_size=$(wc -c < "$tmp/exit_tiny_size")
[ "$tiny_size" -lt "$normal_size" ] || fail "tiny direct ELF should be smaller"
set +e
"$tmp/exit_tiny_size"
code=$?
set -e
[ "$code" -eq 42 ] || fail "tiny direct exit behavior"
pass "tiny direct ELF size/behavior"

cat > "$tmp/tiny_jumps.asm" <<'ASM'
entry _start
section .text
_start:
    jmp done
    mov rdi, 1
done:
    mov rax, 60
    syscall
ASM
./kasm --tiny "$tmp/tiny_jumps.asm" -f bin -o "$tmp/tiny_jumps.bin"
od -An -tx1 -N1 "$tmp/tiny_jumps.bin" | grep -q "eb" || fail "tiny short jmp"
pass "tiny short forward jump"

cat > "$tmp/tiny_near.asm" <<'ASM'
entry _start
section .text
_start:
    jmp far_label
    mov rax, 1
    mov rax, 2
    mov rax, 3
    mov rax, 4
    mov rax, 5
    mov rax, 6
    mov rax, 7
    mov rax, 8
    mov rax, 9
    mov rax, 10
    mov rax, 11
    mov rax, 12
    mov rax, 13
    mov rax, 14
    mov rax, 15
    mov rax, 16
    mov rax, 17
    mov rax, 18
    mov rax, 19
    mov rax, 20
    mov rax, 21
    mov rax, 22
    mov rax, 23
    mov rax, 24
far_label:
    mov rax, 60
    xor rdi, rdi
    syscall
ASM
./kasm --tiny "$tmp/tiny_near.asm" -f bin -o "$tmp/tiny_near.bin"
od -An -tx1 -N1 "$tmp/tiny_near.bin" | grep -q "e9" || fail "tiny near jmp"
pass "tiny near jump fallback"

cat > "$tmp/tiny_back.asm" <<'ASM'
entry _start
section .text
_start:
    mov rax, 2
again:
    dec rax
    cmp rax, 0
    jne again
    mov rax, 60
    xor rdi, rdi
    syscall
ASM
./kasm --tiny "$tmp/tiny_back.asm" -f bin -o "$tmp/tiny_back.bin"
od -An -tx1 "$tmp/tiny_back.bin" | grep -q "75" || fail "tiny short backward jump"
pass "tiny short backward jump"

cat > "$tmp/tiny_imm.asm" <<'ASM'
entry _start
section .text
_start:
    cmp rax, 1
    cmp rax, 200
    mov eax, 60
    syscall
ASM
./kasm --tiny "$tmp/tiny_imm.asm" -f bin -o "$tmp/tiny_imm.bin"
od -An -tx1 "$tmp/tiny_imm.bin" > "$tmp/tiny_imm.hex"
grep -q "48 83 f8 01" "$tmp/tiny_imm.hex" || fail "tiny imm8 cmp"
grep -q "48 81 f8 c8 00 00 00" "$tmp/tiny_imm.hex" || fail "tiny imm32 cmp"
grep -q "b8 3c 00 00 00" "$tmp/tiny_imm.hex" || fail "tiny mov r32 accumulator"
pass "tiny immediate/register-width encodings"

./kasm --tiny --tiny-report "$tmp/tiny_imm.asm" -o "$tmp/tiny_report_bin" > "$tmp/tiny_report.txt"
grep -q "Tiny mode report:" "$tmp/tiny_report.txt" || fail "tiny report header"
grep -q "imm8 encodings used:" "$tmp/tiny_report.txt" || fail "tiny report imm8"
grep -q "estimated bytes saved:" "$tmp/tiny_report.txt" || fail "tiny report savings"
grep -q "final output size:" "$tmp/tiny_report.txt" || fail "tiny report final size"
pass "tiny report"

./kasm --tiny examples/tiny_exit.asm -o "$tmp/tiny_exit_example"
set +e
"$tmp/tiny_exit_example"
code=$?
set -e
[ "$code" -eq 42 ] || fail "tiny_exit example"
./kasm --tiny --tiny-report examples/tiny_hello.asm -o "$tmp/tiny_hello_example" > "$tmp/tiny_hello_report.txt"
out="$("$tmp/tiny_hello_example")"
[ "$out" = "tiny hello" ] || fail "tiny_hello example"
grep -q "Tiny mode report:" "$tmp/tiny_hello_report.txt" || fail "tiny_hello report"
pass "tiny examples"

./kasm examples/hints.asm -o "$tmp/hints_no_flag" 2> "$tmp/hints_no_flag.err"
[ ! -s "$tmp/hints_no_flag.err" ] || fail "hints printed without --hints"
./kasm examples/hints.asm -o "$tmp/hints_with_flag" --hints 2> "$tmp/hints.err"
cmp "$tmp/hints_no_flag" "$tmp/hints_with_flag" || fail "hints changed output bytes"
grep -q "KASM-HINT-ZEROING-001" "$tmp/hints.err" || fail "zeroing hint"
grep -q "KASM-HINT-PARTIAL-001" "$tmp/hints.err" || fail "partial register hint"
grep -q "KASM-HINT-SYSCALL-001" "$tmp/hints.err" || fail "syscall ABI hint"
grep -q "KASM-HINT-SIZE-002" "$tmp/hints.err" || fail "size imm8 hint"
grep -q "KASM-HINT-ADDR-001" "$tmp/hints.err" || fail "addressing hint"
pass "hints default"

./kasm examples/hints.asm -o "$tmp/hints_abi" --hints=abi 2> "$tmp/hints_abi.err"
grep -q "KASM-HINT-SYSCALL-001" "$tmp/hints_abi.err" || fail "abi hint category"
if grep -q "KASM-HINT-ZEROING-001" "$tmp/hints_abi.err"; then
    fail "abi hint category leaked perf hint"
fi
pass "hints category"

cat > "$tmp/hints_stack.asm" <<'ASM'
entry _start
section .text
_start:
    push rbp
    call target
target:
    mov rax, 60
    xor rdi, rdi
    syscall
ASM
./kasm "$tmp/hints_stack.asm" -o "$tmp/hints_stack" --hints=perf 2> "$tmp/hints_stack.err"
grep -q "KASM-HINT-STACK-001" "$tmp/hints_stack.err" || fail "stack alignment hint"
pass "hints stack"

if ./kasm examples/hints.asm --hints=nope -o "$tmp/bad_hints" 2> "$tmp/bad_hints.err"; then
    fail "invalid hints category should fail"
fi
grep -q "unknown hint category" "$tmp/bad_hints.err" || fail "invalid hints category diagnostic"

if ./kasm examples/hints.asm --hints-cpu potato -o "$tmp/bad_hints_cpu" 2> "$tmp/bad_hints_cpu.err"; then
    fail "invalid hints cpu should fail"
fi
grep -q "unknown hints CPU profile" "$tmp/bad_hints_cpu.err" || fail "invalid hints cpu diagnostic"
pass "hints invalid CLI"

./kasm examples/01_hello_raw_syscall.asm -o "$tmp/ex01"
out="$("$tmp/ex01")"
[ "$out" = "Hello from KASM" ] || fail "example 01"
./kasm examples/02_hello_syscall_sugar.asm -o "$tmp/ex02"
out="$("$tmp/ex02")"
[ "$out" = "Hello via syscall sugar" ] || fail "example 02"
./kasm examples/03_hello_stdlib.asm -o "$tmp/ex03"
out="$("$tmp/ex03")"
[ "$out" = "Hello from KASM stdlib" ] || fail "example 03"
./kasm examples/04_exit_code.asm -o "$tmp/ex04"
set +e
"$tmp/ex04"
code=$?
set -e
[ "$code" -eq 42 ] || fail "example 04"
./kasm examples/05_loop.asm -o "$tmp/ex05"
set +e
"$tmp/ex05"
code=$?
set -e
[ "$code" -eq 0 ] || fail "example 05"
./kasm examples/06_stack_memory.asm -o "$tmp/ex06"
set +e
"$tmp/ex06"
code=$?
set -e
[ "$code" -eq 42 ] || fail "example 06"
pass "numbered executable examples"

./kasm examples/hello.asm -o "$tmp/hello_explain" --explain > "$tmp/explain.txt"
grep -q "00000000" "$tmp/explain.txt"
grep -q "encodes:" "$tmp/explain.txt"
grep -q "mov r64, imm32" "$tmp/explain.txt"
pass "explain"

./kasm examples/hello.asm -o "$tmp/hello_explain_normal" --explain=normal > "$tmp/explain_normal.txt"
grep -q "00000000" "$tmp/explain_normal.txt"
grep -q "mov rax, 1" "$tmp/explain_normal.txt"
pass "explain normal"

./kasm examples/hello.asm -o "$tmp/hello_explain_verbose" --explain=verbose > "$tmp/explain_verbose.txt"
grep -q "REX:" "$tmp/explain_verbose.txt"
grep -q "opcode:" "$tmp/explain_verbose.txt"
grep -q "ModRM:" "$tmp/explain_verbose.txt"
grep -q "symbol/displacement" "$tmp/explain_verbose.txt"
pass "explain verbose"

./kasm examples/explain_exit.asm -o "$tmp/explain_exit" --explain=deluxe --explain-format text > "$tmp/explain_deluxe.txt"
grep -q "location: examples/explain_exit.asm" "$tmp/explain_deluxe.txt"
grep -q "normalized: mov eax, 60" "$tmp/explain_deluxe.txt"
grep -q "virtual-address: 0x" "$tmp/explain_deluxe.txt"
grep -q "length: 6" "$tmp/explain_deluxe.txt"
grep -q "form: MOV R32, IMM32" "$tmp/explain_deluxe.txt"
grep -q "opcode: c7" "$tmp/explain_deluxe.txt"
grep -q "ModRM: 0xc0" "$tmp/explain_deluxe.txt"
grep -q "immediate: size=32 value=60" "$tmp/explain_deluxe.txt"
grep -q "note: Linux syscall number is expected in rax" "$tmp/explain_deluxe.txt"
pass "explain deluxe immediate/syscall"

./kasm examples/10_explain_demo.asm -o "$tmp/explain_demo_deluxe" --explain=deluxe > "$tmp/explain_demo_deluxe.txt"
grep -q "REX: 0x4c" "$tmp/explain_demo_deluxe.txt"
grep -q "SIB: 0x24" "$tmp/explain_demo_deluxe.txt"
grep -q "scale: 1" "$tmp/explain_demo_deluxe.txt"
grep -q "displacement: size=32" "$tmp/explain_demo_deluxe.txt"
grep -q "normalized: lea rsi, \\[rel msg\\]" "$tmp/explain_demo_deluxe.txt"
pass "explain deluxe memory/lea"

cat > "$tmp/explain_branch.asm" <<'ASM'
entry _start
section .text
_start:
    cmp rax, 0
    je done
done:
    syscall
ASM
./kasm "$tmp/explain_branch.asm" -o "$tmp/explain_branch" --explain=deluxe > "$tmp/explain_branch.txt"
grep -q "normalized: cmp rax, 0" "$tmp/explain_branch.txt"
grep -q "form: CMP R64, IMM32" "$tmp/explain_branch.txt"
grep -q "normalized: je done" "$tmp/explain_branch.txt"
grep -q "displacement: size=32" "$tmp/explain_branch.txt"
grep -q "near branch using a signed 32-bit displacement" "$tmp/explain_branch.txt"
pass "explain deluxe cmp/branch"

./kasm examples/object_start.asm -f obj -o "$tmp/object_deluxe.o" --explain=deluxe > "$tmp/object_deluxe.txt"
grep -q "relocation:" "$tmp/object_deluxe.txt"
grep -q "R_X86_64_PC32" "$tmp/object_deluxe.txt"
grep -q "symbol=msg" "$tmp/object_deluxe.txt"
pass "explain deluxe relocation"

./kasm examples/hello.asm -o "$tmp/hello_plain"
./kasm examples/hello.asm -o "$tmp/hello_elf_info" --elf-info > "$tmp/hello_elf_info.txt"
cmp "$tmp/hello_plain" "$tmp/hello_elf_info" || fail "elf info changed executable bytes"
grep -q "ELF64 executable: $tmp/hello_elf_info" "$tmp/hello_elf_info.txt" || fail "elf info executable header"
grep -q "entry: 0x401000 (_start)" "$tmp/hello_elf_info.txt" || fail "elf info entry"
grep -q "program header count: 3" "$tmp/hello_elf_info.txt" || fail "elf info ph count"
grep -q "\\[1\\] .text" "$tmp/hello_elf_info.txt" || fail "elf info text section"
grep -q "size=0x78" "$tmp/hello_elf_info.txt" || fail "elf info section size"
grep -q "_start.*section=.text" "$tmp/hello_elf_info.txt" || fail "elf info symbol"
grep -q "The loader starts at 0x401000" "$tmp/hello_elf_info.txt" || fail "elf info entry explanation"
pass "elf info executable"

./kasm examples/object_start.asm -f obj -o "$tmp/object_elf_info_plain.o"
./kasm examples/object_start.asm -f obj -o "$tmp/object_elf_info.o" --elf-info > "$tmp/object_elf_info.txt"
cmp "$tmp/object_elf_info_plain.o" "$tmp/object_elf_info.o" || fail "elf info changed object bytes"
grep -q "ELF64 object: $tmp/object_elf_info.o" "$tmp/object_elf_info.txt" || fail "elf info object header"
grep -q "program header count: 0" "$tmp/object_elf_info.txt" || fail "elf info object ph count"
grep -q "section header count:" "$tmp/object_elf_info.txt" || fail "elf info object sh count"
grep -q ".rela.text" "$tmp/object_elf_info.txt" || fail "elf info relocation section"
grep -q "R_X86_64_PC32" "$tmp/object_elf_info.txt" || fail "elf info relocation type"
grep -q "symbol=msg" "$tmp/object_elf_info.txt" || fail "elf info relocation symbol"
grep -q "_start.*bind=GLOBAL" "$tmp/object_elf_info.txt" || fail "elf info object symbol binding"
pass "elf info object"

if command -v readelf >/dev/null 2>&1; then
    readelf -h "$tmp/hello_elf_info" > "$tmp/readelf_hello_h.txt"
    grep -q "Entry point address:.*0x401000" "$tmp/readelf_hello_h.txt" || fail "elf info readelf entry"
    readelf -S "$tmp/object_elf_info.o" > "$tmp/readelf_object_s.txt"
    grep -q ".rela.text" "$tmp/readelf_object_s.txt" || fail "elf info readelf reloc section"
pass "elf info readelf cross-check"
else
    printf 'SKIP elf info readelf cross-check\n'
fi

./kasm examples/hello.asm -o "$tmp/hello_teach_plain"
./kasm examples/hello.asm -o "$tmp/hello_teach" --teach > "$tmp/hello_teach.txt"
cmp "$tmp/hello_teach_plain" "$tmp/hello_teach" || fail "teach changed executable bytes"
grep -q "Teaching mode: examples/hello.asm" "$tmp/hello_teach.txt" || fail "teach header"
grep -q "Program overview:" "$tmp/hello_teach.txt" || fail "teach overview"
grep -q "syscalls used: write, exit" "$tmp/hello_teach.txt" || fail "teach syscalls"
grep -q "Encoding: 48 c7 c0 01 00 00 00" "$tmp/hello_teach.txt" || fail "teach bytes"
grep -q "rdi, rsi, rdx, r10, r8, and r9" "$tmp/hello_teach.txt" || fail "teach abi"
grep -q "ELF overview:" "$tmp/hello_teach.txt" || fail "teach elf overview"
grep -q "run it with: ./$tmp/hello_teach" "$tmp/hello_teach.txt" || fail "teach run note"
pass "teaching mode hello"

./kasm examples/syscall_sugar_hello.asm -o "$tmp/teach_sugar" --teach > "$tmp/teach_sugar.txt"
grep -q "Syscall sugar:" "$tmp/teach_sugar.txt" || fail "teach syscall sugar"
grep -q "expands this friendly syscall form" "$tmp/teach_sugar.txt" || fail "teach syscall sugar expansion"
grep -q "syscalls used: write, exit" "$tmp/teach_sugar.txt" || fail "teach sugar syscall list"
grep -q "Reference: msg resolves" "$tmp/teach_sugar.txt" || fail "teach label reference"
pass "teaching mode syscall sugar"

./kasm examples/hello.asm -o "$tmp/teach_beginner" --teach-level beginner > "$tmp/teach_beginner.txt"
grep -q "Level: beginner" "$tmp/teach_beginner.txt" || fail "teach beginner level"
if grep -q "Encoding: 48 c7 c0" "$tmp/teach_beginner.txt"; then
    fail "teach beginner should reduce encoding detail"
fi
pass "teaching mode beginner"

./kasm examples/hello.asm -o "$tmp/teach_deep" --teach-level deep > "$tmp/teach_deep.txt"
grep -q "Level: deep" "$tmp/teach_deep.txt" || fail "teach deep level"
grep -q "Trace: source line" "$tmp/teach_deep.txt" || fail "teach deep trace"
grep -q "ELF64 executable:" "$tmp/teach_deep.txt" || fail "teach deep elf info"
pass "teaching mode deep"

if ./kasm examples/hello.asm --teach-level cosmic -o "$tmp/teach_bad" 2> "$tmp/teach_bad.err"; then
    fail "invalid teach level should fail"
fi
grep -q "invalid teach level" "$tmp/teach_bad.err" || fail "invalid teach level diagnostic"
pass "invalid teach level"

for asm in tests/corpus/valid/*.asm; do
    name=$(basename "$asm" .asm)
    ./kasm "$asm" -o "$tmp/corpus_valid_$name"
    [ -s "$tmp/corpus_valid_$name" ] || fail "corpus valid $name output"
done
pass "corpus valid programs"

for asm in tests/corpus/invalid/*.asm; do
    name=$(basename "$asm" .asm)
    if ./kasm "$asm" -o "$tmp/corpus_invalid_$name" 2> "$tmp/corpus_invalid_$name.err"; then
        fail "corpus invalid $name should fail"
    fi
    grep -q "$asm" "$tmp/corpus_invalid_$name.err" || fail "corpus invalid $name filename"
    grep -q "error:" "$tmp/corpus_invalid_$name.err" || fail "corpus invalid $name diagnostic"
done
pass "corpus invalid diagnostics"

./kasm --tiny tests/corpus/encoding/tiny_cmp.asm -f bin -o "$tmp/corpus_tiny_cmp.bin"
od -An -tx1 "$tmp/corpus_tiny_cmp.bin" | grep -q "48 83 f8 01" || fail "corpus encoding tiny cmp"
pass "corpus encoding bytes"

./kasm tests/corpus/elf/object_reloc.asm -f obj -o "$tmp/corpus_object_reloc.o" --elf-info > "$tmp/corpus_object_reloc.txt"
grep -q ".rela.text" "$tmp/corpus_object_reloc.txt" || fail "corpus elf relocation section"
grep -q "R_X86_64_PC32" "$tmp/corpus_object_reloc.txt" || fail "corpus elf relocation type"
pass "corpus elf layout"

./kasm tests/corpus/cli/plain.asm -o "$tmp/corpus_cli_plain"
"$tmp/corpus_cli_plain"
./kasm tests/corpus/explain/teach.asm -o "$tmp/corpus_teach" --teach > "$tmp/corpus_teach.txt"
grep -q "Teaching mode:" "$tmp/corpus_teach.txt" || fail "corpus teach output"
pass "corpus cli/explain"

fuzz_i=0
for fuzz in "@" "section [" "mov [rax + rsp], 1" "macro x" "struct A" "syscall write, 1" "lea rax, [rel]" "db \"unterminated" "times nope db 1" "mov rax, [rax + rbx*3]"; do
    fuzz_i=$((fuzz_i + 1))
    printf 'entry _start\nsection .text\n_start:\n    %s\n' "$fuzz" > "$tmp/fuzz_$fuzz_i.asm"
    set +e
    ./kasm "$tmp/fuzz_$fuzz_i.asm" -o "$tmp/fuzz_$fuzz_i.out" > "$tmp/fuzz_$fuzz_i.stdout" 2> "$tmp/fuzz_$fuzz_i.stderr"
    code=$?
    set -e
    [ "$code" -ne 139 ] || fail "fuzz smoke segfault $fuzz_i"
    [ "$code" -ne 134 ] || fail "fuzz smoke abort $fuzz_i"
done
pass "fuzz invalid input smoke"

./kasm examples/hello.asm --dump-symbols > "$tmp/symbols.txt"
grep -q "Symbols:" "$tmp/symbols.txt"
grep -q "location" "$tmp/symbols.txt"
grep -q "_start" "$tmp/symbols.txt"
grep -q "msg_len" "$tmp/symbols.txt"
pass "dump symbols"

./kasm examples/hello.asm --dump-sections > "$tmp/sections.txt"
grep -q "Sections:" "$tmp/sections.txt"
grep -q "alignment" "$tmp/sections.txt"
grep -q "file_offset" "$tmp/sections.txt"
grep -q ".text" "$tmp/sections.txt"
grep -q ".rodata" "$tmp/sections.txt"
pass "dump sections"

./kasm examples/hello.asm --dump-all > "$tmp/dump_all.txt"
grep -q "Symbols:" "$tmp/dump_all.txt" || fail "dump all symbols"
grep -q "Sections:" "$tmp/dump_all.txt" || fail "dump all sections"
grep -q "Relocations:" "$tmp/dump_all.txt" || fail "dump all relocs"
pass "dump all"

./kasm examples/hello.asm --dump-ir > "$tmp/ir.txt"
grep -q "instr" "$tmp/ir.txt"
grep -q "label" "$tmp/ir.txt"
pass "dump ir"

cat > "$tmp/upper.asm" <<'ASM'
entry _start

SECTION .text
_start:
	MOV     RAX, 60      ; uppercase instruction/register
    XOR     RDI, RDI
    SYSCALL
ASM
./kasm "$tmp/upper.asm" -o "$tmp/upper"
"$tmp/upper"
pass "uppercase and tabs"

printf 'entry _start\r\nsection .text\r\n_start:\r\n    mov rax, 60\r\n    xor rdi, rdi\r\n    syscall\r\n' > "$tmp/crlf.asm"
./kasm "$tmp/crlf.asm" -o "$tmp/crlf"
"$tmp/crlf"
pass "crlf"

cat > "$tmp/undef.asm" <<'ASM'
entry _start
section .text
_start:
    jmp missing
ASM
expect_fail "undefined symbol" "$tmp/undef.asm" "undefined symbol"
grep -q "hint: define the label" "$tmp/undefined symbol.err" || fail "undefined symbol hint"

cat > "$tmp/dup.asm" <<'ASM'
entry _start
section .text
_start:
again:
again:
    ret
ASM
expect_fail "duplicate symbol" "$tmp/dup.asm" "duplicate symbol"
grep -q "hint: labels and defines must be unique" "$tmp/duplicate symbol.err" || fail "duplicate symbol hint"

cat > "$tmp/bad_instr.asm" <<'ASM'
entry _start
section .text
_start:
    nope rax, rbx
ASM
expect_fail "unknown instruction" "$tmp/bad_instr.asm" "unknown instruction"
grep -q "hint: check spelling" "$tmp/unknown instruction.err" || fail "unknown instruction hint"

cat > "$tmp/bad_string.asm" <<'ASM'
entry _start
section .text
_start:
    ret
section .rodata
msg:
    db "unterminated
ASM
expect_fail "unterminated string" "$tmp/bad_string.asm" "unterminated string literal"

cat > "$tmp/bad_reg.asm" <<'ASM'
entry _start
section .text
_start:
    mov nope, 1
ASM
expect_fail "invalid register" "$tmp/bad_reg.asm" "unknown register"
grep -q "hint: check register spelling" "$tmp/invalid register.err" || fail "unknown register hint"

cat > "$tmp/bad_operands.asm" <<'ASM'
entry _start
section .text
_start:
    mov [rax], 1
ASM
expect_fail "invalid operands" "$tmp/bad_operands.asm" "ambiguous memory operand size"
grep -q "hint: store a register first" "$tmp/invalid operands.err" || fail "memory operand hint"

cat > "$tmp/bad_section.asm" <<'ASM'
entry _start
section .bss
_start:
    ret
ASM
expect_fail "invalid section" "$tmp/bad_section.asm" "invalid section"

cat > "$tmp/bad_escape.asm" <<'ASM'
entry _start
section .text
_start:
    ret
section .rodata
msg:
    db "bad\q"
ASM
expect_fail "invalid escape" "$tmp/bad_escape.asm" "invalid escape sequence"

cat > "$tmp/range.asm" <<'ASM'
entry _start
section .text
_start:
    mov rax, 999999999999999999999999999999999999999
ASM
expect_fail "integer range" "$tmp/range.asm" "integer literal out of range"

./kasm examples/hello.asm -f bin -o "$tmp/out.bin"
[ -s "$tmp/out.bin" ] || fail "raw bin"
pass "raw bin"

for pair in "loop 0" "stack 42" "memory 42" "arithmetic 42"; do
    name=$(printf '%s' "$pair" | awk '{print $1}')
    expected=$(printf '%s' "$pair" | awk '{print $2}')
    ./kasm "examples/$name.asm" -o "$tmp/$name"
    set +e
    "$tmp/$name"
    code=$?
    set -e
    [ "$code" -eq "$expected" ] || fail "$name exit code"
    pass "$name example"
done

cat > "$tmp/r8_mem.asm" <<'ASM'
entry _start
section .text
_start:
    push 0
    mov r8, 40
    mov r9, r8
    add r8, 2
    mov qword ptr [rsp], r8
    mov rdi, qword ptr [rsp]
    mov rax, 60
    syscall
ASM
./kasm "$tmp/r8_mem.asm" -o "$tmp/r8_mem"
set +e
"$tmp/r8_mem"
code=$?
set -e
[ "$code" -eq 42 ] || fail "r8/rsp memory encoding"
pass "r8/rsp memory encoding"

cat > "$tmp/r13_mem.asm" <<'ASM'
entry _start
section .text
_start:
    mov r13, rsp
    push 0
    mov rax, 42
    mov qword ptr [r13 - 8], rax
    mov rdi, qword ptr [r13 - 8]
    mov rax, 60
    syscall
ASM
./kasm "$tmp/r13_mem.asm" -o "$tmp/r13_mem"
set +e
"$tmp/r13_mem"
code=$?
set -e
[ "$code" -eq 42 ] || fail "r13 memory encoding"
pass "r13 memory encoding"

cat > "$tmp/bad_scale.asm" <<'ASM'
entry _start
section .text
_start:
    mov rax, qword ptr [rax + rbx*3]
ASM
expect_fail "invalid scale" "$tmp/bad_scale.asm" "invalid scale"

cat > "$tmp/bad_index.asm" <<'ASM'
entry _start
section .text
_start:
    mov rax, qword ptr [rax + rsp]
ASM
expect_fail "invalid index" "$tmp/bad_index.asm" "invalid use of register as index"

./kasm examples/memory.asm -o "$tmp/memory_explain" --explain > "$tmp/memory_explain.txt"
grep -q "ModRM/SIB memory operand" "$tmp/memory_explain.txt" || fail "memory explain"
pass "memory explain"

./kasm examples/memory.asm -o "$tmp/memory_verbose" --explain=verbose > "$tmp/memory_verbose.txt"
grep -q "SIB:" "$tmp/memory_verbose.txt" || fail "memory verbose SIB"
pass "memory verbose"

./kasm examples/include_demo.asm -o "$tmp/include_demo"
out="$("$tmp/include_demo")"
[ "$out" = "Hello from KASM includes" ] || fail "include demo"
pass "include demo"

./kasm examples/macro_print.asm -o "$tmp/macro_print"
out="$("$tmp/macro_print")"
[ "$out" = "Hello macro" ] || fail "macro print"
pass "macro print"

./kasm examples/macro_local.asm -o "$tmp/macro_local"
set +e
"$tmp/macro_local"
code=$?
set -e
[ "$code" -eq 0 ] || fail "macro local"
pass "macro local labels"

cat > "$tmp/define.asm" <<'ASM'
define CODE 42
entry _start
section .text
_start:
    mov rdi, CODE
    mov rax, 60
    syscall
ASM
./kasm "$tmp/define.asm" -o "$tmp/define"
set +e
"$tmp/define"
code=$?
set -e
[ "$code" -eq 42 ] || fail "define constant"
pass "define constant"

mkdir -p "$tmp/inc"
cat > "$tmp/inc/custom.inc" <<'ASM'
macro quit code
    syscall exit, code
end
ASM
cat > "$tmp/custom_include.asm" <<'ASM'
include "custom.inc"
entry _start
section .text
_start:
    quit 42
ASM
./kasm "$tmp/custom_include.asm" -I "$tmp/inc" -o "$tmp/custom_include"
set +e
"$tmp/custom_include"
code=$?
set -e
[ "$code" -eq 42 ] || fail "-I include path"
pass "-I include path"

./kasm examples/include_demo.asm --dump-expanded > "$tmp/expanded.txt"
grep -q "syscall write, stdout, msg, msg_len" "$tmp/expanded.txt" || fail "dump expanded"
pass "dump expanded"

./kasm examples/include_demo.asm -o "$tmp/include_explain" --explain > "$tmp/include_explain.txt"
grep -q "encodes:" "$tmp/include_explain.txt" || fail "include explain"
pass "include explain"

./kasm examples/struct_point.asm -o "$tmp/struct_point"
set +e
"$tmp/struct_point"
code=$?
set -e
[ "$code" -eq 16 ] || fail "sizeof struct exit"
pass "sizeof struct"

./kasm examples/binary_header.asm -f bin -o "$tmp/header.bin"
printf 'KASM\001\000\000\000\000\000\000\000\000\000\000\000' > "$tmp/header.expected"
cmp "$tmp/header.bin" "$tmp/header.expected" || fail "struct raw binary bytes"
pass "struct raw binary"

./kasm examples/binary_header.asm -f bin --dump-structs > "$tmp/structs.txt"
grep -q "Struct Header size=16" "$tmp/structs.txt"
grep -q "entry.*offset=8" "$tmp/structs.txt"
pass "dump structs"

cat > "$tmp/offsetof.asm" <<'ASM'
struct Header
    magic: bytes 4
    version: word
    flags: word
    entry: qword
end
entry _start
section .text
_start:
    mov rdi, offsetof(Header, entry)
    mov rax, 60
    syscall
ASM
./kasm "$tmp/offsetof.asm" -o "$tmp/offsetof"
set +e
"$tmp/offsetof"
code=$?
set -e
[ "$code" -eq 8 ] || fail "offsetof"
pass "offsetof"

cat > "$tmp/align.asm" <<'ASM'
section .rodata
    db 1
    align 4
    db 2
ASM
./kasm "$tmp/align.asm" -f bin -o "$tmp/align.bin"
printf '\001\000\000\000\002' > "$tmp/align.expected"
cmp "$tmp/align.bin" "$tmp/align.expected" || fail "align padding"
pass "align padding"

./kasm examples/struct_point.asm -o "$tmp/struct_explain" --explain > "$tmp/struct_explain.txt"
grep -q "data directive" "$tmp/struct_explain.txt" || fail "struct explain"
pass "struct explain"

./kasm examples/object_start.asm -f obj --explain=verbose -o "$tmp/object_explain.o" > "$tmp/object_explain.txt"
grep -q "relocation:" "$tmp/object_explain.txt" || fail "object explain relocation"
grep -q "R_X86_64_PC32" "$tmp/object_explain.txt" || fail "object explain relocation type"
pass "object explain relocation"

./kasm examples/hello.asm -o "$tmp/map_bin" --map "$tmp/hello.map" --list "$tmp/hello.lst" --explain-file "$tmp/hello.exp" --explain=normal
grep -q "Sections:" "$tmp/hello.map" || fail "map sections"
grep -q "Symbols:" "$tmp/hello.map" || fail "map symbols"
grep -q "00000000" "$tmp/hello.lst" || fail "listing bytes"
grep -q "encodes:" "$tmp/hello.exp" || fail "explain file"
pass "map list explain-file"

if ./kasm examples/hello.asm --explain=banana -o "$tmp/bad_explain" 2> "$tmp/bad_explain.err"; then
    fail "invalid explain mode should fail"
fi
grep -q "invalid explain mode" "$tmp/bad_explain.err" || fail "invalid explain mode diagnostic"
pass "invalid explain mode"

if ./kasm examples/hello.asm --explain-format json -o "$tmp/bad_explain_format" 2> "$tmp/bad_explain_format.err"; then
    fail "invalid explain format should fail"
fi
grep -q "unsupported explain format" "$tmp/bad_explain_format.err" || fail "invalid explain format diagnostic"
grep -q "KASM v1.9 supports --explain-format text" "$tmp/bad_explain_format.err" || fail "invalid explain format hint"
pass "invalid explain format"

./kasm examples/stdlib_hello.asm -o "$tmp/stdlib_hello"
out="$("$tmp/stdlib_hello")"
[ "$out" = "Hello from KASM stdlib" ] || fail "stdlib hello"
pass "stdlib hello"

./kasm examples/stdlib_stderr.asm -o "$tmp/stdlib_stderr"
"$tmp/stdlib_stderr" > "$tmp/stderr.out" 2> "$tmp/stderr.err"
grep -q "Hello stderr from KASM stdlib" "$tmp/stderr.err" || fail "stdlib stderr"
[ ! -s "$tmp/stderr.out" ] || fail "stdlib stderr wrote stdout"
pass "stdlib stderr"

./kasm examples/stdlib_read_echo.asm -o "$tmp/stdlib_echo"
out="$(printf 'echo me' | "$tmp/stdlib_echo")"
[ "$out" = "echo me" ] || fail "stdlib echo"
pass "stdlib read echo"

./kasm examples/stdlib_hello.asm -o "$tmp/stdlib_explain" --explain=verbose > "$tmp/stdlib_explain.txt"
grep -q "mov r64" "$tmp/stdlib_explain.txt" || fail "stdlib explain"
pass "stdlib explain"

./kasm examples/stdlib_hello.asm -o "$tmp/stdlib_map_bin" --map "$tmp/stdlib.map" --list "$tmp/stdlib.lst"
grep -q "Sections:" "$tmp/stdlib.map" || fail "stdlib map"
grep -q "syscall write" "$tmp/stdlib.lst" || fail "stdlib listing expanded syscall"
pass "stdlib map list"

./kasm examples/stdlib_hello.asm -f obj -o "$tmp/stdlib_hello.o"
[ -s "$tmp/stdlib_hello.o" ] || fail "stdlib object"
pass "stdlib object"

cat > "$tmp/stdlib_files_memory.asm" <<'ASM'
include "linux/files.inc"
include "linux/memory.inc"
include "linux/strings.inc"
include "kasm/common.inc"

entry _start
section .text
_start:
    mov rdi, SYS_close
    add rdi, KASM_TRUE
    mov rdi, KASM_STRING_ROUTINES
    mov rax, 60
    syscall
ASM
./kasm "$tmp/stdlib_files_memory.asm" -o "$tmp/stdlib_files_memory"
set +e
"$tmp/stdlib_files_memory"
code=$?
set -e
[ "$code" -eq 0 ] || fail "stdlib files/memory/strings/common"
pass "all stdlib include files"

mkdir -p "$tmp/envinc"
cat > "$tmp/envinc/env.inc" <<'ASM'
macro env_exit code
    syscall exit, code
end
ASM
cat > "$tmp/env_path.asm" <<'ASM'
include "env.inc"
entry _start
section .text
_start:
    env_exit 42
ASM
KASM_INCLUDE_PATH="$tmp/envinc" ./kasm "$tmp/env_path.asm" --no-stdlib -o "$tmp/env_path"
set +e
"$tmp/env_path"
code=$?
set -e
[ "$code" -eq 42 ] || fail "KASM_INCLUDE_PATH"
pass "KASM_INCLUDE_PATH"

if ./kasm -Z 2> "$tmp/unknown_option.err"; then
    fail "unknown option should fail"
fi
grep -q "unknown option" "$tmp/unknown_option.err" || fail "unknown option diagnostic"
grep -q "hint: run 'kasm --help'" "$tmp/unknown_option.err" || fail "unknown option hint"
pass "unknown option"

if ./kasm -o 2> "$tmp/missing_arg.err"; then
    fail "missing option argument should fail"
fi
grep -q "option '-o' requires an argument" "$tmp/missing_arg.err" || fail "missing option argument diagnostic"
pass "missing option argument"

if ./kasm 2> "$tmp/missing_input.err"; then
    fail "missing input should fail"
fi
grep -q "missing input file" "$tmp/missing_input.err" || fail "missing input diagnostic"
pass "missing input"

cat > "$tmp/missing_include.asm" <<'ASM'
include "nope.inc"
entry _start
ASM
expect_fail "missing include" "$tmp/missing_include.asm" "include file not found"
grep -q "hint: add an -I path" "$tmp/missing include.err" || fail "missing include hint"

cat > "$tmp/nasm_syntax.asm" <<'ASM'
BITS 64
section .text
_start:
    ret
ASM
expect_fail "unsupported nasm syntax" "$tmp/nasm_syntax.asm" "unsupported NASM-style syntax"

cat > "$tmp/dup_define.asm" <<'ASM'
define A 1
define A 2
entry _start
section .text
_start:
    mov rax, 60
    xor rdi, rdi
    syscall
ASM
expect_fail "duplicate define" "$tmp/dup_define.asm" "duplicate symbol"

cat > "$tmp/macro_mismatch.asm" <<'ASM'
macro one a
    mov rdi, a
end
entry _start
section .text
_start:
    one
ASM
expect_fail "macro mismatch" "$tmp/macro_mismatch.asm" "macro argument count mismatch"
grep -q "expanded from macro call" "$tmp/macro mismatch.err" || fail "macro origin diagnostic"
grep -q "hint: check the macro definition parameter count" "$tmp/macro mismatch.err" || fail "macro hint diagnostic"

cat > "$tmp/recursive_macro.asm" <<'ASM'
macro rec
    rec
end
entry _start
section .text
_start:
    rec
ASM
expect_fail "recursive macro" "$tmp/recursive_macro.asm" "recursive macro expansion"

cat > "$tmp/missing_end.asm" <<'ASM'
macro nope
    mov rax, 60
ASM
expect_fail "missing macro end" "$tmp/missing_end.asm" "missing macro end"

cat > "$tmp/dup_struct.asm" <<'ASM'
struct A
    x: byte
end
struct A
    y: byte
end
ASM
expect_fail "duplicate struct" "$tmp/dup_struct.asm" "duplicate struct name"

cat > "$tmp/dup_field.asm" <<'ASM'
struct A
    x: byte
    x: word
end
ASM
expect_fail "duplicate field" "$tmp/dup_field.asm" "duplicate field name"

cat > "$tmp/unknown_field.asm" <<'ASM'
struct A
    x: byte
end
section .rodata
A {
    y = 1
}
ASM
expect_fail "unknown field" "$tmp/unknown_field.asm" "unknown field"

cat > "$tmp/long_string.asm" <<'ASM'
struct A
    magic: bytes 2
end
section .rodata
A {
    magic = "TOO"
}
ASM
expect_fail "string too long" "$tmp/long_string.asm" "string too long"

cat > "$tmp/bad_sizeof.asm" <<'ASM'
define X sizeof(Nope)
section .rodata
db X
ASM
expect_fail "invalid sizeof" "$tmp/bad_sizeof.asm" "invalid sizeof"

cat > "$tmp/bad_offsetof.asm" <<'ASM'
struct A
    x: byte
end
define X offsetof(A, y)
section .rodata
db X
ASM
expect_fail "invalid offsetof" "$tmp/bad_offsetof.asm" "invalid offsetof"

cat > "$tmp/bad_align.asm" <<'ASM'
section .rodata
align 3
ASM
expect_fail "invalid align" "$tmp/bad_align.asm" "invalid align value"

./kasm examples/object_start.asm -f elf64-obj -o "$tmp/object_start.o"
[ -s "$tmp/object_start.o" ] || fail "object output"
ld "$tmp/object_start.o" -o "$tmp/object_start"
out="$("$tmp/object_start")"
[ "$out" = "Hello from KASM object" ] || fail "linked object output"
pass "elf64 object ld"

./kasm examples/07_object_ld.asm -f obj -o "$tmp/ex07.o"
ld "$tmp/ex07.o" -o "$tmp/ex07"
out="$("$tmp/ex07")"
[ "$out" = "Hello from KASM object" ] || fail "example 07"
pass "numbered object ld example"

./kasm examples/09_struct_binary_header.asm -f bin -o "$tmp/ex09.bin"
cmp "$tmp/ex09.bin" "$tmp/header.expected" || fail "example 09"
pass "numbered raw struct example"

./kasm examples/10_explain_demo.asm -o "$tmp/ex10" --explain=verbose --map "$tmp/ex10.map" --list "$tmp/ex10.lst" > "$tmp/ex10.explain"
grep -q "REX:" "$tmp/ex10.explain" || fail "example 10 explain REX"
grep -q "opcode:" "$tmp/ex10.explain" || fail "example 10 explain opcode"
grep -q "ModRM:" "$tmp/ex10.explain" || fail "example 10 explain ModRM"
grep -q "SIB:" "$tmp/ex10.explain" || fail "example 10 explain SIB"
grep -q "Sections:" "$tmp/ex10.map" || fail "example 10 map"
grep -q "00000000" "$tmp/ex10.lst" || fail "example 10 listing"
pass "numbered explain/map/list example"

./kasm examples/11_read_echo.asm -o "$tmp/ex11"
out="$(printf 'v1.3 echo' | "$tmp/ex11")"
[ "$out" = "v1.3 echo" ] || fail "example 11"
pass "numbered read echo example"

./kasm examples/labels_and_jumps.asm -o "$tmp/labels_and_jumps"
"$tmp/labels_and_jumps"
./kasm examples/read_write.asm -o "$tmp/read_write"
out="$(printf 'p' | "$tmp/read_write")"
[ "$out" = "p" ] || fail "read_write release example"
./kasm examples/data_struct_layout.asm -f bin -o "$tmp/data_struct_layout.bin"
[ -s "$tmp/data_struct_layout.bin" ] || fail "data_struct_layout release example"
./kasm examples/explain_demo.asm -o "$tmp/explain_demo" --explain=deluxe > "$tmp/explain_demo.txt"
grep -q "location:" "$tmp/explain_demo.txt" || fail "explain_demo release example"
./kasm examples/teaching_demo.asm -o "$tmp/teaching_demo" --teach > "$tmp/teaching_demo.txt"
grep -q "Teaching mode:" "$tmp/teaching_demo.txt" || fail "teaching_demo release example"
pass "release example pack"

if command -v readelf >/dev/null 2>&1; then
    readelf -S "$tmp/object_start.o" > "$tmp/object_sections.txt"
    grep -q ".text" "$tmp/object_sections.txt"
    grep -q ".rela.text" "$tmp/object_sections.txt"
    grep -q ".symtab" "$tmp/object_sections.txt"
    readelf -s "$tmp/object_start.o" > "$tmp/object_symbols.txt"
    grep -q "_start" "$tmp/object_symbols.txt"
    grep -q "msg_len" "$tmp/object_symbols.txt"
    pass "readelf object sections/symbols"
else
    printf 'SKIP readelf object sections/symbols\n'
fi

./kasm examples/object_start.asm -f obj --dump-relocs > "$tmp/relocs.txt"
grep -q "Relocations:" "$tmp/relocs.txt"
grep -q "location" "$tmp/relocs.txt"
pass "dump relocs"

./kasm examples/object_start.asm -f obj -o "$tmp/inspect_object.o"
./kasm inspect "$tmp/inspect_object.o" > "$tmp/inspect_object.txt"
grep -q "Sections:" "$tmp/inspect_object.txt" || fail "inspect sections"
grep -q "Symbols:" "$tmp/inspect_object.txt" || fail "inspect symbols"
grep -q "Relocations:" "$tmp/inspect_object.txt" || fail "inspect relocs"
./kasm inspect "$tmp/inspect_object.o" --symbols > "$tmp/inspect_symbols.txt"
grep -q "_start" "$tmp/inspect_symbols.txt" || fail "inspect symbol _start"
if grep -q "Sections:" "$tmp/inspect_symbols.txt"; then
    fail "inspect --symbols leaked sections"
fi
pass "inspect object"

cat > "$tmp/direct_extern.asm" <<'ASM'
entry _start
extern puts
section .text
_start:
    call puts
ASM
expect_fail "direct extern" "$tmp/direct_extern.asm" "cannot be resolved in direct executable output"

cat > "$tmp/global_undef.asm" <<'ASM'
global missing
section .text
_start:
    ret
ASM
if ./kasm "$tmp/global_undef.asm" -f obj -o "$tmp/global_undef.o" 2> "$tmp/global_undef.err"; then
    fail "global undefined should fail"
fi
grep -q "declared but never defined" "$tmp/global_undef.err" || fail "global undefined diagnostic"
pass "global undefined"

cat > "$tmp/dup_decl.asm" <<'ASM'
global thing
extern thing
section .text
thing:
    ret
ASM
if ./kasm "$tmp/dup_decl.asm" -f obj -o "$tmp/dup_decl.o" 2> "$tmp/dup_decl.err"; then
    fail "duplicate global/extern should fail"
fi
grep -q "cannot be both global and extern\\|duplicate extern/global" "$tmp/dup_decl.err" || fail "duplicate declaration diagnostic"
pass "duplicate global extern"

if command -v gcc >/dev/null 2>&1; then
    ./kasm examples/main_puts.asm -f obj -o "$tmp/main_puts.o"
    if gcc -no-pie "$tmp/main_puts.o" -o "$tmp/main_puts" >/dev/null 2> "$tmp/main_puts.link"; then
        out="$("$tmp/main_puts")"
        [ "$out" = "Hello via puts from KASM" ] || fail "gcc puts output"
        pass "optional gcc puts"
    else
        printf 'SKIP optional gcc puts link\n'
    fi
else
    printf 'SKIP optional gcc puts\n'
fi

echo "ok"
