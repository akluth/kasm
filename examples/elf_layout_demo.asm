struct ElfIdent
    magic: bytes 4
    class: byte
    data: byte
    version: byte
    osabi: byte
    abiversion: byte
    pad: bytes 7
end

struct ToyHeader
    ident: bytes 16
    kind: word
    entry_offset: qword
end

section .rodata
ident:
    ElfIdent {
        magic = "ELF!"
        class = 2
        data = 1
        version = 1
        osabi = 0
        abiversion = 0
    }
toy_header_size = sizeof(ToyHeader)
toy_entry_offset = offsetof(ToyHeader, entry_offset)
