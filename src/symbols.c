#include "diagnostics.h"
#include "symbols.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

Symbol *kasm_symbol_find(SymbolTable *table, const char *name)
{
    for (size_t i = 0; i < table->len; i++)
        if (strcmp(table->items[i].name, name) == 0)
            return &table->items[i];
    return NULL;
}

int kasm_symbol_define(Assembler *as, const char *name, SectionId section,
                       uint64_t offset, int is_const, int64_t value, int line, int column)
{
    Symbol *sym = kasm_symbol_find(&as->symbols, name);
    if (sym) {
        if (sym->is_extern) {
            kasm_error(as, (SourceLoc){ as->path, line, column }, "extern symbol '%s' is also defined", name);
            return 0;
        }
        if (sym->defined) {
            kasm_error(as, (SourceLoc){ as->path, line, column }, "duplicate symbol '%s'; hint: labels and defines must be unique within one assembly unit", name);
            return 0;
        }
    } else {
        if (as->symbols.len == as->symbols.cap) {
            as->symbols.cap = as->symbols.cap ? as->symbols.cap * 2 : 64;
            as->symbols.items = kasm_xrealloc(as->symbols.items, as->symbols.cap * sizeof(Symbol));
        }
        sym = &as->symbols.items[as->symbols.len++];
        memset(sym, 0, sizeof(*sym));
        sym->name = kasm_xstrdup(name);
    }
    sym->section = section;
    sym->offset = offset;
    sym->is_const = is_const;
    sym->value = value;
    sym->line = line;
    sym->column = column;
    sym->defined = 1;
    return 1;
}

static Symbol *ensure_symbol(Assembler *as, const char *name)
{
    Symbol *sym = kasm_symbol_find(&as->symbols, name);
    if (sym)
        return sym;
    if (as->symbols.len == as->symbols.cap) {
        as->symbols.cap = as->symbols.cap ? as->symbols.cap * 2 : 64;
        as->symbols.items = kasm_xrealloc(as->symbols.items, as->symbols.cap * sizeof(Symbol));
    }
    sym = &as->symbols.items[as->symbols.len++];
    memset(sym, 0, sizeof(*sym));
    sym->name = kasm_xstrdup(name);
    sym->section = SEC_NONE;
    return sym;
}

int kasm_symbol_global(Assembler *as, const char *name, int line, int column)
{
    Symbol *sym = ensure_symbol(as, name);
    if (sym->is_extern) {
        kasm_error(as, (SourceLoc){ as->path, line, column }, "symbol '%s' cannot be both global and extern", name);
        return 0;
    }
    if (sym->is_global) {
        kasm_error(as, (SourceLoc){ as->path, line, column }, "duplicate global declaration for '%s'", name);
        return 0;
    }
    sym->is_global = 1;
    if (!sym->line) {
        sym->line = line;
        sym->column = column;
    }
    return 1;
}

int kasm_symbol_extern(Assembler *as, const char *name, int line, int column)
{
    Symbol *sym = ensure_symbol(as, name);
    if (sym->defined) {
        kasm_error(as, (SourceLoc){ as->path, line, column }, "extern symbol '%s' is also defined", name);
        return 0;
    }
    if (sym->is_global || sym->is_extern) {
        kasm_error(as, (SourceLoc){ as->path, line, column }, "duplicate extern/global declaration for '%s'", name);
        return 0;
    }
    sym->is_extern = 1;
    sym->is_global = 1;
    sym->line = line;
    sym->column = column;
    return 1;
}

int kasm_validate_symbols(Assembler *as, int object_mode)
{
    for (size_t i = 0; i < as->symbols.len; i++) {
        Symbol *sym = &as->symbols.items[i];
        if (sym->is_global && !sym->is_extern && !sym->defined) {
            kasm_error(as, (SourceLoc){ as->path, sym->line, sym->column },
                       "global symbol '%s' declared but never defined", sym->name);
            return 0;
        }
        if (sym->is_extern && !object_mode) {
            kasm_error(as, (SourceLoc){ as->path, sym->line, sym->column },
                       "external symbol '%s' cannot be resolved in direct executable output; use -f elf64-obj and link with ld",
                       sym->name);
            return 0;
        }
    }
    return 1;
}

static int is_ident(const char *s)
{
    if (!(*s == '_' || *s == '.' || isalpha((unsigned char)*s)))
        return 0;
    for (s++; *s; s++)
        if (!(*s == '_' || *s == '.' || isalnum((unsigned char)*s)))
            return 0;
    return 1;
}

static int builtin_constant(const char *s, int64_t *out)
{
    if (strcmp(s, "stdin") == 0 || strcmp(s, "STDIN") == 0) {
        *out = 0;
        return 1;
    }
    if (strcmp(s, "stdout") == 0 || strcmp(s, "STDOUT") == 0) {
        *out = 1;
        return 1;
    }
    if (strcmp(s, "stderr") == 0 || strcmp(s, "STDERR") == 0) {
        *out = 2;
        return 1;
    }
    if (strcmp(s, "AT_FDCWD") == 0) {
        *out = -100;
        return 1;
    }
    if (strcmp(s, "O_RDONLY") == 0) {
        *out = 0;
        return 1;
    }
    if (strcmp(s, "O_WRONLY") == 0) {
        *out = 1;
        return 1;
    }
    if (strcmp(s, "O_RDWR") == 0) {
        *out = 2;
        return 1;
    }
    if (strcmp(s, "O_CREAT") == 0) {
        *out = 64;
        return 1;
    }
    if (strcmp(s, "O_TRUNC") == 0) {
        *out = 512;
        return 1;
    }
    if (strcmp(s, "PROT_READ") == 0) {
        *out = 1;
        return 1;
    }
    if (strcmp(s, "PROT_WRITE") == 0) {
        *out = 2;
        return 1;
    }
    if (strcmp(s, "PROT_EXEC") == 0) {
        *out = 4;
        return 1;
    }
    if (strcmp(s, "MAP_PRIVATE") == 0) {
        *out = 2;
        return 1;
    }
    if (strcmp(s, "MAP_ANONYMOUS") == 0) {
        *out = 32;
        return 1;
    }
    return 0;
}

static int eval_atom(Assembler *as, const char *atom, SectionId current_section,
                     uint64_t current_offset, int want_absolute, int64_t *out,
                     SourceLoc loc)
{
    char *tmp = kasm_xstrdup(atom);
    char *s = kasm_trim(tmp);
    int64_t v;
    if (strcmp(s, "$$") == 0) {
        uint64_t base = current_section != SEC_NONE ? as->sections[current_section].vaddr : 0;
        *out = (int64_t)base;
        free(tmp);
        return 1;
    }
    if (strcmp(s, "$") == 0) {
        uint64_t base = (want_absolute || as->bits == KASM_BITS_16) && current_section != SEC_NONE ?
                            as->sections[current_section].vaddr : 0;
        *out = (int64_t)(base + current_offset);
        free(tmp);
        return 1;
    }
    if (isdigit((unsigned char)*s) ||
        ((*s == '-' || *s == '+') && isdigit((unsigned char)s[1]))) {
        char *end = NULL;
        errno = 0;
        (void)strtoll(s, &end, 0);
        if (errno == ERANGE) {
            kasm_error(as, loc, "integer literal out of range '%s'", s);
            free(tmp);
            return 0;
        }
    }
    if (kasm_parse_int(s, &v)) {
        *out = v;
        free(tmp);
        return 1;
    }
    if (builtin_constant(s, out)) {
    } else if (is_ident(s)) {
        Symbol *sym = kasm_symbol_find(&as->symbols, s);
        if (!sym || !sym->defined) {
            kasm_error(as, loc, "undefined symbol '%s'; hint: define the label or assemble as an object with extern/global when linking later", s);
            free(tmp);
            return 0;
        }
        if (sym->is_const) {
            *out = sym->value;
        } else {
            uint64_t base = want_absolute ? as->sections[sym->section].vaddr : 0;
            *out = (int64_t)(base + sym->offset);
        }
    } else {
        kasm_error(as, loc, "invalid expression '%s'", atom);
        free(tmp);
        return 0;
    }
    free(tmp);
    return 1;
}

int kasm_eval_expr(Assembler *as, const char *expr, SectionId current_section,
                   uint64_t current_offset, int want_absolute, int64_t *out,
                   SourceLoc loc)
{
    char *tmp = kasm_xstrdup(expr);
    char *s = kasm_trim(tmp);
    while (*s == '(') {
        size_t n = strlen(s);
        int depth = 0, wraps = n > 1 && s[n - 1] == ')';
        for (size_t i = 0; i < n && wraps; i++) {
            if (s[i] == '(') depth++;
            else if (s[i] == ')') depth--;
            if (depth == 0 && i + 1 < n)
                wraps = 0;
        }
        if (!wraps)
            break;
        s[n - 1] = 0;
        s = kasm_trim(s + 1);
    }
    int in_string = 0;
    char *op = NULL;
    int depth = 0;
    for (char *p = s; *p; p++) {
        if (*p == '"')
            in_string = !in_string;
        if (!in_string && *p == '(')
            depth++;
        else if (!in_string && *p == ')' && depth)
            depth--;
        if (!in_string && depth == 0 && p != s && (*p == '+' || *p == '-')) {
            op = p;
            break;
        }
    }
    if (op) {
        char kind = *op;
        *op = 0;
        int64_t a, b;
        int ok = kasm_eval_expr(as, s, current_section, current_offset, want_absolute, &a, loc) &&
                 kasm_eval_expr(as, op + 1, current_section, current_offset, want_absolute, &b, loc);
        if (ok)
            *out = kind == '+' ? a + b : a - b;
        free(tmp);
        return ok;
    }
    int ok = eval_atom(as, s, current_section, current_offset, want_absolute, out, loc);
    free(tmp);
    return ok;
}
