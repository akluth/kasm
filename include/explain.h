#ifndef KASM_EXPLAIN_H
#define KASM_EXPLAIN_H

#include "kasm.h"

void kasm_explain(Assembler *as, Statement *st, const uint8_t *bytes, size_t len,
                  const char *why);

#endif
