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

echo "KASM benchmarks"
run_one tiny
run_one medium
run_one many_labels
run_one macro_heavy

if command -v nasm >/dev/null 2>&1; then
    echo "nasm installed; no equivalent NASM fixtures, comparison skipped"
else
    echo "nasm not found; comparison skipped"
fi
