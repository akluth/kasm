# Performance And ABI Hints

KASM includes optional educational hints:

```sh
./kasm examples/hints.asm -o hints --hints
```

Hints are non-fatal and are printed to stderr. They do not change emitted bytes.

## CLI

- `--hints`: enable default hint categories
- `--hints=perf,abi,size`: enable selected categories
- `--hints=all`: enable all categories
- `--hints-cpu generic|intel|amd|zen4|skylake`: select a named profile for future profile-specific wording

## Hint Categories

- `perf`: partial-register hazards, zeroing idioms, stack alignment, addressing complexity
- `abi`: Linux syscall ABI notes
- `size`: places where `--tiny` may choose a shorter encoding

## Stable IDs

Each hint has a stable ID for tests and future suppression work:

- `KASM-HINT-ZEROING-001`
- `KASM-HINT-SYSCALL-001`
- `KASM-HINT-PARTIAL-001`
- `KASM-HINT-STACK-001`
- `KASM-HINT-SIZE-001`
- `KASM-HINT-SIZE-002`
- `KASM-HINT-ADDR-001`

KASM hints are intentionally conservative. They are not a CPU simulator and not a compiler optimizer.
