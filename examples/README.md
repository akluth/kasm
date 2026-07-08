# KASM Examples

Build examples from the repository root unless noted.

| File | Demonstrates | Build/run |
| --- | --- | --- |
| `01_hello_raw_syscall.asm` | direct Linux syscall setup | `./kasm examples/01_hello_raw_syscall.asm -o hello && ./hello` |
| `02_hello_syscall_sugar.asm` | syscall sugar | `./kasm examples/02_hello_syscall_sugar.asm -o sugar && ./sugar` |
| `03_hello_stdlib.asm` | inline stdlib `print` and `exit` | `./kasm examples/03_hello_stdlib.asm -o stdlib && ./stdlib` |
| `04_exit_code.asm` | process exit status | `./kasm examples/04_exit_code.asm -o exit42 && ./exit42` |
| `05_loop.asm` | labels and conditional branches | `./kasm examples/05_loop.asm -o loop && ./loop` |
| `labels_and_jumps.asm` | labels and conditional jumps | `./kasm examples/labels_and_jumps.asm -o labels && ./labels` |
| `06_stack_memory.asm` | stack memory operands | `./kasm examples/06_stack_memory.asm -o stack && ./stack` |
| `07_object_ld.asm` | ELF64 object output linked by `ld` | `./kasm examples/07_object_ld.asm -f obj -o object.o && ld object.o -o object && ./object` |
| `08_object_gcc_puts.asm` | optional libc call through object mode | `./kasm examples/08_object_gcc_puts.asm -f obj -o puts.o && gcc -no-pie puts.o -o puts && ./puts` |
| `09_struct_binary_header.asm` | struct binary layout and raw output | `./kasm examples/09_struct_binary_header.asm -f bin -o header.bin` |
| `data_struct_layout.asm` | struct layout release-pack example | `./kasm examples/data_struct_layout.asm -f bin -o header.bin` |
| `10_explain_demo.asm` | verbose explain, map, and listing modes | `./kasm examples/10_explain_demo.asm -o explain --explain=verbose` |
| `doslike/int21_kernel.asm` | tiny DOS-like INT 21h service skeleton | `./kasm examples/doslike/int21_kernel.asm -f bin16 -o int21_kernel.bin` |
| `explain_demo.asm` | compact explain demo alias | `./kasm examples/explain_demo.asm -o explain_demo --explain=deluxe` |
| `hello.asm` | ELF Surgeon Mode | `./kasm --elf-info examples/hello.asm -o hello` |
| `teach_hello.asm` | guided Teaching Mode | `./kasm --teach examples/teach_hello.asm -o teach_hello` |
| `teaching_demo.asm` | Teaching Mode release-pack example | `./kasm --teach examples/teaching_demo.asm -o teaching_demo` |
| `11_read_echo.asm` | stdlib read/write macros | `printf test | ./kasm examples/11_read_echo.asm -o echo && printf test | ./echo` |
| `read_write.asm` | direct read/write syscall sugar | `printf test | ./kasm examples/read_write.asm -o read_write && printf test | ./read_write` |
| `project_hello/` | project build mode with two source files | `cd examples/project_hello && ../../kasm build --verbose && ./build/project_hello` |
| `std_hello/` | bare Linux std helpers and internal linker | `cd examples/std_hello && ../../kasm build --internal-linker && ./build/std_hello` |
| `std_read_write/` | `kread` and `kwrite` helpers | `cd examples/std_read_write && ../../kasm src/main.asm -o std_read_write && printf test | ./std_read_write` |
| `std_project/` | multi-source project using std helpers | `cd examples/std_project && ../../kasm build --internal-linker && ./build/std_project` |
| `explain_exit.asm` | compact deluxe explain fixture | `./kasm examples/explain_exit.asm -o explain_exit --explain=deluxe` |
| `tiny_exit.asm` | compact direct ELF exit program | `./kasm --tiny --tiny-report examples/tiny_exit.asm -o tiny_exit` |
| `tiny_hello.asm` | compact direct ELF hello program | `./kasm --tiny --tiny-report examples/tiny_hello.asm -o tiny_hello` |
| `hints.asm` | performance and ABI hints | `./kasm examples/hints.asm -o hints --hints` |
| `syscall_sugar_hello.asm` | syscall sugar and uppercase built-in constants | `./kasm examples/syscall_sugar_hello.asm -o sugar && ./sugar` |
