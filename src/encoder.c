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
    { "al", 0, 8 }, { "cl", 1, 8 }, { "dl", 2, 8 }, { "bl", 3, 8 },
    { "ah", 4, 8 }, { "ch", 5, 8 }, { "dh", 6, 8 }, { "bh", 7, 8 }
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

static int segreg_code(const char *name, int *code)
{
    if (kasm_streq_ci(name, "es")) { *code = 0; return 1; }
    if (kasm_streq_ci(name, "cs")) { *code = 1; return 1; }
    if (kasm_streq_ci(name, "ss")) { *code = 2; return 1; }
    if (kasm_streq_ci(name, "ds")) { *code = 3; return 1; }
    return 0;
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

static int mem_disp_size(const MemOp *mem, int tiny)
{
    if (mem->rip_relative)
        return 4;
    if (!tiny)
        return 4;
    if (mem->disp == 0 && (mem->base & 7) != 5)
        return 0;
    if (is_i8(mem->disp))
        return 1;
    return 4;
}

typedef struct {
    int is_mem;
    int size_bits;
    int rm;
    int mod;
    int disp_size;
    int64_t disp;
    char symbol[256];
    int has_symbol;
    int segreg;
    int has_segreg;
} Mem16;

static int parse_size_prefix(char **s, int *size_bits)
{
    if (prefix_ci(*s, "word ptr")) { *size_bits = 16; *s = kasm_trim(*s + 8); return 1; }
    if (prefix_ci(*s, "byte ptr")) { *size_bits = 8; *s = kasm_trim(*s + 8); return 1; }
    if (prefix_ci(*s, "word")) { *size_bits = 16; *s = kasm_trim(*s + 4); return 1; }
    if (prefix_ci(*s, "byte")) { *size_bits = 8; *s = kasm_trim(*s + 4); return 1; }
    return 0;
}

static int parse_mem16(Assembler *as, const char *text, SourceLoc loc, Mem16 *m)
{
    memset(m, 0, sizeof(*m));
    m->rm = -1;
    m->segreg = -1;
    char *tmp = kasm_xstrdup(text);
    char *s = kasm_trim(tmp);
    parse_size_prefix(&s, &m->size_bits);
    size_t n = strlen(s);
    if (n < 2 || s[0] != '[' || s[n - 1] != ']') {
        free(tmp);
        return 0;
    }
    s[n - 1] = 0;
    char *inside = kasm_trim(s + 1);
    char *colon = strchr(inside, ':');
    if (colon) {
        *colon = 0;
        if (!segreg_code(kasm_trim(inside), &m->segreg)) {
            kasm_error(as, loc, "invalid segment override '%s'", inside);
            free(tmp);
            return -1;
        }
        m->has_segreg = 1;
        inside = kasm_trim(colon + 1);
    }
    char normalized[512];
    size_t j = 0;
    for (char *p = inside; *p && j + 2 < sizeof(normalized); p++) {
        if (*p == '-') { normalized[j++] = '+'; normalized[j++] = '-'; }
        else if (*p != ' ' && *p != '\t') normalized[j++] = *p;
    }
    normalized[j] = 0;
    int has_bx = 0, has_bp = 0, has_si = 0, has_di = 0, regs_seen = 0;
    char *term = strtok(normalized, "+");
    while (term) {
        if (*term == 0) {
            term = strtok(NULL, "+");
            continue;
        }
        int code, bits;
        if (reg_code(term, &code, &bits)) {
            if (bits != 16 || !(code == 3 || code == 5 || code == 6 || code == 7)) {
                kasm_error(as, loc, "invalid 8086 effective address '%s'; hint: valid bases are BX, BP, SI and DI in supported combinations", text);
                free(tmp);
                return -1;
            }
            has_bx |= code == 3; has_bp |= code == 5; has_si |= code == 6; has_di |= code == 7;
            regs_seen++;
        } else {
            int64_t v = 0;
            if (kasm_parse_int(term, &v)) {
                m->disp += v;
            } else {
                if (m->has_symbol) {
                    kasm_error(as, loc, "invalid 8086 effective address '%s'", text);
                    free(tmp);
                    return -1;
                }
                snprintf(m->symbol, sizeof(m->symbol), "%s", term);
                m->has_symbol = 1;
            }
        }
        term = strtok(NULL, "+");
    }
    if (regs_seen == 0) {
        m->mod = 0; m->rm = 6; m->disp_size = 2; m->is_mem = 1;
        free(tmp);
        return 1;
    }
    if (has_bx && has_si && regs_seen == 2) m->rm = 0;
    else if (has_bx && has_di && regs_seen == 2) m->rm = 1;
    else if (has_bp && has_si && regs_seen == 2) m->rm = 2;
    else if (has_bp && has_di && regs_seen == 2) m->rm = 3;
    else if (has_si && regs_seen == 1) m->rm = 4;
    else if (has_di && regs_seen == 1) m->rm = 5;
    else if (has_bp && regs_seen == 1) m->rm = 6;
    else if (has_bx && regs_seen == 1) m->rm = 7;
    else {
        kasm_error(as, loc, "invalid 8086 effective address '%s'; hint: valid forms include [BX+SI], [BX+DI], [BP+SI], [BP+DI], [SI], [DI], [BP] and [BX]", text);
        free(tmp);
        return -1;
    }
    if (m->has_symbol || m->disp < INT8_MIN || m->disp > INT8_MAX) {
        m->mod = 2; m->disp_size = 2;
    } else if (m->disp != 0 || m->rm == 6) {
        m->mod = 1; m->disp_size = 1;
    } else {
        m->mod = 0; m->disp_size = 0;
    }
    m->is_mem = 1;
    free(tmp);
    return 1;
}

static void emit_mem16_prefix(ByteBuf *out, Mem16 *m)
{
    if (m->has_segreg) {
        static const uint8_t pfx[] = { 0x26, 0x2E, 0x36, 0x3E };
        kasm_buf_append_u8(out, pfx[m->segreg]);
    }
}

static void emit_mem16_addr(Assembler *as, Statement *st, ByteBuf *out, Mem16 *m, int reg, int dry)
{
    modrm(out, m->mod, reg, m->rm);
    if (m->disp_size == 1) {
        kasm_buf_append_u8(out, (uint8_t)(int8_t)m->disp);
    } else if (m->disp_size == 2) {
        int64_t v = m->disp;
        if (m->has_symbol && !dry) {
            if (!kasm_eval_expr(as, m->symbol, st->section, st->offset, 1, &v,
                                (SourceLoc){ as->path, st->line, st->column }))
                v = 0;
        } else if (m->has_symbol) {
            v = 0;
        }
        kasm_buf_append_u16(out, (uint16_t)v);
    }
}

static int emit_modrm_mem(ByteBuf *out, int reg_field, const MemOp *mem, int tiny)
{
    if (mem->rip_relative) {
        modrm(out, 0, reg_field, 5);
        return 0;
    }
    int need_sib = mem->has_index || (mem->base & 7) == 4;
    int disp_size = mem_disp_size(mem, tiny);
    int mod = disp_size == 0 ? 0 : disp_size == 1 ? 1 : 2;
    modrm(out, mod, reg_field, need_sib ? 4 : mem->base);
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

static int mem_encoding_size(int reg_bits, int reg, const MemOp *mem, int tiny)
{
    return rex_size(reg_bits == 64, reg, mem->has_index ? mem->index : 0,
                    mem->rip_relative ? 0 : mem->base) +
           1 + 1 + (mem_uses_sib(mem) ? 1 : 0) + mem_disp_size(mem, tiny);
}

static void append_disp32(ByteBuf *out, int64_t disp)
{
    kasm_buf_append_u32(out, (uint32_t)(int32_t)disp);
}

static void append_mem_disp(ByteBuf *out, const MemOp *mem, int tiny)
{
    int disp_size = mem_disp_size(mem, tiny);
    if (disp_size == 1)
        kasm_buf_append_u8(out, (uint8_t)(int8_t)mem->disp);
    else if (disp_size == 4)
        append_disp32(out, mem->rip_relative ? 0 : mem->disp);
}

static const char *mem_encoding_why(Assembler *as, const MemOp *mem, const char *normal)
{
    static char buf[160];
    int disp_size = mem_disp_size(mem, as->tiny);
    if (as->tiny && !mem->rip_relative && disp_size < 4) {
        snprintf(buf, sizeof(buf), "tiny: %s with %s displacement",
                 normal, disp_size == 0 ? "no" : "disp8");
        return buf;
    }
    return normal;
}

static void count_tiny_mem_disp(Assembler *as, const MemOp *mem)
{
    if (!as->tiny || mem->rip_relative)
        return;
    int disp_size = mem_disp_size(mem, 1);
    if (disp_size == 0)
        as->tiny_disp0_used++;
    else if (disp_size == 1)
        as->tiny_disp8_used++;
}

static uint8_t accumulator_imm_opcode(const char *name)
{
    if (strcmp(name, "add") == 0) return 0x05;
    if (strcmp(name, "or") == 0) return 0x0D;
    if (strcmp(name, "and") == 0) return 0x25;
    if (strcmp(name, "sub") == 0) return 0x2D;
    if (strcmp(name, "xor") == 0) return 0x35;
    if (strcmp(name, "cmp") == 0) return 0x3D;
    return 0;
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
    emit_modrm_mem(out, reg, mem, as->tiny);
    append_mem_disp(out, mem, as->tiny);
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
        emit_modrm_mem(out, b, &mem, as->tiny);
        append_mem_disp(out, &mem, as->tiny);
        *why = mem_encoding_why(as, &mem, "ModRM/SIB memory operand");
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
        *why = mem_encoding_why(as, &mem, "ModRM/SIB memory operand");
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
    if (as->tiny && is_i8(imm)) {
        rex(out, abits == 64, 0, 0, a);
        kasm_buf_append_u8(out, 0x83);
        modrm(out, 3, imm_ext, a);
        kasm_buf_append_u8(out, (uint8_t)(int8_t)imm);
        static char buf[128];
        snprintf(buf, sizeof(buf), "tiny: selected sign-extended imm8 for %s", name);
        *why = buf;
        return 1;
    }
    uint8_t acc_op = accumulator_imm_opcode(name);
    if (as->tiny && acc_op && (a & 7) == 0 && (abits == 32 || abits == 64)) {
        rex(out, abits == 64, 0, 0, a);
        kasm_buf_append_u8(out, acc_op);
        kasm_buf_append_u32(out, (uint32_t)imm);
        static char buf[128];
        snprintf(buf, sizeof(buf), "tiny: selected accumulator imm32 form for %s", name);
        *why = buf;
        return 1;
    } else {
        rex(out, abits == 64, 0, 0, a);
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
                           int allow_imm8, int *used_imm8, int count_stats)
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
        if (count_stats)
            count_tiny_mem_disp(as, &mem);
        return mem_encoding_size(bbits, b, &mem, as->tiny);
    }
    if (!reg_code(st->operands[0].text, &a, &abits))
        return st->size ? (int)st->size : 15;
    int op1_mem = parse_mem_operand(as, st->operands[1].text, loc1, &mem);
    if (op1_mem < 0)
        return st->size ? (int)st->size : 15;
    if (op1_mem) {
        if (count_stats)
            count_tiny_mem_disp(as, &mem);
        return mem_encoding_size(abits, a, &mem, as->tiny);
    }
    if (reg_code(st->operands[1].text, &b, &bbits))
        return rex_size(abits == 64 || bbits == 64, b, 0, a) + 2;
    int64_t imm;
    if (!kasm_eval_expr(as, st->operands[1].text, st->section, st->offset, 0, &imm, loc1))
        return st->size ? (int)st->size : 15;
    if (allow_imm8 && is_i8(imm)) {
        *used_imm8 = 1;
        return rex_size(abits == 64, 0, 0, a) + 3;
    }
    if (as->tiny && accumulator_imm_opcode(name) && (a & 7) == 0 &&
        (abits == 32 || abits == 64)) {
        if (used_imm8)
            *used_imm8 = 2;
        return rex_size(abits == 64, 0, 0, a) + 5;
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
        if (strcmp(op, "push") == 0 && !reg_code(st->operands[0].text, &rd, &b1)) {
            int64_t imm;
            if (!kasm_eval_expr(as, st->operands[0].text, st->section, st->offset, 0, &imm, loc0))
                return st->size ? (int)st->size : 15;
            if (count_stats && as->tiny && is_i8(imm))
                as->tiny_push_imm8_used++;
            return as->tiny && is_i8(imm) ? 2 : 5;
        }
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
            if (count_stats)
                count_tiny_mem_disp(as, &mem);
            return mem_encoding_size(b2, rs, &mem, as->tiny);
        }
        if (!reg_code(st->operands[0].text, &rd, &b1))
            return st->size ? (int)st->size : 15;
        int src_mem = parse_mem_operand(as, st->operands[1].text, loc1, &mem);
        if (src_mem < 0)
            return st->size ? (int)st->size : 15;
        if (src_mem) {
            if (count_stats)
                count_tiny_mem_disp(as, &mem);
            return mem_encoding_size(b1, rd, &mem, as->tiny);
        }
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
        if (count_stats)
            count_tiny_mem_disp(as, &mem);
        return mem_encoding_size(64, rd, &mem, as->tiny);
    }
    if ((strcmp(op, "add") == 0 || strcmp(op, "sub") == 0 ||
         strcmp(op, "and") == 0 || strcmp(op, "or") == 0 ||
         strcmp(op, "xor") == 0 || strcmp(op, "cmp") == 0) && c == 2) {
        int used_imm8 = 0;
        int sz = tiny_binop_size(as, st, op, 1, &used_imm8, count_stats);
        if (count_stats && used_imm8)
            (used_imm8 == 2 ? as->tiny_accumulator_used++ : as->tiny_imm8_used++);
        return sz;
    }
    if (strcmp(op, "test") == 0 && c == 2) {
        int used_imm8 = 0;
        return tiny_binop_size(as, st, op, 0, &used_imm8, count_stats);
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
    as->tiny_push_imm8_used = 0;
    as->tiny_accumulator_used = 0;
    as->tiny_disp8_used = 0;
    as->tiny_disp0_used = 0;
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

static int jcc16_opcode(const char *op)
{
    if (strcmp(op, "jo") == 0) return 0x70;
    if (strcmp(op, "jno") == 0) return 0x71;
    if (strcmp(op, "jc") == 0 || strcmp(op, "jb") == 0 || strcmp(op, "jnae") == 0) return 0x72;
    if (strcmp(op, "jnc") == 0 || strcmp(op, "jae") == 0 || strcmp(op, "jnb") == 0) return 0x73;
    if (strcmp(op, "je") == 0 || strcmp(op, "jz") == 0) return 0x74;
    if (strcmp(op, "jne") == 0 || strcmp(op, "jnz") == 0) return 0x75;
    if (strcmp(op, "jbe") == 0 || strcmp(op, "jna") == 0) return 0x76;
    if (strcmp(op, "ja") == 0 || strcmp(op, "jnbe") == 0) return 0x77;
    if (strcmp(op, "js") == 0) return 0x78;
    if (strcmp(op, "jns") == 0) return 0x79;
    if (strcmp(op, "jp") == 0 || strcmp(op, "jpe") == 0) return 0x7A;
    if (strcmp(op, "jnp") == 0 || strcmp(op, "jpo") == 0) return 0x7B;
    if (strcmp(op, "jl") == 0 || strcmp(op, "jnge") == 0) return 0x7C;
    if (strcmp(op, "jge") == 0 || strcmp(op, "jnl") == 0) return 0x7D;
    if (strcmp(op, "jle") == 0 || strcmp(op, "jng") == 0) return 0x7E;
    if (strcmp(op, "jg") == 0 || strcmp(op, "jnle") == 0) return 0x7F;
    return -1;
}

static int one_byte16_opcode(const char *op)
{
    if (strcmp(op, "pushf") == 0) return 0x9C;
    if (strcmp(op, "popf") == 0) return 0x9D;
    if (strcmp(op, "ret") == 0) return 0xC3;
    if (strcmp(op, "retf") == 0) return 0xCB;
    if (strcmp(op, "iret") == 0) return 0xCF;
    if (strcmp(op, "clc") == 0) return 0xF8;
    if (strcmp(op, "stc") == 0) return 0xF9;
    if (strcmp(op, "cli") == 0) return 0xFA;
    if (strcmp(op, "sti") == 0) return 0xFB;
    if (strcmp(op, "cld") == 0) return 0xFC;
    if (strcmp(op, "std") == 0) return 0xFD;
    if (strcmp(op, "cmc") == 0) return 0xF5;
    if (strcmp(op, "lahf") == 0) return 0x9F;
    if (strcmp(op, "sahf") == 0) return 0x9E;
    if (strcmp(op, "nop") == 0) return 0x90;
    if (strcmp(op, "hlt") == 0) return 0xF4;
    if (strcmp(op, "wait") == 0) return 0x9B;
    if (strcmp(op, "cbw") == 0) return 0x98;
    if (strcmp(op, "cwd") == 0) return 0x99;
    if (strcmp(op, "movsb") == 0) return 0xA4;
    if (strcmp(op, "movsw") == 0) return 0xA5;
    if (strcmp(op, "cmpsb") == 0) return 0xA6;
    if (strcmp(op, "cmpsw") == 0) return 0xA7;
    if (strcmp(op, "stosb") == 0) return 0xAA;
    if (strcmp(op, "stosw") == 0) return 0xAB;
    if (strcmp(op, "lodsb") == 0) return 0xAC;
    if (strcmp(op, "lodsw") == 0) return 0xAD;
    if (strcmp(op, "scasb") == 0) return 0xAE;
    if (strcmp(op, "scasw") == 0) return 0xAF;
    return -1;
}

static int group1_ext16(const char *op)
{
    if (strcmp(op, "add") == 0) return 0;
    if (strcmp(op, "or") == 0) return 1;
    if (strcmp(op, "adc") == 0) return 2;
    if (strcmp(op, "sbb") == 0) return 3;
    if (strcmp(op, "and") == 0) return 4;
    if (strcmp(op, "sub") == 0) return 5;
    if (strcmp(op, "xor") == 0) return 6;
    if (strcmp(op, "cmp") == 0) return 7;
    return -1;
}

static int emit16_eval(Assembler *as, Statement *st, const char *expr, int op_index,
                       int want_absolute, int dry, int64_t *out)
{
    if (dry) {
        *out = 0;
        return 1;
    }
    return kasm_eval_expr(as, expr, st->section, st->offset, want_absolute, out,
                          (SourceLoc){ as->path, st->line, st->operands[op_index].column });
}

static int parse_far16(Assembler *as, Statement *st, const char *text, int op_index,
                       int dry, uint16_t *seg, uint16_t *off)
{
    char *tmp = kasm_xstrdup(text);
    char *s = kasm_trim(tmp);
    if (prefix_ci(s, "far"))
        s = kasm_trim(s + 3);
    char *colon = strchr(s, ':');
    if (!colon) {
        free(tmp);
        return 0;
    }
    *colon = 0;
    int64_t seg_v = 0, off_v = 0;
    int ok = emit16_eval(as, st, kasm_trim(s), op_index, 0, dry, &seg_v) &&
             emit16_eval(as, st, kasm_trim(colon + 1), op_index, 0, dry, &off_v);
    free(tmp);
    if (!ok)
        return -1;
    if (!dry && (seg_v < 0 || seg_v > 0xFFFF || off_v < 0 || off_v > 0xFFFF)) {
        kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[op_index].column },
                   "far pointer components must fit in 16 bits");
        return -1;
    }
    *seg = (uint16_t)seg_v;
    *off = (uint16_t)off_v;
    return 1;
}

static int emit16_mem_group(Assembler *as, Statement *st, ByteBuf *out, const char **why,
                            const char *op, int dry)
{
    int rd = 0, rs = 0, db = 0, sb = 0;
    int g1 = group1_ext16(op);
    if (g1 < 0)
        return 0;
    Mem16 mem;
    int dst_mem = parse_mem16(as, st->operands[0].text,
                              (SourceLoc){ as->path, st->line, st->operands[0].column }, &mem);
    if (dst_mem < 0)
        return -1;
    if (dst_mem) {
        if (reg_code(st->operands[1].text, &rs, &sb)) {
            if (sb != 8 && sb != 16)
                return 0;
            if (mem.size_bits && mem.size_bits != sb) {
                kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[0].column },
                           "memory operand size does not match register");
                return -1;
            }
            static const uint8_t rm_reg[] = { 0x00,0x08,0x10,0x18,0x20,0x28,0x30,0x38 };
            emit_mem16_prefix(out, &mem);
            kasm_buf_append_u8(out, (uint8_t)(rm_reg[g1] + (sb == 16 ? 1 : 0)));
            emit_mem16_addr(as, st, out, &mem, rs, dry);
            *why = "8086 ALU memory, register";
            return 1;
        }
        int sz = mem.size_bits;
        if (!sz) {
            kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[0].column },
                       "ambiguous memory operand size in 16-bit mode; hint: use 'byte [value]' or 'word [value]'");
            return -1;
        }
        int64_t imm = 0;
        if (!emit16_eval(as, st, st->operands[1].text, 1, 0, dry, &imm))
            return -1;
        emit_mem16_prefix(out, &mem);
        kasm_buf_append_u8(out, sz == 8 ? 0x80 : 0x81);
        emit_mem16_addr(as, st, out, &mem, g1, dry);
        if (sz == 8)
            kasm_buf_append_u8(out, (uint8_t)imm);
        else
            kasm_buf_append_u16(out, (uint16_t)imm);
        *why = "8086 ALU memory, immediate";
        return 1;
    }
    if (reg_code(st->operands[0].text, &rd, &db)) {
        int src_mem = parse_mem16(as, st->operands[1].text,
                                  (SourceLoc){ as->path, st->line, st->operands[1].column }, &mem);
        if (src_mem < 0)
            return -1;
        if (src_mem) {
            if (db != 8 && db != 16)
                return 0;
            static const uint8_t reg_rm[] = { 0x02,0x0A,0x12,0x1A,0x22,0x2A,0x32,0x3A };
            emit_mem16_prefix(out, &mem);
            kasm_buf_append_u8(out, (uint8_t)(reg_rm[g1] + (db == 16 ? 1 : 0)));
            emit_mem16_addr(as, st, out, &mem, rd, dry);
            *why = "8086 ALU register, memory";
            return 1;
        }
    }
    return 0;
}

static int emit16(Assembler *as, Statement *st, ByteBuf *out, const char **why, int dry)
{
    const char *op = st->op;
    int c = st->operand_count;
    int rd = 0, rs = 0, db = 0, sb = 0, sd = 0, ss = 0;
    int op1 = one_byte16_opcode(op);
    if (op1 >= 0 && c == 0) {
        kasm_buf_append_u8(out, (uint8_t)op1);
        *why = "8086 one-byte instruction";
        return 1;
    }
    if ((strcmp(op, "rep") == 0 || strcmp(op, "repe") == 0 || strcmp(op, "repz") == 0 ||
         strcmp(op, "repne") == 0 || strcmp(op, "repnz") == 0) && c == 1) {
        int inner = one_byte16_opcode(st->operands[0].text);
        if (inner < 0) {
            kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[0].column }, "invalid string instruction after %s", op);
            return 0;
        }
        kasm_buf_append_u8(out, (strcmp(op, "repne") == 0 || strcmp(op, "repnz") == 0) ? 0xF2 : 0xF3);
        kasm_buf_append_u8(out, (uint8_t)inner);
        *why = "8086 repeat-prefixed string instruction";
        return 1;
    }
    if (strcmp(op, "int") == 0 && c == 1) {
        int64_t v;
        if (!emit16_eval(as, st, st->operands[0].text, 0, 0, dry, &v)) return 0;
        if (!dry && (v < 0 || v > 255)) {
            kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[0].column }, "interrupt vector does not fit in 8 bits");
            return 0;
        }
        kasm_buf_append_u8(out, 0xCD); kasm_buf_append_u8(out, (uint8_t)v);
        *why = "int imm8";
        return 1;
    }
    if ((strcmp(op, "jmp") == 0 || strcmp(op, "call") == 0) && c == 1) {
        uint16_t far_seg = 0, far_off = 0;
        int far = parse_far16(as, st, st->operands[0].text, 0, dry, &far_seg, &far_off);
        if (far < 0)
            return 0;
        if (far) {
            kasm_buf_append_u8(out, strcmp(op, "jmp") == 0 ? 0xEA : 0x9A);
            kasm_buf_append_u16(out, far_off);
            kasm_buf_append_u16(out, far_seg);
            *why = strcmp(op, "jmp") == 0 ? "8086 far jmp ptr16:16" : "8086 far call ptr16:16";
            return 1;
        }
        int64_t target;
        if (!emit16_eval(as, st, st->operands[0].text, 0, 1, dry, &target)) return 0;
        int64_t here = (int64_t)(as->sections[st->section].vaddr + st->offset + 3);
        kasm_buf_append_u8(out, strcmp(op, "jmp") == 0 ? 0xE9 : 0xE8);
        kasm_buf_append_u16(out, (uint16_t)(target - here));
        *why = strcmp(op, "jmp") == 0 ? "8086 near jmp rel16" : "8086 near call rel16";
        return 1;
    }
    int jcc = jcc16_opcode(op);
    if (jcc >= 0 && c == 1) {
        int64_t target;
        if (!emit16_eval(as, st, st->operands[0].text, 0, 1, dry, &target)) return 0;
        int64_t here = (int64_t)(as->sections[st->section].vaddr + st->offset + 2);
        int64_t d = target - here;
        if (!dry && (d < INT8_MIN || d > INT8_MAX)) {
            kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[0].column }, "conditional jump target is outside signed 8-bit range");
            return 0;
        }
        kasm_buf_append_u8(out, (uint8_t)jcc); kasm_buf_append_u8(out, (uint8_t)(int8_t)d);
        *why = "8086 conditional jump rel8";
        return 1;
    }
    if ((strcmp(op, "loop") == 0 || strcmp(op, "loope") == 0 || strcmp(op, "loopz") == 0 ||
         strcmp(op, "loopne") == 0 || strcmp(op, "loopnz") == 0 || strcmp(op, "jcxz") == 0) && c == 1) {
        int64_t target;
        if (!emit16_eval(as, st, st->operands[0].text, 0, 1, dry, &target)) return 0;
        int opcode = strcmp(op, "loop") == 0 ? 0xE2 :
                     (strcmp(op, "loope") == 0 || strcmp(op, "loopz") == 0) ? 0xE1 :
                     (strcmp(op, "jcxz") == 0) ? 0xE3 : 0xE0;
        int64_t d = target - (int64_t)(as->sections[st->section].vaddr + st->offset + 2);
        if (!dry && (d < INT8_MIN || d > INT8_MAX)) {
            kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[0].column }, "loop target is outside signed 8-bit range");
            return 0;
        }
        kasm_buf_append_u8(out, (uint8_t)opcode); kasm_buf_append_u8(out, (uint8_t)(int8_t)d);
        *why = "8086 loop/jcxz rel8";
        return 1;
    }
    if ((strcmp(op, "push") == 0 || strcmp(op, "pop") == 0) && c == 1) {
        if (reg_code(st->operands[0].text, &rd, &db) && db == 16) {
            kasm_buf_append_u8(out, (uint8_t)((strcmp(op, "push") == 0 ? 0x50 : 0x58) + rd));
            *why = "8086 push/pop r16";
            return 1;
        }
        if (segreg_code(st->operands[0].text, &sd)) {
            if (strcmp(op, "push") == 0) kasm_buf_append_u8(out, (uint8_t)(0x06 + (sd << 3)));
            else kasm_buf_append_u8(out, (uint8_t)(0x07 + (sd << 3)));
            *why = "8086 push/pop segment";
            return 1;
        }
    }
    if ((strcmp(op, "in") == 0 || strcmp(op, "out") == 0) && c == 2) {
        int64_t port = 0;
        if (strcmp(op, "in") == 0) {
            if (!reg_code(st->operands[0].text, &rd, &db) || !(rd == 0 && (db == 8 || db == 16))) {
                kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[0].column }, "IN destination must be AL or AX");
                return 0;
            }
            if (kasm_streq_ci(st->operands[1].text, "dx")) {
                kasm_buf_append_u8(out, db == 8 ? 0xEC : 0xED);
            } else {
                if (!emit16_eval(as, st, st->operands[1].text, 1, 0, dry, &port)) return 0;
                if (!dry && (port < 0 || port > 0xFF)) {
                    kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[1].column }, "I/O port immediate must fit in 8 bits");
                    return 0;
                }
                kasm_buf_append_u8(out, db == 8 ? 0xE4 : 0xE5);
                kasm_buf_append_u8(out, (uint8_t)port);
            }
            *why = "8086 in";
            return 1;
        }
        if (!reg_code(st->operands[1].text, &rs, &sb) || !(rs == 0 && (sb == 8 || sb == 16))) {
            kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[1].column }, "OUT source must be AL or AX");
            return 0;
        }
        if (kasm_streq_ci(st->operands[0].text, "dx")) {
            kasm_buf_append_u8(out, sb == 8 ? 0xEE : 0xEF);
        } else {
            if (!emit16_eval(as, st, st->operands[0].text, 0, 0, dry, &port)) return 0;
            if (!dry && (port < 0 || port > 0xFF)) {
                kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[0].column }, "I/O port immediate must fit in 8 bits");
                return 0;
            }
            kasm_buf_append_u8(out, sb == 8 ? 0xE6 : 0xE7);
            kasm_buf_append_u8(out, (uint8_t)port);
        }
        *why = "8086 out";
        return 1;
    }
    if (strcmp(op, "mov") == 0 && c == 2) {
        if (segreg_code(st->operands[0].text, &sd)) {
            if (sd == 1) {
                kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[0].column }, "MOV CS, ... is not valid on x86");
                return 0;
            }
            if (!reg_code(st->operands[1].text, &rs, &sb) || sb != 16) goto bad_mov;
            kasm_buf_append_u8(out, 0x8E); modrm(out, 3, sd, rs);
            *why = "mov segment, r16";
            return 1;
        }
        if (reg_code(st->operands[0].text, &rd, &db) && segreg_code(st->operands[1].text, &ss)) {
            if (db != 16) goto bad_mov;
            kasm_buf_append_u8(out, 0x8C); modrm(out, 3, ss, rd);
            *why = "mov r16, segment";
            return 1;
        }
        Mem16 mem;
        int dst_mem = parse_mem16(as, st->operands[0].text, (SourceLoc){ as->path, st->line, st->operands[0].column }, &mem);
        if (dst_mem < 0) return 0;
        if (dst_mem) {
            if (segreg_code(st->operands[1].text, &ss)) {
                if (mem.size_bits && mem.size_bits != 16) {
                    kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[0].column },
                               "segment memory moves require word memory operands");
                    return 0;
                }
                emit_mem16_prefix(out, &mem);
                kasm_buf_append_u8(out, 0x8C);
                emit_mem16_addr(as, st, out, &mem, ss, dry);
                *why = "mov r/m16, segment";
                return 1;
            }
            if (reg_code(st->operands[1].text, &rs, &sb)) {
                if (sb != 8 && sb != 16) goto bad_mov;
                if (mem.size_bits && mem.size_bits != sb) {
                    kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[0].column }, "memory operand size does not match register");
                    return 0;
                }
                emit_mem16_prefix(out, &mem);
                kasm_buf_append_u8(out, (uint8_t)(sb == 8 ? 0x88 : 0x89));
                emit_mem16_addr(as, st, out, &mem, rs, dry);
                *why = "mov r/m, r";
                return 1;
            }
            int64_t imm;
            int sz = mem.size_bits;
            if (!sz) {
                kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[0].column }, "ambiguous memory operand size in 16-bit mode; hint: use 'byte [value]' or 'word [value]'");
                return 0;
            }
            if (!emit16_eval(as, st, st->operands[1].text, 1, sz == 16, dry, &imm)) return 0;
            emit_mem16_prefix(out, &mem);
            kasm_buf_append_u8(out, sz == 8 ? 0xC6 : 0xC7);
            emit_mem16_addr(as, st, out, &mem, 0, dry);
            if (sz == 8) kasm_buf_append_u8(out, (uint8_t)imm);
            else kasm_buf_append_u16(out, (uint16_t)imm);
            *why = "mov r/m, imm";
            return 1;
        }
        if (reg_code(st->operands[0].text, &rd, &db)) {
            int src_mem = parse_mem16(as, st->operands[1].text, (SourceLoc){ as->path, st->line, st->operands[1].column }, &mem);
            if (src_mem < 0) return 0;
            if (src_mem) {
                if (db != 8 && db != 16) goto bad_mov;
                emit_mem16_prefix(out, &mem);
                kasm_buf_append_u8(out, (uint8_t)(db == 8 ? 0x8A : 0x8B));
                emit_mem16_addr(as, st, out, &mem, rd, dry);
                *why = "mov r, r/m";
                return 1;
            }
            if (reg_code(st->operands[1].text, &rs, &sb)) {
                if (db != sb || (db != 8 && db != 16)) goto bad_mov;
                kasm_buf_append_u8(out, (uint8_t)(db == 8 ? 0x88 : 0x89)); modrm(out, 3, rs, rd);
                *why = "mov r, r";
                return 1;
            }
            int64_t imm;
            if (!emit16_eval(as, st, st->operands[1].text, 1, db == 16, dry, &imm)) return 0;
            if (db != 8 && db != 16) {
                kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[0].column }, "register '%s' is unavailable in 16-bit mode", st->operands[0].text);
                return 0;
            }
            kasm_buf_append_u8(out, (uint8_t)((db == 8 ? 0xB0 : 0xB8) + rd));
            if (db == 8) kasm_buf_append_u8(out, (uint8_t)imm);
            else kasm_buf_append_u16(out, (uint16_t)imm);
            *why = db == 8 ? "mov r8, imm8" : "mov r16, imm16";
            return 1;
        }
bad_mov:
        kasm_error(as, (SourceLoc){ as->path, st->line, st->column }, "invalid operand combination for 'mov' in 16-bit mode");
        return 0;
    }
    if ((strcmp(op, "lea") == 0 || strcmp(op, "lds") == 0 || strcmp(op, "les") == 0) && c == 2) {
        if (!reg_code(st->operands[0].text, &rd, &db) || db != 16) {
            kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[0].column }, "%s destination must be a 16-bit register", op);
            return 0;
        }
        Mem16 mem;
        int src_mem = parse_mem16(as, st->operands[1].text,
                                  (SourceLoc){ as->path, st->line, st->operands[1].column }, &mem);
        if (src_mem <= 0)
            return 0;
        emit_mem16_prefix(out, &mem);
        kasm_buf_append_u8(out, strcmp(op, "lea") == 0 ? 0x8D : strcmp(op, "les") == 0 ? 0xC4 : 0xC5);
        emit_mem16_addr(as, st, out, &mem, rd, dry);
        *why = strcmp(op, "lea") == 0 ? "8086 lea r16, m" : "8086 load far pointer";
        return 1;
    }
    if (strcmp(op, "xchg") == 0 && c == 2) {
        if (!reg_code(st->operands[0].text, &rd, &db) || !reg_code(st->operands[1].text, &rs, &sb) ||
            db != sb || (db != 8 && db != 16)) {
            kasm_error(as, (SourceLoc){ as->path, st->line, st->column }, "XCHG currently supports register/register forms in 16-bit mode");
            return 0;
        }
        if (db == 16 && rd == 0) {
            kasm_buf_append_u8(out, (uint8_t)(0x90 + rs));
        } else if (db == 16 && rs == 0) {
            kasm_buf_append_u8(out, (uint8_t)(0x90 + rd));
        } else {
            kasm_buf_append_u8(out, db == 8 ? 0x86 : 0x87);
            modrm(out, 3, rd, rs);
        }
        *why = "8086 xchg register, register";
        return 1;
    }
    int g1 = group1_ext16(op);
    if (g1 >= 0 && c == 2) {
        int mem_result = emit16_mem_group(as, st, out, why, op, dry);
        if (mem_result < 0)
            return 0;
        if (mem_result > 0)
            return 1;
        if (!reg_code(st->operands[0].text, &rd, &db)) {
            kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[0].column }, "invalid destination operand for '%s'", op);
            return 0;
        }
        if (reg_code(st->operands[1].text, &rs, &sb)) {
            if (db != sb || (db != 8 && db != 16)) return 0;
            static const uint8_t rm_reg[] = { 0x00,0x08,0x10,0x18,0x20,0x28,0x30,0x38 };
            kasm_buf_append_u8(out, (uint8_t)(rm_reg[g1] + (db == 16 ? 1 : 0)));
            modrm(out, 3, rs, rd);
            *why = "8086 ALU r/m, r";
            return 1;
        }
        int64_t imm;
        if (!emit16_eval(as, st, st->operands[1].text, 1, 0, dry, &imm)) return 0;
        if (db != 8 && db != 16) return 0;
        kasm_buf_append_u8(out, db == 8 ? 0x80 : 0x81);
        modrm(out, 3, g1, rd);
        if (db == 8) kasm_buf_append_u8(out, (uint8_t)imm);
        else kasm_buf_append_u16(out, (uint16_t)imm);
        *why = "8086 ALU r/m, imm";
        return 1;
    }
    if (strcmp(op, "test") == 0 && c == 2) {
        Mem16 mem;
        int dst_mem = parse_mem16(as, st->operands[0].text,
                                  (SourceLoc){ as->path, st->line, st->operands[0].column }, &mem);
        if (dst_mem < 0) return 0;
        if (dst_mem) {
            if (reg_code(st->operands[1].text, &rs, &sb)) {
                if (sb != 8 && sb != 16) return 0;
                emit_mem16_prefix(out, &mem);
                kasm_buf_append_u8(out, sb == 8 ? 0x84 : 0x85);
                emit_mem16_addr(as, st, out, &mem, rs, dry);
                *why = "test memory, register";
                return 1;
            }
            int sz = mem.size_bits;
            if (!sz) {
                kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[0].column },
                           "ambiguous memory operand size in 16-bit mode; hint: use 'byte [value]' or 'word [value]'");
                return 0;
            }
            int64_t imm = 0;
            if (!emit16_eval(as, st, st->operands[1].text, 1, 0, dry, &imm)) return 0;
            emit_mem16_prefix(out, &mem);
            kasm_buf_append_u8(out, sz == 8 ? 0xF6 : 0xF7);
            emit_mem16_addr(as, st, out, &mem, 0, dry);
            if (sz == 8) kasm_buf_append_u8(out, (uint8_t)imm);
            else kasm_buf_append_u16(out, (uint16_t)imm);
            *why = "test memory, immediate";
            return 1;
        }
        if (!reg_code(st->operands[0].text, &rd, &db)) return 0;
        if (reg_code(st->operands[1].text, &rs, &sb)) {
            if (db != sb || (db != 8 && db != 16)) return 0;
            kasm_buf_append_u8(out, db == 8 ? 0x84 : 0x85); modrm(out, 3, rs, rd);
            *why = "test r/m, r";
            return 1;
        }
        int64_t imm;
        if (!emit16_eval(as, st, st->operands[1].text, 1, 0, dry, &imm)) return 0;
        kasm_buf_append_u8(out, db == 8 ? 0xF6 : 0xF7); modrm(out, 3, 0, rd);
        if (db == 8) kasm_buf_append_u8(out, (uint8_t)imm);
        else kasm_buf_append_u16(out, (uint16_t)imm);
        *why = "test r/m, imm";
        return 1;
    }
    if ((strcmp(op, "inc") == 0 || strcmp(op, "dec") == 0) && c == 1 &&
        reg_code(st->operands[0].text, &rd, &db) && db == 16) {
        kasm_buf_append_u8(out, (uint8_t)((strcmp(op, "inc") == 0 ? 0x40 : 0x48) + rd));
        *why = "8086 inc/dec r16";
        return 1;
    }
    if ((strcmp(op, "inc") == 0 || strcmp(op, "dec") == 0) && c == 1) {
        Mem16 mem;
        int dst_mem = parse_mem16(as, st->operands[0].text,
                                  (SourceLoc){ as->path, st->line, st->operands[0].column }, &mem);
        if (dst_mem < 0) return 0;
        if (dst_mem) {
            int sz = mem.size_bits;
            if (!sz) {
                kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[0].column },
                           "ambiguous memory operand size in 16-bit mode; hint: use 'byte [value]' or 'word [value]'");
                return 0;
            }
            emit_mem16_prefix(out, &mem);
            kasm_buf_append_u8(out, sz == 8 ? 0xFE : 0xFF);
            emit_mem16_addr(as, st, out, &mem, strcmp(op, "inc") == 0 ? 0 : 1, dry);
            *why = "8086 inc/dec memory";
            return 1;
        }
    }
    if ((strcmp(op, "neg") == 0 || strcmp(op, "not") == 0 ||
         strcmp(op, "mul") == 0 || strcmp(op, "imul") == 0 ||
         strcmp(op, "div") == 0 || strcmp(op, "idiv") == 0) && c == 1 &&
        reg_code(st->operands[0].text, &rd, &db)) {
        int ext = strcmp(op, "not") == 0 ? 2 : strcmp(op, "neg") == 0 ? 3 :
                  strcmp(op, "mul") == 0 ? 4 : strcmp(op, "imul") == 0 ? 5 :
                  strcmp(op, "div") == 0 ? 6 : 7;
        kasm_buf_append_u8(out, db == 8 ? 0xF6 : 0xF7); modrm(out, 3, ext, rd);
        *why = "8086 group unary";
        return 1;
    }
    if ((strcmp(op, "neg") == 0 || strcmp(op, "not") == 0 ||
         strcmp(op, "mul") == 0 || strcmp(op, "imul") == 0 ||
         strcmp(op, "div") == 0 || strcmp(op, "idiv") == 0) && c == 1) {
        Mem16 mem;
        int dst_mem = parse_mem16(as, st->operands[0].text,
                                  (SourceLoc){ as->path, st->line, st->operands[0].column }, &mem);
        if (dst_mem < 0) return 0;
        if (dst_mem) {
            int sz = mem.size_bits;
            if (!sz) {
                kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[0].column },
                           "ambiguous memory operand size in 16-bit mode; hint: use 'byte [value]' or 'word [value]'");
                return 0;
            }
            int ext = strcmp(op, "not") == 0 ? 2 : strcmp(op, "neg") == 0 ? 3 :
                      strcmp(op, "mul") == 0 ? 4 : strcmp(op, "imul") == 0 ? 5 :
                      strcmp(op, "div") == 0 ? 6 : 7;
            emit_mem16_prefix(out, &mem);
            kasm_buf_append_u8(out, sz == 8 ? 0xF6 : 0xF7);
            emit_mem16_addr(as, st, out, &mem, ext, dry);
            *why = "8086 unary memory";
            return 1;
        }
    }
    if ((strcmp(op, "shl") == 0 || strcmp(op, "sal") == 0 || strcmp(op, "shr") == 0 ||
         strcmp(op, "sar") == 0 || strcmp(op, "rol") == 0 || strcmp(op, "ror") == 0 ||
         strcmp(op, "rcl") == 0 || strcmp(op, "rcr") == 0) && c == 2 &&
        reg_code(st->operands[0].text, &rd, &db)) {
        int ext = (strcmp(op, "rol") == 0) ? 0 : (strcmp(op, "ror") == 0) ? 1 :
                  (strcmp(op, "rcl") == 0) ? 2 : (strcmp(op, "rcr") == 0) ? 3 :
                  (strcmp(op, "shl") == 0 || strcmp(op, "sal") == 0) ? 4 :
                  (strcmp(op, "shr") == 0) ? 5 : 7;
        int by_cl = kasm_streq_ci(st->operands[1].text, "cl");
        if (!by_cl && strcmp(st->operands[1].text, "1") != 0) {
            kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[1].column }, "instruction requires 80186 but target CPU is 8086");
            return 0;
        }
        kasm_buf_append_u8(out, (uint8_t)((by_cl ? 0xD2 : 0xD0) + (db == 16 ? 1 : 0)));
        modrm(out, 3, ext, rd);
        *why = "8086 shift/rotate";
        return 1;
    }
    kasm_error(as, (SourceLoc){ as->path, st->line, st->column }, "unknown or unsupported 16-bit instruction '%s'", op);
    return 0;
}

static int estimate16(Assembler *as, Statement *st)
{
    ByteBuf b = { 0 };
    const char *why = "";
    int before = as->errors;
    int ok = emit16(as, st, &b, &why, 1);
    if (ok)
        st->size = (uint32_t)b.len;
    free(b.data);
    if (!ok && as->errors == before)
        kasm_error(as, (SourceLoc){ as->path, st->line, st->column }, "invalid operand combination for '%s' in 16-bit mode", st->op);
    return ok;
}

int kasm_estimate_statement(Assembler *as, Statement *st)
{
    if (st->type != ST_INSTR)
        return 1;
    if (as->bits == KASM_BITS_16)
        return estimate16(as, st);
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
            if (as->tiny && is_i8(imm)) {
                kasm_buf_append_u8(out, 0x6A);
                kasm_buf_append_u8(out, (uint8_t)(int8_t)imm);
                *why = "tiny: selected push imm8";
                return 1;
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
            *why = mem_encoding_why(as, &mem, "mov qword ptr [mem], r64; ModRM/SIB memory operand");
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
            *why = mem_encoding_why(as, &mem, "mov r64, qword ptr [mem]; ModRM/SIB memory operand");
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
        emit_modrm_mem(out, rd, &mem, as->tiny);
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
            append_mem_disp(out, &mem, as->tiny);
        }
        *why = mem.rip_relative ? "RIP-relative LEA" :
               mem_encoding_why(as, &mem, "lea r64, [mem]; ModRM/SIB memory operand");
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
            static char buf[160];
            snprintf(buf, sizeof(buf), "tiny: selected short %s because target distance is %lld bytes",
                     strcmp(op, "jmp") == 0 ? "jump" : "conditional jump",
                     (long long)disp);
            *why = buf;
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
    if (as->bits == KASM_BITS_16) {
        as->sections[SEC_TEXT].vaddr = as->origin_set ? as->origin : 0;
        as->sections[SEC_RODATA].vaddr = as->sections[SEC_TEXT].vaddr + sizes[SEC_TEXT];
        as->sections[SEC_DATA].vaddr = as->sections[SEC_RODATA].vaddr + sizes[SEC_RODATA];
    } else {
        as->sections[SEC_TEXT].vaddr = 0x400000 + kasm_align(header, file_align);
        as->sections[SEC_RODATA].vaddr = kasm_align(as->sections[SEC_TEXT].vaddr + sizes[SEC_TEXT], file_align);
        as->sections[SEC_DATA].vaddr = kasm_align(as->sections[SEC_RODATA].vaddr + sizes[SEC_RODATA], file_align);
    }

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
        } else if (as->bits == KASM_BITS_16) {
            if (!emit16(as, st, b, &why, 0))
                return 0;
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
            while (as->bits != KASM_BITS_16 && !as->tiny && b->len - before < st->size)
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
