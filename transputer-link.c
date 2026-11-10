#ifdef TARGET_DEFS_ONLY
#define EM_TCC_TARGET EM_TRANSPUTER
#define R_DATA_32 R_TRANSPUTER_32
#define R_DATA_PTR R_TRANSPUTER_32
#define R_JMP_SLOT R_TRANSPUTER_JUMP_SLOT
#define R_GLOB_DAT R_TRANSPUTER_32
#define R_COPY R_TRANSPUTER_COPY
#define R_RELATIVE R_TRANSPUTER_RELATIVE
#define R_NUM R_TRANSPUTER_NUM
#define ELF_START_ADDR 0x80000070 /* T800 MemStart (MinInt+0x70) - T414 uses 0x80000048 */
#define ELF_PAGE_SIZE 0x1000
/* Transputer is a bare-metal embedded target - no dynamic linker */
/* Use "-" like TCC_TARGET_PE to indicate no INTERP section needed */
#ifndef CONFIG_TCC_ELFINTERP
#define CONFIG_TCC_ELFINTERP "-"
#endif
#define PCRELATIVE_DLLPLT 1
#define RELOCATE_DLLPLT 1
ST_FUNC int transputer_simple_link(TCCState *s1, const char *output_filename);
ST_FUNC int tp_reloc_width(int type, int executable);
ST_FUNC int tp_data_layout_reloc(int type);
#else /* !TARGET_DEFS_ONLY */
#include "tcc.h"
#include "transputer-enc.h"  /* Unified prefix encoding */
#include "transputer-opcodes.h"

/* Forward declaration */
static int tp_env_enabled(const char *name);
static const char *tp_reloc_symbol_name(Section *symtab_sec, ElfW(Sym) *sym);
static int tp_patch_ldc_abs(uint8_t *slot, int width, int op, uint32_t addr, uint32_t val);
static int tp_patch_iprel(uint8_t *slot, int width, int op, uint32_t addr, uint32_t val);

/*
 * Which part code is for.  Every object records, in its ELF e_flags, what
 * the part it was compiled or assembled for has beyond the T414 (TP_EF_*),
 * and a link given a -m target refuses one that needs more than that part
 * has: the -mt9000 runtime in an -mt800 link, an FPU object in an -mt425
 * one, TVX code anywhere but -mt9000tvx.  A link without -m takes anything.
 * Objects from before 2026-11-03 carry 0 and so pass everywhere.
 */
static const struct { const char *name; unsigned char isa; } tp_parts[] = {
    { "t9000tvx", TP_ISA_T9000TVX },
    { "t9000",    TP_ISA_T9000 },
    { "t805",     TP_ISA_T805 },
    { "no-t9000", TP_ISA_T805 },
    { "t800",     TP_ISA_T800 },
    { "t425",     TP_ISA_T425 },
    { "t414",     TP_ISA_T414 },
};

/* The first name in tp_parts for a part with exactly `isa`, or NULL. */
static const char *tp_part_name(unsigned isa)
{
    int k;

    for (k = 0; k < countof(tp_parts); k++)
        if (tp_parts[k].isa == isa)
            return tp_parts[k].name;
    return NULL;
}

ST_FUNC int tp_select_target(TCCState *s1, const char *name)
{
    int k;

    for (k = 0; k < countof(tp_parts); k++)
        if (!strcmp(name, tp_parts[k].name)) {
            s1->tp_isa = tp_parts[k].isa;
            return 1;
        }
    return 0;
}

ST_FUNC const char *tp_target_name(TCCState *s1)
{
    return tp_part_name(s1->tp_isa);
}

/* An object being linked carries `flags` in its e_flags; `member` names it
 * when it comes from an archive. */
ST_FUNC int tp_check_object_flags(TCCState *s1, unsigned flags,
                                  const char *member)
{
    static const char *const what[] = {
        "T800 instructions", "T805 instructions", "the FPU",
        "T9000 instructions", "TVX"
    };
    unsigned extra = flags & ~s1->tp_isa & ((TP_EF_TVX << 1) - 1);
    char needs[96];
    /* 0 is also what objects from before the flags existed carry */
    const char *built = flags ? tp_part_name(flags) : NULL;
    int k;

    s1->tp_isa_used |= flags;
    if (!s1->tp_link_check || !extra)
        return 0;
    needs[0] = 0;
    for (k = 0; k < countof(what); k++) {
        if (!(extra & 1u << k))
            continue;
        pstrcat(needs, sizeof needs, what[k]);
        extra &= ~(1u << k);
        if (extra)
            pstrcat(needs, sizeof needs,
                    (extra & (extra - 1)) ? ", " : " and ");
    }
    return tcc_error_noabort("%s%s%s%s%s needs %s, which -m%s does not have; "
                             "rebuild it for -m%s or link for a bigger part",
                             member ? "archive member " : "this object",
                             member ? member : "",
                             built ? " (compiled for -m" : "",
                             built ? built : "", built ? ")" : "",
                             needs, tp_target_name(s1), tp_target_name(s1));
}

/*
 * Hooks from tcc_load_object_file (struct tp_objload in tcc.h).
 *
 * Tracing, each switched on by an environment variable:
 *   T4_RELOC_LOG            RELOC_LOAD: every relocation as it is loaded;
 *     T4_RELOC_SYM=<text>     only for symbols containing <text>
 *     T4_RELOC_TYPE=<n>       only of type <n>
 *     T4_RELOC_LOAD_DUMP      plus the bytes at an IPREL32 and what they decode to
 *   T4_RELOC_SUMMARY        RELOC_SUMMARY: IPREL32 counts per symbol per object
 *   T4_RELOC_SYMINFO[=<text>]  RELOC_SYMINFO: the symbol behind each relocation
 *   T4_RELOC_LDC_MISMATCH   an IPREL32 that sits on an ldc
 *   T4_SYMMERGE_DEBUG=<text>  SYMMERGE / SYMMERGE_RELOC: how symbols containing
 *                           <text> are renumbered
 */
static struct {
    int init;
    int log;
    const char *sym_filter;
    int type_filter;
    int load_dump;
    int summary;
    int syminfo;
    const char *syminfo_filter;
    int ldc_mismatch;
    const char *symmerge;
} tp_lt;

static void tp_load_trace_init(void)
{
    const char *env;

    if (tp_lt.init)
        return;
    tp_lt.init = 1;
    env = getenv("T4_RELOC_LOG");
    tp_lt.log = env && *env;
    tp_lt.sym_filter = getenv("T4_RELOC_SYM");
    tp_lt.type_filter = -1;
    env = getenv("T4_RELOC_TYPE");
    if (env && *env)
        tp_lt.type_filter = (int)strtol(env, NULL, 0);
    env = getenv("T4_RELOC_LOAD_DUMP");
    tp_lt.load_dump = env && *env;
    env = getenv("T4_RELOC_SUMMARY");
    tp_lt.summary = env && *env;
    env = getenv("T4_RELOC_SYMINFO");
    tp_lt.syminfo = env && *env;
    tp_lt.syminfo_filter = env && strcmp(env, "1") ? env : NULL;
    env = getenv("T4_RELOC_LDC_MISMATCH");
    tp_lt.ldc_mismatch = env && *env;
    tp_lt.symmerge = getenv("T4_SYMMERGE_DEBUG");
}

static const char *tp_objload_file(struct tp_objload *o)
{
    return o->s1->current_filename ? o->s1->current_filename : "<unknown>";
}

ST_FUNC void tp_objload_sym(struct tp_objload *o, int i, int new_index)
{
    ElfW(Sym) *sym = &o->symtab[i];
    const char *name = o->strtab + sym->st_name;

    tp_load_trace_init();
    if (tp_lt.symmerge && name && strstr(name, tp_lt.symmerge)) {
        fprintf(stderr, "SYMMERGE: file=%s sym='%s' old_idx=%d new_idx=%d "
                "bind=%d type=%d shndx=%d value=0x%lx\n",
                tp_objload_file(o), name, i, new_index,
                ELFW(ST_BIND)(sym->st_info), ELFW(ST_TYPE)(sym->st_info),
                sym->st_shndx, (unsigned long)sym->st_value);
    }
}

/* Count an IPREL32 against its symbol for RELOC_SUMMARY. */
static void tp_objload_count_iprel(struct tp_objload *o, unsigned sym_index)
{
    const char *sum_name = "<unknown>";
    int si;

    if (o->strtab && o->symtab[sym_index].st_name)
        sum_name = o->strtab + o->symtab[sym_index].st_name;
    for (si = 0; si < o->iprel_count; si++) {
        if (o->iprel_syms[si] == sum_name ||
            (o->iprel_syms[si] && sum_name &&
             0 == strcmp(o->iprel_syms[si], sum_name))) {
            o->iprel_counts[si]++;
            return;
        }
    }
    if (o->iprel_count == o->iprel_cap) {
        o->iprel_cap = o->iprel_cap ? o->iprel_cap * 2 : 16;
        o->iprel_syms = tcc_realloc(o->iprel_syms,
                                    o->iprel_cap * sizeof(*o->iprel_syms));
        o->iprel_counts = tcc_realloc(o->iprel_counts,
                                      o->iprel_cap * sizeof(*o->iprel_counts));
    }
    o->iprel_syms[o->iprel_count] = sum_name;
    o->iprel_counts[o->iprel_count] = 1;
    o->iprel_count++;
}

/* The first byte at or after `off` in `ts` that is not a pfix/nfix: the
 * instruction a relocation sits on.  Returns its top nibble in place, or
 * -1, and how many prefix bytes came before it. */
static int tp_objload_opcode_at(Section *ts, unsigned long off, int *pscan)
{
    unsigned char *data = (unsigned char *)ts->data;
    int opcode = -1;
    int scan = 0;

    for (; off + scan < ts->data_offset && scan < 16; scan++) {
        unsigned char hi = data[off + scan] & 0xF0;
        if (hi != 0x20 && hi != 0x60) {
            opcode = hi;
            break;
        }
    }
    *pscan = scan;
    return opcode;
}

ST_FUNC int tp_objload_reloc(struct tp_objload *o, ElfW(Shdr) *sh,
                             ElfW_Rel *rel, Section *ts,
                             unsigned long offseti)
{
    TCCState *s1 = o->s1;       /* for tcc_error_noabort */
    int type = ELFW(R_TYPE)(rel->r_info);
    unsigned sym_index = ELFW(R_SYM)(rel->r_info);
    ElfW(Shdr) *target = &o->shdr[sh->sh_info];
    const char *sym_name = "<unknown>";
    const char *sec_name = "<unknown>";

    tp_load_trace_init();
    /* Check the original input extent before merging sections:
     * the next object's data must never complete this field. */
    if (target->sh_flags & SHF_ALLOC) {
        int width = tp_reloc_width(type, target->sh_flags & SHF_EXECINSTR);
        if (width > 0 && (rel->r_offset > target->sh_size
            || target->sh_size - rel->r_offset < (unsigned)width)) {
            tcc_error_noabort("relocation %d outside %s at offset 0x%lx",
                              type, o->strsec + target->sh_name,
                              (unsigned long)rel->r_offset);
            return -1;
        }
    }
    if (sym_index >= (unsigned)o->nb_syms)
        return 0;       /* the caller reports it */

    if (tp_lt.summary && type == R_TRANSPUTER_IPREL32)
        tp_objload_count_iprel(o, sym_index);

    if (tp_lt.log || tp_lt.syminfo) {
        if (o->strtab && o->symtab[sym_index].st_name)
            sym_name = o->strtab + o->symtab[sym_index].st_name;
        if (sh->sh_info < (unsigned)o->shnum)
            sec_name = o->strsec + target->sh_name;
        if ((!tp_lt.sym_filter || strstr(sym_name, tp_lt.sym_filter)) &&
            (tp_lt.type_filter < 0 || tp_lt.type_filter == type)) {
            fprintf(stderr,
                    "RELOC_LOAD: file=%s member=%s section=%s r_offset=0x%lx type=%d sym=%s\n",
                    tp_objload_file(o), o->member ? o->member : "<none>",
                    sec_name, (unsigned long)rel->r_offset, type, sym_name);
            if (tp_lt.load_dump && type == R_TRANSPUTER_IPREL32) {
                unsigned long off = rel->r_offset + offseti;
                if (ts && ts->data && off < ts->data_offset) {
                    unsigned long start = off >= 8 ? off - 8 : 0;
                    unsigned long end = off + 8;
                    unsigned char *data = (unsigned char *)ts->data;
                    unsigned long di;
                    int opcode, scan;
                    const char *kind = "unknown";
                    if (end >= ts->data_offset)
                        end = ts->data_offset - 1;
                    fprintf(stderr, "RELOC_LOAD_BYTES:");
                    for (di = start; di <= end; di++)
                        fprintf(stderr, " %02x", data[di]);
                    fprintf(stderr, "\n");
                    opcode = tp_objload_opcode_at(ts, off, &scan);
                    if (opcode >= 0)
                        opcode >>= 4;
                    if (opcode == 0x9)
                        kind = "call";
                    else if (opcode == 0x0)
                        kind = "j";
                    else if (opcode == 0xA)
                        kind = "cj";
                    else if (opcode == 0x4)
                        kind = "ldc";
                    fprintf(stderr,
                            "RELOC_LOAD_DECODE: start=0x%lx opcode=0x%x kind=%s scan=%d\n",
                            off, opcode, kind, scan);
                }
            }
        }
        if (tp_lt.ldc_mismatch && type == R_TRANSPUTER_IPREL32) {
            unsigned long off = rel->r_offset + offseti;
            int scan;
            if (ts && ts->data && off < ts->data_offset
                && tp_objload_opcode_at(ts, off, &scan) == 0x40) {
                fprintf(stderr,
                        "RELOC_LDC_MISMATCH: file=%s member=%s section=%s r_offset=0x%lx sym=%s scan=%d\n",
                        tp_objload_file(o), o->member ? o->member : "<none>",
                        sec_name, (unsigned long)rel->r_offset, sym_name, scan);
            }
        }
        if (tp_lt.syminfo && (!tp_lt.syminfo_filter ||
            strstr(sym_name, tp_lt.syminfo_filter))) {
            unsigned char st_info = o->symtab[sym_index].st_info;
            fprintf(stderr,
                    "RELOC_SYMINFO: sym=%s st_info=0x%x bind=%u type=%u shndx=%u st_value=0x%lx\n",
                    sym_name, st_info, ELFW(ST_BIND)(st_info),
                    ELFW(ST_TYPE)(st_info),
                    (unsigned) o->symtab[sym_index].st_shndx,
                    (unsigned long) o->symtab[sym_index].st_value);
        }
    }

    if (tp_lt.symmerge && o->strtab && o->symtab[sym_index].st_name) {
        const char *merge_name = o->strtab + o->symtab[sym_index].st_name;
        if (strstr(merge_name, tp_lt.symmerge)) {
            fprintf(stderr,
                    "SYMMERGE_RELOC: file=%s sym='%s' old_sym=%u "
                    "new_sym=%u type=%d offset=0x%lx\n",
                    tp_objload_file(o), merge_name, sym_index,
                    (unsigned)o->old_to_new_syms[sym_index],
                    type, (unsigned long)rel->r_offset);
        }
    }
    return 0;
}

ST_FUNC void tp_objload_end(struct tp_objload *o)
{
    if (tp_lt.summary && o->iprel_count) {
        int si;
        fprintf(stderr, "RELOC_SUMMARY: file=%s member=%s iprel32=%d\n",
                tp_objload_file(o), o->member ? o->member : "<none>",
                o->iprel_count);
        for (si = 0; si < o->iprel_count; si++) {
            fprintf(stderr, "RELOC_SUMMARY_ITEM: sym=%s count=%d\n",
                    o->iprel_syms[si] ? o->iprel_syms[si] : "<unknown>",
                    o->iprel_counts[si]);
        }
    }
    tcc_free(o->iprel_syms);
    tcc_free(o->iprel_counts);
    o->iprel_syms = NULL;
    o->iprel_counts = NULL;
}

/* Debug knobs from the environment, each read once: several are asked
 * per relocation.  Keyed by the name's address - they are all literals,
 * and a literal spelled twice just takes a second slot. */
static const char *tp_env(const char *name)
{
    static struct { const char *name, *value; } cache[48];
    static int n;
    int i;

    for (i = 0; i < n; i++)
        if (cache[i].name == name)
            return cache[i].value;
    if (n == (int)(sizeof cache / sizeof cache[0]))
        return getenv(name);
    cache[n].name = name;
    cache[n].value = getenv(name);
    return cache[n++].value;
}

static int tp_env_enabled(const char *name)
{
    const char *env = tp_env(name);
    return env && *env;
}

static ElfW(Sym) *find_sym_by_name(Section *symtab, const char *name)
{
    int i;
    ElfW(Sym) *sym;

    for (i = 0; i < symtab->data_offset / sizeof(ElfW(Sym)); i++) {
        sym = &((ElfW(Sym) *)symtab->data)[i];
        if (strcmp((char *)symtab->link->data + sym->st_name, name) == 0)
            return sym;
    }
    return NULL;
}

typedef struct SymMapEntry {
    uint32_t addr;
    char type;
    const char *name;
} SymMapEntry;

static char sym_map_type(ElfW(Sym) *sym, Section *sec)
{
    int bind = ELFW(ST_BIND)(sym->st_info);
    char type = '?';

    if (sym->st_shndx == SHN_ABS) {
        type = 'A';
    } else if (sec) {
        if (sec->sh_flags & SHF_EXECINSTR)
            type = 'T';
        else if (sec->sh_type == SHT_NOBITS)
            type = 'B';
        else
            type = 'D';
    }

    if (bind == STB_LOCAL && type >= 'A' && type <= 'Z') {
        type = (char)(type - 'A' + 'a');
    } else if (bind == STB_WEAK) {
        if (type == 'T')
            type = 'W';
        else if (type == 'D' || type == 'B')
            type = 'V';
    }
    return type;
}

static int sym_map_entry_cmp(const void *a, const void *b)
{
    const SymMapEntry *sa = (const SymMapEntry *)a;
    const SymMapEntry *sb = (const SymMapEntry *)b;
    if (sa->addr < sb->addr)
        return -1;
    if (sa->addr > sb->addr)
        return 1;
    return strcmp(sa->name, sb->name);
}

static void write_tccsym_map(TCCState *s1, Section *symtab_sec,
                             const char *output_filename)
{
    size_t count = 0;
    size_t idx = 0;
    int i;
    SymMapEntry *entries;
    char *map_path;
    FILE *mapf;

    if (!symtab_sec || !symtab_sec->link || !symtab_sec->link->data)
        return;

    for (i = 0; i < symtab_sec->data_offset / sizeof(ElfW(Sym)); i++) {
        ElfW(Sym) *sym = &((ElfW(Sym) *)symtab_sec->data)[i];
        if (!sym->st_name || sym->st_shndx == SHN_UNDEF)
            continue;
        if (sym->st_shndx == SHN_ABS ||
            (sym->st_shndx > 0 && sym->st_shndx < s1->nb_sections))
            count++;
    }

    if (count == 0)
        return;

    entries = tcc_malloc(count * sizeof(*entries));
    if (!entries)
        return;

    for (i = 0; i < symtab_sec->data_offset / sizeof(ElfW(Sym)); i++) {
        ElfW(Sym) *sym = &((ElfW(Sym) *)symtab_sec->data)[i];
        const char *name = (const char *)symtab_sec->link->data + sym->st_name;
        Section *sec = NULL;
        uint32_t addr;

        if (!sym->st_name || sym->st_shndx == SHN_UNDEF)
            continue;
        if (sym->st_shndx == SHN_ABS) {
            addr = (uint32_t)sym->st_value;
        } else if (sym->st_shndx > 0 && sym->st_shndx < s1->nb_sections) {
            sec = s1->sections[sym->st_shndx];
            addr = sec->sh_addr + (uint32_t)sym->st_value;
        } else {
            continue;
        }

        entries[idx].addr = addr;
        entries[idx].type = sym_map_type(sym, sec);
        entries[idx].name = name;
        idx++;
    }

    if (idx == 0) {
        tcc_free(entries);
        return;
    }

    qsort(entries, idx, sizeof(*entries), sym_map_entry_cmp);

    map_path = tcc_malloc(strlen(output_filename) + 8);
    if (!map_path) {
        tcc_free(entries);
        return;
    }
    strcpy(map_path, output_filename);
    strcat(map_path, ".tccsym");

    mapf = fopen(map_path, "w");
    if (mapf) {
        for (size_t j = 0; j < idx; j++) {
            fprintf(mapf, "%08x %c %s\n",
                    entries[j].addr, entries[j].type, entries[j].name);
        }
        fclose(mapf);
    }

    tcc_free(map_path);
    tcc_free(entries);
}

static int tp_patch_u32_checked(uint8_t *image, uint32_t base_addr,
                                uint32_t total_size, uint32_t data_addr,
                                uint32_t val, const char *sym_name)
{
    uint32_t patch_off;

    if (data_addr < base_addr || data_addr + 4 < data_addr) {
        fprintf(stderr,
                "ERROR: patch target %s has invalid address 0x%08x (base 0x%08x)\n",
                sym_name, data_addr, base_addr);
        return -1;
    }

    patch_off = data_addr - base_addr;
    if (patch_off > total_size || total_size - patch_off < 4) {
        fprintf(stderr,
                "ERROR: patch target %s at 0x%08x is outside image [0x%08x, 0x%08x)\n",
                sym_name, data_addr, base_addr, base_addr + total_size);
        return -1;
    }

    image[patch_off + 0] = (val >>  0) & 0xFF;
    image[patch_off + 1] = (val >>  8) & 0xFF;
    image[patch_off + 2] = (val >> 16) & 0xFF;
    image[patch_off + 3] = (val >> 24) & 0xFF;
    return 0;
}

static int tp_find_symbol_addr(TCCState *s1, Section *symtab,
                               const char *name, uint32_t *out_addr)
{
    ElfW(Sym) *sym;
    Section *sec;

    sym = find_sym_by_name(symtab, name);
    if (!sym)
        return 0;
    if (sym->st_shndx == SHN_ABS) {
        *out_addr = (uint32_t)sym->st_value;
        return 1;
    }
    if (sym->st_shndx <= 0 || sym->st_shndx >= s1->nb_sections) {
        fprintf(stderr, "ERROR: symbol %s has invalid shndx %d\n",
                name, sym->st_shndx);
        return -1;
    }
    sec = s1->sections[sym->st_shndx];
    *out_addr = sec->sh_addr + sym->st_value;
    return 1;
}

static int tp_decode_abs_ldc32(const uint8_t *image, size_t total_size,
                               uint32_t base_addr, uint32_t load_addr,
                               uint32_t *out_value)
{
    size_t off;
    uint32_t value = 0;
    int i;
    uint8_t op;

    if (load_addr < base_addr)
        return -1;
    off = (size_t)(load_addr - base_addr);
    if (off > total_size || total_size - off < 8)
        return -1;

    for (i = 0; i < 7; i++) {
        op = image[off + (size_t)i];
        if ((op & 0xF0) != 0x20)
            return -1;
        value = (value << 4) | (op & 0x0F);
    }
    op = image[off + 7];
    if ((op & 0xF0) != 0x40)
        return -1;
    value = (value << 4) | (op & 0x0F);
    *out_value = value;
    return 0;
}

/* Report each undefined symbol once, however many times it is referenced.
 * Returns 1 the first time a given name is seen. */
#define TP_UNDEF_MAX 64
static const char *tp_undef_seen[TP_UNDEF_MAX];
static int tp_undef_count;

static int tp_report_undefined_symbol(const char *name)
{
    int i;
    if (!name)
        name = "<unnamed>";
    for (i = 0; i < tp_undef_count; i++)
        if (!strcmp(tp_undef_seen[i], name))
            return 0;
    if (tp_undef_count < TP_UNDEF_MAX)
        tp_undef_seen[tp_undef_count++] = name;
    return 1;
}

static int tp_verify_abs_ldc_value(uint8_t *image, size_t total_size,
                                   uint32_t base_addr, TCCState *s1,
                                   Section *symtab, const char *load_point_name,
                                   uint32_t expected_value,
                                   const char *expected_desc)
{
    uint32_t load_addr;
    uint32_t patched;
    int have_load;

    have_load = tp_find_symbol_addr(s1, symtab, load_point_name, &load_addr);
    if (have_load <= 0)
        return have_load;
    if (tp_decode_abs_ldc32(image, total_size, base_addr, load_addr, &patched) < 0) {
        fprintf(stderr, "ERROR: malformed LDC sequence at %s (addr=0x%08x)\n",
                load_point_name, load_addr);
        return -1;
    }
    if (patched != expected_value) {
        fprintf(stderr,
                "ERROR: LDC verification failed for %s: got=0x%08x expected=0x%08x (%s)\n",
                load_point_name, patched, expected_value, expected_desc);
        return -1;
    }
    return 1;
}

static void tp_dump_abs_ldc_bytes(uint8_t *image, size_t total_size,
                                  uint32_t base_addr, TCCState *s1,
                                  Section *symtab, const char *load_point_name,
                                  const char *label, int enabled)
{
    uint32_t load_addr;
    uint32_t decoded;
    size_t off;
    int have_load;
    int i;

    if (!enabled)
        return;

    have_load = tp_find_symbol_addr(s1, symtab, load_point_name, &load_addr);
    if (have_load <= 0) {
        fprintf(stderr, "BOOT_LDC_DUMP: %s missing (%s)\n", load_point_name, label);
        return;
    }
    if (load_addr < base_addr) {
        fprintf(stderr, "BOOT_LDC_DUMP: %s below image base (%s)\n", load_point_name, label);
        return;
    }

    off = (size_t)(load_addr - base_addr);
    if (off > total_size || total_size - off < 8) {
        fprintf(stderr, "BOOT_LDC_DUMP: %s out of range addr=0x%08x (%s)\n",
                load_point_name, load_addr, label);
        return;
    }

    fprintf(stderr, "BOOT_LDC_DUMP: %s addr=0x%08x bytes=", load_point_name, load_addr);
    for (i = 0; i < 8; i++)
        fprintf(stderr, "%s%02x", i ? " " : "", image[off + (size_t)i]);

    if (tp_decode_abs_ldc32(image, total_size, base_addr, load_addr, &decoded) < 0) {
        fprintf(stderr, " decoded=<malformed> (%s)\n", label);
        return;
    }
    fprintf(stderr, " decoded=0x%08x (%s)\n", decoded, label);
}

static int tp_find_defined_symbol_addr(TCCState *s1, Section *symtab,
                                       const char *name, uint32_t *out_addr)
{
    ElfW(Sym) *sym;
    Section *sec;

    sym = find_sym_by_name(symtab, name);
    if (!sym)
        return 0;
    if (sym->st_shndx <= 0 || sym->st_shndx >= s1->nb_sections)
        return -1;

    sec = s1->sections[sym->st_shndx];
    *out_addr = sec->sh_addr + sym->st_value;
    return 1;
}

static int tp_patch_abs_ldc_value(uint8_t *image, size_t total_size,
                                  uint32_t base_addr, uint32_t load_addr,
                                  uint32_t value, const char *what)
{
    size_t patch_off;
    uint32_t old_value;
    int i;

    if (tp_decode_abs_ldc32(image, total_size, base_addr, load_addr, &old_value) < 0) {
        fprintf(stderr, "error: %s at 0x%08x is not an eight-byte LDC in the image\n",
                what, load_addr);
        return -1;
    }
    patch_off = (size_t)(load_addr - base_addr);
    for (i = 7; i >= 1; i--) {
        uint8_t nibble = (value >> (i * 4)) & 0x0F;
        image[patch_off++] = 0x20 | nibble;
    }
    image[patch_off] = 0x40 | (value & 0x0F);
    return 0;
}

static int tp_patch_verified_symbol_target(uint8_t *image, size_t total_size,
                                           uint32_t base_addr, TCCState *s1,
                                           Section *symtab,
                                           const char *load_point_name,
                                           const char *target_name,
                                           int required,
                                           int link_debug)
{
    int have_load;
    int have_target;
    uint32_t load_addr;
    uint32_t target_addr;

    /* Optional boot patch families are absent from ordinary bare-metal
       programs.  A missing selected target name is therefore a normal
       no-op unless the corresponding load point made the patch required. */
    if (!load_point_name || !target_name) {
        if (required) {
            fprintf(stderr, "ERROR: required boot patch target is missing\n");
            return -1;
        }
        return 0;
    }

    have_load = tp_find_defined_symbol_addr(s1, symtab, load_point_name, &load_addr);
    have_target = tp_find_defined_symbol_addr(s1, symtab, target_name, &target_addr);
    if (have_load == 0 || have_target == 0) {
        if (required) {
            fprintf(stderr, "ERROR: required boot patch symbols missing: %s -> %s\n",
                    load_point_name, target_name);
            return -1;
        }
        return 0;
    }
    if (have_load < 0 || have_target < 0) {
        fprintf(stderr, "ERROR: invalid boot patch symbol section for %s -> %s\n",
                load_point_name, target_name);
        return -1;
    }
    if (tp_patch_abs_ldc_value(image, total_size, base_addr, load_addr,
                               target_addr, load_point_name) < 0)
        return -1;
    if (tp_verify_abs_ldc_value(image, total_size, base_addr, s1, symtab,
                                load_point_name, target_addr, target_name) != 1)
        return -1;
    tp_dump_abs_ldc_bytes(image, total_size, base_addr, s1, symtab,
                          load_point_name, target_name, link_debug);
    return 1;
}

#include "transputer-linux.h"

/*
 * Resolve the ELF init/fini array bounds, which this target never defines.
 *
 * `elf_output_file()` hands off to transputer_simple_link() before it reaches
 * tcc_add_linker_symbols(), so the six __{pre,}init_array_* / __fini_array_*
 * bounds that the generic linker would have created do not exist.  uClibc's
 * __uClibc_main walks __init_array_start..__init_array_end to run
 * constructors, so leaving them undefined means iterating a garbage range -
 * latent only because those arrays are empty in practice.  Under
 * T4_STRICT_UNRESOLVED_CALLS it is not latent at all: it is a hard link
 * failure, which is what stopped fbwm relinking.
 *
 * The honest resolution is an *empty* range: start == end, so every walker
 * terminates immediately, which is exactly right for a target with no
 * constructors.  Zero is used for both bounds because only their equality is
 * ever observed - the loops are `for (p = start; p < end; p++)`.
 *
 * Returns the value to use, and marks the symbol defined so the undefined-
 * symbol diagnostic below does not fire for it.
 */
static addr_t tp_resolve_array_bound_symbol(Section *symtab_sec, ElfW(Sym) *sym,
                                            addr_t val, unsigned short *shndx)
{
    static const char *const bounds[] = {
        "__init_array_start",    "__init_array_end",
        "__preinit_array_start", "__preinit_array_end",
        "__fini_array_start",    "__fini_array_end",
    };
    const char *name = tp_reloc_symbol_name(symtab_sec, sym);
    size_t k;

    if (!name)
        return val;
    for (k = 0; k < sizeof(bounds) / sizeof(bounds[0]); k++) {
        if (strcmp(name, bounds[k]) == 0) {
            *shndx = SHN_ABS;
            return 0;
        }
    }
    return val;
}

static int tp_find_boot_entry_point(TCCState *s1, Section *symtab_sec,
                                    uint32_t *out_entry_point,
                                    Section **out_entry_section,
                                    ElfW(Sym) **out_entry_sym,
                                    int link_debug)
{
    ElfW(Sym) *start_symbol;
    Section *start_sym_section;
    int start_section;
    int i;

    start_symbol = NULL;
    start_section = 0;
    for (i = 0; i < symtab_sec->data_offset / sizeof(ElfW(Sym)); i++) {
        ElfW(Sym) *sym = &((ElfW(Sym) *)symtab_sec->data)[i];
        const char *name = (char *)symtab_sec->link->data + sym->st_name;
        if (strcmp(name, "_start") == 0) {
            start_symbol = sym;
            start_section = sym->st_shndx;
            break;
        }
    }

    if (!start_symbol || start_section <= 0 || start_section >= s1->nb_sections) {
        fprintf(stderr, "ERROR: _start symbol not found; refusing fallback entry point\n");
        return -1;
    }

    start_sym_section = s1->sections[start_section];
    *out_entry_point = start_sym_section->sh_addr + start_symbol->st_value;
    if (out_entry_section)
        *out_entry_section = start_sym_section;
    if (out_entry_sym)
        *out_entry_sym = start_symbol;

    if (link_debug) {
        fprintf(stderr, "DEBUG: Found _start in section %d (%s)\n",
                start_section, start_sym_section->name);
        fprintf(stderr, " Symbol value: 0x%08x, Section addr: 0x%08x\n",
                (uint32_t)start_symbol->st_value, start_sym_section->sh_addr);
        fprintf(stderr, " Entry point: 0x%08x\n\n", *out_entry_point);
    }

    return 0;
}

static int tp_verify_boot_entry(uint32_t entry_point, uint32_t base_addr,
                                int strict_boot_asserts,
                                int has_start_kernel_load_point,
                                uint32_t start_kernel_addr,
                                uint32_t total_size)
{
    if (!strict_boot_asserts)
        return 0;

    if (entry_point != base_addr) {
        fprintf(stderr,
                "ERROR: boot entry mismatch: _start=0x%08x base=0x%08x\n",
                entry_point, base_addr);
        return -1;
    }
    if (has_start_kernel_load_point) {
        uint64_t image_end = (uint64_t)base_addr + (uint64_t)total_size;
        if (start_kernel_addr < base_addr || (uint64_t)start_kernel_addr >= image_end) {
            fprintf(stderr,
                    "ERROR: start_kernel address 0x%08x is outside image [0x%08x, 0x%08llx)\n",
                    start_kernel_addr, base_addr, (unsigned long long)image_end);
            return -1;
        }
    }

    return 0;
}

static int tp_write_exec_image(const char *output_filename,
                               uint32_t base_addr,
                               uint32_t entry_point,
                               uint32_t total_size,
                               uint32_t total_memsz,
                               int xip_mode,
                               uint32_t text_end,
                               uint32_t data_start,
                               uint8_t *image,
                               unsigned e_flags)
{
    FILE *f;
    Elf32_Ehdr ehdr;
    Elf32_Phdr phdr[2];
	uint32_t phnum = xip_mode ? 2u : 1u;
	uint32_t header_size = sizeof(Elf32_Ehdr) + phnum * sizeof(Elf32_Phdr);

    f = fopen(output_filename, "wb");
    if (!f) {
        fprintf(stderr, "ERROR: could not open output '%s' for write: %s\n",
                output_filename, strerror(errno));
        return -1;
    }

    memset(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[EI_MAG0] = ELFMAG0;
    ehdr.e_ident[EI_MAG1] = ELFMAG1;
    ehdr.e_ident[EI_MAG2] = ELFMAG2;
    ehdr.e_ident[EI_MAG3] = ELFMAG3;
    ehdr.e_ident[EI_CLASS] = ELFCLASS32;
    ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr.e_ident[EI_VERSION] = EV_CURRENT;
    ehdr.e_ident[EI_OSABI] = ELFOSABI_NONE;
    ehdr.e_type = ET_EXEC;
    ehdr.e_machine = EM_TRANSPUTER;
    ehdr.e_version = EV_CURRENT;
    ehdr.e_entry = entry_point;
    ehdr.e_phoff = sizeof(Elf32_Ehdr);
    ehdr.e_ehsize = sizeof(Elf32_Ehdr);
    ehdr.e_phentsize = sizeof(Elf32_Phdr);
    ehdr.e_phnum = phnum;
    ehdr.e_flags = e_flags;     /* TP_EF_*: what the image needs */

    memset(phdr, 0, sizeof(phdr));
    phdr[0].p_type = PT_LOAD;
    phdr[0].p_offset = header_size;
    phdr[0].p_vaddr = base_addr;
    phdr[0].p_paddr = base_addr;
    phdr[0].p_align = 0x1000;
	if (xip_mode) {
		uint32_t text_size = text_end - base_addr;
		uint32_t data_image_off = data_start - base_addr;
		uint32_t data_size = total_size - data_image_off;

		phdr[0].p_filesz = text_size;
		phdr[0].p_memsz = text_size;
		phdr[0].p_flags = PF_R | PF_X;
		phdr[1].p_type = PT_LOAD;
		phdr[1].p_offset = header_size + text_size;
		phdr[1].p_vaddr = data_start;
		phdr[1].p_paddr = data_start;
		phdr[1].p_filesz = data_size;
		phdr[1].p_memsz = total_memsz - data_image_off;
		phdr[1].p_flags = PF_R | PF_W;
		phdr[1].p_align = 0x1000;
	} else {
		phdr[0].p_filesz = total_size;
		phdr[0].p_memsz = total_memsz > total_size ? total_memsz : total_size;
		phdr[0].p_flags = PF_R | PF_W | PF_X;
	}

    if (fwrite(&ehdr, 1, sizeof(ehdr), f) != sizeof(ehdr) ||
		fwrite(phdr, sizeof(Elf32_Phdr), phnum, f) != phnum)
		goto short_write;
	if (xip_mode) {
		uint32_t text_size = text_end - base_addr;
		uint32_t data_image_off = data_start - base_addr;
		uint32_t data_size = total_size - data_image_off;

		if (fwrite(image, 1, text_size, f) != text_size ||
		    fwrite(image + data_image_off, 1, data_size, f) != data_size)
			goto short_write;
	} else if (fwrite(image, 1, total_size, f) != total_size) {
		goto short_write;
	}
    if (fclose(f) != 0) {
        fprintf(stderr, "ERROR: failed to close '%s': %s\n",
                output_filename, strerror(errno));
        return -1;
    }

    return 0;

short_write:
	fprintf(stderr, "ERROR: short write while writing '%s': %s\n",
		output_filename, strerror(errno));
	fclose(f);
	return -1;
}

static void tp_copy_alloc_sections(TCCState *s1, uint8_t *image, uint32_t base_addr)
{
    int i;

    for (i = 1; i < s1->nb_sections; i++) {
        Section *s = s1->sections[i];
        uint32_t offset;
        uint32_t size;

        if (!(s->sh_flags & SHF_ALLOC) || s->sh_type == SHT_NOBITS || !s->data)
            continue;

        offset = s->sh_addr - base_addr;
        size = s->data_offset;
        if (size > 0)
            memcpy(image + offset, s->data, size);
    }
}

static void tp_audit_call_targets(TCCState *s1, Section *symtab_sec,
                                  uint8_t *image, uint32_t base_addr,
                                  uint32_t total_size)
{
    int i;
    int audit_errors = 0;
    int audit_total = 0;

    for (i = 1; i < s1->nb_sections; i++) {
        ElfW_Rel *rel;
        ElfW_Rel *rel_end;
        Section *s = s1->sections[i];

        if (!s->reloc || !(s->sh_flags & SHF_EXECINSTR))
            continue;

        rel_end = (ElfW_Rel *)(s->reloc->data + s->reloc->data_offset);
        for (rel = (ElfW_Rel *)s->reloc->data; rel < rel_end; rel++) {
            int32_t oreg = 0;
            int rtype = ELFW(R_TYPE)(rel->r_info);
            int rsym;
            ElfW(Sym) *rsym_p;
            addr_t expected_val;
            addr_t call_addr;
            size_t off;
            size_t pos;

            if (rtype != R_TRANSPUTER_IPREL32 && rtype != R_TRANSPUTER_INSN)
                continue;

            rsym = ELFW(R_SYM)(rel->r_info);
            rsym_p = &((ElfW(Sym) *)symtab_sec->data)[rsym];
            expected_val = rsym_p->st_value;
            if (rsym_p->st_shndx > 0 && rsym_p->st_shndx < s1->nb_sections)
                expected_val += s1->sections[rsym_p->st_shndx]->sh_addr;
            if (rtype == R_TRANSPUTER_INSN) {
                int op, addend;
                if (tp_decode_instr_at(s->data, s->data_offset, rel->r_offset,
                                       &op, &addend) != TP_ASM_SLOT
                    || (op != TP_ENC_J && op != TP_ENC_CJ && op != TP_ENC_CALL))
                    continue;
                expected_val = (uint32_t)expected_val + (uint32_t)addend;
            }

            call_addr = s->sh_addr + rel->r_offset;
            off = (size_t)(call_addr - base_addr);
            if (off >= total_size)
                continue;

            pos = off;
            while (pos < total_size) {
                uint8_t b = image[pos];
                uint8_t hi = (b >> 4) & 0xF;
                uint8_t lo = b & 0xF;
                oreg += lo;

                if (hi == 0x2) {
                    oreg <<= 4;
                    pos++;
                    continue;
                }
                if (hi == 0x6) {
                    oreg = (~oreg) << 4;
                    pos++;
                    continue;
                }

                {
                    int32_t operand = oreg;
                    addr_t target = 0;

                    if (hi == 0x9 || hi == 0x0 || hi == 0xA)
                        target = base_addr + pos + 1 + operand;
                    if (target && target != expected_val) {
                        fprintf(stderr,
                                "CALL_AUDIT_MISMATCH: addr=0x%08x sym=%s expected=0x%08x actual=0x%08x op=0x%x\n",
                                (unsigned int)call_addr,
                                tp_reloc_symbol_name(symtab_sec, rsym_p),
                                (unsigned int)expected_val,
                                (unsigned int)target, hi);
                        audit_errors++;
                    }
                    audit_total++;
                }
                break;
            }
        }
    }

    fprintf(stderr, "CALL_AUDIT: %d/%d relocations checked, %d mismatches\n",
            audit_total, audit_total, audit_errors);
}

static int tp_reloc_symbol_repair_enabled(int type)
{
    const char *strict_env = tp_env("T4_STRICT_RELOCS");

    if (strict_env && *strict_env)
        return 0;

    switch (type) {
    case R_TRANSPUTER_32:
    case R_TRANSPUTER_IPREL32:
        return 1;
    default:
        return 0;
    }
}

static int tp_ldc_abs_fallback_enabled(void)
{
    const char *strict_env = tp_env("T4_STRICT_LDC_ABS");
    return !(strict_env && *strict_env);
}

static int tp_iprel_fallback_enabled(void)
{
    const char *strict_env = tp_env("T4_STRICT_IPREL");
    return !(strict_env && *strict_env);
}

struct tp_reloc_index {
    int *heads;
    int *next;
    unsigned *bucket;
    unsigned mask;
};

static unsigned tp_reloc_name_hash(const unsigned char *name)
{
    unsigned hash = 0;
    while (*name)
        hash = hash * 33 + *name++;
    return hash;
}

/* Preserve symbol-table order within each hash bucket, including duplicate
 * local names. The repair policy depends on both order and match count. */
static void tp_reloc_index_init(struct tp_reloc_index *index, Section *symtab)
{
    ElfW(Sym) *syms = (ElfW(Sym) *)symtab->data;
    int count = symtab->data_offset / sizeof(*syms);
    unsigned buckets = 1;
    int i;

    while (buckets < (unsigned)count && buckets < (1u << 30))
        buckets <<= 1;
    index->mask = buckets - 1;
    index->heads = tcc_mallocz(buckets * sizeof(*index->heads));
    index->next = tcc_mallocz((size_t)count * sizeof(*index->next));
    index->bucket = tcc_mallocz((size_t)count * sizeof(*index->bucket));
    for (i = count - 1; i > 0; i--) {
        const unsigned char *name = symtab->link->data + syms[i].st_name;
        unsigned bucket = tp_reloc_name_hash(name) & index->mask;
        index->bucket[i] = bucket;
        index->next[i] = index->heads[bucket];
        index->heads[bucket] = i;
    }
}

static void tp_resolve_reloc_symbol(TCCState *s1, Section *symtab_sec,
                                    const struct tp_reloc_index *index,
                                    int type, ElfW(Sym) **sym_io,
                                    int *sym_index_io, addr_t *val_io,
                                    addr_t addr, int reloc_fix_debug)
{
    ElfW(Sym) *sym = *sym_io;

    if (!tp_reloc_symbol_repair_enabled(type))
        return;

    /* If this is the bucket's only symbol, repair cannot choose a different
     * definition. Keep the diagnostic path intact, including ambiguous or
     * undefined local-symbol messages. */
    if (!reloc_fix_debug &&
        index->heads[index->bucket[*sym_index_io]] == *sym_index_io &&
        !index->next[*sym_index_io])
        return;

    if (sym->st_name) {
        const char *rel_name = (const char *)symtab_sec->link->data + sym->st_name;
        int sym_binding = ELFW(ST_BIND)(sym->st_info);

        if (rel_name && rel_name[0] &&
            !(rel_name[0] == 'L' && rel_name[1] == '.') &&
            !(rel_name[0] == '.' && rel_name[1] == 'L')) {
            int best_idx = -1;
            addr_t best_val = 0;
            int best_type_match = 0;
            int match_count = 0;
            int want_type = (type == R_TRANSPUTER_IPREL32) ? STT_FUNC : STT_OBJECT;
            int j;

            for (j = index->heads[index->bucket[*sym_index_io]];
                 j; j = index->next[j]) {
                ElfW(Sym) *cs = &((ElfW(Sym) *)symtab_sec->data)[j];
                const char *cs_name;
                addr_t cs_val;
                int cs_binding;
                int sym_type;
                int binding_score;
                int type_score;
                int score;

                if (cs->st_shndx == SHN_UNDEF || cs->st_shndx == SHN_COMMON)
                    continue;
                cs_name = (const char *)symtab_sec->link->data + cs->st_name;
                if (!cs_name || strcmp(cs_name, rel_name) != 0)
                    continue;

                cs_binding = ELFW(ST_BIND)(cs->st_info);
                /* A global or weak reference resolves only to a global or
                 * weak definition, and a local one only to a local.  Taking
                 * another object's static turned an undefined extern into a
                 * silent call to that static (linux-7's console initcall
                 * table reached the empty static inline fb_console_init() in
                 * fbmem.o); taking a global moved a static's own call sites
                 * onto a same-named global in another file whenever that
                 * global came first in the symbol table. */
                if ((sym_binding == STB_LOCAL) != (cs_binding == STB_LOCAL))
                    continue;
                cs_val = cs->st_value;
                if (cs->st_shndx > 0 && cs->st_shndx < s1->nb_sections)
                    cs_val += s1->sections[cs->st_shndx]->sh_addr;
                match_count++;
                sym_type = ELFW(ST_TYPE)(cs->st_info);
                binding_score =
                    (cs_binding == STB_GLOBAL) ? 8 :
                    (cs_binding == STB_WEAK)   ? 4 : 0;
                type_score =
                    (sym_type == want_type) ? 2 :
                    (sym_type == STT_FUNC || sym_type == STT_OBJECT) ? 1 : 0;
                score = binding_score + type_score;
                if (best_idx < 0 || score > best_type_match) {
                    best_idx = j;
                    best_val = cs_val;
                    best_type_match = score;
                    if (binding_score == 8 && type_score == 2)
                        break;
                }
            }

            if (sym_binding == STB_LOCAL && match_count != 1) {
                if (reloc_fix_debug) {
                    fprintf(stderr,
                            "RELOC_FIX_SKIP: LOCAL sym='%s' matches=%d "
                            "addr=0x%lx val=0x%lx (ambiguous, leaving as-is)\n",
                            rel_name, match_count, (unsigned long)addr,
                            (unsigned long)*val_io);
                }
                best_idx = -1;
            }
            if (best_idx >= 0 && best_val != *val_io) {
                if (reloc_fix_debug) {
                    fprintf(stderr,
                            "RELOC_FIX: type=%d sym=%s bind=%d matches=%d addr=0x%lx old_val=0x%lx new_val=0x%lx (idx %d->%d)\n",
                            type, rel_name, sym_binding, match_count,
                            (unsigned long)addr, (unsigned long)*val_io,
                            (unsigned long)best_val, *sym_index_io, best_idx);
                }
                *val_io = best_val;
                *sym_index_io = best_idx;
                *sym_io = &((ElfW(Sym) *)symtab_sec->data)[best_idx];
            }
        }
    }
}

static void tp_trace_reloc_window(Section *symtab_sec, ElfW(Sym) *sym,
                                  uint8_t *image, size_t total_size,
                                  uint32_t base_addr, addr_t addr, int type,
                                  addr_t val, const char *section_name,
                                  int link_trace, int *reloc_in_range_io,
                                  size_t *reloc_dump_start_io,
                                  size_t *reloc_dump_end_io)
{
    static int reloc_range_init = 0;
    static uint32_t reloc_range_start = 0;
    static uint32_t reloc_range_end = 0;

    *reloc_in_range_io = 0;
    *reloc_dump_start_io = 0;
    *reloc_dump_end_io = 0;

    if (!reloc_range_init) {
        const char *start_env = link_trace ? getenv("T4_RELOC_RANGE_START") : NULL;
        const char *end_env = link_trace ? getenv("T4_RELOC_RANGE_END") : NULL;
        reloc_range_init = 1;
        if (start_env && *start_env) {
            reloc_range_start = (uint32_t)strtoul(start_env, NULL, 0);
            if (end_env && *end_env)
                reloc_range_end = (uint32_t)strtoul(end_env, NULL, 0);
            else
                reloc_range_end = reloc_range_start + 0x200;
            if (link_trace) {
                fprintf(stderr, "RELOC_RANGE: start=0x%08x end=0x%08x\n",
                        reloc_range_start, reloc_range_end);
            }
        }
    }

    if (reloc_range_start && addr >= reloc_range_start && addr < reloc_range_end) {
        size_t reloc_off = addr - base_addr;
        *reloc_in_range_io = 1;
        *reloc_dump_start_io = reloc_off >= 8 ? reloc_off - 8 : 0;
        *reloc_dump_end_io = reloc_off + 8;
        if (*reloc_dump_end_io >= total_size)
            *reloc_dump_end_io = total_size - 1;
        if (link_trace) {
            fprintf(stderr,
                    "RELOC_RANGE_HIT: addr=0x%08x type=%d sym=%s val=0x%08x section=%s\n",
                    (unsigned int)addr, type, tp_reloc_symbol_name(symtab_sec, sym),
                    (unsigned int)val, section_name);
            fprintf(stderr, "RELOC_RANGE_BEFORE:");
            for (size_t di = *reloc_dump_start_io; di <= *reloc_dump_end_io; di++)
                fprintf(stderr, " %02x", image[di]);
            fprintf(stderr, "\n");
        }
    }
}

static const char *tp_reloc_symbol_name(Section *symtab_sec, ElfW(Sym) *sym)
{
    Section *strsec;

    if (!symtab_sec || !symtab_sec->link || !sym || !sym->st_name)
        return "<unknown>";

    strsec = symtab_sec->link;
    if (!strsec || !strsec->data)
        return "<unknown>";
    return (const char *)strsec->data + sym->st_name;
}

/* Required bytes at the relocation site. Old instruction slots are
 * variable-width and their decoders must also check the end of the section. */
ST_FUNC int tp_reloc_width(int type, int executable)
{
    switch (type) {
    case R_TRANSPUTER_NONE: return 0;
    case R_TRANSPUTER_8: case R_TRANSPUTER_IPREL8: return 1;
    case R_TRANSPUTER_16: case R_TRANSPUTER_IPREL16: return 2;
    case R_TRANSPUTER_32: case R_TRANSPUTER_PC_RELATIVE: return 4;
    case R_TRANSPUTER_IPREL32: case R_TRANSPUTER_LDC_ABS:
        return executable ? 1 : 4;
    case R_TRANSPUTER_INSN: return TP_ASM_SLOT;
    default: return -1;
    }
}

ST_FUNC int tp_data_layout_reloc(int type)
{
    return type == R_TRANSPUTER_8 || type == R_TRANSPUTER_16
        || type == R_TRANSPUTER_IPREL8 || type == R_TRANSPUTER_IPREL16
        || type == R_TRANSPUTER_PC_RELATIVE;
}

static int tp_apply_single_relocation(Section *s, Section *symtab_sec,
                                      ElfW(Sym) *sym, uint8_t *ptr,
                                      size_t available, addr_t addr, addr_t val,
                                      int type, int width, int iprel_patch_trace,
                                      int iprel_patch_debug)
{
    TCCState *s1 = s->s1;
    unsigned long pos = addr - s->sh_addr;
    int patched;

    switch (type) {
    case R_TRANSPUTER_INSN: {
        int op, addend;
        uint32_t operand;

        if (tp_decode_instr_at(ptr, TP_ASM_SLOT, 0, &op, &addend) != TP_ASM_SLOT
            || op == TP_ENC_OPR)
            return tcc_error_noabort("invalid instruction relocation at %s+0x%lx", s->name, pos);
        operand = (uint32_t)val + (uint32_t)addend;
        if (op == TP_ENC_J || op == TP_ENC_CALL || op == TP_ENC_CJ)
            operand -= (uint32_t)addr + TP_ASM_SLOT;
        tp_encode_asm_slot(ptr, op, (int32_t)operand);
        break;
    }
    case R_TRANSPUTER_8: case R_TRANSPUTER_16:
    case R_TRANSPUTER_IPREL8: case R_TRANSPUTER_IPREL16: {
        int relative = type == R_TRANSPUTER_IPREL8 || type == R_TRANSPUTER_IPREL16;
        int addend = width == 1 ? (int8_t)*ptr : (int16_t)read16le(ptr);
        int32_t value = (uint32_t)val + (uint32_t)addend - (relative ? (uint32_t)addr : 0);
        int high = 1 << (width * 8 - 1);

        if (value < -high || value >= (relative ? high : 2 * high))
            return tcc_error_noabort("%d-bit relocation overflow for '%s' at %s+0x%lx",
                                     width * 8, tp_reloc_symbol_name(symtab_sec, sym),
                                     s->name, pos);
        if (width == 1)
            *ptr = (uint8_t)value;
        else
            write16le(ptr, (uint16_t)value);
        break;
    }
    case R_TRANSPUTER_32: case R_TRANSPUTER_PC_RELATIVE: {
        uint32_t old_val = read32le(ptr);
        uint32_t new_val = old_val + val - (type == R_TRANSPUTER_PC_RELATIVE ? addr : 0);
        if (!(s->sh_flags & SHF_EXECINSTR) && tp_env_enabled("T4_DATA_RELOC_DEBUG")) {
            fprintf(stderr, "R32_DATA: addr=0x%08x sym=%s old=0x%08x val=0x%08x new=0x%08x section=%s"
                    " shndx=%d st_value=0x%lx bind=%d type=%d\n",
                    (unsigned int)addr, tp_reloc_symbol_name(symtab_sec, sym),
                    old_val, (unsigned int)val, new_val, s->name,
                    sym->st_shndx, (unsigned long)sym->st_value,
                    ELFW(ST_BIND)(sym->st_info), ELFW(ST_TYPE)(sym->st_info));
        }
        write32le(ptr, new_val);
        break;
    }

    case R_TRANSPUTER_IPREL32:
    case R_TRANSPUTER_LDC_ABS:
        if (s->sh_flags & SHF_EXECINSTR) {
            int op, placeholder;
            int len = tp_decode_instr_at(ptr, available < 16 ? available : 16,
                                         0, &op, &placeholder);
            if (!len)
                patched = 0;
            else if (type == R_TRANSPUTER_IPREL32 && op != TP_ENC_LDC)
                patched = tp_patch_iprel(ptr, len, op, addr, val);
            else
                patched = tp_patch_ldc_abs(ptr, len, op, addr, val);
            if (type == R_TRANSPUTER_IPREL32 && (iprel_patch_trace || iprel_patch_debug))
                fprintf(stderr, "IPREL_PATCH_RESULT: addr=0x%08x target=0x%08x width=%d patched=%d\n",
                        (unsigned)addr, (unsigned)val, len, patched);
            if (patched < 0)
                return tcc_error_noabort("relocation overflow for '%s' in %d-byte slot at %s+0x%lx",
                                         tp_reloc_symbol_name(symtab_sec, sym), len, s->name, pos);
            if (!patched)
                return tcc_error_noabort("cannot encode relocation %d for '%s' at %s+0x%lx",
                                         type, tp_reloc_symbol_name(symtab_sec, sym), s->name, pos);
        } else {
            if (type == R_TRANSPUTER_IPREL32) {
                if (!tp_iprel_fallback_enabled())
                    return tcc_error_noabort("IP-relative data relocation at %s+0x%lx in strict mode",
                                             s->name, pos);
                val -= addr;
            } else if (!tp_ldc_abs_fallback_enabled()) {
                return tcc_error_noabort("LDC data relocation at %s+0x%lx in strict mode",
                                         s->name, pos);
            }
            write32le(ptr, read32le(ptr) + val);
        }
        break;
    default:
        return tcc_error_noabort("unsupported Transputer relocation %d at %s+0x%lx", type,
                                 s->name, pos);
    }
    return 0;
}

static int tp_patch_iprel(uint8_t *slot, int width, int op, uint32_t addr, uint32_t val)
{
    uint8_t insn[16];
    int len;

    if (op != TP_ENC_J && op != TP_ENC_CALL && op != TP_ENC_CJ)
        return 0;
    len = tp_encode_instr(insn, op, (int32_t)(val - (addr + width)));
    if (len <= width) {
        memset(slot, 0x20, width - len);
        memcpy(slot + width - len, insn, len);
    } else if (op == TP_ENC_CALL && val == 0 && width >= 2) {
        /* The default kernel link tolerates unresolved calls. Address zero
         * is beyond the short slot's branch range but reachable by gcall. */
        memset(slot, 0x20, width - 2);
        slot[width - 2] = 0x40;
        slot[width - 1] = 0xF6;
    } else {
        return -1;
    }
    return 1;
}

static int tp_patch_ldc_abs(uint8_t *slot, int width, int op, uint32_t addr, uint32_t val)
{
    uint8_t new_insn[16];
    int new_len;

    if (op == TP_ENC_OPR || op == TP_ENC_J || op == TP_ENC_CALL || op == TP_ENC_CJ)
        return 0;

    /* LDPI optimization: replace LDC(absolute) with LDC(relative); LDPI
     * when the relative encoding is strictly shorter than absolute.
     * LDPI adds the PC (address of byte after LDPI) to AReg.
     * Only for LDC opcodes, and only when savings are guaranteed.
     * Restricted to small relative offsets (±32KB) to avoid edge cases
     * with kernel high addresses (0x80000000+). */
    if (op == TP_ENC_LDC) {
        uint32_t pc_after_ldpi = addr + width;
        int32_t rel_offset = (int32_t)(val - pc_after_ldpi);
        int ldc_space = width - 2;  /* bytes available for LDC (2 reserved for LDPI) */

        /* Only attempt for small relative offsets (±32KB) */
        if (ldc_space >= 1 && rel_offset > -32768 && rel_offset < 32768) {
            uint8_t ldpi_insn[16];
            int ldc_len = tp_encode_instr(ldpi_insn, TP_OP_LDC, rel_offset);
            int ldpi_total = ldc_len + 2;  /* LDC bytes + 2 for LDPI */

            /* Only use LDPI if it's strictly shorter than absolute encoding */
            int abs_len = tp_encode_instr(new_insn, op, (int32_t)val);
            if (ldpi_total < abs_len && ldpi_total <= width) {
                memset(slot, 0x20, width);
                memcpy(slot + ldc_space - ldc_len, ldpi_insn, ldc_len);
                slot[width - 2] = 0x21;  /* PFIX 1 */
                slot[width - 1] = 0xFB;  /* OPR B (LDPI) */
                return 1;
            }
        }
    }

    /* Fallback: absolute LDC encoding */
    new_len = tp_encode_instr(new_insn, op, (int32_t)val);

    if (new_len > width)
        return -1;
    memset(slot, 0x20, width - new_len);
    memcpy(slot + width - new_len, new_insn, new_len);
    return 1;
}
/* Returns 1 for a code relocation, 0 for a data relocation. For unknown
   relocations, returns -1. */
int code_reloc (int reloc_type)
{
     switch (reloc_type) {
         case R_TRANSPUTER_IPREL32:
         case R_TRANSPUTER_IPREL16:
         case R_TRANSPUTER_IPREL8:
         case R_TRANSPUTER_INSN:
         case R_TRANSPUTER_LDC_ABS:
         case R_TRANSPUTER_8:
         case R_TRANSPUTER_16:
         case R_TRANSPUTER_32:
         case R_TRANSPUTER_GOTPC32:
         case R_TRANSPUTER_GOTPC16:
         case R_TRANSPUTER_GOTOFF32:
         case R_TRANSPUTER_GOTOFF16:
         case R_TRANSPUTER_GOT32:
         case R_TRANSPUTER_GOT16:
         case R_TRANSPUTER_GLOB_DAT:
         case R_TRANSPUTER_COPY:
            return 0;
         case R_TRANSPUTER_PLT16:
         case R_TRANSPUTER_PLT32:
         case R_TRANSPUTER_JUMP_SLOT:
            return 1;
    }
    //tcc_error ("Unknown relocation type: %d", reloc_type);
    return -1;
}
/* Returns an enumerator to describe whether and when the relocation needs a
   GOT and/or PLT entry to be created. See tcc.h for a description of the
   different values. */
int gotplt_entry_type (int reloc_type)
{
    switch (reloc_type) {
        case R_TRANSPUTER_GLOB_DAT:
        case R_TRANSPUTER_JUMP_SLOT:
        case R_TRANSPUTER_COPY:
        case R_TRANSPUTER_RELATIVE:
            return NO_GOTPLT_ENTRY;
        case R_TRANSPUTER_IPREL32:
        case R_TRANSPUTER_IPREL16:
        case R_TRANSPUTER_IPREL8:
        case R_TRANSPUTER_INSN:
        case R_TRANSPUTER_LDC_ABS:
            return NO_GOTPLT_ENTRY;
/* Bare-metal target: Simple address relocations are solved directly
    by the linker. No GOT/PLT entries needed for static linking. */
        case R_TRANSPUTER_32:
        case R_TRANSPUTER_16:
        case R_TRANSPUTER_8:
            return NO_GOTPLT_ENTRY;
        case R_TRANSPUTER_GOTOFF16:
            return BUILD_GOT_ONLY;
        case R_TRANSPUTER_GOT32:
        case R_TRANSPUTER_GOT16:
        case R_TRANSPUTER_GOTPC32:
        case R_TRANSPUTER_GOTPC16:
        case R_TRANSPUTER_GOTOFF32:
        case R_TRANSPUTER_PLT32:
        case R_TRANSPUTER_PLT16:
            return ALWAYS_GOTPLT_ENTRY;
    }
    //tcc_error ("Unknown relocation type: %d", reloc_type);
    return -1;
}
/* GOT (Global offset table) = Linker Table for data addresses needed for realocation from library
 * PLT (Procedure linkage table) = Linker Table for external functions (procedure calls)
 * addresses from library
 */
ST_FUNC unsigned create_plt_entry(TCCState *s1, unsigned got_offset, struct sym_attr *attr)
{
     Section *plt = s1->plt;
     uint8_t *p;
     unsigned relofs;
        unsigned rel_size;
     unsigned plt_offset;
     if (plt->data_offset == 0) {
         p = section_ptr_add(plt, 16);
         p[0] = 0x80 | PTR_SIZE;
         write32le(p+4, 0x25FA);
         p[8] = 0x80 | PTR_SIZE*2;
         write32le(p+12, 0xF6);
     }
     plt_offset = plt->data_offset;
     relofs = s1->got->reloc ? s1->got->reloc->data_offset : 0;
     rel_size = relofs / sizeof (ElfW_Rel);
     /* Jump to GOT entry where ld.so initially put the address of ip + 4 */
     p = section_ptr_add(plt, 16);
     write32le(p, 0x2080 | (got_offset >> 4) << 8 | (0x0F & got_offset));
     p[4] = 0xF6;
     write32le(p+8, 0x2040 | (rel_size >> 4) << 8 | (0x0F & rel_size));
     write32le(p+12, 0x25FA);
     write32le(p+16, 0x6000 | (plt_offset >> 4) << 8 | (0x0F & plt_offset));
     return plt_offset;
}
/* relocate the PLT: compute addresses and offsets in the PLT now that final
   address for PLT and GOT are known (see fill_program_header) */
ST_FUNC void relocate_plt(TCCState *s1)
{
    uint8_t *p, *p_end;
    if (!s1->plt)
      return;
    p = s1->plt->data;
    p_end = p + s1->plt->data_offset;
    if (p < p_end) {
        uint64_t plt = s1->plt->sh_addr;
        uint64_t got = s1->got->sh_addr;
        uint64_t off = (got >> 12) - (plt >> 12);
        if ((off + ((uint32_t)1 << 20)) >> 21)
            tcc_error_noabort("Failed relocating PLT (off=0x%lx, got=0x%lx, plt=0x%lx)", off, got, plt);
        write32le(p, 0x0);
        write32le(p + 4, 0x0);
        write32le(p + 8, 0x0);
        write32le(p + 12, 0x0);
        write32le(p + 16, 0x0);
        write32le(p + 20, 0x0);
        write32le(p + 24, 0x0);
        write32le(p + 28, 0x0);
        p += 32;
        while (p < p_end) {
            uint64_t pc = plt + (p - s1->plt->data);
            uint32_t addr = got + read32le(p);
            uint64_t off = (addr >> 12) - (pc >> 12);
            if ((off + ((uint32_t)1 << 20)) >> 21)
                tcc_error_noabort("Failed relocating PLT (off=0x%lx, addr=0x%x, pc=0x%lx)", off, addr, pc);
            write32le(p, 0x0);
            write32le(p + 4, 0x0);
            write32le(p + 8, 0x0);
            write32le(p + 12, 0x0);
            p += 16;
        }
    }
}

#include "transputer-linkopt.h"

static void tp_layout_alloc_section(Section *s, uint32_t *current_addr,
                                    uint32_t base_addr, uint32_t *total_size,
                                    int link_debug)
{
    uint32_t section_size = s->data_offset;

    if (s->sh_addralign > 1)
        *current_addr = (*current_addr + s->sh_addralign - 1) &
                        ~(s->sh_addralign - 1);
    s->sh_addr = *current_addr;
    *current_addr += section_size;
    *total_size = *current_addr - base_addr;
    if (link_debug) {
        fprintf(stderr, " -> Allocatable: addr=0x%08x size=%u, total_size now=%u\n",
                s->sh_addr, section_size, *total_size);
    }
}

static int tp_layout_sections(TCCState *s1, uint32_t base_addr,
                              uint32_t *out_total_size,
                              uint32_t *out_total_memsz,
                              uint32_t *out_end_addr,
                              int xip_mode,
                              uint32_t *out_text_end,
                              uint32_t *out_data_start,
                              int link_debug)
{
    uint32_t current_addr = base_addr;
    uint32_t total_size = 0;
    uint32_t total_memsz = 0;
    int i;

    if (link_debug)
        fprintf(stderr, "transputer_simple_link: Found %d sections in input\n", s1->nb_sections);

    for (i = 1; i < s1->nb_sections; i++) {
        Section *s = s1->sections[i];
        uint32_t section_size;

        if (strcmp(s->name, ".text.start") != 0 || !(s->sh_flags & SHF_ALLOC))
            continue;

        section_size = s->data_offset;
        if (link_debug) {
            fprintf(stderr, " Section %d: %s flags=0x%x data_offset=%lu sh_size=%lu type=%d (BOOT CODE - PLACED FIRST)\n",
                    i, s->name, (unsigned int)s->sh_flags,
                    (unsigned long)s->data_offset, (unsigned long)s->sh_size, s->sh_type);
        }
        s->sh_addr = current_addr;
        current_addr += s->sh_type == SHT_NOBITS ? 0 : section_size;
        total_size = current_addr - base_addr;
        if (link_debug) {
            fprintf(stderr, " -> Allocatable: addr=0x%08x size=%u, total_size now=%u\n",
                    s->sh_addr, section_size, total_size);
        }
        break;
    }

    for (i = 1; i < s1->nb_sections; i++) {
        Section *s = s1->sections[i];

        if (strcmp(s->name, ".text.start") == 0 || s->sh_type == SHT_NOBITS ||
            tp_sched_class_rank(s->name) >= 0 ||
            strcmp(s->name, ".init.setup") == 0)
            continue;
		if (xip_mode && !(s->sh_flags & SHF_EXECINSTR))
			continue;

        if (link_debug) {
            fprintf(stderr, " Section %d: %s flags=0x%x data_offset=%lu sh_size=%lu type=%d\n",
                    i, s->name, (unsigned int)s->sh_flags,
                    (unsigned long)s->data_offset, (unsigned long)s->sh_size, s->sh_type);
        }
        if (s->sh_flags & SHF_ALLOC)
            tp_layout_alloc_section(s, &current_addr, base_addr, &total_size,
                                    link_debug);
    }

	if (out_text_end)
		*out_text_end = xip_mode ? current_addr : 0;
	if (xip_mode) {
		if (current_addr > 0x7FFFF000u) {
			fprintf(stderr,
				"ERROR: XiP text ends at 0x%08x, overlapping boot vectors\n",
				current_addr);
			return -1;
		}
		current_addr = 0x80000070u;
		if (out_data_start)
			*out_data_start = current_addr;

		for (i = 1; i < s1->nb_sections; i++) {
			Section *s = s1->sections[i];

			if (!(s->sh_flags & SHF_ALLOC) ||
			    (s->sh_flags & SHF_EXECINSTR) ||
			    s->sh_type == SHT_NOBITS ||
			    tp_sched_class_rank(s->name) >= 0 ||
			    strcmp(s->name, ".init.setup") == 0)
				continue;
			tp_layout_alloc_section(s, &current_addr, base_addr,
						&total_size, link_debug);
		}
	} else if (out_data_start) {
		*out_data_start = 0;
	}

    /* Keep every .init.setup input contiguous so __setup_start..__setup_end
     * can walk obs_kernel_param records (TCC does not honour vmlinux.lds
     * INIT_SETUP). Same contract as SCHED_DATA below. */
    for (i = 1; i < s1->nb_sections; i++) {
        Section *s = s1->sections[i];

        if (strcmp(s->name, ".init.setup") != 0 ||
            s->sh_type == SHT_NOBITS || !(s->sh_flags & SHF_ALLOC))
            continue;
        if (link_debug)
            fprintf(stderr, " .init.setup size=%lu\n",
                    (unsigned long)s->data_offset);
        tp_layout_alloc_section(s, &current_addr, base_addr, &total_size,
                                link_debug);
    }

    /* Match include/asm-generic/vmlinux.lds.h:SCHED_DATA. */
    for (int rank = 0; rank < 6; rank++) {
        for (i = 1; i < s1->nb_sections; i++) {
            Section *s = s1->sections[i];

            if (tp_sched_class_rank(s->name) != rank ||
                s->sh_type == SHT_NOBITS || !(s->sh_flags & SHF_ALLOC))
                continue;
            if (link_debug)
                fprintf(stderr, " Scheduler class %s (rank %d)\n", s->name, rank);
            tp_layout_alloc_section(s, &current_addr, base_addr, &total_size,
                                    link_debug);
        }
    }

    total_memsz = total_size;
    for (i = 1; i < s1->nb_sections; i++) {
        Section *s = s1->sections[i];
        uint32_t section_size;

        if (s->sh_type != SHT_NOBITS || !(s->sh_flags & SHF_ALLOC))
            continue;

        section_size = s->data_offset;
        if (link_debug) {
            fprintf(stderr, " Section %d: %s flags=0x%x data_offset=%lu (BSS)\n",
                    i, s->name, (unsigned int)s->sh_flags, (unsigned long)s->data_offset);
        }
        if (s->sh_addralign > 1)
            current_addr = (current_addr + s->sh_addralign - 1) & ~(s->sh_addralign - 1);
        s->sh_addr = current_addr;
        current_addr += section_size;
        total_memsz = current_addr - base_addr;
        if (link_debug) {
            fprintf(stderr, " -> BSS: addr=0x%08x size=%u, total_memsz now=%u\n",
                    s->sh_addr, section_size, total_memsz);
        }
    }

    *out_total_size = total_size;
    *out_total_memsz = total_memsz;
    *out_end_addr = current_addr;
    return 0;
}

static void tp_define_abs_symbol(Section *symtab_sec, const char *name,
                                 uint32_t value)
{
    ElfW(Sym) *sym = find_sym_by_name(symtab_sec, name);

    if (!sym)
        return;
    sym->st_shndx = SHN_ABS;
    sym->st_value = value;
}

/* Define the bounds normally assigned by an ELF linker script around a
 * named input section.  The Transputer final linker deliberately owns the
 * layout instead of interpreting vmlinux.lds, so consumers of those symbols
 * must see the addresses chosen above before relocations are applied. */
static int tp_define_section_bounds(TCCState *s1, Section *symtab_sec,
                                    const char *section_name,
                                    const char *start_name,
                                    const char *end_name,
                                    int link_debug)
{
    uint32_t start = 0;
    uint32_t end = 0;
    int i;

    for (i = 1; i < s1->nb_sections; i++) {
        Section *s = s1->sections[i];
        uint32_t section_end;

        if (strcmp(s->name, section_name) != 0 ||
            !(s->sh_flags & SHF_ALLOC))
            continue;
        section_end = (uint32_t)s->sh_addr + (uint32_t)s->data_offset;
        if (!start)
            start = (uint32_t)s->sh_addr;
        if (end && (uint32_t)s->sh_addr != end) {
            fprintf(stderr, "ERROR: %s sections are not contiguous\n",
                    section_name);
            return -1;
        }
        end = section_end;
    }

    if (!start)
        return 0;
    tp_define_abs_symbol(symtab_sec, start_name, start);
    tp_define_abs_symbol(symtab_sec, end_name, end);
    if (link_debug)
        fprintf(stderr, " Section bounds: %s=[0x%08x,0x%08x)\n",
                section_name, start, end);
    return 0;
}

static int tp_apply_relocations(TCCState *s1, Section *symtab_sec,
                                const struct tp_reloc_index *index,
                                uint8_t *image, size_t total_size,
                                uint32_t base_addr, uint32_t start_kernel_addr,
                                int link_debug, int link_trace,
                                int reloc_fix_debug, int iprel_patch_trace,
                                int iprel_patch_debug)
{
    Section *s;
    ElfW(Sym) *sym;
    ElfW_Rel *rel, *rel_end;
    int i, sym_index, reloc_count, type;
    int unresolved_calls = 0;
    int failed = 0;
    addr_t val, addr;
    uint8_t *ptr;

    if (link_debug)
        fprintf(stderr, "=== DEBUG: Applying Relocations ===\n");
    for (i = 1; i < s1->nb_sections; i++) {
        s = s1->sections[i];
        /* Only sections in the image: .stab and .debug_* have no address
           here, and patching them wrote far outside the image (-g links
           crashed). */
        if (s->reloc && (s->sh_flags & SHF_ALLOC)) {
            reloc_count = s->reloc->data_offset / sizeof(ElfW_Rel);
            if (link_debug)
                fprintf(stderr, "Section %d (%s) has %d relocations\n", i, s->name, reloc_count);
            rel_end = (ElfW_Rel *)(s->reloc->data + s->reloc->data_offset);
            for (rel = (ElfW_Rel *)s->reloc->data; rel < rel_end; rel++) {
                int reloc_in_range = 0;
                size_t reloc_dump_start = 0;
                size_t reloc_dump_end = 0;
                size_t off, available;
                int width;

                type = ELFW(R_TYPE)(rel->r_info);
                width = tp_reloc_width(type, s->sh_flags & SHF_EXECINSTR);
                if (width < 0)
                    return tcc_error_noabort("unsupported Transputer relocation %d at %s+0x%lx",
                                             type, s->name, (unsigned long)rel->r_offset);
                if (!width)
                    continue;
                sym_index = ELFW(R_SYM)(rel->r_info);
                if ((unsigned)sym_index >= symtab_sec->data_offset / sizeof(*sym))
                    return tcc_error_noabort("invalid symbol index %d at %s+0x%lx",
                                             sym_index, s->name, (unsigned long)rel->r_offset);
                addr = s->sh_addr + rel->r_offset;
                off = (uint32_t)(addr - base_addr);
                if (s->sh_type == SHT_NOBITS || rel->r_offset > s->data_offset
                    || s->data_offset - rel->r_offset < (size_t)width
                    || off > total_size || total_size - off < (size_t)width)
                    return tcc_error_noabort("relocation %d outside %s at offset 0x%lx",
                                             type, s->name, (unsigned long)rel->r_offset);
                available = s->data_offset - rel->r_offset;
                if (available > total_size - off)
                    available = total_size - off;
                sym = &((ElfW(Sym) *)symtab_sec->data)[sym_index];
                val = sym->st_value;
                if (sym->st_shndx > 0 && sym->st_shndx < s1->nb_sections)
                    val += s1->sections[sym->st_shndx]->sh_addr;
                tp_resolve_reloc_symbol(s1, symtab_sec, index, type, &sym, &sym_index,
                                        &val, addr, reloc_fix_debug);
                if (sym->st_shndx == SHN_UNDEF)
                    val = tp_resolve_array_bound_symbol(symtab_sec, sym, val,
                                                        &sym->st_shndx);
                /* Report *any* reference to an undefined symbol.
                 *
                 * This used to be opt-in behind T4_STRICT_UNRESOLVED_CALLS
                 * and to cover only IPREL32 branches whose target resolved to
                 * zero.  That missed the common case entirely: an indirect
                 * call goes through R_TRANSPUTER_LDC_ABS, so a missing
                 * function was encoded as `ldc <garbage>; gcall` with no
                 * diagnostic at all, exit status 0, and a binary that wild-
                 * jumps at run time.  A missing runtime helper (e.g. the
                 * __divdi3 family when transputer-libtcc1.a is not on the
                 * link line) therefore presented as an unexplained hang,
                 * which is expensive to bisect and has cost real debugging
                 * time more than once.
                 *
                 * Default is now a loud warning rather than an error, because
                 * the kernel link legitimately tolerates some unresolved
                 * references; T4_STRICT_UNRESOLVED_CALLS still makes it
                 * fatal.  Weak symbols are silent, as they must be. */
                if (sym->st_shndx == SHN_UNDEF &&
                    ELFW(ST_BIND)(sym->st_info) != STB_WEAK) {
                    const char *un = tp_reloc_symbol_name(symtab_sec, sym);
                    if (tp_report_undefined_symbol(un)) {
                        fprintf(stderr,
                                "%s: undefined symbol '%s' referenced at "
                                "0x%08x (reloc type %d)\n",
                                tp_env_enabled("T4_STRICT_UNRESOLVED_CALLS")
                                    ? "error" : "warning",
                                un ? un : "<unnamed>", (unsigned int)addr,
                                type);
                    }
                    if (tp_env_enabled("T4_STRICT_UNRESOLVED_CALLS")) {
                        unresolved_calls++;
                        continue;
                    }
                }
                /* Debug: log IPREL32 relocations that resolve to address 0 */
                if (type == R_TRANSPUTER_IPREL32 && val == 0 && tp_env_enabled("T4_ZERO_RELOC_DEBUG")) {
                    const char *sn = tp_reloc_symbol_name(symtab_sec, sym);
                    fprintf(stderr, "ZERO_RELOC: IPREL32 addr=0x%08x sym='%s' sym_index=%d "
                            "st_value=0x%lx st_shndx=%d bind=%d type=%d\n",
                            (unsigned int)addr, sn, sym_index,
                            (unsigned long)sym->st_value, sym->st_shndx,
                            ELFW(ST_BIND)(sym->st_info), ELFW(ST_TYPE)(sym->st_info));
                }
                ptr = image + off;
                tp_trace_reloc_window(symtab_sec, sym, image, total_size,
                                      base_addr, addr, type, val, s->name,
                                      link_trace, &reloc_in_range,
                                      &reloc_dump_start, &reloc_dump_end);
                if (available >= 4)
                    tp_debug_reloc_near_start_kernel(symtab_sec, sym, start_kernel_addr,
                                                    addr, type, val, ptr, link_debug);
                if (tp_apply_single_relocation(s, symtab_sec, sym, ptr, available,
                                               addr, val, type, width, iprel_patch_trace,
                                               iprel_patch_debug) < 0)
                    failed++;
                if (reloc_in_range && link_trace) {
                    fprintf(stderr, "RELOC_RANGE_AFTER:");
                    for (size_t di = reloc_dump_start; di <= reloc_dump_end; di++)
                        fprintf(stderr, " %02x", image[di]);
                    fprintf(stderr, "\n");
                }
                if (available >= 4 && start_kernel_addr > 0 &&
                    addr >= start_kernel_addr && addr < start_kernel_addr + 64) {
                    if (link_debug)
                        fprintf(stderr, ", After: %08x\n", read32le(ptr));
                }
            }
        }
    }
    if (link_debug)
        fprintf(stderr, "=== DEBUG: Relocations Complete ===\n\n");
    if (unresolved_calls) {
        fprintf(stderr, "error: %d unresolved Transputer call target(s)\n",
                unresolved_calls);
        return -1;
    }
    if (failed)
        return -1;
    return 0;
}
/* Simple linker for Transputer bare-metal executables */
/* Give SHN_COMMON symbols (-fcommon tentative definitions) their .bss
   space, as resolve_common_syms() does: elf_output_file() hands off to
   this linker before reaching it, and they were addressed as their
   alignment.  The rest of resolve_common_syms(), the linker-provided
   symbols, this linker defines itself. */
static void tp_alloc_common_syms(TCCState *s1)
{
    ElfW(Sym) *sym;

    for_each_elem(symtab_section, 1, sym, ElfW(Sym)) {
        if (sym->st_shndx == SHN_COMMON && sym->st_size) {
            /* symbol alignment is in st_value for SHN_COMMONs */
            sym->st_value = section_add(bss_section, sym->st_size,
                                        sym->st_value);
            sym->st_shndx = bss_section->sh_num;
        }
    }
}

ST_FUNC int transputer_simple_link(TCCState *s1, const char *output_filename)
{
    /* This implements basic ELF linking for Transputer targets */
    /* Similar to simple-transputer-linker but integrated into TCC */
    int xip_mode = tp_env_enabled("T4_XIP_LINK");
    /* MemStart: #80000070 on the T800/T805/T425/T9000, #80000048 on the
       T414 (-mt414 on the link command), which has less on-chip RAM. */
    uint32_t base_addr = xip_mode ? 0x7F001000u
                         : s1->has_text_addr ? s1->text_addr
                         : s1->tp_isa == TP_ISA_T414 ? 0x80000048u : 0x80000070u;
    uint32_t total_size = 0;
    uint32_t total_memsz = 0;  /* includes BSS (p_memsz) */
    uint32_t current_addr = base_addr;
	uint32_t xip_text_end = 0;
	uint32_t xip_data_start = 0;
    uint32_t entry_point;
    uint8_t *image;
    uint32_t start_kernel_addr;
    const char *start_kernel_name;
    int strict_boot_asserts;
    int has_start_kernel_load_point;
    int has_return_label_load_point;
    uint32_t bss_expected_start;
    uint32_t bss_expected_stop;
    int bss_expected_valid;
    int link_debug = tp_env_enabled("T4_LINK_DEBUG");
    int link_trace = tp_env_enabled("T4_LINK_TRACE");
    int reloc_fix_debug = tp_env_enabled("T4_RELOC_FIX_DEBUG");
    int iprel_patch_trace = tp_env_enabled("T4_IPREL_PATCH_TRACE");
    int iprel_patch_debug = tp_env_enabled("T4_IPREL_PATCH_DEBUG");
    InitcallLayout initcall_layout;
    uint8_t *fixed;

    tp_alloc_common_syms(s1);
    fixed = tp_fixed_layout_sections(s1);
    tp_optimize_code_sections(s1, fixed, link_debug);
    tp_icf_fold(s1, fixed, link_debug);
    tp_link_relax(s1, fixed, link_debug);
    tcc_free(fixed);
    if (tp_layout_sections(s1, base_addr, &total_size, &total_memsz,
			   &current_addr, xip_mode, &xip_text_end,
			   &xip_data_start, link_debug) < 0)
		return -1;
    if (tp_define_section_bounds(s1, symtab_section, ".init.ramfs",
                                 "__initramfs_start", "__initramfs_end",
                                 link_debug) < 0)
        return -1;
    /* __setup() records live in .init.setup.  Surrogates in
     * initcall_table.c exist for vmlinux.o; the final link points
     * __setup_start/__setup_end at this section (rdinit= etc.). */
    if (tp_define_section_bounds(s1, symtab_section, ".init.setup",
                                 "__setup_start", "__setup_end",
                                 link_debug) < 0)
        return -1;
    if (tp_define_sched_class_boundaries(s1, symtab_section, link_debug) < 0)
        return -1;
    if (tp_define_initcall_boundaries(s1, symtab_section, &initcall_layout,
                                      link_debug) < 0)
        return -1;
    /* Report total image size */
    if (s1->verbose || total_size > 8 * 1024 * 1024) {
        fprintf(stderr, "transputer_simple_link: Linking %s\n", output_filename);
        fprintf(stderr, " Total image size: %u bytes (%.2f MB)\n",
                total_size, total_size / (1024.0 * 1024.0));
        fprintf(stderr, " Base address: 0x%08x\n", base_addr);
        fprintf(stderr, " End address: 0x%08x\n", current_addr);
		if (xip_mode)
			fprintf(stderr,
				" XiP text: [0x%08x,0x%08x), RAM image: 0x%08x\n",
				base_addr, xip_text_end, xip_data_start);
    }
    /* Check if size is too large for current BTL format (255 byte chunks) */
    /* Maximum practical size is limited by Transputer memory, typically 8-64MB */
    if (total_size > 64 * 1024 * 1024) {
        fprintf(stderr, "ERROR: Image size %u bytes (%.2f MB) exceeds 64MB limit\n",
                total_size, total_size / (1024.0 * 1024.0));
        return -1;
    }
    /* Create output image */
    if (link_debug)
        fprintf(stderr, " Allocating %u bytes for output image...\n", total_size);
    image = tcc_mallocz(total_size);
    if (!image) {
        fprintf(stderr, "ERROR: Failed to allocate %u bytes for output image\n", total_size);
        return -1;
    }
    if (link_debug)
        fprintf(stderr, " Memory allocated successfully\n");
    has_start_kernel_load_point = find_sym_by_name(symtab_section, "__start_kernel_load_point") != NULL;
    has_return_label_load_point = find_sym_by_name(symtab_section, "__return_label_addr") != NULL;
    strict_boot_asserts = has_start_kernel_load_point;
    bss_expected_start = 0;
    bss_expected_stop = 0;
    bss_expected_valid = 0;
    /* Find start_kernel address — try wrapper first, then C entry point */
    start_kernel_addr = 0;
    start_kernel_name = NULL;
    {
        static const char *sk_names[] = {
            "start_kernel_wrapper", "_start_kernel_wrapper",
            "start_kernel", "_start_kernel", NULL
        };
        int ni;
        for (ni = 0; sk_names[ni]; ni++) {
            if (tp_find_symbol_addr(s1, symtab_section, sk_names[ni], &start_kernel_addr) > 0) {
                start_kernel_name = sk_names[ni];
                if (link_debug)
                    fprintf(stderr, "Found %s at 0x%08x\n", sk_names[ni], start_kernel_addr);
                break;
            }
        }
    }
    if (has_start_kernel_load_point && start_kernel_addr == 0) {
        fprintf(stderr, "ERROR: __start_kernel_load_point present but start_kernel symbol was not resolved\n");
        tcc_free(image);
        return -1;
    }
    tp_copy_alloc_sections(s1, image, base_addr);

    {
        struct tp_reloc_index index;
        int result;
        tp_reloc_index_init(&index, symtab_section);
        result = tp_apply_relocations(s1, symtab_section, &index, image,
                                      total_size, base_addr, start_kernel_addr,
                                      link_debug, link_trace, reloc_fix_debug,
                                      iprel_patch_trace, iprel_patch_debug);
        tcc_free(index.heads);
        tcc_free(index.next);
        tcc_free(index.bucket);
        if (result < 0) {
            tcc_free(image);
            return -1;
        }
    }

    /* Post-link CALL target audit: verify all IPREL32 relocations encoded correctly.
     * Decodes each patched CALL/J/CJ instruction and checks the target matches
     * the intended symbol address. Enabled by T4_CALL_AUDIT=1. */
    if (tp_env_enabled("T4_CALL_AUDIT"))
        tp_audit_call_targets(s1, symtab_section, image, base_addr, total_size);

    /* NFIX→OPR scan REMOVED.
     *
     * OPR operands are unsigned (0x00–0xFB). tp_encode_instr() only uses
     * NFIX for negative operands. Since all OPR codes are positive, the
     * codegen never emits NFIX before OPR — the scan was fixing a
     * non-existent problem.
     *
     * Worse, the scan matched byte patterns in .data/.rodata sections
     * (any data containing 0x6X 0xFY), silently corrupting pointers,
     * constants and structure fields. This caused layout-dependent boot
     * failures: shifting .text by even ~100 bytes moved .data, changing
     * which data bytes matched the pattern, corrupting different values
     * each time.
     *
     * Verified: kernel build produces 0 NFIX→OPR matches in .text,
     * 18 false-positive matches in .data (all corruptions).
     */
    {
        const char *dump_addr_env = getenv("T4_POST_RELOC_DUMP_ADDR");
        const char *dump_len_env = getenv("T4_POST_RELOC_DUMP_LEN");
        if (dump_addr_env && *dump_addr_env) {
            uint32_t dump_addr = (uint32_t)strtoul(dump_addr_env, NULL, 0);
            size_t dump_len = dump_len_env && *dump_len_env ? (size_t)strtoul(dump_len_env, NULL, 0) : 64;
            if (dump_addr >= base_addr && dump_addr < base_addr + total_size) {
                size_t dump_off = dump_addr - base_addr;
                size_t dump_end = dump_off + dump_len;
                if (dump_end > total_size)
                    dump_end = total_size;
                fprintf(stderr, "POST_RELOC_DUMP: addr=0x%08x len=%zu\n",
                        dump_addr, dump_end - dump_off);
                for (size_t di = dump_off; di < dump_end; di++) {
                    if ((di - dump_off) % 16 == 0)
                        fprintf(stderr, "  %08x:", (unsigned int)(dump_addr + (di - dump_off)));
                    fprintf(stderr, " %02x", image[di]);
                    if ((di - dump_off) % 16 == 15 || di + 1 == dump_end)
                        fprintf(stderr, "\n");
                }
            } else {
                fprintf(stderr, "POST_RELOC_DUMP: addr=0x%08x out of range (base=0x%08x size=0x%08x)\n",
                        dump_addr, (unsigned int)base_addr, (unsigned int)total_size);
            }
        }
    }
    {
        const char *dump_sym_env = getenv("T4_DUMP_SYM");
        if (dump_sym_env && *dump_sym_env && symtab_section) {
            ElfW(Sym) *dump_sym = find_sym_by_name(symtab_section, dump_sym_env);
            if (!dump_sym) {
                fprintf(stderr, "DUMP_SYM: %s not found\n", dump_sym_env);
            } else if (dump_sym->st_shndx <= 0 || dump_sym->st_shndx >= s1->nb_sections) {
                fprintf(stderr, "DUMP_SYM: %s has bad shndx %d\n",
                        dump_sym_env, dump_sym->st_shndx);
            } else {
                Section *dump_sec = s1->sections[dump_sym->st_shndx];
                uint32_t dump_addr = dump_sec->sh_addr + dump_sym->st_value;
                fprintf(stderr, "DUMP_SYM: %s addr=0x%08x shndx=%d section=%s\n",
                        dump_sym_env, dump_addr, dump_sym->st_shndx,
                        dump_sec->name);
            }
        }
    }
    /* =====================================================================
     * NEW MINIX-STYLE LDC PATCHING (2026-01-15)
     * =====================================================================
     * The rewritten head.S uses direct 8-byte LDC sequences instead of
     * position-independent ldpi+adc calculations. This is simpler and
     * more robust.
     *
     * Each load point has 8 bytes: 7 PFIX + 1 LDC for a full 32-bit address.
     * The linker patches these sequences with the target symbol address.
     */
    if (link_debug)
        fprintf(stderr, "\n=== Minix-Style LDC Sequence Patching ===\n");

    /* Patch BSS start/stop addresses.
     *
     * For this image, the runtime BSS start is not the raw .bss section
     * sh_addr. The netloader zero-fills the NOBITS tail beginning at the end
     * of the last loadable non-NOBITS section (PT_LOAD p_vaddr + p_filesz).
     * Derive the same value here so the hand-written bootstrap loop sees the
     * actual zero-fill bounds.
     */
    if (tp_patch_boot_bss_load_points(image, total_size, base_addr, s1, symtab_section,
                                      strict_boot_asserts, link_debug,
                                      &bss_expected_start, &bss_expected_stop,
                                      &bss_expected_valid) < 0) {
        tcc_free(image);
        return -1;
    }

    /* Patch start_kernel function address */
    if (tp_patch_verified_symbol_target(image, total_size, base_addr, s1, symtab_section,
                                        "__start_kernel_load_point", start_kernel_name,
                                        has_start_kernel_load_point, link_debug) < 0) {
        tcc_free(image);
        return -1;
    }

    /* Patch init_stack workspace pointer (needs calculation: init_stack + THREAD_SIZE - 64) */
    if (tp_patch_boot_init_stack(image, total_size, base_addr, s1, symtab_section,
                                 link_debug) < 0) {
        tcc_free(image);
        return -1;
    }

    /* Patch return address (halt label for when start_kernel returns) */
    if (tp_patch_verified_symbol_target(image, total_size, base_addr, s1, symtab_section,
                                        "__return_label_addr", "halt",
                                        has_return_label_load_point, link_debug) < 0) {
        tcc_free(image);
        return -1;
    }

    if (bss_expected_valid) {
        int bss_start_rc;
        int bss_stop_rc;
        bss_start_rc = tp_verify_abs_ldc_value(image, total_size, base_addr, s1, symtab_section,
                                               "__bss_start_load_point", bss_expected_start, ".bss start");
        bss_stop_rc = tp_verify_abs_ldc_value(image, total_size, base_addr, s1, symtab_section,
                                              "__bss_stop_load_point", bss_expected_stop, ".bss stop");
        if (bss_start_rc < 0 || bss_stop_rc < 0) {
            tcc_free(image);
            return -1;
        }
        if (strict_boot_asserts && (bss_start_rc != 1 || bss_stop_rc != 1)) {
            fprintf(stderr, "ERROR: required BSS load-point symbols are missing\n");
            tcc_free(image);
            return -1;
        }
        tp_dump_abs_ldc_bytes(image, total_size, base_addr, s1, symtab_section,
                              "__bss_start_load_point", ".bss start", link_debug);
        tp_dump_abs_ldc_bytes(image, total_size, base_addr, s1, symtab_section,
                              "__bss_stop_load_point", ".bss stop", link_debug);
    }

    {
        static const char *const boot_targets[][2] = {
            { "__test_main_load_point", "test_main" },
            { "__start_c_load_point", "_start_c" },
            { "__main_load_point", "main" },
            { "__halt_load_point", "_halt" },
            { "__uclibc_main_load_point", "__uClibc_main" },
            { "__crt0_start_addr", "_start" },
            { "__crt0_halt_addr", "_crt0_halt" }
        };
        int i;
        for (i = 0; i < countof(boot_targets); i++) {
            if (tp_patch_verified_symbol_target(image, total_size, base_addr,
                                                s1, symtab_section, boot_targets[i][0],
                                                boot_targets[i][1], 0, link_debug) < 0) {
                tcc_free(image);
                return -1;
            }
        }
    }

    /* Populate the initcall table (__initcall_start[]) with sorted
     * function pointers from scattered __initcall_* symbols. */
    if (initcall_layout.table_addr &&
        tp_populate_initcall_table(image, total_size, base_addr, s1,
                                   symtab_section, &initcall_layout,
                                   link_debug) < 0) {
        tcc_free(image);
        return -1;
    }

    /*
     * Patch linker script symbol surrogates (__linker_stext, etc.)
     * TCC doesn't handle "sym = .;" in linker scripts, so these data
     * words in head.S need to be filled with the correct section addresses.
     *
     * Compute bounds by scanning all ALLOC sections:
     * - _stext = min address of all executable sections
     * - _etext = max end of all executable sections
     * - _edata = max end of all writable non-BSS sections
     * - _end   = max end of all allocated sections (including BSS)
     */
    if (tp_patch_linker_surrogates(image, total_size, base_addr, s1, symtab_section,
                                   link_debug) < 0) {
        tcc_free(image);
        return -1;
    }

    if (link_debug)
        fprintf(stderr, "=== LDC Sequence Patching Complete ===\n\n");

    if (tp_find_boot_entry_point(s1, symtab_section, &entry_point,
                                 NULL, NULL, link_debug) < 0) {
        tcc_free(image);
        return -1;
    }

    if (tp_verify_boot_entry(entry_point, base_addr, strict_boot_asserts,
                             has_start_kernel_load_point, start_kernel_addr,
                             total_size) < 0) {
        tcc_free(image);
        return -1;
    }
    write_tccsym_map(s1, symtab_section, output_filename);
    if (tp_write_exec_image(output_filename, base_addr, entry_point,
                            total_size, total_memsz, xip_mode,
						xip_text_end, xip_data_start, image,
                            s1->tp_isa_used) < 0) {
        tcc_free(image);
        return -1;
    }
    tcc_free(image);
    return 0;
}

/* Instruction encoding now uses tp_encode_instr() from transputer-enc.h */

void relocate_init(Section *sr)
{
    if (sr->sh_type == SHT_REL || sr->sh_type == SHT_RELA) {
        sr->sh_addralign = 4;
        if (!sr->sh_entsize)
            sr->sh_entsize = sizeof(ElfW_Rel);
    }
}
void relocate(TCCState *s1, ElfW_Rel *rel, int type, unsigned char *ptr, addr_t addr, addr_t val)
{
    if (type != R_TRANSPUTER_NONE)
        tcc_error_noabort("Transputer relocation %d at 0x%lx requires the executable linker",
                          type, (unsigned long)addr);
}
#endif
