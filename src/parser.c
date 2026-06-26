#include "diagnostics.h"
#include "encoder.h"
#include "lexer.h"
#include "parser.h"
#include "symbols.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *text;
    char *path;
    int line;
} ExpandedLine;

typedef struct {
    ExpandedLine *items;
    size_t len;
    size_t cap;
} ExpandedLines;

typedef struct {
    char *name;
    char **params;
    int param_count;
    ExpandedLines body;
} MacroDef;

typedef struct {
    MacroDef *items;
    size_t len;
    size_t cap;
    char **stack;
    size_t stack_len;
    size_t stack_cap;
    char **included;
    size_t included_len;
    size_t included_cap;
    int unique_id;
} Preproc;

static int valid_ident(const char *s);
static char *dup_range(const char *a, const char *b);
static int first_word(char *s, char **word, char **rest);

static void expanded_add(ExpandedLines *out, const char *text, const char *path, int line)
{
    if (out->len == out->cap) {
        out->cap = out->cap ? out->cap * 2 : 128;
        out->items = kasm_xrealloc(out->items, out->cap * sizeof(ExpandedLine));
    }
    out->items[out->len].text = kasm_xstrdup(text);
    out->items[out->len].path = kasm_xstrdup(path);
    out->items[out->len].line = line;
    out->len++;
}

static void expanded_free(ExpandedLines *out)
{
    for (size_t i = 0; i < out->len; i++) {
        free(out->items[i].text);
        free(out->items[i].path);
    }
    free(out->items);
}

static int known_word(const char *s)
{
    const char *words[] = {
        "entry","global","extern","section","db","dw","dd","dq","times","syscall",
        "struct","sizeof","offsetof","align","resb","resw","resd","resq",
        "mov","lea","xor","cmp","add","sub","and","or","test","inc","dec","neg","not",
        "push","pop","jmp","call","ret","je","jz","jne","jnz","jg","jge","jl","jle",
        "ja","jae","jb","jbe","include","define","macro","end"
    };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++)
        if (kasm_streq_ci(s, words[i]))
            return 1;
    return 0;
}

static StructField *struct_field_find(StructDef *s, const char *name)
{
    for (size_t i = 0; i < s->field_count; i++)
        if (strcmp(s->fields[i].name, name) == 0)
            return &s->fields[i];
    return NULL;
}

static int is_power_two(int64_t v)
{
    return v > 0 && (v & (v - 1)) == 0;
}

static uint64_t align_u64(uint64_t v, uint64_t a)
{
    return (v + a - 1) & ~(a - 1);
}

static int parse_field_type(char *rest, FieldType *type, uint64_t *size)
{
    char *word = NULL, *tail = NULL;
    if (!first_word(rest, &word, &tail))
        return 0;
    kasm_lower_ascii(word);
    int ok = 1;
    if (strcmp(word, "byte") == 0) {
        *type = FIELD_BYTE; *size = 1;
    } else if (strcmp(word, "word") == 0) {
        *type = FIELD_WORD; *size = 2;
    } else if (strcmp(word, "dword") == 0) {
        *type = FIELD_DWORD; *size = 4;
    } else if (strcmp(word, "qword") == 0) {
        *type = FIELD_QWORD; *size = 8;
    } else if (strcmp(word, "bytes") == 0) {
        int64_t n = 0;
        if (!kasm_parse_int(tail, &n) || n < 0)
            ok = 0;
        *type = FIELD_BYTES; *size = (uint64_t)n;
    } else {
        ok = 0;
    }
    free(word);
    return ok;
}

static int add_struct_field(Assembler *as, StructDef *s, const char *name,
                            FieldType type, uint64_t size, const char *path, int line)
{
    if (!valid_ident(name)) {
        kasm_error(as, (SourceLoc){ path, line, 1 }, "invalid field name '%s'", name);
        return 0;
    }
    if (struct_field_find(s, name)) {
        kasm_error(as, (SourceLoc){ path, line, 1 }, "duplicate field name '%s'", name);
        return 0;
    }
    if (s->field_count == s->field_cap) {
        s->field_cap = s->field_cap ? s->field_cap * 2 : 8;
        s->fields = kasm_xrealloc(s->fields, s->field_cap * sizeof(StructField));
    }
    StructField *f = &s->fields[s->field_count++];
    f->name = kasm_xstrdup(name);
    f->type = type;
    f->offset = s->size;
    f->size = size;
    s->size += size;
    return 1;
}

static int add_struct_def(Assembler *as, StructDef *s, const char *path, int line)
{
    if (kasm_struct_find(as, s->name)) {
        kasm_error(as, (SourceLoc){ path, line, 1 }, "duplicate struct name '%s'", s->name);
        return 0;
    }
    if (as->structs.len == as->structs.cap) {
        as->structs.cap = as->structs.cap ? as->structs.cap * 2 : 16;
        as->structs.items = kasm_xrealloc(as->structs.items, as->structs.cap * sizeof(StructDef));
    }
    as->structs.items[as->structs.len++] = *s;
    memset(s, 0, sizeof(*s));
    return 1;
}

static char *replace_layout_exprs(Assembler *as, const char *line, const char *path, int line_no)
{
    ByteBuf b = { 0 };
    for (const char *p = line; *p; ) {
        if (strncmp(p, "sizeof(", 7) == 0) {
            const char *end = strchr(p + 7, ')');
            if (!end) {
                kasm_error(as, (SourceLoc){ path, line_no, 1 }, "invalid sizeof argument");
                break;
            }
            char *name = dup_range(p + 7, end);
            StructDef *s = kasm_struct_find(as, name);
            if (!s)
                kasm_error(as, (SourceLoc){ path, line_no, 1 }, "invalid sizeof argument '%s'", name);
            char num[64];
            snprintf(num, sizeof(num), "%llu", (unsigned long long)(s ? s->size : 0));
            kasm_buf_append(&b, num, strlen(num));
            free(name);
            p = end + 1;
        } else if (strncmp(p, "offsetof(", 9) == 0) {
            const char *end = strchr(p + 9, ')');
            if (!end) {
                kasm_error(as, (SourceLoc){ path, line_no, 1 }, "invalid offsetof argument");
                break;
            }
            char *args = dup_range(p + 9, end);
            char *comma = strchr(args, ',');
            if (!comma) {
                kasm_error(as, (SourceLoc){ path, line_no, 1 }, "invalid offsetof argument");
                free(args);
                p = end + 1;
                continue;
            }
            *comma = 0;
            char *sn = kasm_trim(args);
            char *fn = kasm_trim(comma + 1);
            StructDef *s = kasm_struct_find(as, sn);
            StructField *f = s ? struct_field_find(s, fn) : NULL;
            if (!s || !f)
                kasm_error(as, (SourceLoc){ path, line_no, 1 }, "invalid offsetof argument '%s, %s'", sn, fn);
            char num[64];
            snprintf(num, sizeof(num), "%llu", (unsigned long long)(f ? f->offset : 0));
            kasm_buf_append(&b, num, strlen(num));
            free(args);
            p = end + 1;
        } else {
            kasm_buf_append_u8(&b, (uint8_t)*p++);
        }
    }
    kasm_buf_append_u8(&b, 0);
    return (char *)b.data;
}

static MacroDef *macro_find(Preproc *pp, const char *name)
{
    for (size_t i = 0; i < pp->len; i++)
        if (strcmp(pp->items[i].name, name) == 0)
            return &pp->items[i];
    return NULL;
}

static int split_csv(char *s, char **items, int max)
{
    int count = 0;
    char *start = s;
    int in_string = 0, bracket = 0;
    for (char *p = s; ; p++) {
        if (*p == '"')
            in_string = !in_string;
        if (!in_string && *p == '[')
            bracket++;
        if (!in_string && *p == ']' && bracket)
            bracket--;
        if (*p == 0 || (!in_string && !bracket && *p == ',')) {
            if (count < max)
                items[count++] = dup_range(start, p);
            if (*p == 0)
                break;
            start = p + 1;
        }
    }
    return count;
}

static char *path_dir(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    if (!slash || (bslash && bslash > slash))
        slash = bslash;
    if (!slash)
        return kasm_xstrdup(".");
    return dup_range(path, slash);
}

static char *join_path(const char *a, const char *b)
{
    size_t alen = strlen(a), blen = strlen(b);
    int sep = alen && a[alen - 1] != '/' && a[alen - 1] != '\\';
    char *out = kasm_xrealloc(NULL, alen + blen + (sep ? 2 : 1));
    snprintf(out, alen + blen + (sep ? 2 : 1), "%s%s%s", a, sep ? "/" : "", b);
    return out;
}

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

static char *resolve_include(Assembler *as, const char *including, const char *inc)
{
    char *dir = path_dir(including);
    char *candidate = join_path(dir, inc);
    free(dir);
    if (file_exists(candidate))
        return candidate;
    free(candidate);
    for (size_t i = 0; i < as->include_path_count; i++) {
        candidate = join_path(as->include_paths[i], inc);
        if (file_exists(candidate))
            return candidate;
        free(candidate);
    }
    return NULL;
}

static int stack_contains(Preproc *pp, const char *path)
{
    for (size_t i = 0; i < pp->stack_len; i++)
        if (strcmp(pp->stack[i], path) == 0)
            return 1;
    return 0;
}

static int included_contains(Preproc *pp, const char *path)
{
    for (size_t i = 0; i < pp->included_len; i++)
        if (strcmp(pp->included[i], path) == 0)
            return 1;
    return 0;
}

static void included_add(Preproc *pp, const char *path)
{
    if (pp->included_len == pp->included_cap) {
        pp->included_cap = pp->included_cap ? pp->included_cap * 2 : 16;
        pp->included = kasm_xrealloc(pp->included, pp->included_cap * sizeof(char *));
    }
    pp->included[pp->included_len++] = kasm_xstrdup(path);
}

static void stack_push(Preproc *pp, const char *path)
{
    if (pp->stack_len == pp->stack_cap) {
        pp->stack_cap = pp->stack_cap ? pp->stack_cap * 2 : 16;
        pp->stack = kasm_xrealloc(pp->stack, pp->stack_cap * sizeof(char *));
    }
    pp->stack[pp->stack_len++] = kasm_xstrdup(path);
}

static void stack_pop(Preproc *pp)
{
    if (pp->stack_len)
        free(pp->stack[--pp->stack_len]);
}

static char *replace_token(const char *line, const char *needle, const char *value)
{
    ByteBuf b = { 0 };
    size_t n = strlen(needle);
    for (const char *p = line; *p; ) {
        int ident_before = p > line && (isalnum((unsigned char)p[-1]) || p[-1] == '_' || p[-1] == '%');
        int match = strncmp(p, needle, n) == 0 &&
                    !(isalnum((unsigned char)p[n]) || p[n] == '_') && !ident_before;
        if (match) {
            kasm_buf_append(&b, value, strlen(value));
            p += n;
        } else {
            kasm_buf_append_u8(&b, (uint8_t)*p++);
        }
    }
    kasm_buf_append_u8(&b, 0);
    return (char *)b.data;
}

static char *replace_macro_locals(const char *line, const char *prefix)
{
    ByteBuf b = { 0 };
    for (const char *p = line; *p; ) {
        if (p[0] == '%' && p[1] == '%') {
            kasm_buf_append(&b, prefix, strlen(prefix));
            p += 2;
        } else {
            kasm_buf_append_u8(&b, (uint8_t)*p++);
        }
    }
    kasm_buf_append_u8(&b, 0);
    return (char *)b.data;
}

static int expand_file(Assembler *as, Preproc *pp, const char *path, ExpandedLines *out, int depth);

static int expand_macro_call(Assembler *as, Preproc *pp, MacroDef *m, char *rest,
                             const char *path, int line, ExpandedLines *out, int depth)
{
    if (depth > 64) {
        kasm_error(as, (SourceLoc){ path, line, 1 }, "macro expansion depth exceeded");
        return 0;
    }
    char *args[32] = { 0 };
    int argc = *kasm_trim(rest) ? split_csv(rest, args, 32) : 0;
    if (argc != m->param_count) {
        kasm_error(as, (SourceLoc){ path, line, 1 }, "macro argument count mismatch for '%s'", m->name);
        fprintf(stderr, "  expanded from macro call at %s:%d\n", path, line);
        fprintf(stderr, "  hint: check the macro definition parameter count\n");
        for (int i = 0; i < argc; i++) free(args[i]);
        return 0;
    }
    char local_prefix[64];
    snprintf(local_prefix, sizeof(local_prefix), "__%s_%d_", m->name, pp->unique_id++);
    for (ExpandedLine *ln = m->body.items; ln < m->body.items + m->body.len; ln++) {
        char *expanded = replace_macro_locals(ln->text, local_prefix);
        for (int i = 0; i < argc; i++) {
            char *next = replace_token(expanded, m->params[i], args[i]);
            free(expanded);
            expanded = next;
        }
        char *layout = replace_layout_exprs(as, expanded, path, line);
        expanded_add(out, layout, path, line);
        free(layout);
        free(expanded);
    }
    for (int i = 0; i < argc; i++) free(args[i]);
    (void)pp;
    (void)depth;
    return 1;
}

static int parse_include_path(char *rest, char **out)
{
    rest = kasm_trim(rest);
    size_t n = strlen(rest);
    if (n < 2 || rest[0] != '"' || rest[n - 1] != '"')
        return 0;
    rest[n - 1] = 0;
    *out = kasm_xstrdup(rest + 1);
    return 1;
}

static int parse_kprint_literal(char *rest)
{
    rest = kasm_trim(rest);
    size_t n = strlen(rest);
    return n >= 2 && rest[0] == '"' && rest[n - 1] == '"';
}

static void expand_kprint_literal(Preproc *pp, ExpandedLines *out, const char *path,
                                  int line, char *literal, int newline)
{
    char label[128];
    snprintf(label, sizeof(label), "__kasm_%s_%d_msg",
             newline ? "kprintln" : "kprint", pp->unique_id++);
    char len_label[160];
    snprintf(len_label, sizeof(len_label), "%s_len", label);
    char buf[4096];
    expanded_add(out, "section .rodata", path, line);
    snprintf(buf, sizeof(buf), "%s:", label);
    expanded_add(out, buf, path, line);
    if (newline)
        snprintf(buf, sizeof(buf), "    db %s, 10", literal);
    else
        snprintf(buf, sizeof(buf), "    db %s", literal);
    expanded_add(out, buf, path, line);
    snprintf(buf, sizeof(buf), "%s = $ - %s", len_label, label);
    expanded_add(out, buf, path, line);
    expanded_add(out, "section .text", path, line);
    snprintf(buf, sizeof(buf), "    syscall write, STDOUT, %s, %s", label, len_label);
    expanded_add(out, buf, path, line);
}

static void macro_free(MacroDef *m)
{
    free(m->name);
    for (int i = 0; i < m->param_count; i++)
        free(m->params[i]);
    free(m->params);
    expanded_free(&m->body);
}

static int macro_conflicts(Preproc *pp, const char *name)
{
    return known_word(name) || macro_find(pp, name) != NULL;
}

static int first_word(char *s, char **word, char **rest)
{
    s = kasm_trim(s);
    if (*s == 0)
        return 0;
    char *p = s;
    while (*p && !isspace((unsigned char)*p))
        p++;
    *word = dup_range(s, p);
    *rest = kasm_trim(p);
    return 1;
}

static int macro_body_is_recursive(Preproc *pp, MacroDef *m)
{
    (void)pp;
    for (size_t i = 0; i < m->body.len; i++) {
        char *tmp = kasm_xstrdup(m->body.items[i].text);
        char *word = NULL, *rest = NULL;
        int ok = first_word(kasm_strip_comment(tmp), &word, &rest);
        (void)rest;
        if (ok && strcmp(word, m->name) == 0) {
            free(word);
            free(tmp);
            return 1;
        }
        free(word);
        free(tmp);
    }
    return 0;
}

static int add_macro(Assembler *as, Preproc *pp, const char *name, char *params,
                     ExpandedLines *body, const char *path, int line)
{
    if (!valid_ident(name) || macro_conflicts(pp, name)) {
        kasm_error(as, (SourceLoc){ path, line, 1 }, "macro conflicts with instruction/directive or is already defined '%s'", name);
        return 0;
    }
    if (pp->len == pp->cap) {
        pp->cap = pp->cap ? pp->cap * 2 : 16;
        pp->items = kasm_xrealloc(pp->items, pp->cap * sizeof(MacroDef));
    }
    MacroDef *m = &pp->items[pp->len++];
    memset(m, 0, sizeof(*m));
    m->name = kasm_xstrdup(name);
    char *param_items[32] = { 0 };
    int count = *kasm_trim(params) ? split_csv(params, param_items, 32) : 0;
    m->params = kasm_xrealloc(NULL, (size_t)(count ? count : 1) * sizeof(char *));
    m->param_count = count;
    for (int i = 0; i < count; i++) {
        if (!valid_ident(param_items[i])) {
            kasm_error(as, (SourceLoc){ path, line, 1 }, "invalid macro parameter '%s'", param_items[i]);
            for (int j = 0; j <= i; j++) free(param_items[j]);
            return 0;
        }
        m->params[i] = param_items[i];
    }
    m->body = *body;
    memset(body, 0, sizeof(*body));
    if (macro_body_is_recursive(pp, m)) {
        kasm_error(as, (SourceLoc){ path, line, 1 }, "recursive macro expansion for '%s'", name);
        return 0;
    }
    return 1;
}

static int emit_struct_instance(Assembler *as, StructDef *s, ExpandedLines *inits,
                                ExpandedLines *out, const char *path, int line)
{
    char **values = kasm_xrealloc(NULL, (s->field_count ? s->field_count : 1) * sizeof(char *));
    memset(values, 0, (s->field_count ? s->field_count : 1) * sizeof(char *));
    for (size_t i = 0; i < inits->len; i++) {
        char *tmp = kasm_xstrdup(inits->items[i].text);
        char *t = kasm_trim(kasm_strip_comment(tmp));
        if (*t == 0) {
            free(tmp);
            continue;
        }
        char *eq = strchr(t, '=');
        if (!eq) {
            kasm_error(as, (SourceLoc){ path, line, 1 }, "invalid struct initializer");
            free(tmp);
            goto fail;
        }
        *eq = 0;
        char *name = kasm_trim(t);
        char *value = kasm_trim(eq + 1);
        StructField *f = struct_field_find(s, name);
        if (!f) {
            kasm_error(as, (SourceLoc){ path, line, 1 }, "unknown field '%s'", name);
            free(tmp);
            goto fail;
        }
        size_t idx = (size_t)(f - s->fields);
        if (values[idx]) {
            kasm_error(as, (SourceLoc){ path, line, 1 }, "duplicate field initializer '%s'", name);
            free(tmp);
            goto fail;
        }
        values[idx] = kasm_xstrdup(value);
        free(tmp);
    }
    for (size_t i = 0; i < s->field_count; i++) {
        StructField *f = &s->fields[i];
        const char *v = values[i] ? values[i] : "0";
        char linebuf[4096];
        if (f->type == FIELD_BYTES) {
            size_t n = strlen(v);
            if (values[i] && n >= 2 && v[0] == '"' && v[n - 1] == '"') {
                size_t str_len = n - 2;
                if (str_len > f->size) {
                    kasm_error(as, (SourceLoc){ path, line, 1 }, "string too long for bytes field '%s'", f->name);
                    goto fail;
                }
                snprintf(linebuf, sizeof(linebuf), "db %s", v);
                expanded_add(out, linebuf, path, line);
                if (str_len < f->size) {
                    snprintf(linebuf, sizeof(linebuf), "times %llu db 0",
                             (unsigned long long)(f->size - str_len));
                    expanded_add(out, linebuf, path, line);
                }
            } else {
                snprintf(linebuf, sizeof(linebuf), "times %llu db 0", (unsigned long long)f->size);
                expanded_add(out, linebuf, path, line);
            }
        } else {
            const char *op = f->type == FIELD_BYTE ? "db" :
                             f->type == FIELD_WORD ? "dw" :
                             f->type == FIELD_DWORD ? "dd" : "dq";
            snprintf(linebuf, sizeof(linebuf), "%s %s", op, v);
            expanded_add(out, linebuf, path, line);
        }
    }
    for (size_t i = 0; i < s->field_count; i++)
        free(values[i]);
    free(values);
    return 1;
fail:
    for (size_t i = 0; i < s->field_count; i++)
        free(values[i]);
    free(values);
    return 0;
}

static int expand_file(Assembler *as, Preproc *pp, const char *path, ExpandedLines *out, int depth)
{
    if (depth > 32) {
        kasm_error(as, (SourceLoc){ path, 1, 1 }, "include expansion depth exceeded");
        return 0;
    }
    if (stack_contains(pp, path)) {
        kasm_error(as, (SourceLoc){ path, 1, 1 }, "include cycle detected");
        return 0;
    }
    if (included_contains(pp, path))
        return 1;
    FILE *f = fopen(path, "rb");
    if (!f) {
        kasm_error(as, (SourceLoc){ path, 1, 1 }, "cannot read include file");
        return 0;
    }
    stack_push(pp, path);
    included_add(pp, path);
    char line[4096];
    int line_no = 0;
    int in_macro = 0;
    char macro_name[256] = { 0 };
    char macro_params[1024] = { 0 };
    int macro_line = 0;
    ExpandedLines macro_body = { 0 };

    while (fgets(line, sizeof(line), f)) {
        line_no++;
        line[strcspn(line, "\r\n")] = 0;
        char orig[4096];
        snprintf(orig, sizeof(orig), "%s", line);
        char work[4096];
        snprintf(work, sizeof(work), "%s", line);
        char *s = kasm_trim(kasm_strip_comment(work));
        if (*s == 0) {
            if (in_macro)
                expanded_add(&macro_body, orig, path, line_no);
            else
                expanded_add(out, orig, path, line_no);
            continue;
        }
        char *word = NULL, *rest = NULL;
        first_word(s, &word, &rest);
        kasm_lower_ascii(word);
        if (!in_macro && (word[0] == '%' || strcmp(word, "bits") == 0 ||
            strcmp(word, "org") == 0 || strcmp(word, "default") == 0)) {
            kasm_error(as, (SourceLoc){ path, line_no, 1 },
                       "unsupported NASM-style syntax '%s'; see docs/SYNTAX.md for KASM syntax", word);
            free(word);
            fclose(f);
            stack_pop(pp);
            return 0;
        }

        if (in_macro) {
            if (strcmp(word, "end") == 0) {
                if (!add_macro(as, pp, macro_name, macro_params, &macro_body, path, macro_line)) {
                    free(word);
                    fclose(f);
                    stack_pop(pp);
                    return 0;
                }
                in_macro = 0;
            } else {
                if (strstr(orig, "%%") == orig) {
                    ; /* local labels are rewritten at expansion time */
                }
                expanded_add(&macro_body, orig, path, line_no);
            }
            free(word);
            continue;
        }

        if (strcmp(word, "struct") == 0) {
            char *name = NULL, *tail = NULL;
            if (!first_word(rest, &name, &tail) || !valid_ident(name) || *tail) {
                kasm_error(as, (SourceLoc){ path, line_no, 1 }, "invalid struct definition");
                free(name);
                free(word);
                fclose(f);
                stack_pop(pp);
                return 0;
            }
            StructDef sd;
            memset(&sd, 0, sizeof(sd));
            sd.name = kasm_xstrdup(name);
            int struct_line = line_no;
            free(name);
            int ok = 1, found_end = 0;
            while (fgets(line, sizeof(line), f)) {
                line_no++;
                line[strcspn(line, "\r\n")] = 0;
                char field_work[4096];
                snprintf(field_work, sizeof(field_work), "%s", line);
                char *fs = kasm_trim(kasm_strip_comment(field_work));
                if (*fs == 0)
                    continue;
                char *fw = NULL, *fr = NULL;
                first_word(fs, &fw, &fr);
                kasm_lower_ascii(fw);
                if (strcmp(fw, "end") == 0) {
                    found_end = 1;
                    free(fw);
                    break;
                }
                if (strcmp(fw, "align") == 0) {
                    int64_t n = 0;
                    if (!kasm_parse_int(fr, &n) || !is_power_two(n)) {
                        kasm_error(as, (SourceLoc){ path, line_no, 1 }, "invalid align value");
                        ok = 0;
                    } else {
                        sd.size = align_u64(sd.size, (uint64_t)n);
                    }
                    free(fw);
                    if (!ok) break;
                    continue;
                }
                free(fw);
                char *colon = strchr(fs, ':');
                if (!colon) {
                    kasm_error(as, (SourceLoc){ path, line_no, 1 }, "invalid field type");
                    ok = 0;
                    break;
                }
                *colon = 0;
                char *field_name = kasm_trim(fs);
                char *field_type = kasm_trim(colon + 1);
                FieldType ft;
                uint64_t fsz = 0;
                if (!parse_field_type(field_type, &ft, &fsz) ||
                    !add_struct_field(as, &sd, field_name, ft, fsz, path, line_no)) {
                    if (as->errors == 0)
                        kasm_error(as, (SourceLoc){ path, line_no, 1 }, "invalid field type");
                    ok = 0;
                    break;
                }
            }
            if (!found_end && ok) {
                kasm_error(as, (SourceLoc){ path, struct_line, 1 }, "missing struct end");
                ok = 0;
            }
            if (ok)
                ok = add_struct_def(as, &sd, path, struct_line);
            if (!ok) {
                free(sd.name);
                for (size_t si = 0; si < sd.field_count; si++)
                    free(sd.fields[si].name);
                free(sd.fields);
                free(word);
                fclose(f);
                stack_pop(pp);
                return 0;
            }
            free(word);
            continue;
        }

        if (strcmp(word, "include") == 0) {
            char *inc = NULL;
            if (!parse_include_path(rest, &inc)) {
                kasm_error(as, (SourceLoc){ path, line_no, 1 }, "invalid include syntax");
                free(word);
                fclose(f);
                stack_pop(pp);
                return 0;
            }
            char *resolved = resolve_include(as, path, inc);
            if (!resolved) {
                kasm_error(as, (SourceLoc){ path, line_no, 1 }, "include file not found '%s'", inc);
                fprintf(stderr, "  hint: add an -I path or fix the include filename\n");
                free(inc);
                free(word);
                fclose(f);
                stack_pop(pp);
                return 0;
            }
            int ok = expand_file(as, pp, resolved, out, depth + 1);
            free(resolved);
            free(inc);
            free(word);
            if (!ok) {
                fprintf(stderr, "  included from %s:%d\n", path, line_no);
                fclose(f);
                stack_pop(pp);
                return 0;
            }
            continue;
        }

        if (strcmp(word, "define") == 0) {
            char *name = NULL, *value = NULL;
            if (!first_word(rest, &name, &value) || !valid_ident(name) || *value == 0) {
                kasm_error(as, (SourceLoc){ path, line_no, 1 }, "invalid define");
                free(name);
                free(word);
                fclose(f);
                stack_pop(pp);
                return 0;
            }
            char defline[4096];
            char *layout_value = replace_layout_exprs(as, value, path, line_no);
            snprintf(defline, sizeof(defline), "%s = %s", name, layout_value);
            expanded_add(out, defline, path, line_no);
            free(layout_value);
            free(name);
            free(word);
            continue;
        }

        if (strcmp(word, "macro") == 0) {
            char *name = NULL, *params = NULL;
            if (!first_word(rest, &name, &params)) {
                kasm_error(as, (SourceLoc){ path, line_no, 1 }, "malformed macro definition");
                free(word);
                fclose(f);
                stack_pop(pp);
                return 0;
            }
            snprintf(macro_name, sizeof(macro_name), "%s", name);
            snprintf(macro_params, sizeof(macro_params), "%s", params);
            macro_line = line_no;
            in_macro = 1;
            free(name);
            free(word);
            continue;
        }

        if (strcmp(word, "kprint") == 0 || strcmp(word, "kprintln") == 0) {
            if (!parse_kprint_literal(rest)) {
                kasm_error(as, (SourceLoc){ path, line_no, 1 },
                           "%s expects one string literal argument", word);
                fprintf(stderr, "  hint: use %s \"text\"\n", word);
                free(word);
                fclose(f);
                stack_pop(pp);
                return 0;
            }
            expand_kprint_literal(pp, out, path, line_no, rest, strcmp(word, "kprintln") == 0);
            free(word);
            continue;
        }

        MacroDef *m = macro_find(pp, word);
        if (m) {
            int ok = expand_macro_call(as, pp, m, rest, path, line_no, out, depth + 1);
            free(word);
            if (!ok) {
                fclose(f);
                stack_pop(pp);
                return 0;
            }
            continue;
        }
        StructDef *sd = kasm_struct_find(as, word);
        if (sd && strcmp(kasm_trim(rest), "{") == 0) {
            ExpandedLines inits = { 0 };
            int found = 0;
            while (fgets(line, sizeof(line), f)) {
                line_no++;
                line[strcspn(line, "\r\n")] = 0;
                char brace_work[4096];
                snprintf(brace_work, sizeof(brace_work), "%s", line);
                char *bs = kasm_trim(kasm_strip_comment(brace_work));
                if (strcmp(bs, "}") == 0) {
                    found = 1;
                    break;
                }
                char *repl = replace_layout_exprs(as, line, path, line_no);
                expanded_add(&inits, repl, path, line_no);
                free(repl);
            }
            if (!found) {
                kasm_error(as, (SourceLoc){ path, line_no, 1 }, "struct initializer missing closing brace");
                expanded_free(&inits);
                free(word);
                fclose(f);
                stack_pop(pp);
                return 0;
            }
            int ok = emit_struct_instance(as, sd, &inits, out, path, line_no);
            expanded_free(&inits);
            free(word);
            if (!ok) {
                fclose(f);
                stack_pop(pp);
                return 0;
            }
            continue;
        }
        if (strncmp(s, "%%", 2) == 0) {
            kasm_error(as, (SourceLoc){ path, line_no, 1 }, "invalid local label outside macro");
            free(word);
            fclose(f);
            stack_pop(pp);
            return 0;
        }
        char *repl = replace_layout_exprs(as, orig, path, line_no);
        expanded_add(out, repl, path, line_no);
        free(repl);
        free(word);
    }
    fclose(f);
    stack_pop(pp);
    if (in_macro) {
        kasm_error(as, (SourceLoc){ path, macro_line, 1 }, "missing macro end");
        expanded_free(&macro_body);
        return 0;
    }
    return 1;
}

static void add_stmt(Assembler *as, Statement st)
{
    Program *p = &as->program;
    if (p->len == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 128;
        p->items = kasm_xrealloc(p->items, p->cap * sizeof(Statement));
    }
    p->items[p->len++] = st;
}

static int valid_ident(const char *s)
{
    if (!(*s == '_' || *s == '.' || isalpha((unsigned char)*s)))
        return 0;
    for (s++; *s; s++)
        if (!(*s == '_' || *s == '.' || isalnum((unsigned char)*s)))
            return 0;
    return 1;
}

static char *dup_range(const char *a, const char *b)
{
    size_t n = (size_t)(b - a);
    char *out = kasm_xrealloc(NULL, n + 1);
    memcpy(out, a, n);
    out[n] = 0;
    char *t = kasm_trim(out);
    char *ret = kasm_xstrdup(t);
    free(out);
    return ret;
}

static int split_operands(char *s, Operand *ops, const char *source)
{
    s = kasm_trim(s);
    if (*s == 0)
        return 0;
    int count = 0;
    int in_string = 0, bracket = 0, escaped = 0;
    char *start = s;
    for (char *p = s; ; p++) {
        if (*p == '\\' && in_string && !escaped) {
            escaped = 1;
            continue;
        }
        if (!escaped && *p == '"')
            in_string = !in_string;
        escaped = 0;
        if (!in_string && *p == '[')
            bracket++;
        if (!in_string && *p == ']' && bracket)
            bracket--;
        if (*p == 0 || (!in_string && !bracket && *p == ',')) {
            if (count < 8) {
                ops[count++].text = dup_range(start, p);
                ops[count - 1].column = kasm_column_of(source, ops[count - 1].text);
            }
            if (*p == 0)
                break;
            start = p + 1;
        }
    }
    return count;
}

typedef struct {
    const char *name;
    int number;
    int arg_count;
    unsigned pointer_mask;
} SyscallInfo;

static const SyscallInfo syscall_table[] = {
    { "read",       0,   3, 1u << 1 },
    { "write",      1,   3, 1u << 1 },
    { "open",       2,   3, 1u << 0 },
    { "close",      3,   1, 0 },
    { "stat",       4,   2, (1u << 0) | (1u << 1) },
    { "fstat",      5,   2, 1u << 1 },
    { "mmap",       9,   6, 0 },
    { "mprotect",   10,  3, 0 },
    { "munmap",     11,  2, 0 },
    { "brk",        12,  1, 0 },
    { "exit",       60,  1, 0 },
    { "exit_group", 231, 1, 0 },
    { "openat",     257, 4, 1u << 1 },
};

static const SyscallInfo *find_syscall(const char *name)
{
    for (size_t i = 0; i < sizeof(syscall_table) / sizeof(syscall_table[0]); i++)
        if (kasm_streq_ci(name, syscall_table[i].name))
            return &syscall_table[i];
    return NULL;
}

static int syscall_pointer_arg(const SyscallInfo *info, int arg_index)
{
    return (info->pointer_mask & (1u << arg_index)) != 0;
}

static int is_simple_ident_arg(const char *s)
{
    if (!(*s == '_' || *s == '.' || isalpha((unsigned char)*s)))
        return 0;
    for (s++; *s; s++)
        if (!(*s == '_' || *s == '.' || isalnum((unsigned char)*s)))
            return 0;
    return 1;
}

static int is_syscall_arg_register(const char *s)
{
    const char *regs[] = {
        "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
    };
    for (size_t i = 0; i < sizeof(regs) / sizeof(regs[0]); i++)
        if (kasm_streq_ci(s, regs[i]))
            return 1;
    return 0;
}

static int is_syscall_builtin_constant(const char *s)
{
    const char *names[] = {
        "stdin", "stdout", "stderr", "STDIN", "STDOUT", "STDERR",
        "AT_FDCWD", "O_RDONLY", "O_WRONLY", "O_RDWR", "O_CREAT", "O_TRUNC",
        "PROT_READ", "PROT_WRITE", "PROT_EXEC", "MAP_PRIVATE", "MAP_ANONYMOUS"
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (strcmp(s, names[i]) == 0)
            return 1;
    return 0;
}

static int syscall_pointer_uses_label_address(const char *arg)
{
    int64_t ignored = 0;
    if (kasm_parse_int(arg, &ignored) || is_syscall_arg_register(arg) ||
        is_syscall_builtin_constant(arg))
        return 0;
    return is_simple_ident_arg(arg);
}

static int syscall_arg_is_unsupported_pointer_expr(const char *arg)
{
    int64_t ignored = 0;
    if (kasm_parse_int(arg, &ignored) || is_syscall_arg_register(arg) ||
        is_syscall_builtin_constant(arg) || is_simple_ident_arg(arg))
        return 0;
    return 1;
}

static void add_instr(Assembler *as, SectionId sec, uint64_t *offsets, int line,
                      const char *source, const char *op, const char **operands, int count)
{
    Statement st;
    memset(&st, 0, sizeof(st));
    st.type = ST_INSTR;
    st.section = sec;
    st.offset = offsets[sec];
    st.line = line;
    st.column = kasm_column_of(source, op);
    st.source = kasm_xstrdup(source);
    st.op = kasm_xstrdup(op);
    kasm_lower_ascii(st.op);
    st.operand_count = count;
    for (int i = 0; i < count; i++) {
        st.operands[i].text = kasm_xstrdup(operands[i]);
        st.operands[i].column = kasm_column_of(source, operands[i]);
    }
    kasm_estimate_statement(as, &st);
    offsets[sec] += st.size;
    add_stmt(as, st);
}

static int parse_data_bytes(Assembler *as, Statement *st, int dry)
{
    uint64_t total = 0;
    int elem = 1;
    if (strcmp(st->op, "dw") == 0) elem = 2;
    if (strcmp(st->op, "dd") == 0) elem = 4;
    if (strcmp(st->op, "dq") == 0) elem = 8;
    for (int i = 0; i < st->operand_count; i++) {
        const char *s = st->operands[i].text;
        size_t len = strlen(s);
        if (*s == '"') {
            if (len < 2 || s[len - 1] != '"') {
                kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[i].column }, "unterminated string literal");
                return 0;
            }
            if (elem != 1) {
                kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[i].column }, "invalid operand combination: strings are only supported for db");
                return 0;
            }
            for (const char *p = s + 1; p[1]; p++) {
                if (*p == '\\' && p[1]) {
                    p++;
                    if (!(*p == 'n' || *p == 't' || *p == '"' || *p == '\\')) {
                        kasm_error(as, (SourceLoc){ as->path, st->line, st->operands[i].column + (int)(p - s) }, "invalid escape sequence");
                        return 0;
                    }
                }
                total++;
            }
        } else {
            total += (uint64_t)elem;
        }
    }
    if (dry)
        st->size = (uint32_t)total;
    return 1;
}

static int parse_line(Assembler *as, char *line, const char *orig, int line_no,
                      SectionId *current, uint64_t offsets[SEC_COUNT])
{
    char *s = kasm_trim(kasm_strip_comment(line));
    if (*s == 0)
        return 1;

    char *colon = strchr(s, ':');
    if (colon) {
        *colon = 0;
        char *name = kasm_trim(s);
        int col = kasm_column_of(orig, name);
        if (!valid_ident(name)) {
            kasm_error(as, (SourceLoc){ as->path, line_no, col }, "unexpected token '%s'", name);
            return 0;
        }
        if (*current == SEC_NONE) {
            kasm_error(as, (SourceLoc){ as->path, line_no, col }, "label outside of section");
            return 0;
        }
        if (!kasm_symbol_define(as, name, *current, offsets[*current], 0, 0, line_no, col))
            return 0;
        Statement st;
        memset(&st, 0, sizeof(st));
        st.type = ST_LABEL;
        st.section = *current;
        st.offset = offsets[*current];
        st.line = line_no;
        st.column = col;
        st.source = kasm_xstrdup(orig);
        st.name = kasm_xstrdup(name);
        add_stmt(as, st);
        s = kasm_trim(colon + 1);
        if (*s == 0)
            return 1;
    }

    char *eq = strchr(s, '=');
    if (eq) {
        *eq = 0;
        char *name = kasm_trim(s);
        char *expr = kasm_trim(eq + 1);
        int64_t value = 0;
        int col = kasm_column_of(orig, name);
        if (!valid_ident(name)) {
            kasm_error(as, (SourceLoc){ as->path, line_no, col }, "unexpected token '%s'", name);
            return 0;
        }
        uint64_t current_offset = *current == SEC_NONE ? 0 : offsets[*current];
        if (!kasm_eval_expr(as, expr, *current, current_offset, 0, &value,
                            (SourceLoc){ as->path, line_no, kasm_column_of(orig, expr) }))
            return 0;
        if (!kasm_symbol_define(as, name, SEC_NONE, 0, 1, value, line_no, col))
            return 0;
        Statement st;
        memset(&st, 0, sizeof(st));
        st.type = ST_CONST;
        st.section = *current;
        st.offset = current_offset;
        st.line = line_no;
        st.column = col;
        st.source = kasm_xstrdup(orig);
        st.name = kasm_xstrdup(name);
        st.expr = kasm_xstrdup(expr);
        add_stmt(as, st);
        return 1;
    }

    char *sp = s;
    while (*sp && !isspace((unsigned char)*sp))
        sp++;
    char *op = dup_range(s, sp);
    int op_col = kasm_column_of(orig, op);
    kasm_lower_ascii(op);
    char *rest = kasm_trim(sp);
    if (op[0] == '%' || strcmp(op, "bits") == 0 ||
        strcmp(op, "org") == 0 || strcmp(op, "default") == 0) {
        kasm_error(as, (SourceLoc){ as->path, line_no, op_col },
                   "unsupported NASM-style syntax '%s'; see docs/SYNTAX.md for KASM syntax", op);
        free(op);
        return 0;
    }

    if (strcmp(op, "entry") == 0) {
        if (*rest == 0) {
            kasm_error(as, (SourceLoc){ as->path, line_no, op_col }, "unexpected token: entry requires a symbol");
            free(op);
            return 0;
        }
        free(as->entry);
        as->entry = kasm_xstrdup(rest);
        Statement st;
        memset(&st, 0, sizeof(st));
        st.type = ST_ENTRY;
        st.line = line_no;
        st.column = op_col;
        st.source = kasm_xstrdup(orig);
        st.name = kasm_xstrdup(rest);
        add_stmt(as, st);
        free(op);
        return 1;
    }

    if (strcmp(op, "global") == 0 || strcmp(op, "extern") == 0) {
        if (*rest == 0) {
            kasm_error(as, (SourceLoc){ as->path, line_no, op_col }, "unexpected token: %s requires a symbol", op);
            free(op);
            return 0;
        }
        int sym_col = kasm_column_of(orig, rest);
        int ok = strcmp(op, "global") == 0 ?
            kasm_symbol_global(as, rest, line_no, sym_col) :
            kasm_symbol_extern(as, rest, line_no, sym_col);
        if (!ok) {
            free(op);
            return 0;
        }
        Statement st;
        memset(&st, 0, sizeof(st));
        st.type = strcmp(op, "global") == 0 ? ST_GLOBAL : ST_EXTERN;
        st.line = line_no;
        st.column = op_col;
        st.source = kasm_xstrdup(orig);
        st.name = kasm_xstrdup(rest);
        add_stmt(as, st);
        free(op);
        return 1;
    }

    if (strcmp(op, "section") == 0) {
        SectionId sec = kasm_section_from_name(rest);
        if (sec == SEC_NONE) {
            kasm_error(as, (SourceLoc){ as->path, line_no, kasm_column_of(orig, rest) }, "invalid section '%s'", rest);
            free(op);
            return 0;
        }
        *current = sec;
        Statement st;
        memset(&st, 0, sizeof(st));
        st.type = ST_SECTION;
        st.section = sec;
        st.line = line_no;
        st.column = op_col;
        st.source = kasm_xstrdup(orig);
        st.name = kasm_xstrdup(rest);
        add_stmt(as, st);
        free(op);
        return 1;
    }

    if (*current == SEC_NONE) {
        kasm_error(as, (SourceLoc){ as->path, line_no, 1 }, "statement outside of section");
        free(op);
        return 0;
    }

    Statement st;
    memset(&st, 0, sizeof(st));
    st.line = line_no;
    st.column = op_col;
    st.section = *current;
    st.offset = offsets[*current];
    st.source = kasm_xstrdup(orig);
    st.op = op;
    st.operand_count = split_operands(rest, st.operands, orig);

    if (strcmp(op, "times") == 0) {
        if (st.operand_count != 1) {
            kasm_error(as, (SourceLoc){ as->path, line_no, op_col }, "invalid operand combination: times expects '<count> db ...'");
            goto bad;
        }
        char *body = st.operands[0].text;
        char *p = body;
        while (*p && !isspace((unsigned char)*p)) p++;
        char *count_s = dup_range(body, p);
        char *count_keep = kasm_xstrdup(count_s);
        int64_t count = 0;
        if (!kasm_eval_expr(as, count_s, *current, offsets[*current], 0, &count,
                            (SourceLoc){ as->path, line_no, kasm_column_of(orig, count_s) }) || count < 0) {
            free(count_s);
            free(count_keep);
            goto bad;
        }
        free(count_s);
        char *subop = kasm_trim(p);
        char *q = subop;
        while (*q && !isspace((unsigned char)*q)) q++;
        char *directive = dup_range(subop, q);
        char *args = kasm_trim(q);
        for (int i = 0; i < st.operand_count; i++) free(st.operands[i].text);
        st.operand_count = split_operands(args, st.operands, orig);
        free(st.op);
        st.op = directive;
        if (!(strcmp(st.op, "db") == 0 || strcmp(st.op, "dw") == 0 ||
              strcmp(st.op, "dd") == 0 || strcmp(st.op, "dq") == 0)) {
            kasm_error(as, (SourceLoc){ as->path, line_no, kasm_column_of(orig, st.op) }, "unknown directive '%s'", st.op);
            goto bad;
        }
        if (!parse_data_bytes(as, &st, 1))
            goto bad;
        st.size *= (uint32_t)count;
        st.expr = count_keep;
        st.type = ST_DATA;
        offsets[*current] += st.size;
        add_stmt(as, st);
        return 1;
    }

    if (strcmp(op, "db") == 0 || strcmp(op, "dw") == 0 ||
        strcmp(op, "dd") == 0 || strcmp(op, "dq") == 0) {
        st.type = ST_DATA;
        if (!parse_data_bytes(as, &st, 1))
            goto bad;
        offsets[*current] += st.size;
        add_stmt(as, st);
        return 1;
    }

    if (strcmp(op, "align") == 0) {
        if (st.operand_count != 1) {
            kasm_error(as, (SourceLoc){ as->path, line_no, op_col }, "invalid align value");
            goto bad;
        }
        int64_t n = 0;
        if (!kasm_eval_expr(as, st.operands[0].text, *current, offsets[*current], 0, &n,
                            (SourceLoc){ as->path, line_no, st.operands[0].column }) ||
            !is_power_two(n)) {
            kasm_error(as, (SourceLoc){ as->path, line_no, st.operands[0].column }, "invalid align value");
            goto bad;
        }
        st.type = ST_DATA;
        st.size = (uint32_t)(align_u64(offsets[*current], (uint64_t)n) - offsets[*current]);
        offsets[*current] += st.size;
        add_stmt(as, st);
        return 1;
    }

    if (strcmp(op, "resb") == 0 || strcmp(op, "resw") == 0 ||
        strcmp(op, "resd") == 0 || strcmp(op, "resq") == 0) {
        if (st.operand_count != 1) {
            kasm_error(as, (SourceLoc){ as->path, line_no, op_col }, "invalid res directive size");
            goto bad;
        }
        int64_t n = 0;
        if (!kasm_eval_expr(as, st.operands[0].text, *current, offsets[*current], 0, &n,
                            (SourceLoc){ as->path, line_no, st.operands[0].column }) || n < 0) {
            kasm_error(as, (SourceLoc){ as->path, line_no, st.operands[0].column }, "invalid res directive size");
            goto bad;
        }
        int elem = strcmp(op, "resb") == 0 ? 1 : strcmp(op, "resw") == 0 ? 2 :
                   strcmp(op, "resd") == 0 ? 4 : 8;
        st.type = ST_DATA;
        st.size = (uint32_t)(n * elem);
        offsets[*current] += st.size;
        add_stmt(as, st);
        return 1;
    }

    if (strcmp(op, "syscall") == 0 && st.operand_count > 0) {
        if (as->no_syscall_sugar) {
            kasm_error(as, (SourceLoc){ as->path, line_no, op_col },
                       "syscall sugar is disabled by --no-syscall-sugar; use plain 'syscall' after setting registers");
            goto bad;
        }
        const SyscallInfo *info = find_syscall(st.operands[0].text);
        if (!info) {
            kasm_error(as, (SourceLoc){ as->path, line_no, st.operands[0].column },
                       "unknown syscall '%s'; supported syscalls: read, write, open, close, stat, fstat, mmap, mprotect, munmap, brk, exit, exit_group, openat",
                       st.operands[0].text);
            goto bad;
        }
        if (st.operand_count - 1 != info->arg_count) {
            kasm_error(as, (SourceLoc){ as->path, line_no, st.operands[0].column },
                       "wrong argument count for syscall '%s': expected %d, got %d; hint: see docs/SYSCALL_SUGAR.md",
                       info->name, info->arg_count, st.operand_count - 1);
            goto bad;
        }
        const char *regs[] = { "rdi", "rsi", "rdx", "r10", "r8", "r9" };
        char num[32];
        snprintf(num, sizeof(num), "%d", info->number);
        const char *ops1[] = { "rax", num };
        add_instr(as, *current, offsets, line_no, orig, "mov", ops1, 2);
        for (int i = 1; i < st.operand_count; i++) {
            const char *arg = st.operands[i].text;
            if (syscall_pointer_arg(info, i - 1) && syscall_arg_is_unsupported_pointer_expr(arg)) {
                kasm_error(as, (SourceLoc){ as->path, line_no, st.operands[i].column },
                           "unsupported pointer argument '%s' for syscall sugar; use a label, register, integer address, or raw syscall setup",
                           arg);
                goto bad;
            }
            if (syscall_pointer_arg(info, i - 1) && syscall_pointer_uses_label_address(arg)) {
                char rel[256];
                snprintf(rel, sizeof(rel), "[rel %s]", arg);
                const char *ops2[] = { regs[i - 1], rel };
                add_instr(as, *current, offsets, line_no, orig, "lea", ops2, 2);
            } else {
                const char *ops2[] = { regs[i - 1], arg };
                add_instr(as, *current, offsets, line_no, orig, "mov", ops2, 2);
            }
        }
        add_instr(as, *current, offsets, line_no, orig, "syscall", NULL, 0);
        for (int i = 0; i < st.operand_count; i++) free(st.operands[i].text);
        free(st.source);
        free(st.op);
        return 1;
    }

    st.type = ST_INSTR;
    if (!kasm_estimate_statement(as, &st))
        goto bad;
    offsets[*current] += st.size;
    add_stmt(as, st);
    return 1;

bad:
    for (int i = 0; i < st.operand_count; i++) free(st.operands[i].text);
    free(st.source);
    free(st.op);
    free(st.expr);
    return 0;
}

int kasm_parse_file(Assembler *as, const char *path)
{
    Preproc pp;
    memset(&pp, 0, sizeof(pp));
    ExpandedLines expanded = { 0 };
    if (!expand_file(as, &pp, path, &expanded, 0))
        goto fail;

    SectionId current = SEC_NONE;
    uint64_t offsets[SEC_COUNT] = { 0, 0, 0 };
    for (size_t i = 0; i < expanded.len; i++) {
        int line_no = (int)i + 1;
        char line[4096];
        char orig[4096];
        snprintf(line, sizeof(line), "%s", expanded.items[i].text);
        snprintf(orig, sizeof(orig), "%s", expanded.items[i].text);
        if (as->source_line_count == as->source_line_cap) {
            as->source_line_cap = as->source_line_cap ? as->source_line_cap * 2 : 128;
            as->source_lines = kasm_xrealloc(as->source_lines,
                                             as->source_line_cap * sizeof(char *));
        }
        as->source_lines[as->source_line_count++] = kasm_xstrdup(orig);
        if (!parse_line(as, line, orig, line_no, &current, offsets)) {
            goto fail;
        }
    }
    int has_global = 0;
    for (size_t gi = 0; gi < as->symbols.len; gi++)
        if (as->symbols.items[gi].is_global)
            has_global = 1;
    if (!as->entry && !as->object_mode && !as->raw_mode && !has_global)
        kasm_error(as, (SourceLoc){ path, 1, 1 }, "missing entry symbol");
    for (size_t i = 0; i < pp.len; i++)
        macro_free(&pp.items[i]);
    free(pp.items);
    for (size_t i = 0; i < pp.stack_len; i++)
        free(pp.stack[i]);
    free(pp.stack);
    for (size_t i = 0; i < pp.included_len; i++)
        free(pp.included[i]);
    free(pp.included);
    expanded_free(&expanded);
    return as->errors == 0;

fail:
    for (size_t i = 0; i < pp.len; i++)
        macro_free(&pp.items[i]);
    free(pp.items);
    for (size_t i = 0; i < pp.stack_len; i++)
        free(pp.stack[i]);
    free(pp.stack);
    for (size_t i = 0; i < pp.included_len; i++)
        free(pp.included[i]);
    free(pp.included);
    expanded_free(&expanded);
    return 0;
}
