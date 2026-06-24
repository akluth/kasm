#ifndef KASM_H
#define KASM_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    SEC_TEXT = 0,
    SEC_RODATA = 1,
    SEC_DATA = 2,
    SEC_COUNT = 3,
    SEC_NONE = -1
} SectionId;

typedef enum {
    ST_ENTRY,
    ST_GLOBAL,
    ST_EXTERN,
    ST_SECTION,
    ST_LABEL,
    ST_CONST,
    ST_DATA,
    ST_INSTR
} StmtType;

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
    uint64_t vaddr;
} ByteBuf;

typedef struct {
    char *name;
    SectionId section;
    uint64_t offset;
    int is_const;
    int64_t value;
    int line;
    int column;
    int defined;
    int is_global;
    int is_extern;
    uint32_t sym_index;
} Symbol;

typedef struct {
    Symbol *items;
    size_t len;
    size_t cap;
} SymbolTable;

typedef struct {
    char *text;
    int column;
} Operand;

typedef struct {
    StmtType type;
    SectionId section;
    uint64_t offset;
    uint32_t size;
    int line;
    int column;
    char *source;
    char *name;
    char *op;
    Operand operands[8];
    int operand_count;
    char *expr;
} Statement;

typedef struct {
    Statement *items;
    size_t len;
    size_t cap;
} Program;

typedef enum {
    FIELD_BYTE,
    FIELD_WORD,
    FIELD_DWORD,
    FIELD_QWORD,
    FIELD_BYTES
} FieldType;

typedef struct {
    char *name;
    FieldType type;
    uint64_t offset;
    uint64_t size;
} StructField;

typedef struct {
    char *name;
    StructField *fields;
    size_t field_count;
    size_t field_cap;
    uint64_t size;
} StructDef;

typedef struct {
    StructDef *items;
    size_t len;
    size_t cap;
} StructTable;

typedef enum {
    RELOC_PC32,
    RELOC_64
} RelocKind;

typedef struct {
    SectionId section;
    uint64_t offset;
    char *symbol;
    RelocKind kind;
    int64_t addend;
    int line;
    int column;
} Reloc;

typedef struct {
    Reloc *items;
    size_t len;
    size_t cap;
} RelocList;

typedef struct {
    const char *path;
    Program program;
    SymbolTable symbols;
    StructTable structs;
    RelocList relocs;
    ByteBuf sections[SEC_COUNT];
    char *entry;
    int explain;
    int explain_mode;
    const char *explain_path;
    FILE *explain_file;
    const char *map_path;
    const char *list_path;
    FILE *list_file;
    int dump_symbols;
    int dump_sections;
    int dump_all;
    int dump_ir;
    int dump_relocs;
    int dump_structs;
    int dump_expanded;
    int dump_tokens;
    int elf_info;
    int teach;
    int teach_level;
    int object_mode;
    int raw_mode;
    int no_stdlib;
    int no_syscall_sugar;
    int print_include_paths;
    int tiny;
    int tiny_report;
    int hints;
    int hint_perf;
    int hint_abi;
    int hint_size;
    const char *hints_cpu;
    uint64_t tiny_jumps_shortened;
    uint64_t tiny_near_jumps;
    uint64_t tiny_imm8_used;
    uint64_t tiny_bytes_saved;
    uint64_t tiny_final_size;
    int errors;
    char **source_lines;
    size_t source_line_count;
    size_t source_line_cap;
    char **include_paths;
    size_t include_path_count;
    size_t include_path_cap;
} Assembler;

typedef struct {
    const char *path;
    int line;
    int column;
} SourceLoc;

void *kasm_xrealloc(void *ptr, size_t size);
char *kasm_xstrdup(const char *s);
char *kasm_trim(char *s);
int kasm_streq_ci(const char *a, const char *b);
int kasm_parse_int(const char *s, int64_t *out);
void kasm_lower_ascii(char *s);
int kasm_column_of(const char *line, const char *token);
const char *kasm_section_name(SectionId id);
SectionId kasm_section_from_name(const char *name);
void kasm_buf_append(ByteBuf *buf, const void *data, size_t len);
void kasm_buf_append_u8(ByteBuf *buf, uint8_t v);
void kasm_buf_append_u16(ByteBuf *buf, uint16_t v);
void kasm_buf_append_u32(ByteBuf *buf, uint32_t v);
void kasm_buf_append_u64(ByteBuf *buf, uint64_t v);
uint64_t kasm_align(uint64_t value, uint64_t align);
void kasm_program_free(Program *program);
void kasm_assembler_free(Assembler *as);
void kasm_add_reloc(Assembler *as, SectionId section, uint64_t offset,
                    const char *symbol, RelocKind kind, int64_t addend,
                    SourceLoc loc);
void kasm_add_include_path(Assembler *as, const char *path);
StructDef *kasm_struct_find(Assembler *as, const char *name);

#endif
