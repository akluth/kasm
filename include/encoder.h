#ifndef KASM_ENCODER_H
#define KASM_ENCODER_H

#include "kasm.h"

int kasm_estimate_statement(Assembler *as, Statement *st);
int kasm_apply_tiny_layout(Assembler *as);
int kasm_encode_program(Assembler *as);

#endif
