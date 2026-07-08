# Boot Sectors

Build the included boot sector:

```sh
mkdir -p build
./kasm examples/boot/hello_boot.asm -f bin16 -o build/hello_boot.img
```

The output is exactly 512 bytes and ends with `55 AA`.

Run with QEMU when installed:

```sh
qemu-system-i386 -drive format=raw,file=build/hello_boot.img,if=floppy
```
