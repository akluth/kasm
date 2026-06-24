# Structs And Binary Layout

Structs define byte layouts and do not emit bytes by themselves.

```asm
struct Header
    magic: bytes 4
    version: word
    flags: word
    entry: qword
end
```

Supported field types:

- `byte`
- `word`
- `dword`
- `qword`
- `bytes N`

Initializers emit bytes:

```asm
Header {
    magic = "KASM"
    version = 1
    flags = 0
    entry = 0
}
```

Omitted fields emit zero bytes. Duplicate and unknown fields are errors. Strings that exceed a `bytes N` field are errors.

`sizeof(StructName)` returns total size. `offsetof(StructName, field)` returns a field offset.

KASM performs no implicit C ABI padding. Use `align N` explicitly where needed.
