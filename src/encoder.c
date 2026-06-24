#include "diagnostics.h"
#include "encoder.h"
#include "explain.h"
#include "symbols.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    int code;
    int bits;
} Reg;

static const Reg regs[] = {
    { "rax", 0, 64 }, { "rcx", 1, 64 }, { "rdx", 2, 64 }, { "rbx", 3, 64 },
    { "rsp", 4, 64 }, { "rbp", 5, 64 }, { "rsi", 6, 64 }, { "rdi", 7, 64 },
    { "r8", 8, 64 }, { "r9", 9, 64 }, { "r10", 10, 64 }, { "r11", 11, 64 },
    { "r12", 12, 64 }, { "r13", 13, 64 }, { "r14", 14, 64 }, { "r15", 15, 64 },
    { "eax", 0, 32 }, { "ecx", 1, 32 }, { "edx", 2, 32 }, { "ebx", 3, 32 },
    { "esp", 4, 32 }, { "ebp", 5, 32 }, { "esi", 6, 32 }, { "edi", 7, 32 },
    { "ax", 0, 16 }, { "cx", 1, 16 }, { "dx", 2, 16 }, { "bx", 3, 16 },
    { "sp", 4, 16 }, { "bp", 5, 16 }, { "si", 6, 16 }, { "di", 7, 16 },
    { "al", 0, 8 }, { "cl", 1, 8 }, { "dl", 2, 8 }, { "bl", 3, 8 }
};

static int reg_code(const char *name, int *code, int *bits)
{
    for (size_t i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
        if (kasm_streq_ci(name, regs[i].name)) {
            *code = regs[i].code;
            *bits = regs[i].bits;
            return 1;
        }
    }
    return 0;
}

static void rex(ByteBuf *b, int w, int r, int x, int m)
{
    uint8_t v = 0x40;
    if (w) v |= 8;
    if (r & 8) v |= 4;
    if (x & 8) v |= 2;
    if (m & 8) v |= 1;
    if (v != 0x40)
        kasm_buf_append_u8(b, v);
}

static void modrm(ByteBuf *b, int mod, int reg, int rm)
{
    kasm_buf_append_u8(b, (uint8_t)((mod << 6) | ((reg & 7) << 3) | (rm & 7)));
}

static int is_i32(int64_t v)
{
    return v >= INT32_MIN && v <= INT32_MAX;
}

static int is_i8(int64_t v)
{
    return v >= INT8_MIN && v <= INT8_MAX;
}

static int prefix_ci(const char *s, const char *prefix)
{
    while (*prefix) {
        if (*s == 0)
            return 0;
        char a = *s, b = *prefix;
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b)
            return 0;
        s++;
        prefix++;
    }
    return 1;
}

typedef struct {
    int is_mem;
    int size_bits;
    int rip_relative;
    char symbol[256];
    int base;
    int has_base;
    int index;
    int has_index;
    int scale;
    int64_t disp;
} MemOp;

static int parse_mem_operand(Assembler *as, const char *text, SourceLoc loc, MemOp *mem)
{
    memset(mem, 0, sizeof(*mem));
    mem->base = -1;
    mem->index = -1;
    mem->scale = 1;

    char *tmp = kasm_xstrdup(text);
    char *s = kasm_trim(tmp);
    if (prefix_ci(s, "qword ptr")) {
        mem->size_bits = 64;
        s = kasm_trim(s + 9);
    } else if (prefix_ci(s, "dword ptr")) {
        mem->size_bits = 32;
        s = kasm_trim(s + 9);
    } else if (prefix_ci(s, "word ptr")) {
        mem->size_bits = 16;
        s = kasm_trim(s + 8);
    } else if (prefix_ci(s, "byte ptr")) {
        mem->size_bits = 8;
        s = kasm_trim(s + 8);
    }
    size_t n = strlen(s);
    if (n < 2 || s[0] != '[') {
        free(tmp);
        return 0;
    }
    if (s[n - 1] != ']') {
        kasm_error(as, loc, "invalid memory operand: missing closing bracket; hint: memory operands look like [rax] or [rel label]");
        free(tmp);
        return -1;
    }
    s[n - 1] = 0;
    char *inside = kasm_trim(s + 1);
    mem->is_mem = 1;
    if (prefix_ci(inside, "rel") && (inside[3] == ' ' || inside[3] == '\t')) {
        snprintf(mem->symbol, sizeof(mem->symbol), "%s", kasm_trim(inside + 3));
        if (mem->symbol[0] == 0) {
            kasm_error(as, loc, "invalid memory operand: missing rel symbol; hint: use [rel label]");
            free(tmp);
            return -1;
        }
        mem->rip_relative = 1;
        free(tmp);
        return 1;
    }

    char normalized[512];
    size_t j = 0;
    for (char *p = inside; *p && j + 2 < sizeof(normalized); p++) {
        if (*p == '-') {
            normalized[j++] = '+';
            normalized[j++] = '-';
        } else if (!(*p == ' ' || *p == '\t')) {
            normalized[j++] = *p;
        }
    }
    normalized[j] = 0;
    if (normalized[0] == 0) {
        kasm_error(as, loc, "invalid memory operand: empty brackets; hint: put a base register or rel label inside brackets");
        free(tmp);
        return -1;
    }

    char *term = strtok(normalized, "+");
    while (term) {
        if (*term == 0) {
            term = strtok(NULL, "+");
            continue;
        }
        char *star = strchr(term, '*');
        int code, bits;
        if (star) {
            *star = 0;
            int64_t scale;
            if (!reg_code(term, &code, &bits)) {
                kasm_error(as, loc, "invalid memory operand: invalid index register '%s'; hint: use a supported 64-bit register as an index", term);
                free(tmp);
                return -1;
            }
            if (!kasm_parse_int(star + 1, &scale) ||
                !(scale == 1 || scale == 2 || scale == 4 || scale == 8)) {
                kasm_error(as, loc, "invalid scale '%s'", star + 1);
                free(tmp);
                return -1;
            }
            if ((code & 7) == 4) {
                kasm_error(as, loc, "invalid use of register as index '%s'", term);
                free(tmp);
                return -1;
            }
            if (mem->has_index) {
                kasm_error(as, loc, "invalid memory operand: two index registers");
                free(tmp);
                return -1;
            }
            mem->has_index = 1;
            mem->index = code;
            mem->scale = (int)scale;
        } else if (reg_code(term, &code, &bits)) {
            if (!mem->has_base) {
                mem->has_base = 1;
                mem->base = code;
            } else if (!mem->has_index) {
                if ((code & 7) == 4) {
                    kasm_error(as, loc, "invalid use of register as index '%s'", term);
                    free(tmp);
                    return -1;
                }
                mem->has_index = 1;
                mem->index = code;
                mem->scale = 1;
            } else {
                kasm_error(as, loc, "invalid memory operand: two index registers");
                free(tmp);
                return -1;
            }
        } else {
            int64_t disp;
            if (!kasm_parse_int(term, &disp)) {
                kasm_error(as, loc, "unsupported addressing mode '%s'", text);
                free(tmp);
                return -1;
            }
            mem->disp += disp;
        }
        term = strtok(NULL, "+");
    }
    if (!mem->has_base) {
        kasm_error(as, loc, "unsupported absolute memory addressing '%s'", text);
        free(tmp);
        return -1;
    }
    if (mem->disp < INT32_MIN || mem->disp > INT32_MAX) {
        kasm_error(as, loc, "displacement out of range");
        free(tmp);
        return -1;
    }
    free(tmp);
    return 1;
}

static int is_mem_operand(const char *text)
{
    char *tmp = kasm_xstrdup(text);
    char *s = kasm_trim(tmp);
    int yes = s[0] == '[' || prefix_ci(s, "qword ptr") ||
              prefix_ci(s, "dword ptr") ||
              prefix_ci(s, "word ptr") ||
              prefix_ci(s, "byte ptr");
    free(tmp);
    return yes;
}

static int scale_bits(int scale)
{
    if (scale == 1) return 0;
    if (scale == 2) return 1;
    if (scale == 4) return 2;
    return 3;
}

static int emit_modrm_mem(ByteBuf *out, int reg_field, const MemOp *mem)
{
    if (mem->rip_relative) {
        modrm(out, 0, reg_field, 5);
        return 0;
    }
    int need_sib = mem->has_index || (mem->base & 7) == 4;
    modrm(out, 2, reg_field, need_sib ? 4 : mem->base);
    if (need_sib) {
        int idx = mem->has_index ? mem->index : 4;
        kasm_buf_append_u8(out, (uint8_t)((scale_bits(mem->scale) << 6) |
                                          ((idx & 7) << 3) | (mem->base & 7)));
    }
    return 1;
}

static int mem_uses_sib(const MemOp *mem)
{
    return !mem->rip_relative && (mem->has_index || (mem->base & 7) == 4);
}

static int rex_size(int w, int r, int x, int b)
{
    uint8_t v = 0x40;
    if (w) v |= 8;
    if (r & 8) v |= 4;
    if (x & 8) v |= 2;
    if (b & 8) v |= 1;
    return v != 0x40;
}

static int mem_encoding_size(int reg_bits, int reg, const MemOp *mem)
{
    return rex_size(reg_bits == 64, reg, mem->has_index ? mem->index : 0,
                    mem->rip_relative ? 0 : mem->base) +
           1 + 1 + (mem_uses_sib(mem) ? 1 : 0) + 4;
}

static void append_disp32(ByteBuf *out, int64_t disp)
{
    kasm_buf_append_u32(out, (uint32_t)(int32_t)disp);
}

static int emit_reg_mem_common(Assembler *as, Statement *st, ByteBuf *out,
                               int dst_is_reg, int reg, int reg_bits, MemOp *mem,
                               uint8_t opcode, const char *why)
{
    if (mem->size_bits && mem->size_bits != reg_bits) {
        kasm_error(as, (SourceLoc){ as->path, st->line, st->column }, "invalid operand size");
        return 0;
    }
    rex(out, reg_bits == 64, reg, mem->has_index ? mem->index : 0,
        mem->rip_relative ? 0 : mem->base);
    kasm_buf_append_u8(out, opcode);
    emit_modrm_mem(out, reg, mem);
    append_disp32(out, mem->rip_relative ? 0 : mem->disp);
    if (mem->rip_relative) {
        if (as->object_mode) {
            kasm_add_reloc(as, st->section, out->len - 4, mem->symbol, RELOC_PC32, -4,
                           (SourceLoc){ as->path, st->line, st->column });
        } else {
            int64_t target;
            if (!kasm_eval_expr(as, mem->symbol, st->section, st->offset, 1, &target,
                                (SourceLoc){ as->path, st->line, st->column }))
                return 0;
            int64_t here = (int64_t)(as->sections[st->section].vaddr + out->len);
            uint32_t disp = (uint32_t)(int32_t)(target - here);
            memcpy(out->data + out->len - 4, &disp, 4);
        }
    }
    (void)dst_is_reg;
    (void)why;
    return 1;
}

static int emit_binop(Assembler *as, Statement *st, ByteBuf *out, const char **why,
                      const char *name, uint8_t rm_reg_op, uint8_t reg_rm_op,
                      uint8_t imm_group, uint8_t imm_ext)
{
    int a, b, abits, bbits;
    MemOp mem;
    SourceLoc loc0 = { as->path, st->line, st->operands[0].column };
    SourceLoc loc1 = { as->path, st->line, st->operands[1].column };
    int op0_mem = parse_mem_operand(as, st->operands[0].text, loc0, &mem);
    if (op0_mem < 0) return 0;
    if (op0_mem) {
        if (!reg_code(st->operands[1].text, &b, &bbits)) {
            kasm_error(as, loc1, "invalid operand combination for '%s'", name);
            return 0;
        }
        if (mem.size_bits && mem.size_bits != bbits) {
            kasm_error(as, loc0, "invalid operand size");
            return 0;
        }
        rex(out, bbits == 64, b, mem.has_index ? mem.index : 0, mem.rip_relative ? 0 : mem.base);
        kasm_buf_append_u8(out, rm_reg_op);
        emit_modrm_mem(out, b, &mem);
        append_disp32(out, mem.rip_relative ? 0 : mem.disp);
        *why = "ModRM/SIB memory operand";
        return 1;
    }
    if (!reg_code(st->operands[0].text, &a, &abits)) {
        kasm_error(as, loc0, "unknown register '%s'; hint: check register spelling such as rax, rdi, rsp, or r8-r15", st->operands[0].text);
        return 0;
    }
    int op1_mem = parse_mem_operand(as, st->operands[1].text, loc1, &mem);
    if (op1_mem < 0) return 0;
    if (op1_mem) {
        if (!emit_reg_mem_common(as, st, out, 1, a, abits, &mem, reg_rm_op, "mem"))
            return 0;
        *why = "ModRM/SIB memory operand";
        return 1;
    }
    if (reg_code(st->operands[1].text, &b, &bbits)) {
        rex(out, abits == 64 || bbits == 64, b, 0, a);
        kasm_buf_append_u8(out, rm_reg_op);
        modrm(out, 3, b, a);
        static char buf[64];
        snprintf(buf, sizeof(buf), "%s r64, r64", name);
        *why = buf;
        return 1;
    }
    int64_t imm;
    if (!kasm_eval_expr(as, st->operands[1].text, st->section, st->offset, 0, &imm, loc1))
        return 0;
    if (!is_i32(imm)) {
        kasm_error(as, loc1, "integer literal out of range '%s'", st->operands[1].text);
        return 0;
    }
    rex(out, abits == 64, 0, 0, a);
    if (as->tiny && is_i8(imm)) {
        kasm_buf_append_u8(out, 0x83);
        modrm(out, 3, imm_ext, a);
        kasm_buf_append_u8(out, (uint8_t)(int8_t)imm);
    } else {
        kasm_buf_append_u8(out, imm_group);
        modrm(out, 3, imm_ext, a);
        kasm_buf_append_u32(out, (uint32_t)imm);
    }
    static char buf[64];
    snprintf(buf, sizeof(buf), "%s r64, %s", name, as->tiny && is_i8(imm) ? "imm8" : "imm32");
    *why = buf;
    return 1;
}

static int emit_group1_mem_imm_error(Assembler *as, Statement *st)
{
    kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[0].column },
               "ambiguous memory operand size; hint: store a register first, for example mov rax, 1 then mov qword ptr [mem], rax");
    return 0;
}

static uint8_t jcc_opcode(const char *op)
{
    if (strcmp(op, "je") == 0 || strcmp(op, "jz") == 0) return 0x84;
    if (strcmp(op, "jne") == 0 || strcmp(op, "jnz") == 0) return 0x85;
    if (strcmp(op, "jg") == 0) return 0x8F;
    if (strcmp(op, "jge") == 0) return 0x8D;
    if (strcmp(op, "jl") == 0) return 0x8C;
    if (strcmp(op, "jle") == 0) return 0x8E;
    if (strcmp(op, "ja") == 0) return 0x87;
    if (strcmp(op, "jae") == 0) return 0x83;
    if (strcmp(op, "jb") == 0) return 0x82;
    return 0x86;
}

static uint8_t jcc_short_opcode(const char *op)
{
    if (strcmp(op, "je") == 0 || strcmp(op, "jz") == 0) return 0x74;
    if (strcmp(op, "jne") == 0 || strcmp(op, "jnz") == 0) return 0x75;
    if (strcmp(op, "jg") == 0) return 0x7F;
    if (strcmp(op, "jge") == 0) return 0x7D;
    if (strcmp(op, "jl") == 0) return 0x7C;
    if (strcmp(op, "jle") == 0) return 0x7E;
    if (strcmp(op, "ja") == 0) return 0x77;
    if (strcmp(op, "jae") == 0) return 0x73;
    if (strcmp(op, "jb") == 0) return 0x72;
    return 0x76;
}

static int expr_is_plain_symbol(const char *expr, char *name, size_t cap)
{
    char *tmp = kasm_xstrdup(expr);
    char *s = kasm_trim(tmp);
    int ok = 1;
    if (!(*s == '_' || *s == '.' || ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z'))))
        ok = 0;
    for (char *p = s + 1; ok && *p; p++)
        if (!(*p == '_' || *p == '.' || (*p >= '0' && *p <= '9') ||
              (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')))
            ok = 0;
    if (ok)
        snprintf(name, cap, "%s", s);
    free(tmp);
    return ok;
}

static int branch_target_offset(Assembler *as, Statement *st, uint64_t *target)
{
    char symname[256];
    if (!expr_is_plain_symbol(st->operands[0].text, symname, sizeof(symname)))
        return 0;
    Symbol *sym = kasm_symbol_find(&as->symbols, symname);
    if (!sym || !sym->defined || sym->is_const || sym->section != st->section)
        return 0;
    *target = sym->offset;
    return 1;
}

static int branch_can_short(Assembler *as, Statement *st)
{
    uint64_t target;
    if (as->object_mode || !branch_target_offset(as, st, &target))
        return 0;
    int64_t disp = (int64_t)target - (int64_t)(st->offset + 2);
    return is_i8(disp);
}

static int is_branch_op(const char *op)
{
    return strcmp(op, "jmp") == 0 ||
           strcmp(op, "je") == 0 || strcmp(op, "jz") == 0 ||
           strcmp(op, "jne") == 0 || strcmp(op, "jnz") == 0 ||
           strcmp(op, "jg") == 0 || strcmp(op, "jge") == 0 ||
           strcmp(op, "jl") == 0 || strcmp(op, "jle") == 0 ||
           strcmp(op, "ja") == 0 || strcmp(op, "jae") == 0 ||
           strcmp(op, "jb") == 0 || strcmp(op, "jbe") == 0;
}

static int tiny_binop_size(Assembler *as, Statement *st, const char *name,
                           int allow_imm8, int *used_imm8)
{
    int a, b, abits, bbits;
    MemOp mem;
    SourceLoc loc0 = { as->path, st->line, st->operands[0].column };
    SourceLoc loc1 = { as->path, st->line, st->operands[1].column };
    int op0_mem = parse_mem_operand(as, st->operands[0].text, loc0, &mem);
    if (op0_mem < 0)
        return st->size ? (int)st->size : 15;
    if (op0_mem) {
        if (!reg_code(st->operands[1].text, &b, &bbits))
            return st->size ? (int)st->size : 15;
        return mem_encoding_size(bbits, b, &mem);
    }
    if (!reg_code(st->operands[0].text, &a, &abits))
        return st->size ? (int)st->size : 15;
    int op1_mem = parse_mem_operand(as, st->operands[1].text, loc1, &mem);
    if (op1_mem < 0)
        return st->size ? (int)st->size : 15;
    if (op1_mem)
        return mem_encoding_size(abits, a, &mem);
    if (reg_code(st->operands[1].text, &b, &bbits))
        return rex_size(abits == 64 || bbits == 64, b, 0, a) + 2;
    int64_t imm;
    if (!kasm_eval_expr(as, st->operands[1].text, st->section, st->offset, 0, &imm, loc1))
        return st->size ? (int)st->size : 15;
    if (allow_imm8 && is_i8(imm)) {
        *used_imm8 = 1;
        return rex_size(abits == 64, 0, 0, a) + 3;
    }
    (void)name;
    return rex_size(abits == 64, 0, 0, a) + 6;
}

static int tiny_instr_size(Assembler *as, Statement *st, int count_stats)
{
    const char *op = st->op;
    int c = st->operand_count;
    int rd, rs, b1, b2;
    MemOp mem;
    SourceLoc loc0 = { as->path, st->line, c > 0 ? st->operands[0].column : st->column };
    SourceLoc loc1 = { as->path, st->line, c > 1 ? st->operands[1].column : st->column };
    if (strcmp(op, "syscall") == 0 && c == 0) return 2;
    if (strcmp(op, "ret") == 0 && c == 0) return 1;
    if ((strcmp(op, "push") == 0 || strcmp(op, "pop") == 0) && c == 1) {
        if (strcmp(op, "push") == 0 && !reg_code(st->operands[0].text, &rd, &b1))
            return 5;
        if (!reg_code(st->operands[0].text, &rd, &b1))
            return st->size ? (int)st->size : 15;
        return rex_size(0, 0, 0, rd) + 1;
    }
    if ((strcmp(op, "inc") == 0 || strcmp(op, "dec") == 0 ||
         strcmp(op, "neg") == 0 || strcmp(op, "not") == 0) && c == 1) {
        if (!reg_code(st->operands[0].text, &rd, &b1))
            return st->size ? (int)st->size : 15;
        return rex_size(b1 == 64, 0, 0, rd) + 2;
    }
    if (strcmp(op, "mov") == 0 && c == 2) {
        int dst_mem = parse_mem_operand(as, st->operands[0].text, loc0, &mem);
        if (dst_mem < 0)
            return st->size ? (int)st->size : 15;
        if (dst_mem) {
            if (!reg_code(st->operands[1].text, &rs, &b2))
                return st->size ? (int)st->size : 15;
            return mem_encoding_size(b2, rs, &mem);
        }
        if (!reg_code(st->operands[0].text, &rd, &b1))
            return st->size ? (int)st->size : 15;
        int src_mem = parse_mem_operand(as, st->operands[1].text, loc1, &mem);
        if (src_mem < 0)
            return st->size ? (int)st->size : 15;
        if (src_mem)
            return mem_encoding_size(b1, rd, &mem);
        if (reg_code(st->operands[1].text, &rs, &b2))
            return rex_size(b1 == 64 || b2 == 64, rs, 0, rd) + 2;
        int64_t imm;
        if (!kasm_eval_expr(as, st->operands[1].text, st->section, st->offset, 0, &imm, loc1))
            return st->size ? (int)st->size : 15;
        if (b1 == 8)
            return rex_size(0, 0, 0, rd) + 2;
        if (b1 == 16)
            return 1 + rex_size(0, 0, 0, rd) + 4;
        if (is_i32(imm)) {
            if (b1 == 32)
                return rex_size(0, 0, 0, rd) + 5;
            return rex_size(1, 0, 0, rd) + 6;
        }
        return rex_size(b1 == 64, 0, 0, rd) + 9;
    }
    if (strcmp(op, "lea") == 0 && c == 2) {
        if (!reg_code(st->operands[0].text, &rd, &b1))
            return st->size ? (int)st->size : 15;
        int pm = parse_mem_operand(as, st->operands[1].text, loc1, &mem);
        if (pm <= 0)
            return st->size ? (int)st->size : 15;
        return mem_encoding_size(64, rd, &mem);
    }
    if ((strcmp(op, "add") == 0 || strcmp(op, "sub") == 0 ||
         strcmp(op, "and") == 0 || strcmp(op, "or") == 0 ||
         strcmp(op, "xor") == 0 || strcmp(op, "cmp") == 0) && c == 2) {
        int used_imm8 = 0;
        int sz = tiny_binop_size(as, st, op, 1, &used_imm8);
        if (count_stats && used_imm8)
            as->tiny_imm8_used++;
        return sz;
    }
    if (strcmp(op, "test") == 0 && c == 2) {
        int used_imm8 = 0;
        return tiny_binop_size(as, st, op, 0, &used_imm8);
    }
    if (strcmp(op, "call") == 0 && c == 1)
        return 5;
    if (is_branch_op(op) && c == 1) {
        if (branch_can_short(as, st)) {
            if (count_stats)
                as->tiny_jumps_shortened++;
            return 2;
        }
        if (count_stats)
            as->tiny_near_jumps++;
        return strcmp(op, "jmp") == 0 ? 5 : 6;
    }
    return st->size ? (int)st->size : 15;
}

static void tiny_update_symbol_offsets(Assembler *as)
{
    for (size_t i = 0; i < as->program.len; i++) {
        Statement *st = &as->program.items[i];
        if (st->type == ST_LABEL && st->name) {
            Symbol *sym = kasm_symbol_find(&as->symbols, st->name);
            if (sym && sym->defined && !sym->is_const) {
                sym->section = st->section;
                sym->offset = st->offset;
            }
        }
    }
}

static void tiny_update_constants(Assembler *as)
{
    for (size_t i = 0; i < as->program.len; i++) {
        Statement *st = &as->program.items[i];
        if (st->type == ST_CONST && st->name && st->expr) {
            Symbol *sym = kasm_symbol_find(&as->symbols, st->name);
            int64_t value = 0;
            if (sym && kasm_eval_expr(as, st->expr, st->section, st->offset, 0, &value,
                                      (SourceLoc){ as->path, st->line, st->column }))
                sym->value = value;
        }
    }
}

int kasm_apply_tiny_layout(Assembler *as)
{
    if (!as->tiny)
        return 1;
    for (int pass = 0; pass < 16; pass++) {
        uint64_t offsets[SEC_COUNT] = { 0, 0, 0 };
        int changed = 0;
        for (size_t i = 0; i < as->program.len; i++) {
            Statement *st = &as->program.items[i];
            if (st->type == ST_LABEL || st->type == ST_CONST) {
                uint64_t current = st->section == SEC_NONE ? st->offset : offsets[st->section];
                if (st->offset != current) {
                    st->offset = current;
                    changed = 1;
                }
            } else if (st->type == ST_DATA || st->type == ST_INSTR) {
                uint32_t old_size = st->size;
                uint64_t old_offset = st->offset;
                st->offset = offsets[st->section];
                if (st->type == ST_INSTR)
                    st->size = (uint32_t)tiny_instr_size(as, st, 0);
                offsets[st->section] += st->size;
                if (old_size != st->size || old_offset != st->offset)
                    changed = 1;
            }
        }
        tiny_update_symbol_offsets(as);
        tiny_update_constants(as);
        if (!changed)
            break;
    }
    as->tiny_jumps_shortened = 0;
    as->tiny_near_jumps = 0;
    as->tiny_imm8_used = 0;
    as->tiny_bytes_saved = 0;
    for (size_t i = 0; i < as->program.len; i++) {
        Statement *st = &as->program.items[i];
        if (st->type == ST_INSTR) {
            uint32_t sz = (uint32_t)tiny_instr_size(as, st, 1);
            st->size = sz;
            if (15 > sz)
                as->tiny_bytes_saved += 15 - sz;
        }
    }
    return as->errors == 0;
}

int kasm_estimate_statement(Assembler *as, Statement *st)
{
    if (st->type != ST_INSTR)
        return 1;
    const char *op = st->op;
    int c = st->operand_count;
    if (strcmp(op, "syscall") == 0 && c == 0) st->size = 15;
    else if (strcmp(op, "ret") == 0 && c == 0) st->size = 15;
    else if ((strcmp(op, "jmp") == 0 || strcmp(op, "call") == 0) && c == 1) st->size = 15;
    else if ((strcmp(op, "je") == 0 || strcmp(op, "jz") == 0 ||
              strcmp(op, "jne") == 0 || strcmp(op, "jnz") == 0 ||
              strcmp(op, "jg") == 0 || strcmp(op, "jge") == 0 ||
              strcmp(op, "jl") == 0 || strcmp(op, "jle") == 0 ||
              strcmp(op, "ja") == 0 || strcmp(op, "jae") == 0 ||
              strcmp(op, "jb") == 0 || strcmp(op, "jbe") == 0) && c == 1) st->size = 15;
    else if ((strcmp(op, "push") == 0 || strcmp(op, "pop") == 0) && c == 1) st->size = 15;
    else if ((strcmp(op, "inc") == 0 || strcmp(op, "dec") == 0 ||
              strcmp(op, "neg") == 0 || strcmp(op, "not") == 0) && c == 1) st->size = 15;
    else if (strcmp(op, "lea") == 0 && c == 2) st->size = 15;
    else if ((strcmp(op, "xor") == 0 || strcmp(op, "mov") == 0 ||
              strcmp(op, "cmp") == 0 || strcmp(op, "add") == 0 ||
              strcmp(op, "sub") == 0 || strcmp(op, "and") == 0 ||
              strcmp(op, "or") == 0 || strcmp(op, "test") == 0) && c == 2) st->size = 15;
    else if (strcmp(op, "syscall") == 0 || strcmp(op, "ret") == 0 ||
             strcmp(op, "jmp") == 0 || strcmp(op, "call") == 0 ||
             strcmp(op, "je") == 0 || strcmp(op, "jz") == 0 ||
             strcmp(op, "jne") == 0 || strcmp(op, "jnz") == 0 ||
             strcmp(op, "jg") == 0 || strcmp(op, "jge") == 0 ||
             strcmp(op, "jl") == 0 || strcmp(op, "jle") == 0 ||
             strcmp(op, "ja") == 0 || strcmp(op, "jae") == 0 ||
             strcmp(op, "jb") == 0 || strcmp(op, "jbe") == 0 ||
             strcmp(op, "push") == 0 || strcmp(op, "pop") == 0 ||
             strcmp(op, "inc") == 0 || strcmp(op, "dec") == 0 ||
             strcmp(op, "neg") == 0 || strcmp(op, "not") == 0 ||
             strcmp(op, "lea") == 0 || strcmp(op, "xor") == 0 ||
             strcmp(op, "mov") == 0 || strcmp(op, "cmp") == 0 ||
             strcmp(op, "add") == 0 || strcmp(op, "sub") == 0 ||
             strcmp(op, "and") == 0 || strcmp(op, "or") == 0 ||
             strcmp(op, "test") == 0) {
        kasm_error(as, (SourceLoc){ as->path, st->line, st->column }, "invalid operand combination for '%s'; hint: see docs/INSTRUCTIONS.md for supported forms", op);
        return 0;
    } else {
        kasm_error(as, (SourceLoc){ as->path, st->line, 1 }, "unknown instruction '%s'; hint: check spelling or see docs/INSTRUCTIONS.md", op);
        return 0;
    }
    return 1;
}

static int emit_data_once(Assembler *as, Statement *st, ByteBuf *b)
{
    if (strcmp(st->op, "align") == 0 || strcmp(st->op, "resb") == 0 ||
        strcmp(st->op, "resw") == 0 || strcmp(st->op, "resd") == 0 ||
        strcmp(st->op, "resq") == 0) {
        for (uint32_t i = 0; i < st->size; i++)
            kasm_buf_append_u8(b, 0);
        return 1;
    }
    int elem = 1;
    if (strcmp(st->op, "dw") == 0) elem = 2;
    if (strcmp(st->op, "dd") == 0) elem = 4;
    if (strcmp(st->op, "dq") == 0) elem = 8;
    for (int i = 0; i < st->operand_count; i++) {
        const char *s = st->operands[i].text;
        size_t n = strlen(s);
        if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
            for (size_t j = 1; j + 1 < n; j++) {
                uint8_t ch = (uint8_t)s[j];
                if (s[j] == '\\' && j + 1 < n - 1) {
                    j++;
                    if (s[j] == 'n') ch = 10;
                    else if (s[j] == 't') ch = 9;
                    else ch = (uint8_t)s[j];
                }
                kasm_buf_append_u8(b, ch);
            }
        } else {
            char sym[256];
            if (as->object_mode && elem == 8 && expr_is_plain_symbol(s, sym, sizeof(sym))) {
                kasm_add_reloc(as, st->section, b->len, sym, RELOC_64, 0,
                               (SourceLoc){ as->path, st->line, st->operands[i].column });
                kasm_buf_append_u64(b, 0);
                continue;
            }
            int64_t v;
            if (!kasm_eval_expr(as, s, st->section, st->offset, elem == 8, &v,
                                (SourceLoc){ as->path, st->line, st->operands[i].column }))
                return 0;
            if (elem == 1) kasm_buf_append_u8(b, (uint8_t)v);
            if (elem == 2) kasm_buf_append_u16(b, (uint16_t)v);
            if (elem == 4) kasm_buf_append_u32(b, (uint32_t)v);
            if (elem == 8) kasm_buf_append_u64(b, (uint64_t)v);
        }
    }
    return 1;
}

static int emit_instr(Assembler *as, Statement *st, ByteBuf *out, const char **why)
{
    uint8_t *start = out->data ? out->data + out->len : NULL;
    (void)start;
    int rd, rs, b1, b2;
    const char *op = st->op;
    SourceLoc loc = { as->path, st->line, st->column };

    if (strcmp(op, "syscall") == 0) {
        kasm_buf_append_u8(out, 0x0F);
        kasm_buf_append_u8(out, 0x05);
        *why = "syscall";
        return 1;
    }
    if (strcmp(op, "ret") == 0) {
        kasm_buf_append_u8(out, 0xC3);
        *why = "ret";
        return 1;
    }
    if (strcmp(op, "push") == 0 || strcmp(op, "pop") == 0) {
        if (strcmp(op, "push") == 0 && !reg_code(st->operands[0].text, &rd, &b1)) {
            int64_t imm;
            if (!kasm_eval_expr(as, st->operands[0].text, st->section, st->offset, 0, &imm,
                                (SourceLoc){ as->path, st->line, st->operands[0].column }))
                return 0;
            if (!is_i32(imm)) {
                kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[0].column }, "integer literal out of range");
                return 0;
            }
            kasm_buf_append_u8(out, 0x68);
            kasm_buf_append_u32(out, (uint32_t)imm);
            *why = "push imm32";
            return 1;
        }
        if (!reg_code(st->operands[0].text, &rd, &b1)) {
            kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[0].column }, "unknown register '%s'; hint: check register spelling such as rax, rdi, rsp, or r8-r15", st->operands[0].text);
            return 0;
        }
        rex(out, 0, 0, 0, rd);
        kasm_buf_append_u8(out, (uint8_t)((strcmp(op, "push") == 0 ? 0x50 : 0x58) + (rd & 7)));
        *why = strcmp(op, "push") == 0 ? "push r64" : "pop r64";
        return 1;
    }
    if (strcmp(op, "inc") == 0 || strcmp(op, "dec") == 0 ||
        strcmp(op, "neg") == 0 || strcmp(op, "not") == 0) {
        if (!reg_code(st->operands[0].text, &rd, &b1)) {
            kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[0].column }, "unknown register '%s'; hint: check register spelling such as rax, rdi, rsp, or r8-r15", st->operands[0].text);
            return 0;
        }
        rex(out, b1 == 64, 0, 0, rd);
        if (strcmp(op, "inc") == 0 || strcmp(op, "dec") == 0) {
            kasm_buf_append_u8(out, 0xFF);
            modrm(out, 3, strcmp(op, "inc") == 0 ? 0 : 1, rd);
        } else {
            kasm_buf_append_u8(out, 0xF7);
            modrm(out, 3, strcmp(op, "not") == 0 ? 2 : 3, rd);
        }
        *why = strcmp(op, "inc") == 0 ? "inc r64" :
               strcmp(op, "dec") == 0 ? "dec r64" :
               strcmp(op, "not") == 0 ? "not r64" : "neg r64";
        return 1;
    }
    if (strcmp(op, "mov") == 0) {
        MemOp mem;
        int dst_mem = parse_mem_operand(as, st->operands[0].text,
                                        (SourceLoc){ as->path, st->line, st->operands[0].column }, &mem);
        if (dst_mem < 0) return 0;
        if (dst_mem) {
            if (!reg_code(st->operands[1].text, &rs, &b2)) {
                if (is_mem_operand(st->operands[1].text))
                    kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[1].column }, "unsupported instruction form: memory to memory mov");
                else
                    return emit_group1_mem_imm_error(as, st);
                return 0;
            }
            if (!emit_reg_mem_common(as, st, out, 0, rs, b2, &mem, 0x89, "mem"))
                return 0;
            *why = "mov qword ptr [mem], r64; ModRM/SIB memory operand";
            return 1;
        }
        if (!reg_code(st->operands[0].text, &rd, &b1)) {
            kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[0].column }, "unknown register '%s'; hint: check register spelling such as rax, rdi, rsp, or r8-r15", st->operands[0].text);
            return 0;
        }
        int src_mem = parse_mem_operand(as, st->operands[1].text,
                                        (SourceLoc){ as->path, st->line, st->operands[1].column }, &mem);
        if (src_mem < 0) return 0;
        if (src_mem) {
            if (!emit_reg_mem_common(as, st, out, 1, rd, b1, &mem, 0x8B, "mem"))
                return 0;
            *why = "mov r64, qword ptr [mem]; ModRM/SIB memory operand";
            return 1;
        }
        if (reg_code(st->operands[1].text, &rs, &b2)) {
            rex(out, b1 == 64 || b2 == 64, rs, 0, rd);
            kasm_buf_append_u8(out, 0x89);
            modrm(out, 3, rs, rd);
            *why = b1 == 32 && b2 == 32 ? "mov r32, r32" : "mov r64, r64";
            return 1;
        }
        int64_t imm;
        if (!kasm_eval_expr(as, st->operands[1].text, st->section, st->offset, 0, &imm,
                            (SourceLoc){ as->path, st->line, st->operands[1].column }))
            return 0;
        if (b1 == 8) {
            if (imm < INT8_MIN || imm > UINT8_MAX) {
                kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[1].column }, "integer literal out of range '%s'", st->operands[1].text);
                return 0;
            }
            rex(out, 0, 0, 0, rd);
            kasm_buf_append_u8(out, (uint8_t)(0xB0 + (rd & 7)));
            kasm_buf_append_u8(out, (uint8_t)imm);
            *why = "mov r8, imm8";
            return 1;
        }
        if (b1 == 16) {
            if (imm < INT16_MIN || imm > UINT16_MAX) {
                kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[1].column }, "integer literal out of range '%s'", st->operands[1].text);
                return 0;
            }
            kasm_buf_append_u8(out, 0x66);
            rex(out, 0, 0, 0, rd);
            kasm_buf_append_u8(out, 0xC7);
            modrm(out, 3, 0, rd);
            kasm_buf_append_u16(out, (uint16_t)imm);
            *why = "mov r16, imm16";
            return 1;
        }
        if (is_i32(imm)) {
            rex(out, b1 == 64, 0, 0, rd);
            if (as->tiny && b1 == 32) {
                kasm_buf_append_u8(out, (uint8_t)(0xB8 + (rd & 7)));
                kasm_buf_append_u32(out, (uint32_t)imm);
            } else {
                kasm_buf_append_u8(out, 0xC7);
                modrm(out, 3, 0, rd);
                kasm_buf_append_u32(out, (uint32_t)imm);
            }
            *why = b1 == 32 ? "mov r32, imm32" : "mov r64, imm32";
        } else {
            rex(out, b1 == 64, 0, 0, rd);
            kasm_buf_append_u8(out, (uint8_t)(0xB8 + (rd & 7)));
            kasm_buf_append_u64(out, (uint64_t)imm);
            *why = "mov r64, imm64";
        }
        return 1;
    }
    if (strcmp(op, "add") == 0)
        return emit_binop(as, st, out, why, "add", 0x01, 0x03, 0x81, 0);
    if (strcmp(op, "sub") == 0)
        return emit_binop(as, st, out, why, "sub", 0x29, 0x2B, 0x81, 5);
    if (strcmp(op, "and") == 0)
        return emit_binop(as, st, out, why, "and", 0x21, 0x23, 0x81, 4);
    if (strcmp(op, "or") == 0)
        return emit_binop(as, st, out, why, "or", 0x09, 0x0B, 0x81, 1);
    if (strcmp(op, "xor") == 0)
        return emit_binop(as, st, out, why, "xor", 0x31, 0x33, 0x81, 6);
    if (strcmp(op, "cmp") == 0)
        return emit_binop(as, st, out, why, "cmp", 0x39, 0x3B, 0x81, 7);
    if (strcmp(op, "test") == 0) {
        if (!reg_code(st->operands[0].text, &rd, &b1)) {
            kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[0].column }, "unknown register '%s'; hint: check register spelling such as rax, rdi, rsp, or r8-r15", st->operands[0].text);
            return 0;
        }
        if (reg_code(st->operands[1].text, &rs, &b2)) {
            rex(out, b1 == 64 || b2 == 64, rs, 0, rd);
            kasm_buf_append_u8(out, 0x85);
            modrm(out, 3, rs, rd);
            *why = "test r64, r64";
            return 1;
        }
        int64_t imm;
        if (!kasm_eval_expr(as, st->operands[1].text, st->section, st->offset, 0, &imm,
                            (SourceLoc){ as->path, st->line, st->operands[1].column }))
            return 0;
        rex(out, b1 == 64, 0, 0, rd);
        kasm_buf_append_u8(out, 0xF7);
        modrm(out, 3, 0, rd);
        kasm_buf_append_u32(out, (uint32_t)imm);
        *why = "test r64, imm32";
        return 1;
    }
    if (strcmp(op, "lea") == 0) {
        MemOp mem;
        if (!reg_code(st->operands[0].text, &rd, &b1)) {
            kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[0].column }, "unknown register '%s'; hint: check register spelling such as rax, rdi, rsp, or r8-r15", st->operands[0].text);
            return 0;
        }
        int pm = parse_mem_operand(as, st->operands[1].text,
                                   (SourceLoc){ as->path, st->line, st->operands[1].column }, &mem);
        if (pm <= 0) return 0;
        rex(out, 1, rd, mem.has_index ? mem.index : 0, mem.rip_relative ? 0 : mem.base);
        kasm_buf_append_u8(out, 0x8D);
        emit_modrm_mem(out, rd, &mem);
        if (mem.rip_relative) {
            if (as->object_mode) {
                kasm_add_reloc(as, st->section, out->len, mem.symbol, RELOC_PC32, -4,
                               (SourceLoc){ as->path, st->line, st->operands[1].column });
                kasm_buf_append_u32(out, 0);
            } else {
                int64_t target;
                if (!kasm_eval_expr(as, mem.symbol, st->section, st->offset, 1, &target,
                                    (SourceLoc){ as->path, st->line, st->operands[1].column }))
                    return 0;
                int64_t here = (int64_t)(as->sections[st->section].vaddr + out->len + 4);
                kasm_buf_append_u32(out, (uint32_t)(int32_t)(target - here));
            }
        } else {
            append_disp32(out, mem.disp);
        }
        *why = mem.rip_relative ? "RIP-relative LEA" : "lea r64, [mem]; ModRM/SIB memory operand";
        return 1;
    }
    if (strcmp(op, "jmp") == 0 || strcmp(op, "call") == 0 ||
        strcmp(op, "je") == 0 || strcmp(op, "jz") == 0 ||
        strcmp(op, "jne") == 0 || strcmp(op, "jnz") == 0 ||
        strcmp(op, "jg") == 0 || strcmp(op, "jge") == 0 ||
        strcmp(op, "jl") == 0 || strcmp(op, "jle") == 0 ||
        strcmp(op, "ja") == 0 || strcmp(op, "jae") == 0 ||
        strcmp(op, "jb") == 0 || strcmp(op, "jbe") == 0) {
        char sym[256];
        int64_t target = 0;
        if (as->object_mode) {
            if (!expr_is_plain_symbol(st->operands[0].text, sym, sizeof(sym))) {
                kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[0].column },
                           "unsupported relocation expression '%s'", st->operands[0].text);
                return 0;
            }
        } else if (!kasm_eval_expr(as, st->operands[0].text, st->section, st->offset, 1, &target,
                                   (SourceLoc){ as->path, st->line, st->operands[0].column })) {
            return 0;
        }
        int is_call = strcmp(op, "call") == 0;
        int is_jcc = !(strcmp(op, "jmp") == 0 || is_call);
        if (as->tiny && !is_call && branch_can_short(as, st)) {
            uint64_t target_off = 0;
            (void)branch_target_offset(as, st, &target_off);
            int64_t disp = (int64_t)target_off - (int64_t)(st->offset + 2);
            kasm_buf_append_u8(out, strcmp(op, "jmp") == 0 ? 0xEB : jcc_short_opcode(op));
            kasm_buf_append_u8(out, (uint8_t)(int8_t)disp);
            *why = strcmp(op, "jmp") == 0 ? "jmp rel8" : "jcc rel8";
            return 1;
        }
        int sz = is_jcc ? 6 : 5;
        if (strcmp(op, "jmp") == 0) kasm_buf_append_u8(out, 0xE9);
        else if (strcmp(op, "call") == 0) kasm_buf_append_u8(out, 0xE8);
        else {
            kasm_buf_append_u8(out, 0x0F);
            kasm_buf_append_u8(out, jcc_opcode(op));
        }
        if (as->object_mode) {
            kasm_add_reloc(as, st->section, out->len, sym, RELOC_PC32, -4,
                           (SourceLoc){ as->path, st->line, st->operands[0].column });
            kasm_buf_append_u32(out, 0);
        } else {
            int64_t here = (int64_t)(as->sections[st->section].vaddr + st->offset + sz);
            kasm_buf_append_u32(out, (uint32_t)(target - here));
        }
        *why = strcmp(op, "call") == 0 ? "call rel32" :
               strcmp(op, "jmp") == 0 ? "jmp rel32" :
               strcmp(op, "je") == 0 ? "je rel32" : "jne rel32";
        return 1;
    }

    kasm_error(as, loc, "unknown instruction '%s'; hint: check spelling or see docs/INSTRUCTIONS.md", op);
    return 0;
}

int kasm_encode_program(Assembler *as)
{
    int tiny_direct = as->tiny && !as->object_mode && !as->raw_mode;
    uint64_t header = 64 + (tiny_direct ? 1 : 3) * 56;
    uint64_t sizes[SEC_COUNT] = { 0, 0, 0 };
    for (size_t i = 0; i < as->program.len; i++) {
        Statement *st = &as->program.items[i];
        if ((st->type == ST_DATA || st->type == ST_INSTR) &&
            st->offset + st->size > sizes[st->section])
            sizes[st->section] = st->offset + st->size;
    }
    uint64_t file_align = tiny_direct ? 1 : 0x1000;
    as->sections[SEC_TEXT].vaddr = 0x400000 + kasm_align(header, file_align);
    as->sections[SEC_RODATA].vaddr = kasm_align(as->sections[SEC_TEXT].vaddr + sizes[SEC_TEXT], file_align);
    as->sections[SEC_DATA].vaddr = kasm_align(as->sections[SEC_RODATA].vaddr + sizes[SEC_RODATA], file_align);

    for (size_t i = 0; i < as->program.len; i++) {
        Statement *st = &as->program.items[i];
        if (st->type != ST_DATA && st->type != ST_INSTR)
            continue;
        ByteBuf *b = &as->sections[st->section];
        size_t before = b->len;
        const char *why = "data";
        if (st->type == ST_DATA) {
            int64_t reps = 1;
            if (st->expr) {
                if (!kasm_eval_expr(as, st->expr, st->section, st->offset, 0, &reps,
                                    (SourceLoc){ as->path, st->line, st->column }))
                    return 0;
            }
            for (int64_t r = 0; r < reps; r++)
                if (!emit_data_once(as, st, b))
                    return 0;
            why = "data directive";
        } else if (!emit_instr(as, st, b, &why)) {
            return 0;
        }
        if (as->explain || as->list_file)
            kasm_explain(as, st, b->data + before, b->len - before, why);
        if (st->type == ST_INSTR) {
            if (b->len - before > st->size) {
                kasm_error(as, (SourceLoc){ as->path, st->line, st->column },
                           "object output cannot encode requested operand within fixed instruction slot");
                return 0;
            }
            while (!as->tiny && b->len - before < st->size)
                kasm_buf_append_u8(b, 0x90);
        }
    }
    if (as->entry) {
        Symbol *entry = kasm_symbol_find(&as->symbols, as->entry);
        if (!entry || entry->is_const) {
            kasm_error(as, (SourceLoc){ as->path, 1, 1 }, "missing entry symbol '%s'", as->entry);
            return 0;
        }
    }
    return as->errors == 0;
}
