#include "explain.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static FILE *explain_out(Assembler *as)
{
    return as->explain_file ? as->explain_file : stdout;
}

static void print_bytes(FILE *out, const uint8_t *bytes, size_t len)
{
    for (size_t i = 0; i < len; i++)
        fprintf(out, "%02X ", bytes[i]);
}

static int has_rex(uint8_t b)
{
    return b >= 0x40 && b <= 0x4f;
}

static uint32_t read_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_u64_le(const uint8_t *p)
{
    uint64_t lo = read_u32_le(p);
    uint64_t hi = read_u32_le(p + 4);
    return lo | (hi << 32);
}

static void print_lower_bytes(FILE *out, const uint8_t *bytes, size_t len)
{
    for (size_t i = 0; i < len; i++)
        fprintf(out, "%02x%s", bytes[i], i + 1 == len ? "" : " ");
}

static void print_upper_word(FILE *out, const char *s)
{
    for (; *s; s++)
        fputc(toupper((unsigned char)*s), out);
}

static int opcode_has_modrm(uint8_t op1, uint8_t op2, int two_byte)
{
    if (two_byte)
        return op2 >= 0x80 && op2 <= 0x8f ? 0 : 0;
    switch (op1) {
    case 0x01: case 0x03: case 0x09: case 0x0b:
    case 0x21: case 0x23: case 0x29: case 0x2b:
    case 0x31: case 0x33: case 0x39: case 0x3b:
    case 0x81: case 0x85: case 0x89: case 0x8b:
    case 0x8d: case 0xc7: case 0xf7: case 0xff:
        return 1;
    default:
        return 0;
    }
}

static int opcode_imm_size(Statement *st, uint8_t op1, uint8_t op2, int two_byte,
                           int has_modrm_byte)
{
    (void)op2;
    (void)two_byte;
    if (op1 == 0x6a)
        return 1;
    if (op1 == 0x68)
        return 4;
    if (op1 >= 0xb8 && op1 <= 0xbf)
        return st->op && strcmp(st->op, "mov") == 0 &&
               st->operand_count > 0 && st->operands[0].text[0] == 'e' ? 4 : 8;
    if (op1 == 0x05 || op1 == 0x0d || op1 == 0x25 ||
        op1 == 0x2d || op1 == 0x35 || op1 == 0x3d)
        return 4;
    if (has_modrm_byte && op1 == 0x83)
        return 1;
    if (has_modrm_byte && (op1 == 0x81 || op1 == 0xc7))
        return 4;
    if (has_modrm_byte && op1 == 0xf7 && st->op && strcmp(st->op, "test") == 0)
        return 4;
    return 0;
}

static const char *friendly_note(Statement *st, const char *why)
{
    if (!st->op)
        return "data bytes emitted as written";
    if (strcmp(st->op, "syscall") == 0)
        return "Linux syscall number is expected in rax; args in rdi,rsi,rdx,r10,r8,r9";
    if (strcmp(st->op, "ret") == 0)
        return "return to the address at the top of the stack";
    if (strcmp(st->op, "lea") == 0)
        return "compute an address without loading from memory";
    if (strcmp(st->op, "mov") == 0 && st->operand_count == 2) {
        if (strstr(why, "imm"))
            return "move an immediate value into the destination register";
        if (strstr(st->operands[0].text, "["))
            return "store a register value into memory";
        if (strstr(st->operands[1].text, "["))
            return "load a value from memory into a register";
        return "copy the source operand into the destination";
    }
    if (strcmp(st->op, "cmp") == 0)
        return "compare operands by setting flags without storing the subtraction result";
    if (strcmp(st->op, "test") == 0)
        return "bitwise-test operands by setting flags without storing a result";
    if (st->op[0] == 'j')
        return strstr(why, "tiny: selected short") ?
            "tiny mode selected a signed 8-bit branch displacement" :
            "near branch using a signed 32-bit displacement";
    if (strcmp(st->op, "call") == 0)
        return "near call using a signed 32-bit displacement";
    if (strcmp(st->op, "push") == 0 || strcmp(st->op, "pop") == 0)
        return "stack operation; rsp is updated by the CPU";
    return "encoding selected from KASM's supported instruction subset";
}

static void print_normalized(FILE *out, Statement *st)
{
    if (!st->op) {
        fprintf(out, "<data>");
        return;
    }
    fprintf(out, "%s", st->op);
    for (int i = 0; i < st->operand_count; i++)
        fprintf(out, "%s %s", i == 0 ? "" : ",", st->operands[i].text);
}

static void deluxe_bytes(Assembler *as, Statement *st, const uint8_t *bytes, size_t len,
                         const char *why)
{
    FILE *out = explain_out(as);
    uint64_t vaddr = as->sections[st->section].vaddr + st->offset;
    fprintf(out, "  location: %s:%d:%d\n", as->path ? as->path : "<unknown>",
            st->line, st->column);
    fprintf(out, "  source: %s\n", st->source ? st->source : "");
    fprintf(out, "  normalized: ");
    print_normalized(out, st);
    fprintf(out, "\n");
    fprintf(out, "  offset: 0x%08llx\n", (unsigned long long)st->offset);
    fprintf(out, "  virtual-address: 0x%llx\n", (unsigned long long)vaddr);
    fprintf(out, "  bytes: ");
    print_lower_bytes(out, bytes, len);
    fprintf(out, "\n");
    fprintf(out, "  length: %zu\n", len);
    fprintf(out, "  form: ");
    print_upper_word(out, why);
    fprintf(out, "\n");

    if (st->type != ST_INSTR) {
        fprintf(out, "  detail: data directive\n");
        fprintf(out, "  note: %s\n", friendly_note(st, why));
        return;
    }

    size_t i = 0;
    fprintf(out, "  prefixes:\n");
    if (i < len && has_rex(bytes[i])) {
        uint8_t r = bytes[i++];
        fprintf(out, "    REX: 0x%02x W=%u R=%u X=%u B=%u\n",
                r, (r >> 3) & 1, (r >> 2) & 1, (r >> 1) & 1, r & 1);
    } else {
        fprintf(out, "    none\n");
    }
    fprintf(out, "    operand-size override: none\n");
    fprintf(out, "    address-size override: none\n");
    fprintf(out, "    segment override: none\n");

    if (i >= len) {
        fprintf(out, "  detail: unavailable\n");
        return;
    }

    uint8_t op1 = bytes[i++];
    uint8_t op2 = 0;
    int two_byte = 0;
    fprintf(out, "  opcode:");
    if (op1 == 0x0f && i < len) {
        two_byte = 1;
        op2 = bytes[i++];
        fprintf(out, " %02x %02x\n", op1, op2);
    } else {
        fprintf(out, " %02x\n", op1);
    }

    int has_modrm_byte = opcode_has_modrm(op1, op2, two_byte);
    if (has_modrm_byte && i < len) {
        uint8_t m = bytes[i++];
        unsigned mod = (m >> 6) & 3;
        unsigned reg = (m >> 3) & 7;
        unsigned rm = m & 7;
        int sib_base = -1;
        fprintf(out, "  ModRM: 0x%02x\n", m);
        fprintf(out, "    mod bits: %u\n", mod);
        fprintf(out, "    reg/opcode bits: %u\n", reg);
        fprintf(out, "    r/m bits: %u\n", rm);
        if (mod != 3 && rm == 4 && i < len) {
            uint8_t s = bytes[i++];
            sib_base = s & 7;
            fprintf(out, "  SIB: 0x%02x\n", s);
            fprintf(out, "    scale: %u\n", 1u << ((s >> 6) & 3));
            fprintf(out, "    index: %u\n", (s >> 3) & 7);
            fprintf(out, "    base: %u\n", s & 7);
        } else {
            fprintf(out, "  SIB: none\n");
        }
        int disp_size = 0;
        if (mod == 1)
            disp_size = 1;
        else if (mod == 2 || (mod == 0 && rm == 5) ||
                 (mod == 0 && rm == 4 && sib_base == 5))
            disp_size = 4;
        if (disp_size == 1 && i < len) {
            int8_t disp = (int8_t)bytes[i];
            fprintf(out, "  displacement: size=8 value=%d bytes=", disp);
            print_lower_bytes(out, bytes + i, 1);
            fprintf(out, "\n");
            i += 1;
        } else if (disp_size == 4 && i + 4 <= len) {
            int32_t disp = (int32_t)read_u32_le(bytes + i);
            fprintf(out, "  displacement: size=32 value=%d bytes=", disp);
            print_lower_bytes(out, bytes + i, 4);
            fprintf(out, "\n");
            i += 4;
        } else {
            fprintf(out, "  displacement: none\n");
        }
    } else {
        fprintf(out, "  ModRM: none\n");
        fprintf(out, "  SIB: none\n");
        if ((op1 == 0xeb || (op1 >= 0x70 && op1 <= 0x7f)) && i < len) {
            int8_t disp = (int8_t)bytes[i];
            fprintf(out, "  displacement: size=8 value=%d bytes=", disp);
            print_lower_bytes(out, bytes + i, 1);
            fprintf(out, "\n");
            i += 1;
        } else if ((op1 == 0xe8 || op1 == 0xe9 || (two_byte && op2 >= 0x80 && op2 <= 0x8f)) &&
            i + 4 <= len) {
            int32_t disp = (int32_t)read_u32_le(bytes + i);
            fprintf(out, "  displacement: size=32 value=%d bytes=", disp);
            print_lower_bytes(out, bytes + i, 4);
            fprintf(out, "\n");
            i += 4;
        } else {
            fprintf(out, "  displacement: none\n");
        }
    }

    int imm_size = opcode_imm_size(st, op1, op2, two_byte, has_modrm_byte);
    if (imm_size && i + (size_t)imm_size <= len) {
        fprintf(out, "  immediate: size=%d value=", imm_size * 8);
        if (imm_size == 8)
            fprintf(out, "%lld", (long long)read_u64_le(bytes + i));
        else
            fprintf(out, "%d", (int32_t)read_u32_le(bytes + i));
        fprintf(out, " bytes=");
        print_lower_bytes(out, bytes + i, (size_t)imm_size);
        fprintf(out, " little-endian\n");
    } else {
        fprintf(out, "  immediate: none\n");
    }

    int saw_reloc = 0;
    for (size_t r = 0; r < as->relocs.len; r++) {
        Reloc *rel = &as->relocs.items[r];
        if (rel->section == st->section &&
            rel->offset >= st->offset && rel->offset < st->offset + len) {
            if (!saw_reloc)
                fprintf(out, "  relocation:\n");
            saw_reloc = 1;
            fprintf(out, "    offset=0x%08llx type=%s symbol=%s addend=%lld\n",
                    (unsigned long long)rel->offset,
                    rel->kind == RELOC_PC32 ? "R_X86_64_PC32" : "R_X86_64_64",
                    rel->symbol, (long long)rel->addend);
        }
    }
    if (!saw_reloc)
        fprintf(out, "  relocation: none\n");
    fprintf(out, "  note: %s\n", friendly_note(st, why));
}

static void verbose_bytes(Assembler *as, Statement *st, const uint8_t *bytes, size_t len)
{
    FILE *out = explain_out(as);
    size_t i = 0;
    if (len == 0)
        return;
    if (has_rex(bytes[i])) {
        uint8_t r = bytes[i++];
        fprintf(out, "    REX: %02X\n", r);
        fprintf(out, "      W=%u%s\n", (r >> 3) & 1, (r & 8) ? " 64-bit operand" : "");
        fprintf(out, "      R=%u\n", (r >> 2) & 1);
        fprintf(out, "      X=%u\n", (r >> 1) & 1);
        fprintf(out, "      B=%u\n", r & 1);
    }
    if (i < len) {
        fprintf(out, "    opcode:");
        if (bytes[i] == 0x0f && i + 1 < len) {
            fprintf(out, " %02X %02X\n", bytes[i], bytes[i + 1]);
            i += 2;
        } else {
            fprintf(out, " %02X\n", bytes[i++]);
        }
    }
    if (i < len) {
        uint8_t m = bytes[i];
        int likely_modrm = 0;
        if (len - i >= 1 && !(m == 0x90 || m == 0xc3))
            likely_modrm = 1;
        if (likely_modrm && st->operand_count > 0 &&
            !(bytes[0] == 0xe8 || bytes[0] == 0xe9 || (bytes[0] == 0x68))) {
            i++;
            fprintf(out, "    ModRM: %02X\n", m);
            fprintf(out, "      mod=%u\n", (m >> 6) & 3);
            fprintf(out, "      reg/opcode=%u\n", (m >> 3) & 7);
            fprintf(out, "      rm=%u%s\n", m & 7, (m & 7) == 4 && ((m >> 6) & 3) != 3 ? " uses SIB" : "");
            if ((m & 7) == 4 && ((m >> 6) & 3) != 3 && i < len) {
                uint8_t s = bytes[i++];
                fprintf(out, "    SIB: %02X\n", s);
                fprintf(out, "      scale=%u\n", 1u << ((s >> 6) & 3));
                fprintf(out, "      index=%u\n", (s >> 3) & 7);
                fprintf(out, "      base=%u\n", s & 7);
            }
        }
    }
    if (len >= 4) {
        fprintf(out, "    trailing bytes:");
        size_t start = len > 4 ? len - 4 : 0;
        for (size_t j = start; j < len; j++)
            fprintf(out, " %02X", bytes[j]);
        fprintf(out, "\n");
    }
    for (size_t r = 0; r < as->relocs.len; r++) {
        Reloc *rel = &as->relocs.items[r];
        if (rel->section == st->section &&
            rel->offset >= st->offset && rel->offset < st->offset + len) {
            fprintf(out, "    relocation:\n");
            fprintf(out, "      section: %s\n", rel->section == SEC_TEXT ? ".rela.text" :
                    rel->section == SEC_DATA ? ".rela.data" : ".rela.rodata");
            fprintf(out, "      offset: 0x%08llX\n", (unsigned long long)rel->offset);
            fprintf(out, "      type: %s\n", rel->kind == RELOC_PC32 ? "R_X86_64_PC32" : "R_X86_64_64");
            fprintf(out, "      symbol: %s\n", rel->symbol);
            fprintf(out, "      addend: %lld\n", (long long)rel->addend);
        }
    }
    if (st->op && st->operand_count > 0) {
        for (int o = 0; o < st->operand_count; o++) {
            if (st->operands[o].text && strstr(st->operands[o].text, "[rel ")) {
                fprintf(out, "    symbol/displacement: RIP-relative operand %s\n", st->operands[o].text);
            }
        }
    }
}

void kasm_explain(Assembler *as, Statement *st, const uint8_t *bytes, size_t len,
                  const char *why)
{
    FILE *out = explain_out(as);
    if (as->explain) {
        fprintf(out, "%08llX  ", (unsigned long long)st->offset);
        print_bytes(out, bytes, len);
        for (size_t i = len; i < 10; i++)
            fprintf(out, "   ");
        fprintf(out, "  %s\n", st->source ? st->source : "");
        if (st->type == ST_DATA)
            fprintf(out, "    emits: %zu bytes\n", len);
        fprintf(out, "    encodes: %s\n", why);
        if (as->explain_mode == 2)
            verbose_bytes(as, st, bytes, len);
        else if (as->explain_mode == 3)
            deluxe_bytes(as, st, bytes, len, why);
        fprintf(out, "\n");
    }

    if (as->list_file) {
        fprintf(as->list_file, "%08llX  ", (unsigned long long)st->offset);
        print_bytes(as->list_file, bytes, len);
        fprintf(as->list_file, "  %s\n", st->source ? st->source : "");
    }
}
