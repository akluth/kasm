# DOS-Like Real-Mode Kernels

KASM 0.2.1 can assemble small MS-DOS-inspired real-mode kernels and monitors.
The goal is not binary compatibility with MS-DOS 4.0. The supported workflow is
to build raw 8086 code that uses familiar DOS-era ideas: interrupt vectors,
INT-style services, BIOS calls, COM-style offsets, port I/O, and simple resident
service routines.

Useful 0.2.1 instructions for this style:

- `jmp 0x0000:0x7C00` and `call far 0x1234:0x5678`
- `in al, 0x60`, `in ax, dx`, `out 0xE9, al`, `out dx, ax`
- `mov word [0x84], handler` and `mov word [0x86], ax` for IVT entries
- `lea`, `lds`, `les`, and `xchg`
- memory ALU forms such as `add word [counter], 1`
- `iret` for interrupt handlers

Example:

```sh
./kasm examples/doslike/int21_kernel.asm -f bin16 -o build/int21_kernel.bin
```

The example installs an INT 21h vector and implements a tiny AH=09h-style
string output service using BIOS INT 10h teletype output. It is a teaching
skeleton for a DOS-like kernel API, not a complete DOS clone.
