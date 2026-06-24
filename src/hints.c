#include "hints.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int streq_ci(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static int is_zero_expr(const char *s)
{
    int64_t v = 0;
    return kasm_parse_int(s, &v) && v == 0;
}

static int is_reg_name(const char *s)
{
    static const char *regs[] = {
        "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
        "eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp",
        "ax", "bx", "cx", "dx", "si", "di", "bp", "sp",
        "al", "bl", "cl", "dl"
    };
    for (size_t i = 0; i < sizeof(regs) / sizeof(regs[0]); i++)
        if (streq_ci(s, regs[i]))
            return 1;
    return 0;
}

static const char *full_reg_for_partial(const char *s)
{
    if (streq_ci(s, "ax") || streq_ci(s, "al")) return "rax";
    if (streq_ci(s, "bx") || streq_ci(s, "bl")) return "rbx";
    if (streq_ci(s, "cx") || streq_ci(s, "cl")) return "rcx";
    if (streq_ci(s, "dx") || streq_ci(s, "dl")) return "rdx";
    if (streq_ci(s, "si")) return "rsi";
    if (streq_ci(s, "di")) return "rdi";
    if (streq_ci(s, "bp")) return "rbp";
    if (streq_ci(s, "sp")) return "rsp";
    return NULL;
}

static int operand_mentions_full(const char *operand, const char *full)
{
    size_t n = strlen(full);
    for (const char *p = operand; *p; p++) {
        if ((p == operand || !isalnum((unsigned char)p[-1])) &&
            !isalnum((unsigned char)p[n])) {
            int ok = 1;
            for (size_t i = 0; i < n; i++) {
                if (tolower((unsigned char)p[i]) != tolower((unsigned char)full[i])) {
                    ok = 0;
                    break;
                }
            }
            if (ok)
                return 1;
        }
    }
    return 0;
}

static int operand_writes_full(const char *operand)
{
    static const char *fulls[] = {
        "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
        "eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp"
    };
    for (size_t i = 0; i < sizeof(fulls) / sizeof(fulls[0]); i++)
        if (streq_ci(operand, fulls[i]))
            return 1;
    return 0;
}

static void emit_hint_path(Assembler *as, Statement *st, const char *severity,
                           const char *id, const char *msg)
{
    fprintf(stderr, "%s: %s:%d:%d: [%s] %s\n", severity,
            as->path ? as->path : "<unknown>", st->line, st->column, id, msg);
}

static int branch_like(const char *op)
{
    return strcmp(op, "jmp") == 0 || strcmp(op, "je") == 0 ||
           strcmp(op, "jz") == 0 || strcmp(op, "jne") == 0 ||
           strcmp(op, "jnz") == 0 || strcmp(op, "jg") == 0 ||
           strcmp(op, "jge") == 0 || strcmp(op, "jl") == 0 ||
           strcmp(op, "jle") == 0 || strcmp(op, "ja") == 0 ||
           strcmp(op, "jae") == 0 || strcmp(op, "jb") == 0 ||
           strcmp(op, "jbe") == 0;
}

static int stack_delta_for(Statement *st, int *delta)
{
    int64_t imm = 0;
    if (!st->op)
        return 0;
    if (strcmp(st->op, "push") == 0 && st->operand_count == 1) {
        *delta = -8;
        return 1;
    }
    if (strcmp(st->op, "pop") == 0 && st->operand_count == 1) {
        *delta = 8;
        return 1;
    }
    if ((strcmp(st->op, "sub") == 0 || strcmp(st->op, "add") == 0) &&
        st->operand_count == 2 && streq_ci(st->operands[0].text, "rsp") &&
        kasm_parse_int(st->operands[1].text, &imm)) {
        *delta = (int)(strcmp(st->op, "sub") == 0 ? -imm : imm);
        return 1;
    }
    return 0;
}

void kasm_run_hints(Assembler *as)
{
    if (!as->hints)
        return;

    const char *last_partial_full = NULL;
    int stack_delta = 0;
    int stack_known = 1;

    for (size_t i = 0; i < as->program.len; i++) {
        Statement *st = &as->program.items[i];
        if (st->type == ST_LABEL) {
            last_partial_full = NULL;
            stack_known = 1;
            stack_delta = 0;
            continue;
        }
        if (st->type != ST_INSTR || !st->op)
            continue;

        if (as->hint_perf && strcmp(st->op, "mov") == 0 && st->operand_count == 2 &&
            is_reg_name(st->operands[0].text) && is_zero_expr(st->operands[1].text)) {
            emit_hint_path(as, st, "hint", "KASM-HINT-ZEROING-001",
                           "`mov reg, 0` could often be `xor reg, reg` or `sub reg, reg` for shorter zeroing, but those forms change flags.");
        }

        if (as->hint_abi && strcmp(st->op, "syscall") == 0) {
            emit_hint_path(as, st, "note", "KASM-HINT-SYSCALL-001",
                           "`syscall` uses Linux ABI: rax=syscall, rdi/rsi/rdx/r10/r8/r9=args; rcx and r11 are clobbered.");
        }

        if (as->hint_perf && last_partial_full) {
            for (int opi = 0; opi < st->operand_count; opi++) {
                if (operand_mentions_full(st->operands[opi].text, last_partial_full)) {
                    emit_hint_path(as, st, "warning", "KASM-HINT-PARTIAL-001",
                                   "possible partial-register dependency: an 8/16-bit write is followed by a full 64-bit register use.");
                    last_partial_full = NULL;
                    break;
                }
            }
        }

        if (as->hint_perf && strcmp(st->op, "mov") == 0 && st->operand_count >= 1) {
            const char *full = full_reg_for_partial(st->operands[0].text);
            last_partial_full = full;
        } else if (st->operand_count >= 1 && operand_writes_full(st->operands[0].text)) {
            last_partial_full = NULL;
        }

        if (as->hint_perf && strcmp(st->op, "call") == 0) {
            if (stack_known && ((stack_delta % 16 + 16) % 16) != 0)
                emit_hint_path(as, st, "warning", "KASM-HINT-STACK-001",
                               "possible stack misalignment before `call`; SysV ABI callees expect 16-byte alignment at call boundaries.");
        }

        if (as->hint_size && !as->tiny && branch_like(st->op))
            emit_hint_path(as, st, "hint", "KASM-HINT-SIZE-001",
                           "branch may use a shorter encoding in `--tiny` mode if the target is in rel8 range.");

        if (as->hint_size && !as->tiny && (strcmp(st->op, "cmp") == 0 ||
            strcmp(st->op, "add") == 0 || strcmp(st->op, "sub") == 0 ||
            strcmp(st->op, "and") == 0 || strcmp(st->op, "or") == 0 ||
            strcmp(st->op, "xor") == 0) && st->operand_count == 2) {
            int64_t imm = 0;
            if (kasm_parse_int(st->operands[1].text, &imm) && imm >= -128 && imm <= 127)
                emit_hint_path(as, st, "hint", "KASM-HINT-SIZE-002",
                               "this immediate may use a shorter sign-extended imm8 encoding in `--tiny` mode.");
        }

        if (as->hint_perf) {
            for (int opi = 0; opi < st->operand_count; opi++) {
                const char *op = st->operands[opi].text;
                if (strchr(op, '[') && strchr(op, '*'))
                    emit_hint_path(as, st, "note", "KASM-HINT-ADDR-001",
                                   "scaled-index memory addressing is expressive but can be more complex for address-generation units than base+disp.");
            }
        }

        int delta = 0;
        if (stack_delta_for(st, &delta))
            stack_delta += delta;
        else if (strcmp(st->op, "mov") == 0 && st->operand_count > 0 && streq_ci(st->operands[0].text, "rsp"))
            stack_known = 0;
    }
}
