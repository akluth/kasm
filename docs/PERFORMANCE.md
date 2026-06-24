# Performance

Run benchmarks with:

```sh
make bench
```

The benchmark script assembles small fixtures:

- tiny program
- medium program
- many-label program
- macro-heavy program

Results depend on CPU, filesystem, shell, WSL/native Linux differences, compiler, and whether the binary is stripped or rebuilt. Treat benchmark output as a local sanity check, not a general claim that KASM is faster than another assembler.
