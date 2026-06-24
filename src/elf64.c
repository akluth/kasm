#include "diagnostics.h"
#include "elf64.h"
#include "symbols.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/stat.h>
#endif

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
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

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
} Elf64_Shdr;

typedef struct {
    uint32_t st_name;
    unsigned char st_info;
    unsigned char st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Elf64_Sym;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
} Elf64_Rela;

#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHF_WRITE 1
#define SHF_ALLOC 2
#define SHF_EXECINSTR 4
#define SHN_UNDEF 0
#define SHN_ABS 0xfff1
#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STT_NOTYPE 0
#define STT_SECTION 3
#define R_X86_64_64 1
#define R_X86_64_PC32 2

static uint32_t str_add(ByteBuf *b, const char *s)
{
    uint32_t off = (uint32_t)b->len;
    kasm_buf_append(b, s, strlen(s) + 1);
    return off;
}

static void sym_add(ByteBuf *b, uint32_t name, unsigned char bind, unsigned char type,
                    uint16_t shndx, uint64_t value)
{
    Elf64_Sym sym;
    memset(&sym, 0, sizeof(sym));
    sym.st_name = name;
    sym.st_info = (unsigned char)((bind << 4) | (type & 0xf));
    sym.st_shndx = shndx;
    sym.st_value = value;
    kasm_buf_append(b, &sym, sizeof(sym));
}

static uint16_t obj_section_index(SectionId sec)
{
    if (sec == SEC_TEXT) return 1;
    if (sec == SEC_RODATA) return 2;
    if (sec == SEC_DATA) return 3;
    return SHN_UNDEF;
}

static int reloc_count(Assembler *as, SectionId sec)
{
    int n = 0;
    for (size_t i = 0; i < as->relocs.len; i++)
        if (as->relocs.items[i].section == sec)
            n++;
    return n;
}

static void write_relas_for(Assembler *as, SectionId sec, ByteBuf *out)
{
    for (size_t i = 0; i < as->relocs.len; i++) {
        Reloc *r = &as->relocs.items[i];
        if (r->section != sec)
            continue;
        Symbol *sym = kasm_symbol_find(&as->symbols, r->symbol);
        Elf64_Rela rela;
        memset(&rela, 0, sizeof(rela));
        rela.r_offset = r->offset;
        rela.r_info = ((uint64_t)(sym ? sym->sym_index : 0) << 32) |
                      (uint32_t)(r->kind == RELOC_64 ? R_X86_64_64 : R_X86_64_PC32);
        rela.r_addend = r->addend;
        kasm_buf_append(out, &rela, sizeof(rela));
    }
}

static int write_pad(FILE *f, uint64_t at, uint64_t target)
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

static int write_section(FILE *f, uint64_t *pos, uint64_t off, ByteBuf *sec)
{
    if (!write_pad(f, *pos, off))
        return 0;
    *pos = off;
    if (sec->len && fwrite(sec->data, 1, sec->len, f) != sec->len)
        return 0;
    *pos += sec->len;
    return 1;
}

int kasm_write_bin(Assembler *as, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        kasm_error(as, (SourceLoc){ path, 1, 1 }, "file write error");
        return 0;
    }
    for (int i = 0; i < SEC_COUNT; i++) {
        if (as->sections[i].len &&
            fwrite(as->sections[i].data, 1, as->sections[i].len, f) != as->sections[i].len) {
            fclose(f);
            kasm_error(as, (SourceLoc){ path, 1, 1 }, "file write error");
            return 0;
        }
    }
    fclose(f);
    return 1;
}

int kasm_write_elf64_obj(Assembler *as, const char *path)
{
    int have_rela_text = reloc_count(as, SEC_TEXT) > 0;
    int have_rela_rodata = reloc_count(as, SEC_RODATA) > 0;
    int have_rela_data = reloc_count(as, SEC_DATA) > 0;
    int idx = 4;
    int rela_text_idx = have_rela_text ? idx++ : 0;
    int rela_rodata_idx = have_rela_rodata ? idx++ : 0;
    int rela_data_idx = have_rela_data ? idx++ : 0;
    int symtab_idx = idx++;
    int strtab_idx = idx++;
    int shstrtab_idx = idx++;
    int shnum = idx;

    ByteBuf shstr = { 0 };
    uint32_t sh_name[16];
    sh_name[0] = 0;
    sh_name[1] = str_add(&shstr, ".text");
    sh_name[2] = str_add(&shstr, ".rodata");
    sh_name[3] = str_add(&shstr, ".data");
    if (have_rela_text) sh_name[rela_text_idx] = str_add(&shstr, ".rela.text");
    if (have_rela_rodata) sh_name[rela_rodata_idx] = str_add(&shstr, ".rela.rodata");
    if (have_rela_data) sh_name[rela_data_idx] = str_add(&shstr, ".rela.data");
    sh_name[symtab_idx] = str_add(&shstr, ".symtab");
    sh_name[strtab_idx] = str_add(&shstr, ".strtab");
    sh_name[shstrtab_idx] = str_add(&shstr, ".shstrtab");

    ByteBuf strtab = { 0 };
    str_add(&strtab, "");
    ByteBuf symtab = { 0 };
    sym_add(&symtab, 0, STB_LOCAL, STT_NOTYPE, SHN_UNDEF, 0);
    sym_add(&symtab, 0, STB_LOCAL, STT_SECTION, 1, 0);
    sym_add(&symtab, 0, STB_LOCAL, STT_SECTION, 2, 0);
    sym_add(&symtab, 0, STB_LOCAL, STT_SECTION, 3, 0);
    uint32_t sym_index = 4;

    for (size_t i = 0; i < as->symbols.len; i++) {
        Symbol *s = &as->symbols.items[i];
        if (!s->defined || s->is_global || s->is_extern)
            continue;
        uint16_t shndx = s->is_const ? SHN_ABS : obj_section_index(s->section);
        uint64_t value = s->is_const ? (uint64_t)s->value : s->offset;
        s->sym_index = sym_index++;
        sym_add(&symtab, str_add(&strtab, s->name), STB_LOCAL, STT_NOTYPE, shndx, value);
    }
    uint32_t first_global = sym_index;
    for (size_t i = 0; i < as->symbols.len; i++) {
        Symbol *s = &as->symbols.items[i];
        if (!s->is_global && !s->is_extern)
            continue;
        uint16_t shndx = s->is_extern ? SHN_UNDEF :
                         (s->is_const ? SHN_ABS : obj_section_index(s->section));
        uint64_t value = s->is_extern ? 0 :
                         (s->is_const ? (uint64_t)s->value : s->offset);
        s->sym_index = sym_index++;
        sym_add(&symtab, str_add(&strtab, s->name), STB_GLOBAL, STT_NOTYPE, shndx, value);
    }

    ByteBuf rela_text = { 0 }, rela_rodata = { 0 }, rela_data = { 0 };
    write_relas_for(as, SEC_TEXT, &rela_text);
    write_relas_for(as, SEC_RODATA, &rela_rodata);
    write_relas_for(as, SEC_DATA, &rela_data);

    Elf64_Shdr *sh = kasm_xrealloc(NULL, (size_t)shnum * sizeof(Elf64_Shdr));
    memset(sh, 0, (size_t)shnum * sizeof(Elf64_Shdr));
    uint64_t off = sizeof(Elf64_Ehdr);

#define PLACE_SECTION(n, buf, align) do { \
        off = kasm_align(off, (align)); \
        sh[(n)].sh_offset = off; \
        sh[(n)].sh_size = (buf).len; \
        off += (buf).len; \
    } while (0)

    sh[1].sh_name = sh_name[1];
    sh[1].sh_type = SHT_PROGBITS;
    sh[1].sh_flags = SHF_ALLOC | SHF_EXECINSTR;
    sh[1].sh_addralign = 16;
    PLACE_SECTION(1, as->sections[SEC_TEXT], 16);

    sh[2].sh_name = sh_name[2];
    sh[2].sh_type = SHT_PROGBITS;
    sh[2].sh_flags = SHF_ALLOC;
    sh[2].sh_addralign = 8;
    PLACE_SECTION(2, as->sections[SEC_RODATA], 8);

    sh[3].sh_name = sh_name[3];
    sh[3].sh_type = SHT_PROGBITS;
    sh[3].sh_flags = SHF_ALLOC | SHF_WRITE;
    sh[3].sh_addralign = 8;
    PLACE_SECTION(3, as->sections[SEC_DATA], 8);

    if (have_rela_text) {
        sh[rela_text_idx].sh_name = sh_name[rela_text_idx];
        sh[rela_text_idx].sh_type = SHT_RELA;
        sh[rela_text_idx].sh_link = (uint32_t)symtab_idx;
        sh[rela_text_idx].sh_info = 1;
        sh[rela_text_idx].sh_addralign = 8;
        sh[rela_text_idx].sh_entsize = sizeof(Elf64_Rela);
        PLACE_SECTION(rela_text_idx, rela_text, 8);
    }
    if (have_rela_rodata) {
        sh[rela_rodata_idx].sh_name = sh_name[rela_rodata_idx];
        sh[rela_rodata_idx].sh_type = SHT_RELA;
        sh[rela_rodata_idx].sh_link = (uint32_t)symtab_idx;
        sh[rela_rodata_idx].sh_info = 2;
        sh[rela_rodata_idx].sh_addralign = 8;
        sh[rela_rodata_idx].sh_entsize = sizeof(Elf64_Rela);
        PLACE_SECTION(rela_rodata_idx, rela_rodata, 8);
    }
    if (have_rela_data) {
        sh[rela_data_idx].sh_name = sh_name[rela_data_idx];
        sh[rela_data_idx].sh_type = SHT_RELA;
        sh[rela_data_idx].sh_link = (uint32_t)symtab_idx;
        sh[rela_data_idx].sh_info = 3;
        sh[rela_data_idx].sh_addralign = 8;
        sh[rela_data_idx].sh_entsize = sizeof(Elf64_Rela);
        PLACE_SECTION(rela_data_idx, rela_data, 8);
    }

    sh[symtab_idx].sh_name = sh_name[symtab_idx];
    sh[symtab_idx].sh_type = SHT_SYMTAB;
    sh[symtab_idx].sh_link = (uint32_t)strtab_idx;
    sh[symtab_idx].sh_info = first_global;
    sh[symtab_idx].sh_addralign = 8;
    sh[symtab_idx].sh_entsize = sizeof(Elf64_Sym);
    PLACE_SECTION(symtab_idx, symtab, 8);

    sh[strtab_idx].sh_name = sh_name[strtab_idx];
    sh[strtab_idx].sh_type = SHT_STRTAB;
    sh[strtab_idx].sh_addralign = 1;
    PLACE_SECTION(strtab_idx, strtab, 1);

    sh[shstrtab_idx].sh_name = sh_name[shstrtab_idx];
    sh[shstrtab_idx].sh_type = SHT_STRTAB;
    sh[shstrtab_idx].sh_addralign = 1;
    PLACE_SECTION(shstrtab_idx, shstr, 1);

    off = kasm_align(off, 8);
    uint64_t shoff = off;

    Elf64_Ehdr eh;
    memset(&eh, 0, sizeof(eh));
    eh.e_ident[0] = 0x7f;
    eh.e_ident[1] = 'E';
    eh.e_ident[2] = 'L';
    eh.e_ident[3] = 'F';
    eh.e_ident[4] = 2;
    eh.e_ident[5] = 1;
    eh.e_ident[6] = 1;
    eh.e_type = 1;
    eh.e_machine = 62;
    eh.e_version = 1;
    eh.e_shoff = shoff;
    eh.e_ehsize = sizeof(Elf64_Ehdr);
    eh.e_shentsize = sizeof(Elf64_Shdr);
    eh.e_shnum = (uint16_t)shnum;
    eh.e_shstrndx = (uint16_t)shstrtab_idx;

    FILE *f = fopen(path, "wb");
    if (!f) {
        kasm_error(as, (SourceLoc){ path, 1, 1 }, "file write error");
        goto fail;
    }
    uint64_t pos = 0;
    if (fwrite(&eh, 1, sizeof(eh), f) != sizeof(eh))
        goto write_fail;
    pos = sizeof(eh);
#define WRITE_PLACED(n, buf) do { \
        if (!write_section(f, &pos, sh[(n)].sh_offset, &(buf))) goto write_fail; \
    } while (0)
    WRITE_PLACED(1, as->sections[SEC_TEXT]);
    WRITE_PLACED(2, as->sections[SEC_RODATA]);
    WRITE_PLACED(3, as->sections[SEC_DATA]);
    if (have_rela_text) WRITE_PLACED(rela_text_idx, rela_text);
    if (have_rela_rodata) WRITE_PLACED(rela_rodata_idx, rela_rodata);
    if (have_rela_data) WRITE_PLACED(rela_data_idx, rela_data);
    WRITE_PLACED(symtab_idx, symtab);
    WRITE_PLACED(strtab_idx, strtab);
    WRITE_PLACED(shstrtab_idx, shstr);
    if (!write_pad(f, pos, shoff))
        goto write_fail;
    if (fwrite(sh, sizeof(Elf64_Shdr), (size_t)shnum, f) != (size_t)shnum)
        goto write_fail;
    fclose(f);
    free(sh);
    free(shstr.data);
    free(strtab.data);
    free(symtab.data);
    free(rela_text.data);
    free(rela_rodata.data);
    free(rela_data.data);
    return 1;

write_fail:
    fclose(f);
    kasm_error(as, (SourceLoc){ path, 1, 1 }, "file write error");
fail:
    free(sh);
    free(shstr.data);
    free(strtab.data);
    free(symtab.data);
    free(rela_text.data);
    free(rela_rodata.data);
    free(rela_data.data);
    return 0;
}

int kasm_write_elf64(Assembler *as, const char *path)
{
    Symbol *entry = kasm_symbol_find(&as->symbols, as->entry ? as->entry : "");
    if (!entry || entry->is_const) {
        kasm_error(as, (SourceLoc){ as->path, 1, 1 }, "missing entry symbol");
        return 0;
    }

    uint64_t base = 0x400000;
    uint64_t phoff = 64;
    uint16_t phnum = as->tiny ? 1 : 3;
    uint64_t file_align = as->tiny ? 1 : 0x1000;
    uint64_t text_off = kasm_align(64 + phnum * 56, file_align);
    uint64_t ro_off = kasm_align(text_off + as->sections[SEC_TEXT].len, file_align);
    uint64_t data_off = kasm_align(ro_off + as->sections[SEC_RODATA].len, file_align);
    as->sections[SEC_TEXT].vaddr = base + text_off;
    as->sections[SEC_RODATA].vaddr = base + ro_off;
    as->sections[SEC_DATA].vaddr = base + data_off;

    Elf64_Ehdr eh;
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
    eh.e_entry = as->sections[entry->section].vaddr + entry->offset;
    eh.e_phoff = phoff;
    eh.e_ehsize = 64;
    eh.e_phentsize = 56;
    eh.e_phnum = phnum;

    Elf64_Phdr ph[3];
    memset(ph, 0, sizeof(ph));
    if (as->tiny) {
        ph[0].p_type = 1;
        ph[0].p_flags = 7;
        ph[0].p_offset = 0;
        ph[0].p_vaddr = base;
        ph[0].p_paddr = base;
        ph[0].p_filesz = data_off + as->sections[SEC_DATA].len;
        ph[0].p_memsz = ph[0].p_filesz;
        ph[0].p_align = 0x1000;
    } else {
        ph[0].p_type = 1;
        ph[0].p_flags = 5;
        ph[0].p_offset = text_off;
        ph[0].p_vaddr = as->sections[SEC_TEXT].vaddr;
        ph[0].p_paddr = ph[0].p_vaddr;
        ph[0].p_filesz = as->sections[SEC_TEXT].len;
        ph[0].p_memsz = ph[0].p_filesz;
        ph[0].p_align = file_align;

        ph[1].p_type = 1;
        ph[1].p_flags = 4;
        ph[1].p_offset = ro_off;
        ph[1].p_vaddr = as->sections[SEC_RODATA].vaddr;
        ph[1].p_paddr = ph[1].p_vaddr;
        ph[1].p_filesz = as->sections[SEC_RODATA].len;
        ph[1].p_memsz = ph[1].p_filesz;
        ph[1].p_align = file_align;

        ph[2].p_type = 1;
        ph[2].p_flags = 6;
        ph[2].p_offset = data_off;
        ph[2].p_vaddr = as->sections[SEC_DATA].vaddr;
        ph[2].p_paddr = ph[2].p_vaddr;
        ph[2].p_filesz = as->sections[SEC_DATA].len;
        ph[2].p_memsz = ph[2].p_filesz;
        ph[2].p_align = file_align;
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        kasm_error(as, (SourceLoc){ path, 1, 1 }, "file write error");
        return 0;
    }
    uint64_t pos = 0;
    if (fwrite(&eh, 1, sizeof(eh), f) != sizeof(eh) ||
        fwrite(ph, 1, (size_t)phnum * 56, f) != (size_t)phnum * 56) {
        fclose(f);
        kasm_error(as, (SourceLoc){ path, 1, 1 }, "file write error");
        return 0;
    }
    pos = sizeof(eh) + (uint64_t)phnum * 56;
    if (!write_section(f, &pos, text_off, &as->sections[SEC_TEXT]) ||
        !write_section(f, &pos, ro_off, &as->sections[SEC_RODATA]) ||
        !write_section(f, &pos, data_off, &as->sections[SEC_DATA])) {
        fclose(f);
        kasm_error(as, (SourceLoc){ path, 1, 1 }, "file write error");
        return 0;
    }
    fclose(f);
#ifndef _WIN32
    chmod(path, 0755);
#endif
    return 1;
}
