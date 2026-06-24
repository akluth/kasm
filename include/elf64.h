#ifndef KASM_ELF64_H
#define KASM_ELF64_H

#include "kasm.h"

int kasm_write_elf64(Assembler *as, const char *path);
int kasm_write_elf64_obj(Assembler *as, const char *path);
int kasm_write_bin(Assembler *as, const char *path);

#endif
