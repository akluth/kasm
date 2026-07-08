# Boot Examples

Build the boot sector with KASM itself:

```sh
./kasm examples/boot/hello_boot.asm -f bin16 -o build/hello_boot.img
```

Run it with QEMU when available:

```sh
qemu-system-i386 -drive format=raw,file=build/hello_boot.img,if=floppy
```
