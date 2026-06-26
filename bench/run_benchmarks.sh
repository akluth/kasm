#!/bin/sh
set -eu

mkdir -p bench/tmp

run_one() {
    name=$1
    src="bench/fixtures/$name.asm"
    out="bench/tmp/$name.out"
    start=$(date +%s%N 2>/dev/null || date +%s)
    ./kasm "$src" -o "$out" >/dev/null
    end=$(date +%s%N 2>/dev/null || date +%s)
    if [ "$end" -gt 1000000000000 ] 2>/dev/null; then
        ms=$(( (end - start) / 1000000 ))
        echo "KASM $name: ${ms} ms"
    else
        echo "KASM $name: completed"
    fi
}

size_compare() {
    name=$1
    src=$2
    normal="bench/tmp/${name}_normal.out"
    tiny="bench/tmp/${name}_tiny.out"
    ./kasm "$src" -o "$normal" >/dev/null
    ./kasm --tiny "$src" -o "$tiny" >/dev/null
    normal_size=$(wc -c < "$normal")
    tiny_size=$(wc -c < "$tiny")
    saved=$(( normal_size - tiny_size ))
    echo "SIZE $name: normal=${normal_size} tiny=${tiny_size} saved=${saved}"
}

echo "KASM benchmarks"
run_one tiny
run_one medium
run_one many_labels
run_one macro_heavy

echo "KASM tiny size comparisons"
size_compare syscall_hello examples/tiny_hello.asm
size_compare multi_label_flow examples/labels_and_jumps.asm
size_compare std_helper examples/std_hello/src/main.asm

if command -v nasm >/dev/null 2>&1; then
    echo "nasm installed; no equivalent NASM fixtures, comparison skipped"
else
    echo "nasm not found; comparison skipped"
fi
