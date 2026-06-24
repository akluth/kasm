; Linux x86-64 syscall numbers and common constants.
define SYS_read 0
define SYS_write 1
define SYS_open 2
define SYS_close 3
define SYS_mmap 9
define SYS_mprotect 10
define SYS_munmap 11
define SYS_brk 12
define SYS_exit 60
define SYS_exit_group 231
define SYS_openat 257

define stdin 0
define stdout 1
define stderr 2

define AT_FDCWD -100

define O_RDONLY 0
define O_WRONLY 1
define O_RDWR 2
define O_CREAT 64
define O_TRUNC 512

define PROT_READ 1
define PROT_WRITE 2
define PROT_EXEC 4

define MAP_PRIVATE 2
define MAP_ANONYMOUS 32
