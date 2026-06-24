; Struct binary layout example alias for release packs.

struct Header
    magic: bytes 4
    version: word
    flags: word
    entry: qword
end

section .rodata
Header {
    magic = "KASM"
    version = 1
    flags = 0
    entry = 0
}
