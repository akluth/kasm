#ifndef KASM_DIAGNOSTICS_H
#define KASM_DIAGNOSTICS_H

#include "kasm.h"

void kasm_error(Assembler *as, SourceLoc loc, const char *fmt, ...);

#endif
