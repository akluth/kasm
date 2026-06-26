#include "diagnostics.h"
#include "elf64.h"
#include "encoder.h"
#include "hints.h"
#include "parser.h"
#include "symbols.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef KASM_INSTALL_LIB
#define KASM_INSTALL_LIB "/usr/local/share/kasm/lib"
#endif

void *kasm_xrealloc(void *ptr, size_t size)
{
    void *out = realloc(ptr, size ? size : 1);
    if (!out) {
        fprintf(stderr, "kasm: out of memory\n");
        exit(1);
    }
    return out;
}

char *kasm_xstrdup(const char *s)
{
    size_t len = strlen(s);
    char *out = kasm_xrealloc(NULL, len + 1);
    memcpy(out, s, len + 1);
    return out;
}

char *kasm_trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = 0;
    return s;
}

int kasm_streq_ci(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

int kasm_parse_int(const char *s, int64_t *out)
{
    char *end = NULL;
    errno = 0;
    long long v = strtoll(s, &end, 0);
    if (errno || end == s || *kasm_trim(end) != 0)
        return 0;
    *out = (int64_t)v;
    return 1;
}

void kasm_lower_ascii(char *s)
{
    for (; *s; s++)
        *s = (char)tolower((unsigned char)*s);
}

int kasm_column_of(const char *line, const char *token)
{
    if (!line || !token || !*token)
        return 1;
    const char *p = strstr(line, token);
    if (!p) {
        char *lower_line = kasm_xstrdup(line);
        char *lower_token = kasm_xstrdup(token);
        kasm_lower_ascii(lower_line);
        kasm_lower_ascii(lower_token);
        p = strstr(lower_line, lower_token);
        int col = p ? (int)(p - lower_line) + 1 : 1;
        free(lower_line);
        free(lower_token);
        return col;
    }
    return (int)(p - line) + 1;
}

const char *kasm_section_name(SectionId id)
{
    if (id == SEC_TEXT)
        return ".text";
    if (id == SEC_RODATA)
        return ".rodata";
    if (id == SEC_DATA)
        return ".data";
    return "<none>";
}

SectionId kasm_section_from_name(const char *name)
{
    if (kasm_streq_ci(name, ".text"))
        return SEC_TEXT;
    if (kasm_streq_ci(name, ".rodata"))
        return SEC_RODATA;
    if (kasm_streq_ci(name, ".data"))
        return SEC_DATA;
    return SEC_NONE;
}

void kasm_buf_append(ByteBuf *buf, const void *data, size_t len)
{
    if (buf->len + len > buf->cap) {
        size_t cap = buf->cap ? buf->cap * 2 : 64;
        while (cap < buf->len + len)
            cap *= 2;
        buf->data = kasm_xrealloc(buf->data, cap);
        buf->cap = cap;
    }
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
}

void kasm_buf_append_u8(ByteBuf *buf, uint8_t v) { kasm_buf_append(buf, &v, 1); }
void kasm_buf_append_u16(ByteBuf *buf, uint16_t v) { kasm_buf_append(buf, &v, 2); }
void kasm_buf_append_u32(ByteBuf *buf, uint32_t v) { kasm_buf_append(buf, &v, 4); }
void kasm_buf_append_u64(ByteBuf *buf, uint64_t v) { kasm_buf_append(buf, &v, 8); }

uint64_t kasm_align(uint64_t value, uint64_t align)
{
    return (value + align - 1) & ~(align - 1);
}

static void free_stmt(Statement *st)
{
    free(st->source);
    free(st->name);
    free(st->op);
    free(st->expr);
    for (int i = 0; i < st->operand_count; i++)
        free(st->operands[i].text);
}

void kasm_program_free(Program *program)
{
    for (size_t i = 0; i < program->len; i++)
        free_stmt(&program->items[i]);
    free(program->items);
}

void kasm_assembler_free(Assembler *as)
{
    kasm_program_free(&as->program);
    for (size_t i = 0; i < as->symbols.len; i++)
        free(as->symbols.items[i].name);
    free(as->symbols.items);
    for (size_t i = 0; i < as->structs.len; i++) {
        free(as->structs.items[i].name);
        for (size_t j = 0; j < as->structs.items[i].field_count; j++)
            free(as->structs.items[i].fields[j].name);
        free(as->structs.items[i].fields);
    }
    free(as->structs.items);
    for (size_t i = 0; i < as->relocs.len; i++)
        free(as->relocs.items[i].symbol);
    free(as->relocs.items);
    for (size_t i = 0; i < as->include_path_count; i++)
        free(as->include_paths[i]);
    free(as->include_paths);
    for (int i = 0; i < SEC_COUNT; i++)
        free(as->sections[i].data);
    if (as->explain_file)
        fclose(as->explain_file);
    if (as->list_file)
        fclose(as->list_file);
    for (size_t i = 0; i < as->source_line_count; i++)
        free(as->source_lines[i]);
    free(as->source_lines);
    free(as->entry);
}

StructDef *kasm_struct_find(Assembler *as, const char *name)
{
    for (size_t i = 0; i < as->structs.len; i++)
        if (kasm_streq_ci(as->structs.items[i].name, name))
            return &as->structs.items[i];
    return NULL;
}

void kasm_add_include_path(Assembler *as, const char *path)
{
    if (as->include_path_count == as->include_path_cap) {
        as->include_path_cap = as->include_path_cap ? as->include_path_cap * 2 : 8;
        as->include_paths = kasm_xrealloc(as->include_paths,
                                          as->include_path_cap * sizeof(char *));
    }
    as->include_paths[as->include_path_count++] = kasm_xstrdup(path);
}

static void add_exe_relative_include(Assembler *as, const char *argv0)
{
    const char *slash = strrchr(argv0, '/');
    const char *bslash = strrchr(argv0, '\\');
    if (!slash || (bslash && bslash > slash))
        slash = bslash;
    if (!slash)
        return;
    size_t dir_len = (size_t)(slash - argv0);
    const char suffix[] = "/../share/kasm/lib";
    char *path = kasm_xrealloc(NULL, dir_len + sizeof(suffix));
    memcpy(path, argv0, dir_len);
    memcpy(path + dir_len, suffix, sizeof(suffix));
    kasm_add_include_path(as, path);
    free(path);
}

void kasm_add_reloc(Assembler *as, SectionId section, uint64_t offset,
                    const char *symbol, RelocKind kind, int64_t addend,
                    SourceLoc loc)
{
    if (as->relocs.len == as->relocs.cap) {
        as->relocs.cap = as->relocs.cap ? as->relocs.cap * 2 : 32;
        as->relocs.items = kasm_xrealloc(as->relocs.items, as->relocs.cap * sizeof(Reloc));
    }
    Reloc *r = &as->relocs.items[as->relocs.len++];
    r->section = section;
    r->offset = offset;
    r->symbol = kasm_xstrdup(symbol);
    r->kind = kind;
    r->addend = addend;
    r->line = loc.line;
    r->column = loc.column;
}

static void usage(FILE *out)
{
    fprintf(out, "KASM 0.1.0\n");
    fprintf(out, "usage: kasm [options] input.asm\n");
    fprintf(out, "       kasm build [--config FILE] [--verbose] [--no-link]\n");
    fprintf(out, "       kasm link file.o... -o app [--entry SYMBOL]\n");
    fprintf(out, "       kasm inspect [--headers] [--segments] [--sections] [--symbols] [--relocs] file\n");
    fprintf(out, "       kasm disasm [--section NAME] [--start ADDR] [--length N] file\n\n");
    fprintf(out, "output:\n");
    fprintf(out, "  -o FILE           write output file\n");
    fprintf(out, "  -f elf64|elf64-obj|obj|bin  output format, default elf64\n");
    fprintf(out, "  --combine         reserved for future multi-file combining\n");
    fprintf(out, "  --tiny, -Oz       prefer smaller safe encodings\n");
    fprintf(out, "  --no-tiny         disable tiny mode after an earlier --tiny/-Oz\n");
    fprintf(out, "  --tiny-report     print tiny mode optimization report\n");
    fprintf(out, "\nproject build:\n");
    fprintf(out, "  build             build project described by kasm.toml\n");
    fprintf(out, "  build --config F  use a specific project config\n");
    fprintf(out, "  build --verbose   print assemble/link commands\n");
    fprintf(out, "  build --no-link   assemble project objects only\n");
    fprintf(out, "  build --internal-linker  link executable projects with KASM linker\n");
    fprintf(out, "  build --linker internal  same as --internal-linker\n");
    fprintf(out, "\nlink:\n");
    fprintf(out, "  link file.o... -o app  link ELF64 objects with built-in linker\n");
    fprintf(out, "  link --entry S         use entry symbol S, default _start\n");
    fprintf(out, "\nbinary tools:\n");
    fprintf(out, "  inspect FILE      inspect ELF64 headers, segments, sections, symbols, relocations\n");
    fprintf(out, "  disasm FILE       disassemble supported x86-64 encodings from ELF .text\n");
    fprintf(out, "\ninclude/preprocessor:\n");
    fprintf(out, "  -I PATH           add include search path\n");
    fprintf(out, "  --print-include-paths  print include search paths\n");
    fprintf(out, "  --print-std-path  print KASM standard include roots\n");
    fprintf(out, "  --no-stdlib       disable bundled include search paths\n");
    fprintf(out, "  --no-std          alias for --no-stdlib\n");
    fprintf(out, "\nsyscalls:\n");
    fprintf(out, "  --no-syscall-sugar  reject named syscall pseudo-instructions\n");
    fprintf(out, "\nexplain/list/map:\n");
    fprintf(out, "  --explain         print byte listing and encoding notes\n");
    fprintf(out, "  --explain=normal  normal explain output\n");
    fprintf(out, "  --explain=verbose verbose byte-level explain output\n");
    fprintf(out, "  --explain=deluxe  structured encoding explanation\n");
    fprintf(out, "  --explain-format text  select explain output format\n");
    fprintf(out, "  --explain-file F  write explain output to file\n");
    fprintf(out, "  --elf-info       print generated ELF anatomy summary\n");
    fprintf(out, "  --teach          print a guided assembly/ELF lesson\n");
    fprintf(out, "  --teach-level L  beginner, intermediate, or deep\n");
    fprintf(out, "  --map FILE        write map file\n");
    fprintf(out, "  --list FILE       write compact listing file\n");
    fprintf(out, "\nhints:\n");
    fprintf(out, "  --hints           print educational performance/ABI hints\n");
    fprintf(out, "  --hints=LIST      enable hint categories: perf,abi,size,all\n");
    fprintf(out, "  --hints-cpu CPU   select hint profile: generic,intel,amd,zen4,skylake\n");
    fprintf(out, "\ndebug/dump:\n");
    fprintf(out, "  --dump-symbols    print parsed symbol table\n");
    fprintf(out, "  --dump-sections   print section sizes and flags\n");
    fprintf(out, "  --dump-ir         print parsed IR\n");
    fprintf(out, "  --dump-relocs     print collected relocation entries\n");
    fprintf(out, "  --dump-all        print symbols, sections, and relocations\n");
    fprintf(out, "  --dump-structs    print struct layouts\n");
    fprintf(out, "  --dump-expanded   print source after includes and macros\n");
    fprintf(out, "  --dump-tokens     print one expanded line per token row\n");
    fprintf(out, "\ngeneral:\n");
    fprintf(out, "  --version         print version\n");
    fprintf(out, "  --help            print this help\n");
}

static int require_arg(int argc, char **argv, int i)
{
    if (i + 1 < argc)
        return 1;
    fprintf(stderr, "error: option '%s' requires an argument\n", argv[i]);
    return 0;
}

static void enable_all_hints(Assembler *as)
{
    as->hints = 1;
    as->hint_perf = 1;
    as->hint_abi = 1;
    as->hint_size = 1;
}

static int enable_hint_categories(Assembler *as, const char *list)
{
    as->hints = 1;
    char *tmp = kasm_xstrdup(list);
    char *part = strtok(tmp, ",");
    while (part) {
        part = kasm_trim(part);
        if (strcmp(part, "all") == 0) {
            as->hint_perf = 1;
            as->hint_abi = 1;
            as->hint_size = 1;
        } else if (strcmp(part, "perf") == 0) {
            as->hint_perf = 1;
        } else if (strcmp(part, "abi") == 0) {
            as->hint_abi = 1;
        } else if (strcmp(part, "size") == 0) {
            as->hint_size = 1;
        } else {
            fprintf(stderr, "error: unknown hint category '%s'\n", part);
            fprintf(stderr, "hint: supported categories are perf, abi, size, all\n");
            free(tmp);
            return 0;
        }
        part = strtok(NULL, ",");
    }
    free(tmp);
    return 1;
}

static int valid_hint_cpu(const char *cpu)
{
    return strcmp(cpu, "generic") == 0 || strcmp(cpu, "intel") == 0 ||
           strcmp(cpu, "amd") == 0 || strcmp(cpu, "zen4") == 0 ||
           strcmp(cpu, "skylake") == 0;
}

static const char *stmt_type_name(StmtType type)
{
    switch (type) {
    case ST_ENTRY: return "entry";
    case ST_GLOBAL: return "global";
    case ST_EXTERN: return "extern";
    case ST_SECTION: return "section";
    case ST_LABEL: return "label";
    case ST_CONST: return "const";
    case ST_DATA: return "data";
    case ST_INSTR: return "instr";
    }
    return "unknown";
}

static uint64_t file_size_or_zero(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    long n = ftell(f);
    fclose(f);
    return n < 0 ? 0 : (uint64_t)n;
}

static void print_tiny_report(Assembler *as);
static void dump_symbols(Assembler *as);
static void dump_sections(Assembler *as);
static void dump_ir(Assembler *as);
static void dump_relocs(Assembler *as);
static void dump_all(Assembler *as);
static void dump_structs(Assembler *as);
static void write_map_file(Assembler *as, const char *format);
static void print_elf_info(Assembler *as, const char *output, const char *format);
static void print_teaching_mode(Assembler *as, const char *output, const char *format);
static int run_inspect(int argc, char **argv);
static int run_disasm(int argc, char **argv);
static int run_link_command(int argc, char **argv);

static void add_input(char ***items, size_t *len, size_t *cap, const char *path)
{
    if (*len == *cap) {
        *cap = *cap ? *cap * 2 : 8;
        *items = kasm_xrealloc(*items, *cap * sizeof(char *));
    }
    (*items)[(*len)++] = (char *)path;
}

static const char *path_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    if (!slash || (bslash && bslash > slash))
        slash = bslash;
    return slash ? slash + 1 : path;
}

static char *default_output_path(const char *input, const char *format, int multi_input)
{
    const char *base = path_basename(input);
    const char *dot = strrchr(base, '.');
    size_t stem_len = dot && dot != base ? (size_t)(dot - base) : strlen(base);
    const char *ext = ".o";
    if (!multi_input) {
        if (strcmp(format, "bin") == 0)
            ext = ".bin";
        else if (strcmp(format, "elf64") == 0)
            ext = "";
    }
    char *out = kasm_xrealloc(NULL, stem_len + strlen(ext) + 1);
    memcpy(out, base, stem_len);
    strcpy(out + stem_len, ext);
    return out;
}

static int has_output_mode(const Assembler *as)
{
    return as->dump_symbols || as->dump_sections || as->dump_ir ||
           as->dump_relocs || as->dump_structs || as->dump_expanded ||
           as->dump_tokens || as->elf_info || as->teach || as->explain ||
           as->map_path || as->list_path;
}

static int valid_format(const char *format)
{
    return strcmp(format, "elf64") == 0 || strcmp(format, "elf64-obj") == 0 ||
           strcmp(format, "obj") == 0 || strcmp(format, "bin") == 0;
}

static void copy_assembler_options(Assembler *dst, const Assembler *src)
{
    dst->explain = src->explain;
    dst->explain_mode = src->explain_mode;
    dst->explain_path = src->explain_path;
    dst->map_path = src->map_path;
    dst->list_path = src->list_path;
    dst->dump_symbols = src->dump_symbols;
    dst->dump_sections = src->dump_sections;
    dst->dump_all = src->dump_all;
    dst->dump_ir = src->dump_ir;
    dst->dump_relocs = src->dump_relocs;
    dst->dump_structs = src->dump_structs;
    dst->dump_expanded = src->dump_expanded;
    dst->dump_tokens = src->dump_tokens;
    dst->elf_info = src->elf_info;
    dst->teach = src->teach;
    dst->teach_level = src->teach_level;
    dst->no_stdlib = src->no_stdlib;
    dst->no_syscall_sugar = src->no_syscall_sugar;
    dst->print_include_paths = src->print_include_paths;
    dst->print_std_path = src->print_std_path;
    dst->tiny = src->tiny;
    dst->tiny_report = src->tiny_report;
    dst->hints = src->hints;
    dst->hint_perf = src->hint_perf;
    dst->hint_abi = src->hint_abi;
    dst->hint_size = src->hint_size;
    dst->hints_cpu = src->hints_cpu;
    for (size_t i = 0; i < src->include_path_count; i++)
        kasm_add_include_path(dst, src->include_paths[i]);
}

static int write_output(Assembler *as, const char *input, const char *output, const char *format)
{
    if (strcmp(format, "elf64") == 0)
        return kasm_write_elf64(as, output);
    if (strcmp(format, "elf64-obj") == 0 || strcmp(format, "obj") == 0)
        return kasm_write_elf64_obj(as, output);
    if (strcmp(format, "bin") == 0)
        return kasm_write_bin(as, output);
    kasm_error(as, (SourceLoc){ input, 1, 1 }, "unknown output format '%s'", format);
    return 0;
}

static int assemble_one(const Assembler *options, const char *input,
                        const char *output, const char *format)
{
    Assembler as;
    memset(&as, 0, sizeof(as));
    copy_assembler_options(&as, options);
    as.path = input;
    as.object_mode = strcmp(format, "elf64-obj") == 0 || strcmp(format, "obj") == 0;
    as.raw_mode = strcmp(format, "bin") == 0;

    if (as.explain_path) {
        as.explain_file = fopen(as.explain_path, "wb");
        if (!as.explain_file) {
            kasm_error(&as, (SourceLoc){ as.explain_path, 1, 1 }, "cannot write explain file");
            goto fail;
        }
    }
    if (as.list_path) {
        as.list_file = fopen(as.list_path, "wb");
        if (!as.list_file) {
            kasm_error(&as, (SourceLoc){ as.list_path, 1, 1 }, "cannot write listing file");
            goto fail;
        }
    }

    if (!kasm_parse_file(&as, input) || as.errors)
        goto fail;
    as.object_mode = strcmp(format, "elf64-obj") == 0 || strcmp(format, "obj") == 0;
    as.raw_mode = strcmp(format, "bin") == 0;
    if (!kasm_validate_symbols(&as, as.object_mode) || as.errors)
        goto fail;
    if (!kasm_apply_tiny_layout(&as) || as.errors)
        goto fail;
    if (!kasm_encode_program(&as) || as.errors)
        goto fail;
    kasm_run_hints(&as);

    if (as.dump_all)
        dump_all(&as);
    else if (as.dump_symbols)
        dump_symbols(&as);
    if (!as.dump_all && as.dump_sections)
        dump_sections(&as);
    if (as.dump_ir)
        dump_ir(&as);
    if (!as.dump_all && as.dump_relocs)
        dump_relocs(&as);
    if (as.dump_structs)
        dump_structs(&as);
    if (as.dump_expanded || as.dump_tokens) {
        for (size_t i = 0; i < as.source_line_count; i++)
            printf("%s\n", as.source_lines[i]);
    }
    if (as.elf_info)
        print_elf_info(&as, output, format);
    if (as.teach)
        print_teaching_mode(&as, output, format);
    write_map_file(&as, format);

    int ok = 1;
    if (output) {
        ok = write_output(&as, input, output, format);
        if (ok && as.tiny_report)
            as.tiny_final_size = file_size_or_zero(output);
    }
    if (ok && as.tiny_report)
        print_tiny_report(&as);

    kasm_assembler_free(&as);
    return ok ? 0 : 1;

fail:
    kasm_assembler_free(&as);
    return 1;
}

static void print_tiny_report(Assembler *as)
{
    puts("Tiny mode report:");
    printf("- jumps shortened: %llu\n", (unsigned long long)as->tiny_jumps_shortened);
    printf("- imm8 encodings used: %llu\n", (unsigned long long)as->tiny_imm8_used);
    printf("- push imm8 encodings used: %llu\n", (unsigned long long)as->tiny_push_imm8_used);
    printf("- accumulator encodings used: %llu\n", (unsigned long long)as->tiny_accumulator_used);
    printf("- disp8 memory encodings used: %llu\n", (unsigned long long)as->tiny_disp8_used);
    printf("- zero-displacement memory encodings used: %llu\n", (unsigned long long)as->tiny_disp0_used);
    printf("- near jumps required: %llu\n", (unsigned long long)as->tiny_near_jumps);
    printf("- estimated bytes saved: %llu\n", (unsigned long long)as->tiny_bytes_saved);
    printf("- final output size: %llu bytes\n", (unsigned long long)as->tiny_final_size);
}

static void dump_symbols(Assembler *as)
{
    puts("Symbols:");
    puts("name\tbinding\tsection\toffset\taddress\ttype\tlocation");
    for (size_t i = 0; i < as->symbols.len; i++) {
        Symbol *s = &as->symbols.items[i];
        uint64_t addr = s->is_extern ? 0 :
                        (s->is_const || s->section == SEC_NONE ? (uint64_t)s->value :
                         (as->object_mode ? s->offset : as->sections[s->section].vaddr + s->offset));
        printf("%s\t%s\t%s\t0x%llx\t0x%llx\t%s\t%s:%d:%d\n", s->name,
               s->is_extern ? "extern" : (s->is_global ? "global" : "local"),
               kasm_section_name(s->section),
               (unsigned long long)(s->is_const ? 0 : s->offset),
               (unsigned long long)addr,
               s->is_extern ? "undef" : (s->is_const ? "const" : "label"),
               as->path ? as->path : "<input>", s->line, s->column ? s->column : 1);
    }
}

static void dump_sections(Assembler *as)
{
    puts("Sections:");
    puts("section\tsize\talignment\tflags\tfile_offset\taddress");
    printf(".text\t%llu\t16\tr-x\tn/a\t0x%llx\n",
           (unsigned long long)as->sections[SEC_TEXT].len,
           (unsigned long long)as->sections[SEC_TEXT].vaddr);
    printf(".rodata\t%llu\t16\tr--\tn/a\t0x%llx\n",
           (unsigned long long)as->sections[SEC_RODATA].len,
           (unsigned long long)as->sections[SEC_RODATA].vaddr);
    printf(".data\t%llu\t16\trw-\tn/a\t0x%llx\n",
           (unsigned long long)as->sections[SEC_DATA].len,
           (unsigned long long)as->sections[SEC_DATA].vaddr);
}

static void dump_ir(Assembler *as)
{
    for (size_t i = 0; i < as->program.len; i++) {
        Statement *st = &as->program.items[i];
        printf("%04d:%02d %-7s %-7s off=0x%llx size=%u",
               st->line, st->column, stmt_type_name(st->type),
               kasm_section_name(st->section), (unsigned long long)st->offset, st->size);
        if (st->name)
            printf(" name=%s", st->name);
        if (st->op)
            printf(" op=%s", st->op);
        for (int j = 0; j < st->operand_count; j++)
            printf(" arg%d=%s", j, st->operands[j].text);
        if (st->expr)
            printf(" expr=%s", st->expr);
        putchar('\n');
    }
}

static void dump_relocs(Assembler *as)
{
    puts("Relocations:");
    puts("section\toffset\ttype\tsymbol\taddend\tlocation");
    for (size_t i = 0; i < as->relocs.len; i++) {
        Reloc *r = &as->relocs.items[i];
        printf("%s\t0x%llx\t%s\t%s\t%lld\t%s:%d:%d\n",
               r->section == SEC_TEXT ? ".rela.text" :
               r->section == SEC_DATA ? ".rela.data" : ".rela.rodata",
               (unsigned long long)r->offset,
               r->kind == RELOC_PC32 ? "R_X86_64_PC32" : "R_X86_64_64",
               r->symbol, (long long)r->addend,
               as->path ? as->path : "<input>", r->line, r->column ? r->column : 1);
    }
}

static void dump_all(Assembler *as)
{
    dump_symbols(as);
    putchar('\n');
    dump_sections(as);
    putchar('\n');
    dump_relocs(as);
}

static const char *field_type_name(FieldType t)
{
    if (t == FIELD_BYTE) return "byte";
    if (t == FIELD_WORD) return "word";
    if (t == FIELD_DWORD) return "dword";
    if (t == FIELD_QWORD) return "qword";
    return "bytes";
}

static void dump_structs(Assembler *as)
{
    for (size_t i = 0; i < as->structs.len; i++) {
        StructDef *s = &as->structs.items[i];
        printf("Struct %s size=%llu\n", s->name, (unsigned long long)s->size);
        for (size_t j = 0; j < s->field_count; j++) {
            StructField *f = &s->fields[j];
            printf("  %-12s offset=%llu  size=%llu  type=%s",
                   f->name, (unsigned long long)f->offset,
                   (unsigned long long)f->size, field_type_name(f->type));
            if (f->type == FIELD_BYTES)
                printf(" %llu", (unsigned long long)f->size);
            putchar('\n');
        }
    }
}

static const char *elf_section_type(Assembler *as, SectionId sec)
{
    (void)as;
    (void)sec;
    return "PROGBITS";
}

static const char *elf_section_flags(SectionId sec)
{
    if (sec == SEC_TEXT)
        return "ALLOC|EXEC";
    if (sec == SEC_RODATA)
        return "ALLOC";
    if (sec == SEC_DATA)
        return "ALLOC|WRITE";
    return "";
}

static const char *elf_reloc_name(RelocKind kind)
{
    return kind == RELOC_64 ? "R_X86_64_64" : "R_X86_64_PC32";
}

static int elf_reloc_count(Assembler *as, SectionId sec)
{
    int n = 0;
    for (size_t i = 0; i < as->relocs.len; i++)
        if (as->relocs.items[i].section == sec)
            n++;
    return n;
}

static uint64_t object_aux_size(Assembler *as, const char *kind, int sec)
{
    if (strcmp(kind, "rela") == 0)
        return (uint64_t)elf_reloc_count(as, (SectionId)sec) * 24;
    if (strcmp(kind, "symtab") == 0)
        return (uint64_t)(4 + as->symbols.len) * 24;
    if (strcmp(kind, "strtab") == 0) {
        uint64_t n = 1;
        for (size_t i = 0; i < as->symbols.len; i++)
            n += strlen(as->symbols.items[i].name) + 1;
        return n;
    }
    if (strcmp(kind, "shstrtab") == 0) {
        uint64_t n = 1;
        const char *base[] = { ".text", ".rodata", ".data", ".symtab", ".strtab", ".shstrtab" };
        for (size_t i = 0; i < sizeof(base) / sizeof(base[0]); i++)
            n += strlen(base[i]) + 1;
        if (elf_reloc_count(as, SEC_TEXT)) n += strlen(".rela.text") + 1;
        if (elf_reloc_count(as, SEC_RODATA)) n += strlen(".rela.rodata") + 1;
        if (elf_reloc_count(as, SEC_DATA)) n += strlen(".rela.data") + 1;
        return n;
    }
    return 0;
}

static void print_symbol_info(Assembler *as)
{
    puts("\nSymbols:");
    if (!as->symbols.len) {
        puts("  <none>");
        return;
    }
    for (size_t i = 0; i < as->symbols.len; i++) {
        Symbol *s = &as->symbols.items[i];
        const char *bind = s->is_extern ? "GLOBAL" : (s->is_global ? "GLOBAL" : "LOCAL");
        const char *type = s->is_const ? "ABS" : (s->is_extern ? "UND" : "NOTYPE");
        const char *sec = s->is_const ? "ABS" : (s->is_extern ? "UND" : kasm_section_name(s->section));
        uint64_t value = s->is_extern ? 0 :
            (s->is_const || s->section == SEC_NONE ? (uint64_t)s->value :
             (as->object_mode ? s->offset : as->sections[s->section].vaddr + s->offset));
        printf("  %-16s section=%s value=0x%llx size=0 bind=%s type=%s line=%d\n",
               s->name, sec, (unsigned long long)value, bind, type, s->line);
    }
}

static void print_reloc_info(Assembler *as)
{
    puts("\nRelocations:");
    if (!as->relocs.len) {
        puts("  <none>");
        return;
    }
    for (size_t i = 0; i < as->relocs.len; i++) {
        Reloc *r = &as->relocs.items[i];
        printf("  %s off=0x%llx type=%s symbol=%s addend=%lld source=%d:%d\n",
               kasm_section_name(r->section), (unsigned long long)r->offset,
               elf_reloc_name(r->kind), r->symbol, (long long)r->addend,
               r->line, r->column);
    }
}

static void print_executable_elf_info(Assembler *as, const char *output)
{
    uint64_t base = 0x400000;
    uint16_t phnum = as->tiny ? 1 : 3;
    uint64_t file_align = as->tiny ? 1 : 0x1000;
    uint64_t text_off = kasm_align(64 + (uint64_t)phnum * 56, file_align);
    uint64_t ro_off = kasm_align(text_off + as->sections[SEC_TEXT].len, file_align);
    uint64_t data_off = kasm_align(ro_off + as->sections[SEC_RODATA].len, file_align);
    Symbol *entry = kasm_symbol_find(&as->symbols, as->entry ? as->entry : "");
    uint64_t entry_addr = entry && !entry->is_const ?
        as->sections[entry->section].vaddr + entry->offset : 0;

    printf("ELF64 executable: %s\n", output ? output : "<not written>");
    printf("entry: 0x%llx (%s)\n", (unsigned long long)entry_addr,
           as->entry ? as->entry : "<none>");
    puts("Header:");
    puts("  class: ELF64");
    puts("  endian: little");
    puts("  type: executable");
    puts("  machine: x86-64");
    printf("  entry point: 0x%llx\n", (unsigned long long)entry_addr);
    puts("  program header offset: 0x40");
    printf("  program header count: %u\n", phnum);
    puts("  section header offset: 0x0");
    puts("  section header count: 0");

    puts("\nProgram headers:");
    if (as->tiny) {
        uint64_t filesz = data_off + as->sections[SEC_DATA].len;
        printf("  LOAD off=0x%06llx vaddr=0x%llx filesz=0x%llx memsz=0x%llx flags=R|W|X align=0x1000\n",
               0ull, (unsigned long long)base, (unsigned long long)filesz,
               (unsigned long long)filesz);
    } else {
        printf("  LOAD off=0x%06llx vaddr=0x%llx filesz=0x%llx memsz=0x%llx flags=R|X align=0x1000\n",
               (unsigned long long)text_off, (unsigned long long)as->sections[SEC_TEXT].vaddr,
               (unsigned long long)as->sections[SEC_TEXT].len,
               (unsigned long long)as->sections[SEC_TEXT].len);
        printf("  LOAD off=0x%06llx vaddr=0x%llx filesz=0x%llx memsz=0x%llx flags=R align=0x1000\n",
               (unsigned long long)ro_off, (unsigned long long)as->sections[SEC_RODATA].vaddr,
               (unsigned long long)as->sections[SEC_RODATA].len,
               (unsigned long long)as->sections[SEC_RODATA].len);
        printf("  LOAD off=0x%06llx vaddr=0x%llx filesz=0x%llx memsz=0x%llx flags=R|W align=0x1000\n",
               (unsigned long long)data_off, (unsigned long long)as->sections[SEC_DATA].vaddr,
               (unsigned long long)as->sections[SEC_DATA].len,
               (unsigned long long)as->sections[SEC_DATA].len);
    }

    puts("\nSections:");
    printf("  [1] .text type=%s flags=%s off=0x%llx vaddr=0x%llx size=0x%llx align=16\n",
           elf_section_type(as, SEC_TEXT), elf_section_flags(SEC_TEXT),
           (unsigned long long)text_off, (unsigned long long)as->sections[SEC_TEXT].vaddr,
           (unsigned long long)as->sections[SEC_TEXT].len);
    printf("  [2] .rodata type=%s flags=%s off=0x%llx vaddr=0x%llx size=0x%llx align=8\n",
           elf_section_type(as, SEC_RODATA), elf_section_flags(SEC_RODATA),
           (unsigned long long)ro_off, (unsigned long long)as->sections[SEC_RODATA].vaddr,
           (unsigned long long)as->sections[SEC_RODATA].len);
    printf("  [3] .data type=%s flags=%s off=0x%llx vaddr=0x%llx size=0x%llx align=8\n",
           elf_section_type(as, SEC_DATA), elf_section_flags(SEC_DATA),
           (unsigned long long)data_off, (unsigned long long)as->sections[SEC_DATA].vaddr,
           (unsigned long long)as->sections[SEC_DATA].len);

    print_symbol_info(as);
    print_reloc_info(as);
    puts("\nEntry point explanation:");
    if (entry && !entry->is_const)
        printf("  The loader starts at 0x%llx, label %s in %s offset 0x%llx from source line %d.\n",
               (unsigned long long)entry_addr, entry->name, kasm_section_name(entry->section),
               (unsigned long long)entry->offset, entry->line);
    else
        puts("  No resolved entry label is available.");
}

static void print_object_elf_info(Assembler *as, const char *output)
{
    int have_rela_text = elf_reloc_count(as, SEC_TEXT) > 0;
    int have_rela_rodata = elf_reloc_count(as, SEC_RODATA) > 0;
    int have_rela_data = elf_reloc_count(as, SEC_DATA) > 0;
    int shnum = 7 + have_rela_text + have_rela_rodata + have_rela_data;
    uint64_t off = 64;
    uint64_t text_off = kasm_align(off, 16);
    off = text_off + as->sections[SEC_TEXT].len;
    uint64_t ro_off = kasm_align(off, 8);
    off = ro_off + as->sections[SEC_RODATA].len;
    uint64_t data_off = kasm_align(off, 8);
    off = data_off + as->sections[SEC_DATA].len;

    printf("ELF64 object: %s\n", output ? output : "<not written>");
    puts("entry: 0x0 (<relocatable object>)");
    puts("Header:");
    puts("  class: ELF64");
    puts("  endian: little");
    puts("  type: object");
    puts("  machine: x86-64");
    puts("  entry point: 0x0");
    puts("  program header offset: 0x0");
    puts("  program header count: 0");

    puts("\nProgram headers:");
    puts("  <none>");

    puts("\nSections:");
    printf("  [1] .text type=PROGBITS flags=ALLOC|EXEC off=0x%llx vaddr=0x0 size=0x%llx align=16\n",
           (unsigned long long)text_off, (unsigned long long)as->sections[SEC_TEXT].len);
    printf("  [2] .rodata type=PROGBITS flags=ALLOC off=0x%llx vaddr=0x0 size=0x%llx align=8\n",
           (unsigned long long)ro_off, (unsigned long long)as->sections[SEC_RODATA].len);
    printf("  [3] .data type=PROGBITS flags=ALLOC|WRITE off=0x%llx vaddr=0x0 size=0x%llx align=8\n",
           (unsigned long long)data_off, (unsigned long long)as->sections[SEC_DATA].len);
    int idx = 4;
    if (have_rela_text) {
        off = kasm_align(off, 8);
        printf("  [%d] .rela.text type=RELA flags= off=0x%llx vaddr=0x0 size=0x%llx align=8\n",
               idx++, (unsigned long long)off, (unsigned long long)object_aux_size(as, "rela", SEC_TEXT));
        off += object_aux_size(as, "rela", SEC_TEXT);
    }
    if (have_rela_rodata) {
        off = kasm_align(off, 8);
        printf("  [%d] .rela.rodata type=RELA flags= off=0x%llx vaddr=0x0 size=0x%llx align=8\n",
               idx++, (unsigned long long)off, (unsigned long long)object_aux_size(as, "rela", SEC_RODATA));
        off += object_aux_size(as, "rela", SEC_RODATA);
    }
    if (have_rela_data) {
        off = kasm_align(off, 8);
        printf("  [%d] .rela.data type=RELA flags= off=0x%llx vaddr=0x0 size=0x%llx align=8\n",
               idx++, (unsigned long long)off, (unsigned long long)object_aux_size(as, "rela", SEC_DATA));
        off += object_aux_size(as, "rela", SEC_DATA);
    }
    off = kasm_align(off, 8);
    printf("  [%d] .symtab type=SYMTAB flags= off=0x%llx vaddr=0x0 size=0x%llx align=8\n",
           idx++, (unsigned long long)off, (unsigned long long)object_aux_size(as, "symtab", 0));
    off += object_aux_size(as, "symtab", 0);
    printf("  [%d] .strtab type=STRTAB flags= off=0x%llx vaddr=0x0 size=0x%llx align=1\n",
           idx++, (unsigned long long)off, (unsigned long long)object_aux_size(as, "strtab", 0));
    off += object_aux_size(as, "strtab", 0);
    printf("  [%d] .shstrtab type=STRTAB flags= off=0x%llx vaddr=0x0 size=0x%llx align=1\n",
           idx++, (unsigned long long)off, (unsigned long long)object_aux_size(as, "shstrtab", 0));
    off += object_aux_size(as, "shstrtab", 0);
    off = kasm_align(off, 8);
    printf("\n  section header offset: 0x%llx\n", (unsigned long long)off);
    printf("  section header count: %d\n", shnum);

    print_symbol_info(as);
    print_reloc_info(as);
    puts("\nEntry point explanation:");
    puts("  Relocatable objects do not have a loader entry point; the linker chooses one later.");
}

static void print_elf_info(Assembler *as, const char *output, const char *format)
{
    if (strcmp(format, "bin") == 0) {
        puts("ELF info unavailable for raw binary output.");
        return;
    }
    if (as->object_mode)
        print_object_elf_info(as, output);
    else
        print_executable_elf_info(as, output);
}

static int starts_with_word_ci(const char *s, const char *word)
{
    while (*s && isspace((unsigned char)*s))
        s++;
    while (*word) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*word))
            return 0;
        s++;
        word++;
    }
    return *s == 0 || isspace((unsigned char)*s);
}

static int source_looks_syscall_sugar(const char *source)
{
    return source && starts_with_word_ci(source, "syscall") && strchr(source, ',') != NULL;
}

static const char *linux_syscall_name_from_number(const char *n)
{
    if (strcmp(n, "0") == 0) return "read";
    if (strcmp(n, "1") == 0) return "write";
    if (strcmp(n, "2") == 0) return "open";
    if (strcmp(n, "3") == 0) return "close";
    if (strcmp(n, "9") == 0) return "mmap";
    if (strcmp(n, "11") == 0) return "munmap";
    if (strcmp(n, "12") == 0) return "brk";
    if (strcmp(n, "60") == 0) return "exit";
    if (strcmp(n, "231") == 0) return "exit_group";
    if (strcmp(n, "257") == 0) return "openat";
    return NULL;
}

static const char *instruction_meaning(Statement *st)
{
    if (strcmp(st->op, "mov") == 0 && st->operand_count == 2) {
        if (kasm_streq_ci(st->operands[0].text, "rax")) {
            const char *sys = linux_syscall_name_from_number(st->operands[1].text);
            if (sys)
                return "choose a Linux syscall number in rax";
        }
        return "copy a value into the destination operand";
    }
    if (strcmp(st->op, "lea") == 0)
        return "compute an address, often a label address, without loading memory";
    if (strcmp(st->op, "syscall") == 0)
        return "enter the Linux kernel using the syscall ABI";
    if (strcmp(st->op, "xor") == 0 && st->operand_count == 2 &&
        kasm_streq_ci(st->operands[0].text, st->operands[1].text))
        return "zero a register by XORing it with itself";
    if (strcmp(st->op, "jmp") == 0)
        return "jump to another label";
    if (st->op[0] == 'j' && strcmp(st->op, "jmp") != 0)
        return "branch to a label if the current flags match the condition";
    if (strcmp(st->op, "call") == 0)
        return "call a label and push the return address";
    if (strcmp(st->op, "ret") == 0)
        return "return to the address on top of the stack";
    if (strcmp(st->op, "cmp") == 0)
        return "compare operands and update CPU flags";
    if (strcmp(st->op, "add") == 0 || strcmp(st->op, "sub") == 0 ||
        strcmp(st->op, "and") == 0 || strcmp(st->op, "or") == 0 ||
        strcmp(st->op, "test") == 0 || strcmp(st->op, "inc") == 0 ||
        strcmp(st->op, "dec") == 0)
        return "perform an arithmetic or flag-setting operation";
    return "not yet explained";
}

static void print_bytes_for_statement(Assembler *as, Statement *st)
{
    if (st->section == SEC_NONE || st->section >= SEC_COUNT) {
        puts("Encoding: <none>");
        return;
    }
    ByteBuf *b = &as->sections[st->section];
    if (st->offset >= b->len || st->size == 0) {
        puts("Encoding: <none>");
        return;
    }
    size_t n = st->size;
    if (st->offset + n > b->len)
        n = b->len - st->offset;
    printf("Encoding:");
    for (size_t i = 0; i < n; i++)
        printf(" %02x", b->data[st->offset + i]);
    putchar('\n');
}

static void print_register_notes(Statement *st)
{
    if (st->operand_count > 0 && st->operands[0].text)
        printf("Registers affected: %s\n", st->operands[0].text);
    else if (strcmp(st->op, "syscall") == 0)
        puts("Registers affected: rax receives the syscall return value; rcx and r11 are clobbered by syscall");
    else
        puts("Registers affected: not yet explained");
}

static void print_label_reference_notes(Assembler *as, Statement *st)
{
    for (int i = 0; i < st->operand_count; i++) {
        const char *op = st->operands[i].text;
        char name[128];
        const char *start = NULL;
        if (strncmp(op, "[rel ", 5) == 0) {
            start = op + 5;
            size_t len = strcspn(start, "]");
            if (len >= sizeof(name))
                len = sizeof(name) - 1;
            memcpy(name, start, len);
            name[len] = 0;
        } else if ((*op == '_' || *op == '.' || isalpha((unsigned char)*op)) && !strchr(op, '[')) {
            size_t len = strlen(op);
            if (len >= sizeof(name))
                len = sizeof(name) - 1;
            memcpy(name, op, len);
            name[len] = 0;
        } else {
            continue;
        }
        Symbol *sym = kasm_symbol_find(&as->symbols, name);
        if (sym && sym->defined && !sym->is_const) {
            uint64_t value = as->object_mode ? sym->offset : as->sections[sym->section].vaddr + sym->offset;
            printf("Reference: %s resolves to %s+0x%llx (0x%llx here).\n",
                   name, kasm_section_name(sym->section), (unsigned long long)sym->offset,
                   (unsigned long long)value);
        }
    }
}

static int count_data_labels(Assembler *as)
{
    int n = 0;
    for (size_t i = 0; i < as->symbols.len; i++) {
        Symbol *s = &as->symbols.items[i];
        if (s->defined && !s->is_const && (s->section == SEC_RODATA || s->section == SEC_DATA))
            n++;
    }
    return n;
}

static void print_syscalls_used(Assembler *as)
{
    int any = 0;
    int last_sugar_line = -1;
    for (size_t i = 0; i < as->program.len; i++) {
        Statement *st = &as->program.items[i];
        if (st->type != ST_INSTR)
            continue;
        if (source_looks_syscall_sugar(st->source)) {
            if (st->line == last_sugar_line)
                continue;
            last_sugar_line = st->line;
            char tmp[128];
            const char *p = strstr(st->source, "syscall");
            if (!p)
                p = strstr(st->source, "SYSCALL");
            if (p) {
                p += 7;
                while (*p && isspace((unsigned char)*p))
                    p++;
                size_t len = strcspn(p, ", \t");
                if (len >= sizeof(tmp))
                    len = sizeof(tmp) - 1;
                memcpy(tmp, p, len);
                tmp[len] = 0;
                if (tmp[0]) {
                    printf("%s%s", any ? ", " : "", tmp);
                    any = 1;
                }
            }
        } else if (strcmp(st->op, "mov") == 0 && st->operand_count == 2 &&
                   kasm_streq_ci(st->operands[0].text, "rax")) {
            const char *name = linux_syscall_name_from_number(st->operands[1].text);
            if (name) {
                printf("%s%s", any ? ", " : "", name);
                any = 1;
            }
        }
    }
    if (!any)
        printf("<none detected>");
    putchar('\n');
}

static void print_teaching_mode(Assembler *as, const char *output, const char *format)
{
    int instruction_count = 0;
    for (size_t i = 0; i < as->program.len; i++)
        if (as->program.items[i].type == ST_INSTR)
            instruction_count++;
    Symbol *entry = kasm_symbol_find(&as->symbols, as->entry ? as->entry : "");

    printf("Teaching mode: %s\n", as->path ? as->path : "<input>");
    if (as->teach_level == 1)
        puts("Level: beginner");
    else if (as->teach_level == 3)
        puts("Level: deep");
    else
        puts("Level: intermediate");
    puts("This walkthrough explains the generated code, Linux syscall ABI, and ELF layout.\n");

    puts("Program overview:");
    printf("- entry label: %s\n", as->entry ? as->entry : "<none>");
    printf("- sections: .text=0x%llx bytes, .rodata=0x%llx bytes, .data=0x%llx bytes\n",
           (unsigned long long)as->sections[SEC_TEXT].len,
           (unsigned long long)as->sections[SEC_RODATA].len,
           (unsigned long long)as->sections[SEC_DATA].len);
    printf("- instructions: %d\n", instruction_count);
    printf("- data labels: %d\n", count_data_labels(as));
    printf("- syscalls used: ");
    print_syscalls_used(as);

    puts("\nSource walkthrough:");
    int step = 1;
    int last_sugar_line = -1;
    for (size_t i = 0; i < as->program.len; i++) {
        Statement *st = &as->program.items[i];
        if (st->type == ST_SECTION) {
            printf("\n[%d] Section %s\n", step++, st->name);
            puts(st->section == SEC_TEXT ? "Code lives here." :
                 st->section == SEC_RODATA ? "Read-only data lives here." :
                 "Writable data lives here.");
            if (entry && st->section == entry->section)
                printf("The CPU starts at %s because this label is used as the ELF entry point.\n", entry->name);
        } else if (st->type == ST_LABEL) {
            printf("\n[%d] Label %s\n", step++, st->name);
            printf("Meaning: names offset 0x%llx in %s.\n",
                   (unsigned long long)st->offset, kasm_section_name(st->section));
        } else if (st->type == ST_DATA) {
            printf("\n[%d] Data %s\n", step++, st->source ? st->source : st->op);
            puts("Meaning: emit bytes into the current data section.");
            if (as->teach_level != 1)
                print_bytes_for_statement(as, st);
        } else if (st->type == ST_INSTR) {
            if (source_looks_syscall_sugar(st->source) && st->line != last_sugar_line) {
                printf("\n[%d] Syscall sugar: %s\n", step++, st->source);
                puts("Meaning: KASM expands this friendly syscall form into register setup plus a raw syscall instruction.");
                last_sugar_line = st->line;
            }
            printf("\n[%d] %s", step++, st->op);
            for (int j = 0; j < st->operand_count; j++)
                printf("%s %s", j == 0 ? "" : ",", st->operands[j].text);
            putchar('\n');
            printf("Meaning: %s.\n", instruction_meaning(st));
            print_register_notes(st);
            print_label_reference_notes(as, st);
            if (as->teach_level != 1)
                print_bytes_for_statement(as, st);
            if (as->teach_level == 3)
                printf("Trace: source line %d, section %s, offset 0x%llx, size %u.\n",
                       st->line, kasm_section_name(st->section),
                       (unsigned long long)st->offset, st->size);
        }
    }

    puts("\nRegister / ABI notes:");
    puts("- rax contains the Linux syscall number before syscall.");
    puts("- rdi, rsi, rdx, r10, r8, and r9 contain syscall arguments 1 through 6.");
    puts("- after syscall, rax contains the return value or a negative errno.");

    puts("\nELF overview:");
    printf("- output type: %s\n", strcmp(format, "bin") == 0 ? "raw binary" :
           (as->object_mode ? "ELF64 relocatable object" : "ELF64 executable"));
    if (!as->object_mode && strcmp(format, "bin") != 0 && entry)
        printf("- entry point: %s at 0x%llx\n", entry->name,
               (unsigned long long)(as->sections[entry->section].vaddr + entry->offset));
    printf("- .text address: 0x%llx\n", (unsigned long long)as->sections[SEC_TEXT].vaddr);
    printf("- .data address: 0x%llx\n", (unsigned long long)as->sections[SEC_DATA].vaddr);
    if (as->teach_level == 3 && strcmp(format, "bin") != 0)
        print_elf_info(as, output, format);

    puts("\nFinal run note:");
    if (!as->object_mode && strcmp(format, "elf64") == 0 && output)
        printf("- run it with: ./%s\n", output);
    else
        puts("- this output is not directly runnable; link object files with ld or choose executable output.");
}

static void write_map_file(Assembler *as, const char *format)
{
    if (!as->map_path)
        return;
    FILE *f = fopen(as->map_path, "wb");
    if (!f) {
        kasm_error(as, (SourceLoc){ as->map_path, 1, 1 }, "cannot write map file");
        return;
    }
    fprintf(f, "KASM map file\n");
    fprintf(f, "Format: %s\n", format);
    fprintf(f, "Entry: %s\n\n", as->entry ? as->entry : "<none>");
    fprintf(f, "Sections:\n");
    fprintf(f, "  .text    vaddr=0x%08llX size=0x%08llX flags=RX\n",
            (unsigned long long)as->sections[SEC_TEXT].vaddr,
            (unsigned long long)as->sections[SEC_TEXT].len);
    fprintf(f, "  .rodata  vaddr=0x%08llX size=0x%08llX flags=R\n",
            (unsigned long long)as->sections[SEC_RODATA].vaddr,
            (unsigned long long)as->sections[SEC_RODATA].len);
    fprintf(f, "  .data    vaddr=0x%08llX size=0x%08llX flags=RW\n\n",
            (unsigned long long)as->sections[SEC_DATA].vaddr,
            (unsigned long long)as->sections[SEC_DATA].len);
    fprintf(f, "Symbols:\n");
    for (size_t i = 0; i < as->symbols.len; i++) {
        Symbol *s = &as->symbols.items[i];
        uint64_t addr = s->is_extern ? 0 :
                        (s->is_const || s->section == SEC_NONE ? (uint64_t)s->value :
                         (as->object_mode ? s->offset : as->sections[s->section].vaddr + s->offset));
        fprintf(f, "  %-16s %s+0x%llX  0x%llX  %s\n", s->name,
                kasm_section_name(s->section), (unsigned long long)s->offset,
                (unsigned long long)addr,
                s->is_extern ? "extern" : (s->is_global ? "global" : "local"));
    }
    if (as->relocs.len) {
        fprintf(f, "\nRelocations:\n");
        for (size_t i = 0; i < as->relocs.len; i++) {
            Reloc *r = &as->relocs.items[i];
            fprintf(f, "  %s offset=0x%llX type=%s symbol=%s addend=%lld\n",
                    kasm_section_name(r->section), (unsigned long long)r->offset,
                    r->kind == RELOC_PC32 ? "R_X86_64_PC32" : "R_X86_64_64",
                    r->symbol, (long long)r->addend);
        }
    }
    if (as->structs.len) {
        fprintf(f, "\nStructs:\n");
        for (size_t i = 0; i < as->structs.len; i++)
            fprintf(f, "  %s size=%llu\n", as->structs.items[i].name,
                    (unsigned long long)as->structs.items[i].size);
    }
    fclose(f);
}

typedef struct {
    char *project_name;
    char *project_type;
    char **sources;
    size_t source_count;
    size_t source_cap;
    char *out_dir;
    char *output;
    char *linker;
    char *entry;
} ProjectConfig;

static void project_config_free(ProjectConfig *cfg)
{
    free(cfg->project_name);
    free(cfg->project_type);
    for (size_t i = 0; i < cfg->source_count; i++)
        free(cfg->sources[i]);
    free(cfg->sources);
    free(cfg->out_dir);
    free(cfg->output);
    free(cfg->linker);
    free(cfg->entry);
}

static char *path_dirname_dup(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    if (!slash || (bslash && bslash > slash))
        slash = bslash;
    if (!slash)
        return kasm_xstrdup(".");
    size_t len = (size_t)(slash - path);
    if (len == 0)
        len = 1;
    char *out = kasm_xrealloc(NULL, len + 1);
    memcpy(out, path, len);
    out[len] = 0;
    return out;
}

static char *path_join_dup(const char *a, const char *b)
{
    if (!b || !*b)
        return kasm_xstrdup(a);
    if (b[0] == '/' || (strlen(b) > 2 && b[1] == ':'))
        return kasm_xstrdup(b);
    if (strcmp(a, ".") == 0)
        return kasm_xstrdup(b);
    size_t alen = strlen(a);
    size_t blen = strlen(b);
    int need_slash = alen > 0 && a[alen - 1] != '/' && a[alen - 1] != '\\';
    char *out = kasm_xrealloc(NULL, alen + (need_slash ? 1 : 0) + blen + 1);
    memcpy(out, a, alen);
    size_t pos = alen;
    if (need_slash)
        out[pos++] = '/';
    memcpy(out + pos, b, blen + 1);
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

static int ensure_dir(const char *path)
{
#ifdef _WIN32
    (void)path;
    fprintf(stderr, "error: kasm build is currently Linux-first and cannot create directories on this platform\n");
    return 0;
#else
    if (mkdir(path, 0777) == 0)
        return 1;
    if (errno == EEXIST)
        return 1;
    fprintf(stderr, "error: cannot create output directory '%s'\n", path);
    return 0;
#endif
}

static char *parse_quoted_value(char *s)
{
    s = kasm_trim(s);
    if (*s != '"')
        return NULL;
    s++;
    char *end = strchr(s, '"');
    if (!end || *kasm_trim(end + 1) != 0)
        return NULL;
    *end = 0;
    return kasm_xstrdup(s);
}

static int add_project_source(ProjectConfig *cfg, const char *src)
{
    if (cfg->source_count == cfg->source_cap) {
        cfg->source_cap = cfg->source_cap ? cfg->source_cap * 2 : 4;
        cfg->sources = kasm_xrealloc(cfg->sources, cfg->source_cap * sizeof(char *));
    }
    cfg->sources[cfg->source_count++] = kasm_xstrdup(src);
    return 1;
}

static int parse_sources_array(ProjectConfig *cfg, char *value)
{
    char *s = kasm_trim(value);
    if (*s != '[')
        return 0;
    s++;
    while (1) {
        s = kasm_trim(s);
        if (*s == ']') {
            s++;
            return *kasm_trim(s) == 0;
        }
        if (*s != '"')
            return 0;
        s++;
        char *end = strchr(s, '"');
        if (!end)
            return 0;
        *end = 0;
        add_project_source(cfg, s);
        s = kasm_trim(end + 1);
        if (*s == ',') {
            s++;
            continue;
        }
        if (*s == ']') {
            s++;
            return *kasm_trim(s) == 0;
        }
        return 0;
    }
}

static int set_string_field(char **field, char *value)
{
    char *parsed = parse_quoted_value(value);
    if (!parsed)
        return 0;
    free(*field);
    *field = parsed;
    return 1;
}

static int parse_project_config(const char *path, ProjectConfig *cfg)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: missing config '%s'\n", path);
        return 0;
    }
    char line[4096];
    enum { SEC_NONE_CFG, SEC_PROJECT_CFG, SEC_BUILD_CFG } section = SEC_NONE_CFG;
    int line_no = 0;
    while (fgets(line, sizeof(line), f)) {
        line_no++;
        char *comment = strchr(line, '#');
        if (comment)
            *comment = 0;
        char *s = kasm_trim(line);
        if (*s == 0)
            continue;
        if (strcmp(s, "[project]") == 0) {
            section = SEC_PROJECT_CFG;
            continue;
        }
        if (strcmp(s, "[build]") == 0) {
            section = SEC_BUILD_CFG;
            continue;
        }
        if (*s == '[') {
            fclose(f);
            fprintf(stderr, "%s:%d:1: error: malformed config: unknown section '%s'\n", path, line_no, s);
            return 0;
        }
        char *eq = strchr(s, '=');
        if (!eq) {
            fclose(f);
            fprintf(stderr, "%s:%d:1: error: malformed config: expected key = value\n", path, line_no);
            return 0;
        }
        *eq = 0;
        char *key = kasm_trim(s);
        char *value = kasm_trim(eq + 1);
        int ok = 0;
        if (section == SEC_PROJECT_CFG) {
            if (strcmp(key, "name") == 0)
                ok = set_string_field(&cfg->project_name, value);
            else if (strcmp(key, "type") == 0)
                ok = set_string_field(&cfg->project_type, value);
        } else if (section == SEC_BUILD_CFG) {
            if (strcmp(key, "sources") == 0)
                ok = parse_sources_array(cfg, value);
            else if (strcmp(key, "out_dir") == 0)
                ok = set_string_field(&cfg->out_dir, value);
            else if (strcmp(key, "output") == 0)
                ok = set_string_field(&cfg->output, value);
            else if (strcmp(key, "linker") == 0)
                ok = set_string_field(&cfg->linker, value);
            else if (strcmp(key, "entry") == 0)
                ok = set_string_field(&cfg->entry, value);
        }
        if (!ok) {
            fclose(f);
            fprintf(stderr, "%s:%d:1: error: malformed config near key '%s'\n", path, line_no, key);
            return 0;
        }
    }
    fclose(f);
    if (!cfg->project_name || !cfg->project_type || !cfg->out_dir ||
        !cfg->output || cfg->source_count == 0) {
        fprintf(stderr, "error: missing required fields in '%s'\n", path);
        fprintf(stderr, "hint: required fields are project.name, project.type, build.sources, build.out_dir, and build.output\n");
        return 0;
    }
    if (strcmp(cfg->project_type, "executable") != 0) {
        fprintf(stderr, "error: unsupported project type '%s'\n", cfg->project_type);
        fprintf(stderr, "hint: KASM project build currently supports project.type = \"executable\"\n");
        return 0;
    }
    if (!cfg->linker)
        cfg->linker = kasm_xstrdup("ld");
    if (!cfg->entry)
        cfg->entry = kasm_xstrdup("_start");
    return 1;
}

static char *object_path_for_source(const char *out_dir, const char *source)
{
    char *obj = default_output_path(source, "elf64-obj", 1);
    char *out = path_join_dup(out_dir, obj);
    free(obj);
    return out;
}

static char *shell_quote_dup(const char *s)
{
    size_t len = 2;
    for (const char *p = s; *p; p++)
        len += *p == '\'' ? 4 : 1;
    char *out = kasm_xrealloc(NULL, len + 1);
    char *q = out;
    *q++ = '\'';
    for (const char *p = s; *p; p++) {
        if (*p == '\'') {
            memcpy(q, "'\\''", 4);
            q += 4;
        } else {
            *q++ = *p;
        }
    }
    *q++ = '\'';
    *q = 0;
    return out;
}

static int append_cmd(char **cmd, size_t *len, size_t *cap, const char *part)
{
    size_t n = strlen(part);
    if (*len + n + 1 > *cap) {
        size_t next = *cap ? *cap * 2 : 128;
        while (next < *len + n + 1)
            next *= 2;
        *cmd = kasm_xrealloc(*cmd, next);
        *cap = next;
    }
    memcpy(*cmd + *len, part, n + 1);
    *len += n;
    return 1;
}

typedef struct {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} InspectEhdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} InspectPhdr;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} InspectShdr;

typedef struct {
    uint32_t st_name;
    unsigned char st_info;
    unsigned char st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} InspectSym;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
} InspectRela;

static const char *inspect_section_name(const char *names, size_t names_len, uint32_t off)
{
    if (!names || off >= names_len)
        return "<bad-name>";
    return names + off;
}

static const char *inspect_symbol_name(const char *names, size_t names_len, uint32_t off)
{
    if (!names || off >= names_len)
        return "";
    return names + off;
}

static const char *inspect_bind(unsigned char info)
{
    unsigned char bind = (unsigned char)(info >> 4);
    if (bind == 0) return "local";
    if (bind == 1) return "global";
    if (bind == 2) return "weak";
    return "other";
}

static const char *inspect_sym_type(unsigned char info)
{
    unsigned char type = (unsigned char)(info & 0xf);
    if (type == 0) return "notype";
    if (type == 1) return "object";
    if (type == 2) return "func";
    if (type == 3) return "section";
    if (type == 4) return "file";
    return "other";
}

static const char *inspect_reloc_type(uint32_t type)
{
    if (type == 1) return "R_X86_64_64";
    if (type == 2) return "R_X86_64_PC32";
    if (type == 10) return "R_X86_64_32";
    if (type == 11) return "R_X86_64_32S";
    return "R_X86_64_UNKNOWN";
}

static void inspect_flags(uint64_t flags, char *out, size_t out_len)
{
    snprintf(out, out_len, "%s%s%s",
             (flags & 2) ? "A" : "-",
             (flags & 1) ? "W" : "-",
             (flags & 4) ? "X" : "-");
}

static int read_exact(FILE *f, uint64_t off, void *buf, size_t n)
{
    if (fseek(f, (long)off, SEEK_SET) != 0)
        return 0;
    return fread(buf, 1, n, f) == n;
}

static int range_ok(uint64_t off, uint64_t size, size_t file_size)
{
    return off <= (uint64_t)file_size && size <= (uint64_t)file_size - off;
}

static int read_file_image(const char *path, unsigned char **out, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: cannot read file '%s'\n", path);
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        fprintf(stderr, "error: cannot seek file '%s'\n", path);
        return 0;
    }
    long n = ftell(f);
    if (n < 0) {
        fclose(f);
        fprintf(stderr, "error: cannot size file '%s'\n", path);
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        fprintf(stderr, "error: cannot rewind file '%s'\n", path);
        return 0;
    }
    unsigned char *buf = kasm_xrealloc(NULL, (size_t)n ? (size_t)n : 1);
    if ((size_t)n && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        fprintf(stderr, "error: cannot read file '%s'\n", path);
        return 0;
    }
    fclose(f);
    *out = buf;
    *out_len = (size_t)n;
    return 1;
}

static int link_write_pad(FILE *f, uint64_t at, uint64_t target)
{
    static const unsigned char zero[4096] = { 0 };
    while (at < target) {
        uint64_t n = target - at;
        if (n > sizeof(zero))
            n = sizeof(zero);
        if (fwrite(zero, 1, (size_t)n, f) != n)
            return 0;
        at += n;
    }
    return 1;
}

static int link_write_section(FILE *f, uint64_t *pos, uint64_t off, ByteBuf *sec)
{
    if (!link_write_pad(f, *pos, off))
        return 0;
    *pos = off;
    if (sec->len && fwrite(sec->data, 1, sec->len, f) != sec->len)
        return 0;
    *pos += sec->len;
    return 1;
}

typedef struct {
    unsigned char *buf;
    size_t len;
    InspectEhdr eh;
    InspectPhdr *ph;
    InspectShdr *sh;
    const char *shstr;
    size_t shstr_len;
} ElfImage;

static const char *elf_type_name(uint16_t type)
{
    if (type == 1) return "REL";
    if (type == 2) return "EXEC";
    if (type == 3) return "DYN";
    return "OTHER";
}

static const char *ph_type_name(uint32_t type)
{
    if (type == 1) return "LOAD";
    if (type == 2) return "DYNAMIC";
    if (type == 3) return "INTERP";
    if (type == 4) return "NOTE";
    if (type == 6) return "PHDR";
    return "OTHER";
}

static const char *sh_type_name(uint32_t type)
{
    if (type == 0) return "NULL";
    if (type == 1) return "PROGBITS";
    if (type == 2) return "SYMTAB";
    if (type == 3) return "STRTAB";
    if (type == 4) return "RELA";
    if (type == 8) return "NOBITS";
    if (type == 9) return "REL";
    if (type == 11) return "DYNSYM";
    return "OTHER";
}

static void ph_flags(uint32_t flags, char *out, size_t out_len)
{
    snprintf(out, out_len, "%s%s%s",
             (flags & 4) ? "R" : "-",
             (flags & 2) ? "W" : "-",
             (flags & 1) ? "X" : "-");
}

static int load_elf_image(const char *path, ElfImage *elf)
{
    memset(elf, 0, sizeof(*elf));
    if (!read_file_image(path, &elf->buf, &elf->len))
        return 0;
    if (elf->len < sizeof(InspectEhdr)) {
        fprintf(stderr, "error: unsupported file type: file is too small for ELF64\n");
        return 0;
    }
    memcpy(&elf->eh, elf->buf, sizeof(elf->eh));
    if (elf->eh.e_ident[0] != 0x7f || elf->eh.e_ident[1] != 'E' ||
        elf->eh.e_ident[2] != 'L' || elf->eh.e_ident[3] != 'F') {
        fprintf(stderr, "error: unsupported file type: not an ELF file\n");
        return 0;
    }
    if (elf->eh.e_ident[4] != 2) {
        fprintf(stderr, "error: unsupported ELF class: expected ELF64\n");
        return 0;
    }
    if (elf->eh.e_ident[5] != 1) {
        fprintf(stderr, "error: unsupported ELF endianness: expected little-endian\n");
        return 0;
    }
    if (elf->eh.e_ehsize && elf->eh.e_ehsize != sizeof(InspectEhdr)) {
        fprintf(stderr, "error: malformed ELF header size\n");
        return 0;
    }
    if (elf->eh.e_phnum) {
        if (elf->eh.e_phentsize != sizeof(InspectPhdr) ||
            !range_ok(elf->eh.e_phoff, (uint64_t)elf->eh.e_phnum * sizeof(InspectPhdr), elf->len)) {
            fprintf(stderr, "error: malformed ELF program header table\n");
            return 0;
        }
        elf->ph = kasm_xrealloc(NULL, (size_t)elf->eh.e_phnum * sizeof(InspectPhdr));
        memcpy(elf->ph, elf->buf + elf->eh.e_phoff, (size_t)elf->eh.e_phnum * sizeof(InspectPhdr));
    }
    if (elf->eh.e_shnum) {
        if (elf->eh.e_shentsize != sizeof(InspectShdr) ||
            !range_ok(elf->eh.e_shoff, (uint64_t)elf->eh.e_shnum * sizeof(InspectShdr), elf->len)) {
            fprintf(stderr, "error: malformed ELF section header table\n");
            return 0;
        }
        elf->sh = kasm_xrealloc(NULL, (size_t)elf->eh.e_shnum * sizeof(InspectShdr));
        memcpy(elf->sh, elf->buf + elf->eh.e_shoff, (size_t)elf->eh.e_shnum * sizeof(InspectShdr));
        if (elf->eh.e_shstrndx < elf->eh.e_shnum &&
            range_ok(elf->sh[elf->eh.e_shstrndx].sh_offset, elf->sh[elf->eh.e_shstrndx].sh_size, elf->len)) {
            elf->shstr = (const char *)(elf->buf + elf->sh[elf->eh.e_shstrndx].sh_offset);
            elf->shstr_len = (size_t)elf->sh[elf->eh.e_shstrndx].sh_size;
        }
    }
    return 1;
}

static void free_elf_image(ElfImage *elf)
{
    free(elf->buf);
    free(elf->ph);
    free(elf->sh);
}

static void inspect_print_headers(ElfImage *elf)
{
    puts("ELF Header:");
    printf("class\tELF64\n");
    printf("endianness\tlittle\n");
    printf("type\t%s (%u)\n", elf_type_name(elf->eh.e_type), elf->eh.e_type);
    printf("machine\t%s (%u)\n", elf->eh.e_machine == 62 ? "x86-64" : "other", elf->eh.e_machine);
    printf("entry\t0x%llx\n", (unsigned long long)elf->eh.e_entry);
    printf("program_header_offset\t0x%llx\n", (unsigned long long)elf->eh.e_phoff);
    printf("section_header_offset\t0x%llx\n", (unsigned long long)elf->eh.e_shoff);
}

static void inspect_print_segments(ElfImage *elf)
{
    puts("Program Headers:");
    puts("type\tflags\tvaddr\tfile_offset\tfile_size\tmem_size\talignment");
    for (uint16_t i = 0; i < elf->eh.e_phnum; i++) {
        char flags[4];
        ph_flags(elf->ph[i].p_flags, flags, sizeof(flags));
        printf("%s\t%s\t0x%llx\t0x%llx\t%llu\t%llu\t%llu\n",
               ph_type_name(elf->ph[i].p_type), flags,
               (unsigned long long)elf->ph[i].p_vaddr,
               (unsigned long long)elf->ph[i].p_offset,
               (unsigned long long)elf->ph[i].p_filesz,
               (unsigned long long)elf->ph[i].p_memsz,
               (unsigned long long)elf->ph[i].p_align);
    }
}

static void inspect_print_sections(ElfImage *elf)
{
    puts("Sections:");
    puts("name\ttype\tflags\taddress\tfile_offset\tsize\talignment");
    if (!elf->sh) return;
    for (uint16_t i = 1; i < elf->eh.e_shnum; i++) {
        char flags[4];
        inspect_flags(elf->sh[i].sh_flags, flags, sizeof(flags));
        printf("%s\t%s\t%s\t0x%llx\t0x%llx\t%llu\t%llu\n",
               inspect_section_name(elf->shstr, elf->shstr_len, elf->sh[i].sh_name),
               sh_type_name(elf->sh[i].sh_type), flags,
               (unsigned long long)elf->sh[i].sh_addr,
               (unsigned long long)elf->sh[i].sh_offset,
               (unsigned long long)elf->sh[i].sh_size,
               (unsigned long long)elf->sh[i].sh_addralign);
    }
}

static int inspect_find_symbols(ElfImage *elf, InspectSym **symtab, size_t *sym_count,
                                const char **strtab, size_t *strtab_len)
{
    *symtab = NULL; *sym_count = 0; *strtab = NULL; *strtab_len = 0;
    if (!elf->sh) return 1;
    for (uint16_t i = 1; i < elf->eh.e_shnum; i++) {
        if ((elf->sh[i].sh_type == 2 || elf->sh[i].sh_type == 11) &&
            elf->sh[i].sh_entsize == sizeof(InspectSym)) {
            if (!range_ok(elf->sh[i].sh_offset, elf->sh[i].sh_size, elf->len)) {
                fprintf(stderr, "error: malformed ELF symbol table\n");
                return 0;
            }
            *symtab = (InspectSym *)(void *)(elf->buf + elf->sh[i].sh_offset);
            *sym_count = (size_t)(elf->sh[i].sh_size / elf->sh[i].sh_entsize);
            if (elf->sh[i].sh_link < elf->eh.e_shnum &&
                range_ok(elf->sh[elf->sh[i].sh_link].sh_offset, elf->sh[elf->sh[i].sh_link].sh_size, elf->len)) {
                *strtab = (const char *)(elf->buf + elf->sh[elf->sh[i].sh_link].sh_offset);
                *strtab_len = (size_t)elf->sh[elf->sh[i].sh_link].sh_size;
            }
            return 1;
        }
    }
    return 1;
}

static void inspect_print_symbols(ElfImage *elf)
{
    InspectSym *symtab = NULL;
    size_t sym_count = 0, strtab_len = 0;
    const char *strtab = NULL;
    puts("Symbols:");
    puts("name\tvalue\tsize\tbinding\ttype\tsection");
    if (!inspect_find_symbols(elf, &symtab, &sym_count, &strtab, &strtab_len))
        return;
    for (size_t i = 0; i < sym_count; i++) {
        InspectSym *s = &symtab[i];
        const char *sec = s->st_shndx == 0 ? "<undef>" :
            (s->st_shndx == 0xfff1 ? "<abs>" :
             (elf->sh && s->st_shndx < elf->eh.e_shnum ?
              inspect_section_name(elf->shstr, elf->shstr_len, elf->sh[s->st_shndx].sh_name) : "<bad-section>"));
        printf("%s\t0x%llx\t%llu\t%s\t%s\t%s\n",
               inspect_symbol_name(strtab, strtab_len, s->st_name),
               (unsigned long long)s->st_value,
               (unsigned long long)s->st_size,
               inspect_bind(s->st_info),
               inspect_sym_type(s->st_info),
               sec);
    }
}

static void inspect_print_relocs(ElfImage *elf)
{
    InspectSym *symtab = NULL;
    size_t sym_count = 0, strtab_len = 0;
    const char *strtab = NULL;
    puts("Relocations:");
    puts("section\toffset\ttype\tsymbol\taddend");
    if (!elf->sh || !inspect_find_symbols(elf, &symtab, &sym_count, &strtab, &strtab_len))
        return;
    for (uint16_t i = 1; i < elf->eh.e_shnum; i++) {
        if (elf->sh[i].sh_type != 4 || elf->sh[i].sh_entsize != sizeof(InspectRela))
            continue;
        if (!range_ok(elf->sh[i].sh_offset, elf->sh[i].sh_size, elf->len)) {
            fprintf(stderr, "error: malformed ELF relocation section\n");
            return;
        }
        InspectRela *rela = (InspectRela *)(void *)(elf->buf + elf->sh[i].sh_offset);
        size_t count = (size_t)(elf->sh[i].sh_size / elf->sh[i].sh_entsize);
        for (size_t j = 0; j < count; j++) {
            uint32_t sym_index = (uint32_t)(rela[j].r_info >> 32);
            uint32_t type = (uint32_t)(rela[j].r_info & 0xffffffffu);
            const char *sym = sym_index < sym_count ?
                inspect_symbol_name(strtab, strtab_len, symtab[sym_index].st_name) : "<bad-symbol>";
            printf("%s\t0x%llx\t%s\t%s\t%lld\n",
                   inspect_section_name(elf->shstr, elf->shstr_len, elf->sh[i].sh_name),
                   (unsigned long long)rela[j].r_offset,
                   inspect_reloc_type(type), sym, (long long)rela[j].r_addend);
        }
    }
}

static int run_inspect(int argc, char **argv)
{
    const char *path = NULL;
    int show_headers = 0, show_segments = 0, show_sections = 0, show_symbols = 0, show_relocs = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--headers") == 0) show_headers = 1;
        else if (strcmp(argv[i], "--segments") == 0) show_segments = 1;
        else if (strcmp(argv[i], "--sections") == 0) show_sections = 1;
        else if (strcmp(argv[i], "--symbols") == 0) show_symbols = 1;
        else if (strcmp(argv[i], "--relocs") == 0) show_relocs = 1;
        else if (strcmp(argv[i], "--all") == 0) show_headers = show_segments = show_sections = show_symbols = show_relocs = 1;
        else if (argv[i][0] == '-') {
            fprintf(stderr, "error: unknown inspect option '%s'\n", argv[i]);
            return 2;
        } else if (!path) {
            path = argv[i];
        } else {
            fprintf(stderr, "error: inspect accepts one file\n");
            return 2;
        }
    }
    if (!path) {
        fprintf(stderr, "error: missing file for inspect\n");
        return 2;
    }
    if (!show_headers && !show_segments && !show_sections && !show_symbols && !show_relocs)
        show_headers = show_segments = show_sections = show_symbols = show_relocs = 1;
    ElfImage elf;
    if (!load_elf_image(path, &elf)) {
        free_elf_image(&elf);
        return 1;
    }
    int printed = 0;
#define INSPECT_SECTION(fn) do { if (printed) putchar('\n'); fn(&elf); printed = 1; } while (0)
    if (show_headers) INSPECT_SECTION(inspect_print_headers);
    if (show_segments) INSPECT_SECTION(inspect_print_segments);
    if (show_sections) INSPECT_SECTION(inspect_print_sections);
    if (show_symbols) INSPECT_SECTION(inspect_print_symbols);
    if (show_relocs) INSPECT_SECTION(inspect_print_relocs);
#undef INSPECT_SECTION
    free_elf_image(&elf);
    return 0;
}

static const char *dis_reg64(int code)
{
    static const char *names[] = {
        "rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi",
        "r8","r9","r10","r11","r12","r13","r14","r15"
    };
    return names[code & 15];
}

static const char *dis_reg32(int code)
{
    static const char *names[] = {
        "eax","ecx","edx","ebx","esp","ebp","esi","edi",
        "r8d","r9d","r10d","r11d","r12d","r13d","r14d","r15d"
    };
    return names[code & 15];
}

static uint32_t dis_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t dis_u64(const unsigned char *p)
{
    uint64_t lo = dis_u32(p);
    uint64_t hi = dis_u32(p + 4);
    return lo | (hi << 32);
}

static const char *jcc_name_from_low(unsigned low)
{
    static const char *names[] = {
        "jo","jno","jb","jae","je","jne","jbe","ja",
        "js","jns","jp","jnp","jl","jge","jle","jg"
    };
    return names[low & 15];
}

static void dis_bytes(const unsigned char *p, size_t n)
{
    for (size_t i = 0; i < n; i++)
        printf("%02x%s", p[i], i + 1 == n ? "" : " ");
}

static int dis_modrm_operand(char *rm_out, size_t rm_len, char *reg_out, size_t reg_len,
                             const unsigned char *buf, size_t len, size_t *idx,
                             int rex_w, int rex_r, int rex_x, int rex_b,
                             uint64_t instr_addr)
{
    if (*idx >= len)
        return 0;
    unsigned char m = buf[(*idx)++];
    unsigned mod = (m >> 6) & 3;
    unsigned reg = ((m >> 3) & 7) | (rex_r ? 8 : 0);
    unsigned rm = (m & 7) | (rex_b ? 8 : 0);
    snprintf(reg_out, reg_len, "%s", rex_w ? dis_reg64((int)reg) : dis_reg32((int)reg));
    if (mod == 3) {
        snprintf(rm_out, rm_len, "%s", rex_w ? dis_reg64((int)rm) : dis_reg32((int)rm));
        return 1;
    }

    int has_sib = ((m & 7) == 4);
    int base = (int)rm;
    int index = -1;
    int scale = 1;
    if (has_sib) {
        if (*idx >= len)
            return 0;
        unsigned char s = buf[(*idx)++];
        scale = 1 << ((s >> 6) & 3);
        unsigned idx_reg = ((s >> 3) & 7) | (rex_x ? 8 : 0);
        unsigned base_reg = (s & 7) | (rex_b ? 8 : 0);
        if ((idx_reg & 7) != 4)
            index = (int)idx_reg;
        base = (int)base_reg;
    }

    int64_t disp = 0;
    int rip_relative = 0;
    if (mod == 1) {
        if (*idx >= len)
            return 0;
        disp = (int8_t)buf[(*idx)++];
    } else if (mod == 2 || (mod == 0 && !has_sib && (rm & 7) == 5) ||
               (mod == 0 && has_sib && (base & 7) == 5)) {
        if (*idx + 4 > len)
            return 0;
        disp = (int32_t)dis_u32(buf + *idx);
        *idx += 4;
        if (mod == 0 && !has_sib && (rm & 7) == 5)
            rip_relative = 1;
    }

    char tmp[192];
    if (rip_relative) {
        snprintf(tmp, sizeof(tmp), "[rel 0x%llx]", (unsigned long long)(instr_addr + *idx + disp));
    } else {
        snprintf(tmp, sizeof(tmp), "[%s", dis_reg64(base));
        if (index >= 0) {
            size_t used = strlen(tmp);
            snprintf(tmp + used, sizeof(tmp) - used, " + %s*%d", dis_reg64(index), scale);
        }
        if (disp > 0) {
            size_t used = strlen(tmp);
            snprintf(tmp + used, sizeof(tmp) - used, " + %lld", (long long)disp);
        } else if (disp < 0) {
            size_t used = strlen(tmp);
            snprintf(tmp + used, sizeof(tmp) - used, " - %lld", (long long)-disp);
        }
        size_t used = strlen(tmp);
        snprintf(tmp + used, sizeof(tmp) - used, "]");
    }
    snprintf(rm_out, rm_len, "%s", tmp);
    return 1;
}

static size_t dis_one(const unsigned char *buf, size_t len, uint64_t addr)
{
    size_t i = 0;
    int rex_w = 0, rex_r = 0, rex_x = 0, rex_b = 0;
    if (len && buf[i] >= 0x40 && buf[i] <= 0x4f) {
        unsigned char r = buf[i++];
        rex_w = (r >> 3) & 1;
        rex_r = (r >> 2) & 1;
        rex_x = (r >> 1) & 1;
        rex_b = r & 1;
    }
    if (i >= len)
        return 0;
    unsigned char op = buf[i++];
    char text[256] = { 0 };

    if (op == 0x0f && i < len && buf[i] == 0x05) {
        i++;
        snprintf(text, sizeof(text), "syscall");
    } else if (op == 0xc3) {
        snprintf(text, sizeof(text), "ret");
    } else if (op == 0x90) {
        snprintf(text, sizeof(text), "nop");
    } else if ((op & 0xf8) == 0x50) {
        snprintf(text, sizeof(text), "push %s", dis_reg64((op & 7) | (rex_b ? 8 : 0)));
    } else if ((op & 0xf8) == 0x58) {
        snprintf(text, sizeof(text), "pop %s", dis_reg64((op & 7) | (rex_b ? 8 : 0)));
    } else if (op == 0x6a && i < len) {
        snprintf(text, sizeof(text), "push %d", (int)(int8_t)buf[i]);
        i++;
    } else if (op == 0x68 && i + 4 <= len) {
        snprintf(text, sizeof(text), "push %d", (int)(int32_t)dis_u32(buf + i));
        i += 4;
    } else if ((op & 0xf8) == 0xb8) {
        int reg = (op & 7) | (rex_b ? 8 : 0);
        if (rex_w && i + 8 <= len) {
            snprintf(text, sizeof(text), "mov %s, 0x%llx", dis_reg64(reg), (unsigned long long)dis_u64(buf + i));
            i += 8;
        } else if (i + 4 <= len) {
            snprintf(text, sizeof(text), "mov %s, %u", dis_reg32(reg), dis_u32(buf + i));
            i += 4;
        } else {
            i = 1;
            snprintf(text, sizeof(text), "db 0x%02x", buf[0]);
        }
    } else if (op == 0xe8 && i + 4 <= len) {
        int32_t d = (int32_t)dis_u32(buf + i);
        i += 4;
        snprintf(text, sizeof(text), "call 0x%llx", (unsigned long long)(addr + i + d));
    } else if (op == 0xe9 && i + 4 <= len) {
        int32_t d = (int32_t)dis_u32(buf + i);
        i += 4;
        snprintf(text, sizeof(text), "jmp 0x%llx", (unsigned long long)(addr + i + d));
    } else if (op == 0xeb && i < len) {
        int8_t d = (int8_t)buf[i++];
        snprintf(text, sizeof(text), "jmp 0x%llx", (unsigned long long)(addr + i + d));
    } else if (op >= 0x70 && op <= 0x7f && i < len) {
        int8_t d = (int8_t)buf[i++];
        snprintf(text, sizeof(text), "%s 0x%llx", jcc_name_from_low(op & 15),
                 (unsigned long long)(addr + i + d));
    } else if (op == 0x0f && i < len && buf[i] >= 0x80 && buf[i] <= 0x8f && i + 4 < len) {
        unsigned char j = buf[i++];
        int32_t d = (int32_t)dis_u32(buf + i);
        i += 4;
        snprintf(text, sizeof(text), "%s 0x%llx", jcc_name_from_low(j & 15),
                 (unsigned long long)(addr + i + d));
    } else {
        const char *mn = NULL;
        int dir_reg_rm = 0;
        if (op == 0x89) { mn = "mov"; dir_reg_rm = 0; }
        else if (op == 0x8b) { mn = "mov"; dir_reg_rm = 1; }
        else if (op == 0x8d) { mn = "lea"; dir_reg_rm = 1; }
        else if (op == 0x01) { mn = "add"; dir_reg_rm = 0; }
        else if (op == 0x03) { mn = "add"; dir_reg_rm = 1; }
        else if (op == 0x29) { mn = "sub"; dir_reg_rm = 0; }
        else if (op == 0x2b) { mn = "sub"; dir_reg_rm = 1; }
        else if (op == 0x31) { mn = "xor"; dir_reg_rm = 0; }
        else if (op == 0x33) { mn = "xor"; dir_reg_rm = 1; }
        else if (op == 0x39) { mn = "cmp"; dir_reg_rm = 0; }
        else if (op == 0x3b) { mn = "cmp"; dir_reg_rm = 1; }
        else if (op == 0x85) { mn = "test"; dir_reg_rm = 0; }
        if (mn) {
            char rm[192], reg[32];
            size_t modrm_i = i;
            if (dis_modrm_operand(rm, sizeof(rm), reg, sizeof(reg), buf, len, &modrm_i,
                                  rex_w, rex_r, rex_x, rex_b, addr)) {
                i = modrm_i;
                if (dir_reg_rm)
                    snprintf(text, sizeof(text), "%s %s, %s", mn, reg, rm);
                else
                    snprintf(text, sizeof(text), "%s %s, %s", mn, rm, reg);
            }
        } else if ((op == 0x81 || op == 0x83 || op == 0xc7 || op == 0xf7) && i < len) {
            char rm[192], reg[32];
            size_t modrm_i = i;
            if (dis_modrm_operand(rm, sizeof(rm), reg, sizeof(reg), buf, len, &modrm_i,
                                  rex_w, rex_r, rex_x, rex_b, addr)) {
                unsigned ext = (buf[i] >> 3) & 7;
                const char *grp = op == 0xc7 ? "mov" :
                    op == 0xf7 ? "test" :
                    ext == 0 ? "add" : ext == 4 ? "and" : ext == 5 ? "sub" :
                    ext == 6 ? "xor" : ext == 7 ? "cmp" : NULL;
                int imm_size = op == 0x83 ? 1 : 4;
                if (grp && modrm_i + (size_t)imm_size <= len) {
                    int64_t imm = imm_size == 1 ? (int8_t)buf[modrm_i] : (int32_t)dis_u32(buf + modrm_i);
                    i = modrm_i + (size_t)imm_size;
                    snprintf(text, sizeof(text), "%s %s, %lld", grp, rm, (long long)imm);
                }
            }
        } else if ((op == 0x05 || op == 0x0d || op == 0x25 ||
                    op == 0x2d || op == 0x35 || op == 0x3d) && i + 4 <= len) {
            const char *mn2 = op == 0x05 ? "add" : op == 0x0d ? "or" :
                op == 0x25 ? "and" : op == 0x2d ? "sub" :
                op == 0x35 ? "xor" : "cmp";
            snprintf(text, sizeof(text), "%s %s, %d", mn2, rex_w ? "rax" : "eax",
                     (int)(int32_t)dis_u32(buf + i));
            i += 4;
        }
        if (!*text) {
            i = 1;
            snprintf(text, sizeof(text), "db 0x%02x", buf[0]);
        }
    }

    printf("%016llx:  ", (unsigned long long)addr);
    dis_bytes(buf, i);
    for (size_t pad = i; pad < 8; pad++)
        printf("   ");
    printf("    %s\n", text);
    return i ? i : 1;
}

static int elf_find_text_range(ElfImage *elf, const char *section_name,
                               uint64_t start, int have_start, uint64_t length,
                               const unsigned char **bytes, size_t *size, uint64_t *addr)
{
    if (section_name && elf->sh) {
        for (uint16_t i = 1; i < elf->eh.e_shnum; i++) {
            const char *name = inspect_section_name(elf->shstr, elf->shstr_len, elf->sh[i].sh_name);
            if (strcmp(name, section_name) == 0) {
                if (!range_ok(elf->sh[i].sh_offset, elf->sh[i].sh_size, elf->len))
                    return 0;
                *bytes = elf->buf + elf->sh[i].sh_offset;
                *size = (size_t)elf->sh[i].sh_size;
                *addr = elf->sh[i].sh_addr;
                return 1;
            }
        }
        return 0;
    }
    if (have_start) {
        for (uint16_t i = 0; i < elf->eh.e_phnum; i++) {
            if (elf->ph[i].p_type == 1 && start >= elf->ph[i].p_vaddr &&
                start < elf->ph[i].p_vaddr + elf->ph[i].p_filesz) {
                uint64_t delta = start - elf->ph[i].p_vaddr;
                uint64_t avail = elf->ph[i].p_filesz - delta;
                if (!range_ok(elf->ph[i].p_offset + delta, avail, elf->len))
                    return 0;
                *bytes = elf->buf + elf->ph[i].p_offset + delta;
                *size = (size_t)(length && length < avail ? length : avail);
                *addr = start;
                return 1;
            }
        }
    }
    if (elf->sh) {
        for (uint16_t i = 1; i < elf->eh.e_shnum; i++) {
            const char *name = inspect_section_name(elf->shstr, elf->shstr_len, elf->sh[i].sh_name);
            if (strcmp(name, ".text") == 0 && range_ok(elf->sh[i].sh_offset, elf->sh[i].sh_size, elf->len)) {
                *bytes = elf->buf + elf->sh[i].sh_offset;
                *size = (size_t)elf->sh[i].sh_size;
                *addr = elf->sh[i].sh_addr;
                return 1;
            }
        }
    }
    for (uint16_t i = 0; i < elf->eh.e_phnum; i++) {
        if (elf->ph[i].p_type == 1 && (elf->ph[i].p_flags & 1) &&
            range_ok(elf->ph[i].p_offset, elf->ph[i].p_filesz, elf->len)) {
            *bytes = elf->buf + elf->ph[i].p_offset;
            *size = (size_t)elf->ph[i].p_filesz;
            *addr = elf->ph[i].p_vaddr;
            return 1;
        }
    }
    return 0;
}

static int run_disasm(int argc, char **argv)
{
    const char *path = NULL;
    const char *section = NULL;
    uint64_t start = 0, length = 0;
    int have_start = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--section") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: option '--section' requires an argument\n");
                return 2;
            }
            section = argv[++i];
        } else if (strcmp(argv[i], "--start") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: option '--start' requires an argument\n");
                return 2;
            }
            int64_t v = 0;
            if (!kasm_parse_int(argv[++i], &v) || v < 0) {
                fprintf(stderr, "error: invalid --start value\n");
                return 2;
            }
            start = (uint64_t)v;
            have_start = 1;
        } else if (strcmp(argv[i], "--length") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: option '--length' requires an argument\n");
                return 2;
            }
            int64_t v = 0;
            if (!kasm_parse_int(argv[++i], &v) || v < 0) {
                fprintf(stderr, "error: invalid --length value\n");
                return 2;
            }
            length = (uint64_t)v;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "error: unknown disasm option '%s'\n", argv[i]);
            return 2;
        } else if (!path) {
            path = argv[i];
        } else {
            fprintf(stderr, "error: disasm accepts one file\n");
            return 2;
        }
    }
    if (!path) {
        fprintf(stderr, "error: missing file for disasm\n");
        return 2;
    }
    ElfImage elf;
    if (!load_elf_image(path, &elf)) {
        free_elf_image(&elf);
        return 1;
    }
    const unsigned char *bytes = NULL;
    size_t size = 0;
    uint64_t addr = 0;
    if (!elf_find_text_range(&elf, section, start, have_start, length, &bytes, &size, &addr)) {
        fprintf(stderr, "error: cannot locate bytes to disassemble\n");
        free_elf_image(&elf);
        return 1;
    }
    if (length && length < size)
        size = (size_t)length;
    size_t off = 0;
    while (off < size) {
        size_t n = dis_one(bytes + off, size - off, addr + off);
        if (!n) n = 1;
        off += n;
    }
    free_elf_image(&elf);
    return 0;
}

typedef struct {
    char *name;
    uint64_t value;
    const char *path;
} LinkSymbol;

typedef struct {
    LinkSymbol *items;
    size_t len;
    size_t cap;
} LinkSymbolTable;

typedef struct {
    char *path;
    InspectShdr *sh;
    uint16_t shnum;
    char *shstr;
    size_t shstr_len;
    InspectSym *symtab;
    size_t sym_count;
    char *strtab;
    size_t strtab_len;
    ByteBuf section_data[SEC_COUNT];
    uint64_t input_size[SEC_COUNT];
    uint64_t output_off[SEC_COUNT];
    uint16_t obj_sec_index[SEC_COUNT];
} LinkObject;

static void link_symbol_table_free(LinkSymbolTable *tab)
{
    for (size_t i = 0; i < tab->len; i++)
        free(tab->items[i].name);
    free(tab->items);
}

static LinkSymbol *link_symbol_find(LinkSymbolTable *tab, const char *name)
{
    for (size_t i = 0; i < tab->len; i++)
        if (strcmp(tab->items[i].name, name) == 0)
            return &tab->items[i];
    return NULL;
}

static int link_symbol_add(LinkSymbolTable *tab, const char *name, uint64_t value,
                           const char *path)
{
    if (!name || !*name)
        return 1;
    LinkSymbol *old = link_symbol_find(tab, name);
    if (old) {
        fprintf(stderr, "%s: error: duplicate global symbol '%s'\n", path, name);
        fprintf(stderr, "hint: keep one global definition and mark other references extern\n");
        return 0;
    }
    if (tab->len == tab->cap) {
        tab->cap = tab->cap ? tab->cap * 2 : 64;
        tab->items = kasm_xrealloc(tab->items, tab->cap * sizeof(LinkSymbol));
    }
    tab->items[tab->len].name = kasm_xstrdup(name);
    tab->items[tab->len].value = value;
    tab->items[tab->len].path = path;
    tab->len++;
    return 1;
}

static void link_object_free(LinkObject *obj)
{
    free(obj->path);
    free(obj->sh);
    free(obj->shstr);
    free(obj->symtab);
    free(obj->strtab);
    for (int i = 0; i < SEC_COUNT; i++)
        free(obj->section_data[i].data);
}

static SectionId link_section_id(const char *name)
{
    if (strcmp(name, ".text") == 0) return SEC_TEXT;
    if (strcmp(name, ".rodata") == 0) return SEC_RODATA;
    if (strcmp(name, ".data") == 0 || strcmp(name, ".bss") == 0) return SEC_DATA;
    return SEC_NONE;
}

static int load_link_object(const char *path, LinkObject *obj)
{
    memset(obj, 0, sizeof(*obj));
    obj->path = kasm_xstrdup(path);
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "%s: error: cannot read object file\n", path);
        return 0;
    }
    InspectEhdr eh;
    if (fread(&eh, 1, sizeof(eh), f) != sizeof(eh) ||
        eh.e_ident[0] != 0x7f || eh.e_ident[1] != 'E' ||
        eh.e_ident[2] != 'L' || eh.e_ident[3] != 'F' ||
        eh.e_ident[4] != 2 || eh.e_ident[5] != 1 || eh.e_type != 1) {
        fclose(f);
        fprintf(stderr, "%s: error: malformed input object: expected ELF64 relocatable\n", path);
        return 0;
    }
    if (!eh.e_shoff || !eh.e_shnum || eh.e_shentsize != sizeof(InspectShdr)) {
        fclose(f);
        fprintf(stderr, "%s: error: malformed input object: invalid section table\n", path);
        return 0;
    }
    obj->shnum = eh.e_shnum;
    obj->sh = kasm_xrealloc(NULL, (size_t)obj->shnum * sizeof(InspectShdr));
    if (!read_exact(f, eh.e_shoff, obj->sh, (size_t)obj->shnum * sizeof(InspectShdr))) {
        fclose(f);
        fprintf(stderr, "%s: error: malformed input object: cannot read section table\n", path);
        return 0;
    }
    if (eh.e_shstrndx >= obj->shnum) {
        fclose(f);
        fprintf(stderr, "%s: error: malformed input object: invalid section name table\n", path);
        return 0;
    }
    obj->shstr_len = (size_t)obj->sh[eh.e_shstrndx].sh_size;
    obj->shstr = kasm_xrealloc(NULL, obj->shstr_len ? obj->shstr_len : 1);
    if (obj->shstr_len &&
        !read_exact(f, obj->sh[eh.e_shstrndx].sh_offset, obj->shstr, obj->shstr_len)) {
        fclose(f);
        fprintf(stderr, "%s: error: malformed input object: cannot read section names\n", path);
        return 0;
    }
    for (uint16_t i = 1; i < obj->shnum; i++) {
        const char *name = inspect_section_name(obj->shstr, obj->shstr_len, obj->sh[i].sh_name);
        SectionId sec = link_section_id(name);
        if (sec != SEC_NONE && strncmp(name, ".rela", 5) != 0) {
            obj->obj_sec_index[sec] = i;
            obj->input_size[sec] = obj->sh[i].sh_size;
            if (obj->sh[i].sh_type == 1 && obj->sh[i].sh_size) {
                obj->section_data[sec].data = kasm_xrealloc(NULL, (size_t)obj->sh[i].sh_size);
                obj->section_data[sec].len = (size_t)obj->sh[i].sh_size;
                obj->section_data[sec].cap = (size_t)obj->sh[i].sh_size;
                if (!read_exact(f, obj->sh[i].sh_offset, obj->section_data[sec].data,
                                (size_t)obj->sh[i].sh_size)) {
                    fclose(f);
                    fprintf(stderr, "%s: error: malformed input object: cannot read section %s\n", path, name);
                    return 0;
                }
            } else if (obj->sh[i].sh_type != 8 && obj->sh[i].sh_size) {
                fclose(f);
                fprintf(stderr, "%s: error: malformed input object: unsupported section type for %s\n", path, name);
                return 0;
            }
        } else if (strcmp(name, ".symtab") == 0 && obj->sh[i].sh_type == 2 &&
                   obj->sh[i].sh_entsize == sizeof(InspectSym)) {
            obj->sym_count = (size_t)(obj->sh[i].sh_size / obj->sh[i].sh_entsize);
            obj->symtab = kasm_xrealloc(NULL, obj->sym_count * sizeof(InspectSym));
            if (!read_exact(f, obj->sh[i].sh_offset, obj->symtab,
                            obj->sym_count * sizeof(InspectSym))) {
                fclose(f);
                fprintf(stderr, "%s: error: malformed input object: cannot read symbol table\n", path);
                return 0;
            }
            if (obj->sh[i].sh_link < obj->shnum) {
                obj->strtab_len = (size_t)obj->sh[obj->sh[i].sh_link].sh_size;
                obj->strtab = kasm_xrealloc(NULL, obj->strtab_len ? obj->strtab_len : 1);
                if (obj->strtab_len &&
                    !read_exact(f, obj->sh[obj->sh[i].sh_link].sh_offset, obj->strtab, obj->strtab_len)) {
                    fclose(f);
                    fprintf(stderr, "%s: error: malformed input object: cannot read string table\n", path);
                    return 0;
                }
            }
        }
    }
    fclose(f);
    if (!obj->symtab || !obj->strtab) {
        fprintf(stderr, "%s: error: malformed input object: missing symbol table\n", path);
        return 0;
    }
    return 1;
}

static uint64_t link_section_vaddr(SectionId sec, uint64_t text_vaddr,
                                   uint64_t ro_vaddr, uint64_t data_vaddr)
{
    if (sec == SEC_TEXT) return text_vaddr;
    if (sec == SEC_RODATA) return ro_vaddr;
    if (sec == SEC_DATA) return data_vaddr;
    return 0;
}

static int link_sym_value(LinkObject *objects, size_t obj_count, LinkSymbolTable *globals,
                          LinkObject *obj, uint32_t sym_index, uint64_t text_vaddr,
                          uint64_t ro_vaddr, uint64_t data_vaddr, uint64_t *out)
{
    if (sym_index >= obj->sym_count) {
        fprintf(stderr, "%s: error: relocation references invalid symbol index %u\n",
                obj->path, sym_index);
        return 0;
    }
    InspectSym *s = &obj->symtab[sym_index];
    const char *name = inspect_symbol_name(obj->strtab, obj->strtab_len, s->st_name);
    if (s->st_shndx == 0) {
        LinkSymbol *g = link_symbol_find(globals, name);
        if (!g) {
            fprintf(stderr, "%s: error: undefined symbol '%s'\n", obj->path, name);
            fprintf(stderr, "hint: provide an object file that defines '%s'\n", name);
            return 0;
        }
        *out = g->value;
        return 1;
    }
    if (s->st_shndx == 0xfff1) {
        *out = s->st_value;
        return 1;
    }
    for (size_t oi = 0; oi < obj_count; oi++) {
        LinkObject *candidate = &objects[oi];
        if (candidate != obj)
            continue;
        for (int sec = 0; sec < SEC_COUNT; sec++) {
            if (candidate->obj_sec_index[sec] == s->st_shndx) {
                *out = link_section_vaddr((SectionId)sec, text_vaddr, ro_vaddr, data_vaddr) +
                       candidate->output_off[sec] + s->st_value;
                return 1;
            }
        }
    }
    fprintf(stderr, "%s: error: symbol '%s' references unsupported section index %u\n",
            obj->path, name, s->st_shndx);
    return 0;
}

static void write_u32_at(ByteBuf *buf, uint64_t off, uint32_t v)
{
    memcpy(buf->data + off, &v, 4);
}

static void write_u64_at(ByteBuf *buf, uint64_t off, uint64_t v)
{
    memcpy(buf->data + off, &v, 8);
}

static int apply_link_relocations(LinkObject *objects, size_t obj_count,
                                  LinkSymbolTable *globals, ByteBuf merged[SEC_COUNT],
                                  uint64_t text_vaddr, uint64_t ro_vaddr, uint64_t data_vaddr)
{
    for (size_t oi = 0; oi < obj_count; oi++) {
        LinkObject *obj = &objects[oi];
        for (uint16_t si = 1; si < obj->shnum; si++) {
            if (obj->sh[si].sh_type != 4 || obj->sh[si].sh_entsize != sizeof(InspectRela))
                continue;
            if (obj->sh[si].sh_info >= obj->shnum) {
                fprintf(stderr, "%s: error: malformed relocation section index %u\n",
                        obj->path, obj->sh[si].sh_info);
                return 0;
            }
            SectionId target_sec = SEC_NONE;
            for (int sec = 0; sec < SEC_COUNT; sec++)
                if (obj->obj_sec_index[sec] == obj->sh[si].sh_info)
                    target_sec = (SectionId)sec;
            if (target_sec == SEC_NONE) {
                const char *sec_name = inspect_section_name(obj->shstr, obj->shstr_len, obj->sh[si].sh_name);
                fprintf(stderr, "%s: error: unsupported relocation target section for %s\n", obj->path, sec_name);
                return 0;
            }
            size_t count = (size_t)(obj->sh[si].sh_size / obj->sh[si].sh_entsize);
            InspectRela *rela = kasm_xrealloc(NULL, count * sizeof(InspectRela));
            FILE *f = fopen(obj->path, "rb");
            if (!f || !read_exact(f, obj->sh[si].sh_offset, rela, count * sizeof(InspectRela))) {
                if (f) fclose(f);
                free(rela);
                fprintf(stderr, "%s: error: cannot read relocation section\n", obj->path);
                return 0;
            }
            fclose(f);
            for (size_t ri = 0; ri < count; ri++) {
                uint32_t sym_index = (uint32_t)(rela[ri].r_info >> 32);
                uint32_t type = (uint32_t)(rela[ri].r_info & 0xffffffffu);
                uint64_t sym_value = 0;
                if (!link_sym_value(objects, obj_count, globals, obj, sym_index,
                                    text_vaddr, ro_vaddr, data_vaddr, &sym_value)) {
                    free(rela);
                    return 0;
                }
                uint64_t place_off = obj->output_off[target_sec] + rela[ri].r_offset;
                uint64_t place_addr = link_section_vaddr(target_sec, text_vaddr, ro_vaddr, data_vaddr) + place_off;
                const char *target_name = inspect_section_name(obj->shstr, obj->shstr_len,
                                                               obj->sh[obj->sh[si].sh_info].sh_name);
                if (type == 1) {
                    if (place_off + 8 > merged[target_sec].len) {
                        fprintf(stderr, "%s: error: relocation offset 0x%llx outside section %s\n",
                                obj->path, (unsigned long long)rela[ri].r_offset, target_name);
                        free(rela);
                        return 0;
                    }
                    write_u64_at(&merged[target_sec], place_off, sym_value + (uint64_t)rela[ri].r_addend);
                } else if (type == 2 || type == 10 || type == 11) {
                    int64_t value = (int64_t)sym_value + rela[ri].r_addend;
                    if (type == 2)
                        value -= (int64_t)place_addr;
                    if (place_off + 4 > merged[target_sec].len) {
                        fprintf(stderr, "%s: error: relocation offset 0x%llx outside section %s\n",
                                obj->path, (unsigned long long)rela[ri].r_offset, target_name);
                        free(rela);
                        return 0;
                    }
                    if (type == 2 || type == 11) {
                        if (value < INT32_MIN || value > INT32_MAX) {
                            fprintf(stderr, "%s: error: relocation overflow at %s+0x%llx type=%s\n",
                                    obj->path, target_name, (unsigned long long)rela[ri].r_offset,
                                    inspect_reloc_type(type));
                            free(rela);
                            return 0;
                        }
                    } else if (value < 0 || value > UINT32_MAX) {
                        fprintf(stderr, "%s: error: relocation overflow at %s+0x%llx type=%s\n",
                                obj->path, target_name, (unsigned long long)rela[ri].r_offset,
                                inspect_reloc_type(type));
                        free(rela);
                        return 0;
                    }
                    write_u32_at(&merged[target_sec], place_off, (uint32_t)(int32_t)value);
                } else {
                    fprintf(stderr, "%s: error: unsupported relocation type %u at %s+0x%llx\n",
                            obj->path, type, target_name, (unsigned long long)rela[ri].r_offset);
                    fprintf(stderr, "hint: KASM internal linker supports R_X86_64_64, R_X86_64_PC32, R_X86_64_32, and R_X86_64_32S\n");
                    free(rela);
                    return 0;
                }
            }
            free(rela);
        }
    }
    return 1;
}

static int write_linked_executable(const char *output, ByteBuf merged[SEC_COUNT],
                                   uint64_t entry_addr)
{
    uint64_t base = 0x400000;
    uint64_t phoff = 64;
    uint16_t phnum = 3;
    uint64_t file_align = 0x1000;
    uint64_t text_off = kasm_align(64 + (uint64_t)phnum * 56, file_align);
    uint64_t ro_off = kasm_align(text_off + merged[SEC_TEXT].len, file_align);
    uint64_t data_off = kasm_align(ro_off + merged[SEC_RODATA].len, file_align);
    uint64_t text_vaddr = base + text_off;
    uint64_t ro_vaddr = base + ro_off;
    uint64_t data_vaddr = base + data_off;
    (void)text_vaddr;
    (void)ro_vaddr;
    (void)data_vaddr;

    InspectEhdr eh;
    memset(&eh, 0, sizeof(eh));
    eh.e_ident[0] = 0x7f;
    eh.e_ident[1] = 'E';
    eh.e_ident[2] = 'L';
    eh.e_ident[3] = 'F';
    eh.e_ident[4] = 2;
    eh.e_ident[5] = 1;
    eh.e_ident[6] = 1;
    eh.e_type = 2;
    eh.e_machine = 62;
    eh.e_version = 1;
    eh.e_entry = entry_addr;
    eh.e_phoff = phoff;
    eh.e_ehsize = 64;
    eh.e_phentsize = 56;
    eh.e_phnum = phnum;

    InspectPhdr ph[3];
    memset(ph, 0, sizeof(ph));
    ph[0].p_type = 1;
    ph[0].p_flags = 5;
    ph[0].p_offset = text_off;
    ph[0].p_vaddr = text_vaddr;
    ph[0].p_paddr = text_vaddr;
    ph[0].p_filesz = merged[SEC_TEXT].len;
    ph[0].p_memsz = merged[SEC_TEXT].len;
    ph[0].p_align = file_align;

    ph[1].p_type = 1;
    ph[1].p_flags = 4;
    ph[1].p_offset = ro_off;
    ph[1].p_vaddr = ro_vaddr;
    ph[1].p_paddr = ro_vaddr;
    ph[1].p_filesz = merged[SEC_RODATA].len;
    ph[1].p_memsz = merged[SEC_RODATA].len;
    ph[1].p_align = file_align;

    ph[2].p_type = 1;
    ph[2].p_flags = 6;
    ph[2].p_offset = data_off;
    ph[2].p_vaddr = data_vaddr;
    ph[2].p_paddr = data_vaddr;
    ph[2].p_filesz = merged[SEC_DATA].len;
    ph[2].p_memsz = merged[SEC_DATA].len;
    ph[2].p_align = file_align;

    FILE *f = fopen(output, "wb");
    if (!f) {
        fprintf(stderr, "%s: error: cannot write linked executable\n", output);
        return 0;
    }
    uint64_t pos = 0;
    if (fwrite(&eh, 1, sizeof(eh), f) != sizeof(eh) ||
        fwrite(ph, 1, (size_t)phnum * 56, f) != (size_t)phnum * 56) {
        fclose(f);
        fprintf(stderr, "%s: error: cannot write linked executable\n", output);
        return 0;
    }
    pos = sizeof(eh) + (uint64_t)phnum * 56;
    if (!link_write_section(f, &pos, text_off, &merged[SEC_TEXT]) ||
        !link_write_section(f, &pos, ro_off, &merged[SEC_RODATA]) ||
        !link_write_section(f, &pos, data_off, &merged[SEC_DATA])) {
        fclose(f);
        fprintf(stderr, "%s: error: cannot write linked executable\n", output);
        return 0;
    }
    fclose(f);
#ifndef _WIN32
    chmod(output, 0755);
#endif
    return 1;
}

static int kasm_internal_link(const char **inputs, size_t input_count,
                              const char *output, const char *entry)
{
    if (!input_count) {
        fprintf(stderr, "error: no object files to link\n");
        return 2;
    }
    LinkObject *objects = kasm_xrealloc(NULL, input_count * sizeof(LinkObject));
    memset(objects, 0, input_count * sizeof(LinkObject));
    ByteBuf merged[SEC_COUNT];
    memset(merged, 0, sizeof(merged));
    LinkSymbolTable globals = { 0 };
    int ok = 1;
    for (size_t i = 0; i < input_count; i++) {
        if (!load_link_object(inputs[i], &objects[i])) {
            ok = 0;
            goto done;
        }
        for (int sec = 0; sec < SEC_COUNT; sec++) {
            objects[i].output_off[sec] = merged[sec].len;
            if (objects[i].section_data[sec].len) {
                kasm_buf_append(&merged[sec], objects[i].section_data[sec].data,
                                objects[i].section_data[sec].len);
            } else if (objects[i].input_size[sec]) {
                for (uint64_t z = 0; z < objects[i].input_size[sec]; z++)
                    kasm_buf_append_u8(&merged[sec], 0);
            }
        }
    }

    uint64_t base = 0x400000;
    uint16_t phnum = 3;
    uint64_t file_align = 0x1000;
    uint64_t text_off = kasm_align(64 + (uint64_t)phnum * 56, file_align);
    uint64_t ro_off = kasm_align(text_off + merged[SEC_TEXT].len, file_align);
    uint64_t data_off = kasm_align(ro_off + merged[SEC_RODATA].len, file_align);
    uint64_t text_vaddr = base + text_off;
    uint64_t ro_vaddr = base + ro_off;
    uint64_t data_vaddr = base + data_off;

    for (size_t oi = 0; oi < input_count; oi++) {
        LinkObject *obj = &objects[oi];
        for (size_t si = 0; si < obj->sym_count; si++) {
            InspectSym *s = &obj->symtab[si];
            if ((s->st_info >> 4) != 1 || s->st_shndx == 0)
                continue;
            const char *name = inspect_symbol_name(obj->strtab, obj->strtab_len, s->st_name);
            uint64_t value = 0;
            if (s->st_shndx == 0xfff1) {
                value = s->st_value;
            } else {
                SectionId sec = SEC_NONE;
                for (int k = 0; k < SEC_COUNT; k++)
                    if (obj->obj_sec_index[k] == s->st_shndx)
                        sec = (SectionId)k;
                if (sec == SEC_NONE) {
                    fprintf(stderr, "%s: error: global symbol '%s' is in unsupported section index %u\n",
                            obj->path, name, s->st_shndx);
                    ok = 0;
                    goto done;
                }
                value = link_section_vaddr(sec, text_vaddr, ro_vaddr, data_vaddr) +
                        obj->output_off[sec] + s->st_value;
            }
            if (!link_symbol_add(&globals, name, value, obj->path)) {
                ok = 0;
                goto done;
            }
        }
    }

    if (!apply_link_relocations(objects, input_count, &globals, merged,
                                text_vaddr, ro_vaddr, data_vaddr)) {
        ok = 0;
        goto done;
    }
    LinkSymbol *entry_sym = link_symbol_find(&globals, entry ? entry : "_start");
    if (!entry_sym) {
        fprintf(stderr, "error: undefined entry symbol '%s'\n", entry ? entry : "_start");
        fprintf(stderr, "hint: define the entry symbol or pass --entry SYMBOL\n");
        ok = 0;
        goto done;
    }
    ok = write_linked_executable(output, merged, entry_sym->value);

done:
    for (size_t i = 0; i < input_count; i++)
        link_object_free(&objects[i]);
    free(objects);
    for (int sec = 0; sec < SEC_COUNT; sec++)
        free(merged[sec].data);
    link_symbol_table_free(&globals);
    return ok ? 0 : 1;
}

static int run_link_command(int argc, char **argv)
{
    const char *output = NULL;
    const char *entry = "_start";
    const char **inputs = NULL;
    size_t input_count = 0, input_cap = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: option '-o' requires an argument\n");
                free(inputs);
                return 2;
            }
            output = argv[++i];
        } else if (strcmp(argv[i], "--entry") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: option '--entry' requires an argument\n");
                free(inputs);
                return 2;
            }
            entry = argv[++i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "error: unknown link option '%s'\n", argv[i]);
            free(inputs);
            return 2;
        } else {
            if (input_count == input_cap) {
                input_cap = input_cap ? input_cap * 2 : 8;
                inputs = kasm_xrealloc(inputs, input_cap * sizeof(char *));
            }
            inputs[input_count++] = argv[i];
        }
    }
    if (!output) {
        fprintf(stderr, "error: link requires -o output\n");
        free(inputs);
        return 2;
    }
    int rc = kasm_internal_link(inputs, input_count, output, entry);
    free(inputs);
    return rc;
}

static int run_project_build(const char *argv0, int argc, char **argv)
{
    const char *config_path = "kasm.toml";
    int verbose = 0;
    int no_link = 0;
    int dump_symbols_flag = 0;
    int dump_sections_flag = 0;
    int dump_relocs_flag = 0;
    int dump_all_flag = 0;
    int internal_linker = 0;
    const char *linker_override = NULL;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: option '--config' requires an argument\n");
                return 2;
            }
            config_path = argv[++i];
        } else if (strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "--no-link") == 0) {
            no_link = 1;
        } else if (strcmp(argv[i], "--internal-linker") == 0) {
            internal_linker = 1;
        } else if (strcmp(argv[i], "--linker") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: option '--linker' requires an argument\n");
                return 2;
            }
            const char *linker_arg = argv[++i];
            if (strcmp(linker_arg, "internal") == 0) {
                internal_linker = 1;
            } else {
                linker_override = linker_arg;
            }
        } else if (strcmp(argv[i], "--dump-symbols") == 0) {
            dump_symbols_flag = 1;
        } else if (strcmp(argv[i], "--dump-sections") == 0) {
            dump_sections_flag = 1;
        } else if (strcmp(argv[i], "--dump-relocs") == 0) {
            dump_relocs_flag = 1;
        } else if (strcmp(argv[i], "--dump-all") == 0) {
            dump_all_flag = 1;
            dump_symbols_flag = 1;
            dump_sections_flag = 1;
            dump_relocs_flag = 1;
        } else {
            fprintf(stderr, "error: unknown build option '%s'\n", argv[i]);
            return 2;
        }
    }
    ProjectConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    if (!parse_project_config(config_path, &cfg)) {
        project_config_free(&cfg);
        return 2;
    }
    if (linker_override) {
        free(cfg.linker);
        cfg.linker = kasm_xstrdup(linker_override);
    }
    char *config_dir = path_dirname_dup(config_path);
    char *out_dir = path_join_dup(config_dir, cfg.out_dir);
    if (!ensure_dir(out_dir)) {
        free(config_dir);
        free(out_dir);
        project_config_free(&cfg);
        return 1;
    }

    char **objects = kasm_xrealloc(NULL, cfg.source_count * sizeof(char *));
    int status = 0;
    for (size_t i = 0; i < cfg.source_count; i++) {
        char *source = path_join_dup(config_dir, cfg.sources[i]);
        if (!file_exists(source)) {
            fprintf(stderr, "error: missing source file '%s'\n", source);
            free(source);
            status = 1;
            objects[i] = NULL;
            break;
        }
        objects[i] = object_path_for_source(out_dir, cfg.sources[i]);
        for (size_t j = 0; j < i; j++) {
            if (objects[j] && strcmp(objects[j], objects[i]) == 0) {
                fprintf(stderr, "error: duplicate source output '%s'\n", objects[i]);
                status = 1;
                break;
            }
        }
        if (status) {
            free(source);
            break;
        }
        Assembler opts;
        memset(&opts, 0, sizeof(opts));
        opts.dump_symbols = dump_symbols_flag;
        opts.dump_sections = dump_sections_flag;
        opts.dump_relocs = dump_relocs_flag;
        opts.dump_all = dump_all_flag;
        char *env_paths = getenv("KASM_INCLUDE_PATH") ? kasm_xstrdup(getenv("KASM_INCLUDE_PATH")) : NULL;
        if (env_paths) {
            char *part = strtok(env_paths, ":");
            while (part) {
                if (*part)
                    kasm_add_include_path(&opts, part);
                part = strtok(NULL, ":");
            }
            free(env_paths);
        }
        kasm_add_include_path(&opts, "lib/kasm");
        kasm_add_include_path(&opts, "lib");
        add_exe_relative_include(&opts, argv0);
        kasm_add_include_path(&opts, KASM_INSTALL_LIB);
        if (!cfg.entry || strcmp(cfg.entry, "_start") != 0) {
            (void)cfg.entry;
        }
        if (verbose)
            printf("assemble: %s -> %s\n", source, objects[i]);
        if (assemble_one(&opts, source, objects[i], "elf64-obj") != 0) {
            fprintf(stderr, "error: assembler failure in '%s'\n", source);
            status = 1;
        }
        kasm_assembler_free(&opts);
        free(source);
        if (status)
            break;
    }

    if (!status && !no_link && internal_linker) {
        char *exe = path_join_dup(out_dir, cfg.output);
        if (verbose)
            printf("link: internal");
        if (verbose) {
            for (size_t i = 0; i < cfg.source_count; i++)
                printf(" %s", objects[i]);
            printf(" -o %s --entry %s\n", exe, cfg.entry);
        }
        if (kasm_internal_link((const char **)objects, cfg.source_count, exe, cfg.entry) != 0)
            status = 1;
        free(exe);
    } else if (!status && !no_link) {
        char *exe = path_join_dup(out_dir, cfg.output);
        size_t cmd_len = 0, cmd_cap = 0;
        char *cmd = NULL;
        char *q = shell_quote_dup(cfg.linker);
        append_cmd(&cmd, &cmd_len, &cmd_cap, q);
        free(q);
        append_cmd(&cmd, &cmd_len, &cmd_cap, " -e ");
        q = shell_quote_dup(cfg.entry);
        append_cmd(&cmd, &cmd_len, &cmd_cap, q);
        free(q);
        for (size_t i = 0; i < cfg.source_count; i++) {
            append_cmd(&cmd, &cmd_len, &cmd_cap, " ");
            q = shell_quote_dup(objects[i]);
            append_cmd(&cmd, &cmd_len, &cmd_cap, q);
            free(q);
        }
        append_cmd(&cmd, &cmd_len, &cmd_cap, " -o ");
        q = shell_quote_dup(exe);
        append_cmd(&cmd, &cmd_len, &cmd_cap, q);
        free(q);
        if (verbose)
            printf("link: %s\n", cmd);
        int rc = system(cmd);
        if (rc == -1 || !WIFEXITED(rc) || WEXITSTATUS(rc) != 0) {
            fprintf(stderr, "error: linker failure\n");
            status = 1;
        }
        free(cmd);
        free(exe);
    } else if (!status && verbose) {
        puts("link: skipped (--no-link)");
    }

    for (size_t i = 0; i < cfg.source_count; i++)
        free(objects[i]);
    free(objects);
    free(config_dir);
    free(out_dir);
    project_config_free(&cfg);
    return status ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "build") == 0)
        return run_project_build(argv[0], argc, argv);
    if (argc > 1 && strcmp(argv[1], "link") == 0)
        return run_link_command(argc, argv);
    if (argc > 1 && strcmp(argv[1], "inspect") == 0)
        return run_inspect(argc, argv);
    if (argc > 1 && strcmp(argv[1], "disasm") == 0)
        return run_disasm(argc, argv);

    char **inputs = NULL;
    size_t input_count = 0;
    size_t input_cap = 0;
    const char *output = NULL;
    const char *format = "elf64";
    int format_set = 0;
    int combine = 0;
    Assembler as;
    memset(&as, 0, sizeof(as));

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (!require_arg(argc, argv, i))
                return 2;
            output = argv[++i];
        } else if (strcmp(argv[i], "-f") == 0) {
            if (!require_arg(argc, argv, i))
                return 2;
            format = argv[++i];
            format_set = 1;
        } else if (strcmp(argv[i], "--tiny") == 0 || strcmp(argv[i], "-Oz") == 0) {
            as.tiny = 1;
        } else if (strcmp(argv[i], "--no-tiny") == 0) {
            as.tiny = 0;
            as.tiny_report = 0;
        } else if (strcmp(argv[i], "--tiny-report") == 0) {
            as.tiny = 1;
            as.tiny_report = 1;
        } else if (strcmp(argv[i], "-I") == 0) {
            if (!require_arg(argc, argv, i))
                return 2;
            kasm_add_include_path(&as, argv[++i]);
        } else if (strcmp(argv[i], "--explain") == 0) {
            as.explain = 1;
            as.explain_mode = 1;
        } else if (strncmp(argv[i], "--explain=", 10) == 0) {
            as.explain = 1;
            const char *mode = argv[i] + 10;
            if (strcmp(mode, "normal") == 0) {
                as.explain_mode = 1;
            } else if (strcmp(mode, "verbose") == 0) {
                as.explain_mode = 2;
            } else if (strcmp(mode, "deluxe") == 0) {
                as.explain_mode = 3;
            } else if (strcmp(mode, "json") == 0) {
                fprintf(stderr, "error: unsupported explain JSON mode\n");
                return 2;
            } else {
                fprintf(stderr, "error: invalid explain mode '%s'\n", mode);
                return 2;
            }
        } else if (strcmp(argv[i], "--explain-format") == 0) {
            if (!require_arg(argc, argv, i))
                return 2;
            const char *fmt = argv[++i];
            if (strcmp(fmt, "text") != 0) {
                fprintf(stderr, "error: unsupported explain format '%s'\n", fmt);
                fprintf(stderr, "hint: KASM 0.1.0 supports --explain-format text\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--explain-file") == 0) {
            if (!require_arg(argc, argv, i))
                return 2;
            as.explain_path = argv[++i];
            as.explain = 1;
            if (!as.explain_mode)
                as.explain_mode = 1;
        } else if (strcmp(argv[i], "--map") == 0) {
            if (!require_arg(argc, argv, i))
                return 2;
            as.map_path = argv[++i];
        } else if (strcmp(argv[i], "--list") == 0) {
            if (!require_arg(argc, argv, i))
                return 2;
            as.list_path = argv[++i];
        } else if (strcmp(argv[i], "--hints") == 0) {
            enable_all_hints(&as);
        } else if (strncmp(argv[i], "--hints=", 8) == 0) {
            if (!enable_hint_categories(&as, argv[i] + 8))
                return 2;
        } else if (strcmp(argv[i], "--hints-cpu") == 0) {
            if (!require_arg(argc, argv, i))
                return 2;
            as.hints_cpu = argv[++i];
            if (!as.hints)
                enable_all_hints(&as);
            if (!valid_hint_cpu(as.hints_cpu)) {
                fprintf(stderr, "error: unknown hints CPU profile '%s'\n", as.hints_cpu);
                fprintf(stderr, "hint: supported profiles are generic, intel, amd, zen4, skylake\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--print-include-paths") == 0) {
            as.print_include_paths = 1;
        } else if (strcmp(argv[i], "--print-std-path") == 0) {
            as.print_std_path = 1;
        } else if (strcmp(argv[i], "--no-stdlib") == 0 ||
                   strcmp(argv[i], "--no-std") == 0) {
            as.no_stdlib = 1;
        } else if (strcmp(argv[i], "--no-syscall-sugar") == 0) {
            as.no_syscall_sugar = 1;
        } else if (strcmp(argv[i], "--combine") == 0) {
            combine = 1;
        } else if (strcmp(argv[i], "--dump-symbols") == 0) {
            as.dump_symbols = 1;
        } else if (strcmp(argv[i], "--dump-sections") == 0) {
            as.dump_sections = 1;
        } else if (strcmp(argv[i], "--dump-all") == 0) {
            as.dump_all = 1;
            as.dump_symbols = 1;
            as.dump_sections = 1;
            as.dump_relocs = 1;
        } else if (strcmp(argv[i], "--dump-ir") == 0) {
            as.dump_ir = 1;
        } else if (strcmp(argv[i], "--dump-relocs") == 0) {
            as.dump_relocs = 1;
        } else if (strcmp(argv[i], "--dump-structs") == 0) {
            as.dump_structs = 1;
        } else if (strcmp(argv[i], "--dump-expanded") == 0) {
            as.dump_expanded = 1;
        } else if (strcmp(argv[i], "--dump-tokens") == 0) {
            as.dump_tokens = 1;
        } else if (strcmp(argv[i], "--elf-info") == 0) {
            as.elf_info = 1;
        } else if (strcmp(argv[i], "--teach") == 0) {
            as.teach = 1;
            if (!as.teach_level)
                as.teach_level = 2;
        } else if (strcmp(argv[i], "--teach-level") == 0) {
            if (!require_arg(argc, argv, i))
                return 2;
            const char *level = argv[++i];
            as.teach = 1;
            if (strcmp(level, "beginner") == 0) {
                as.teach_level = 1;
            } else if (strcmp(level, "intermediate") == 0) {
                as.teach_level = 2;
            } else if (strcmp(level, "deep") == 0) {
                as.teach_level = 3;
            } else {
                fprintf(stderr, "error: invalid teach level '%s'\n", level);
                fprintf(stderr, "hint: supported teach levels are beginner, intermediate, deep\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--version") == 0) {
            puts("KASM 0.1.0");
            return 0;
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "error: unknown option '%s'\n", argv[i]);
            fprintf(stderr, "hint: run 'kasm --help' for usage\n");
            return 2;
        } else {
            add_input(&inputs, &input_count, &input_cap, argv[i]);
        }
    }
    if (combine) {
        fprintf(stderr, "error: --combine is not implemented yet\n");
        free(inputs);
        kasm_assembler_free(&as);
        return 2;
    }
    if (input_count > 1 && output) {
        fprintf(stderr, "error: multiple input files with one -o output are ambiguous\n");
        free(inputs);
        kasm_assembler_free(&as);
        return 2;
    }
    if (output && strcmp(output, "") == 0) {
        fprintf(stderr, "error: output file must not be empty\n");
        free(inputs);
        kasm_assembler_free(&as);
        return 2;
    }
    if (!valid_format(format)) {
        fprintf(stderr, "error: unknown output format '%s'\n", format);
        free(inputs);
        kasm_assembler_free(&as);
        return 2;
    }
    if (input_count > 1 && format_set &&
        strcmp(format, "elf64-obj") != 0 && strcmp(format, "obj") != 0) {
        fprintf(stderr, "error: multiple input files only support object output\n");
        free(inputs);
        kasm_assembler_free(&as);
        return 2;
    }

    const char *env_path = getenv("KASM_INCLUDE_PATH");
    if (env_path && *env_path) {
        char *tmp = kasm_xstrdup(env_path);
        char *part = strtok(tmp, ":");
        while (part) {
            if (*part)
                kasm_add_include_path(&as, part);
            part = strtok(NULL, ":");
        }
        free(tmp);
    }
    if (!as.no_stdlib) {
        kasm_add_include_path(&as, "lib/kasm");
        kasm_add_include_path(&as, "lib");
        add_exe_relative_include(&as, argv[0]);
        kasm_add_include_path(&as, KASM_INSTALL_LIB);
    }
    if (as.print_include_paths) {
        for (size_t pi = 0; pi < as.include_path_count; pi++)
            printf("%s\n", as.include_paths[pi]);
        free(inputs);
        kasm_assembler_free(&as);
        return 0;
    }
    if (as.print_std_path) {
        puts("lib/kasm");
        puts(KASM_INSTALL_LIB);
        free(inputs);
        kasm_assembler_free(&as);
        return 0;
    }

    if (!input_count) {
            fprintf(stderr, "error: missing input file\n");
        usage(stderr);
        free(inputs);
        kasm_assembler_free(&as);
        return 2;
    }

    char **outputs = kasm_xrealloc(NULL, input_count * sizeof(char *));
    for (size_t i = 0; i < input_count; i++) {
        outputs[i] = NULL;
        const char *effective_format = input_count > 1 ? "elf64-obj" : format;
        outputs[i] = output ? kasm_xstrdup(output) :
                     (has_output_mode(&as) && input_count == 1 ? NULL :
                      default_output_path(inputs[i], effective_format, input_count > 1));
        if (outputs[i] && strcmp(outputs[i], inputs[i]) == 0) {
            fprintf(stderr, "error: output file must not be the same as input file\n");
            for (size_t j = 0; j <= i; j++)
                free(outputs[j]);
            free(outputs);
            free(inputs);
            kasm_assembler_free(&as);
            return 2;
        }
        for (size_t j = 0; j < i; j++) {
            if (outputs[i] && outputs[j] && strcmp(outputs[i], outputs[j]) == 0) {
                fprintf(stderr, "error: duplicate output filename '%s'\n", outputs[i]);
                for (size_t k = 0; k <= i; k++)
                    free(outputs[k]);
                free(outputs);
                free(inputs);
                kasm_assembler_free(&as);
                return 2;
            }
        }
    }

    int status = 0;
    for (size_t i = 0; i < input_count; i++) {
        const char *effective_format = input_count > 1 ? "elf64-obj" : format;
        if (assemble_one(&as, inputs[i], outputs[i], effective_format) != 0)
            status = 1;
    }
    for (size_t i = 0; i < input_count; i++)
        free(outputs[i]);
    free(outputs);
    free(inputs);
    kasm_assembler_free(&as);
    return status;
}
