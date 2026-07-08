# Raw Binary

`-f bin` keeps the historical raw-binary behavior for the default 64-bit target.
`-f bin16` is a convenience alias for raw binary plus 16-bit 8086 encoding.

Raw binary output concatenates emitted sections without ELF headers. In 16-bit
mode, `org` affects symbol values and expressions but does not write padding.

Data directives `db`, `dw`, `dd`, `dq`, `resb`, `resw`, `resd`, `resq`,
`times`, and `align` emit little-endian bytes.
