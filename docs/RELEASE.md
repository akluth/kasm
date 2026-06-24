# Release Notes

KASM release artifacts are built from source with the repository Makefile.

Recommended local check:

```sh
make clean
make test
make release
make size
make bench
make dist
```

The release binary is built with `-Os` and stripped when `strip` is available.

The distribution archive is `dist/kasm-VERSION.tar.gz`. It includes source, docs, examples, tests, benchmarks, CI metadata, and the Makefile. Generated test and benchmark scratch directories are removed from the archive.

For reproducibility, build from a clean checkout with a fixed compiler toolchain. KASM does not embed timestamps into its own generated executables.
