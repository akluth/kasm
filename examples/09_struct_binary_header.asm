; Binary layout example. Emit with: ../kasm 09_struct_binary_header.asm -f bin -o header.bin
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
