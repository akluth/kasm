struct Header
    magic: bytes 4
    version: word
    flags: word
    entry: qword
end

section .rodata
header:
    Header {
        magic = "KASM"
        version = 1
        flags = 0
        entry = 0
    }
align 16
