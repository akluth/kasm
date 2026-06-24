# Project Build Mode

KASM v2.0 includes a small project build command for real multi-file examples:

```sh
kasm build
kasm build --config path/to/kasm.toml
kasm build --verbose
kasm build --no-link
kasm build --dump-symbols
kasm build --internal-linker
```

`kasm build` reads `kasm.toml` from the current directory unless `--config` is provided. The supported TOML subset is intentionally strict: section headers, quoted string values, and one-line quoted string arrays.

```toml
[project]
name = "hello"
type = "executable"

[build]
sources = ["src/main.asm", "src/util.asm"]
out_dir = "build"
output = "hello"
linker = "ld"
entry = "_start"
```

The builder assembles each listed source independently as an ELF64 relocatable object in `out_dir`, then invokes the configured linker for executable projects. `linker` defaults to `ld`, and `entry` defaults to `_start`.

Use `--no-link` to stop after object generation. Use `--verbose` to print every assemble step and the exact linker command.

Cross-file linking is still done by the system linker. KASM does not yet implement a safe internal project combiner or linker.
