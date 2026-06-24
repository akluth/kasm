#ifndef KASM_SYMBOLS_H
#define KASM_SYMBOLS_H

#include "kasm.h"

Symbol *kasm_symbol_find(SymbolTable *table, const char *name);
int kasm_symbol_define(Assembler *as, const char *name, SectionId section,
                       uint64_t offset, int is_const, int64_t value, int line, int column);
int kasm_symbol_global(Assembler *as, const char *name, int line, int column);
int kasm_symbol_extern(Assembler *as, const char *name, int line, int column);
int kasm_validate_symbols(Assembler *as, int object_mode);
int kasm_eval_expr(Assembler *as, const char *expr, SectionId current_section,
                   uint64_t current_offset, int want_absolute, int64_t *out,
                   SourceLoc loc);

#endif
