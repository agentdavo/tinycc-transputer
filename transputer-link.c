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
ST_FUNC unsigned tp_target_flags(TCCState *s1)
{
    unsigned f;

    if (s1->tp_t414)
        return 0;
    if (s1->tp_t425)
        return TP_EF_T800OPS | TP_EF_T805OPS;
    if (s1->tp_t800)
        return TP_EF_T800OPS | TP_EF_FPU;
    f = TP_EF_T800OPS | TP_EF_T805OPS | TP_EF_FPU;      /* the T805 */
    if (s1->tp_t9000)
        f |= TP_EF_T9000;
    if (s1->tp_tvx)
        f |= TP_EF_TVX;
    return f;
}

/* The -m option that selects the current target, less its "-m". */
ST_FUNC const char *tp_target_name(TCCState *s1)
{
    if (s1->tp_t414)
        return "t414";
    if (s1->tp_t425)
        return "t425";
    if (s1->tp_t800)
        return "t800";
    if (!s1->tp_t9000)
        return "t805";
    return s1->tp_tvx ? "t9000tvx" : "t9000";
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
    static const struct { unsigned flags; const char *name; } parts[] = {
        { TP_EF_T800OPS | TP_EF_T805OPS, "-mt425" },
        { TP_EF_T800OPS | TP_EF_FPU, "-mt800" },
        { TP_EF_T800OPS | TP_EF_T805OPS | TP_EF_FPU, "-mt805" },
        { TP_EF_T800OPS | TP_EF_T805OPS | TP_EF_FPU | TP_EF_T9000, "-mt9000" },
        { TP_EF_T800OPS | TP_EF_T805OPS | TP_EF_FPU | TP_EF_T9000 | TP_EF_TVX,
          "-mt9000tvx" },
    };
    unsigned extra = flags & ~tp_target_flags(s1) & ((TP_EF_TVX << 1) - 1);
    char needs[96];
    const char *built = NULL;
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
    for (k = 0; k < countof(parts); k++)
        if (parts[k].flags == flags)
            built = parts[k].name;
    return tcc_error_noabort("%s%s%s%s%s needs %s, which -m%s does not have; "
                             "rebuild it for -m%s or link for a bigger part",
                             member ? "archive member " : "this object",
                             member ? member : "",
                             built ? " (compiled for " : "",
                             built ? built : "", built ? ")" : "",
                             needs, tp_target_name(s1), tp_target_name(s1));
}

/*
 * Transputer post-compilation optimizers.
 * These run on individual .text sections after compilation but before
 * final section address assignment and relocation patching.
 */

/*
 * Peephole: in-place replacements by pfix 0 (a no-op at an instruction
 * boundary), so no address moves.
 *   j 0                      -> pfix 0   (j can deschedule; pfix 0 cannot)
 *   ldc 0; sum|diff|shl|shr|or|xor -> no-ops
 *   ldc 1; prod              -> no-ops
 *   adc 0, ldnlp 0           -> no-op
 * It runs over one function at a time, from its symbol, so decoding starts
 * on an instruction boundary and never strays into data between functions.
 * An instruction holding a relocation is a placeholder that the relocation
 * pass re-encodes later (`ldc sym` is pfix 0 x 8; ldc 0 until then), and a
 * pair whose second instruction is a branch target is reached without its
 * first; both are left alone.
 */

static int tp_peep_has_reloc(const uint8_t *reloc, size_t pos, int n)
{
    while (n-- > 0)
        if (reloc[pos++])
            return 1;
    return 0;
}

static void tp_peep_nop(uint8_t *data, size_t pos, int n)
{
    while (n-- > 0)
        data[pos++] = (TP_OP_PFIX << 4) | 0;
}

/* data[0..len) is one function; reloc[k] is set where a relocation
 * starts. */
static void tp_peephole_function(uint8_t *data, size_t len,
                                 const uint8_t *reloc)
{
    uint8_t *target = tcc_mallocz(len + 1);
    size_t i;
    int op1, opd1, n1, op2, opd2, n2;

    for (i = 0; i < len; i += n1) {
        n1 = tp_decode_instr_at(data, len, i, &op1, &opd1);
        if (n1 == 0)
            break;
        if (op1 == TP_OP_J || op1 == TP_OP_CJ) {
            long t = (long)(i + n1) + opd1;
            if (t >= 0 && t <= (long)len)
                target[t] = 1;
        }
    }

    for (i = 0; i < len; i += n1) {
        n1 = tp_decode_instr_at(data, len, i, &op1, &opd1);
        if (n1 == 0)
            break;
        if (tp_peep_has_reloc(reloc, i, n1))
            continue;
        if ((n1 == 1 && op1 == TP_OP_J && opd1 == 0)
            || (op1 == TP_OP_ADC && opd1 == 0)
            || (op1 == TP_OP_LDNLP && opd1 == 0)) {
            tp_peep_nop(data, i, n1);
            continue;
        }
        if (op1 != TP_OP_LDC || (opd1 != 0 && opd1 != 1) || i + n1 >= len)
            continue;
        n2 = tp_decode_instr_at(data, len, i + n1, &op2, &opd2);
        if (n2 == 0 || op2 != TP_OP_OPR || target[i + n1]
            || tp_peep_has_reloc(reloc, i + n1, n2))
            continue;
        if (opd1 == 0 ? (opd2 == TP_OPR_SUM || opd2 == TP_OPR_DIFF
                         || opd2 == TP_OPR_SHL || opd2 == TP_OPR_SHR
                         || opd2 == TP_OPR_OR || opd2 == TP_OPR_XOR)
                      : opd2 == TP_OPR_PROD) {
            tp_peep_nop(data, i, n1 + n2);
            n1 += n2;
        }
    }
    tcc_free(target);
}

static void transputer_peephole_optimize(TCCState *s1, Section *sec)
{
    uint8_t *reloc = tcc_mallocz(sec->data_offset + 1);
    ElfW(Sym) *sym;

    if (sec->reloc) {
        ElfW_Rel *rel;
        for_each_elem(sec->reloc, 0, rel, ElfW_Rel)
            if (rel->r_offset < sec->data_offset)
                reloc[rel->r_offset] = 1;
    }
    for_each_elem(symtab_section, 1, sym, ElfW(Sym)) {
        if (ELFW(ST_TYPE)(sym->st_info) == STT_FUNC
            && sym->st_shndx == sec->sh_num && sym->st_size
            && sym->st_value + sym->st_size <= sec->data_offset)
            tp_peephole_function(sec->data + sym->st_value, sym->st_size,
                                 reloc + sym->st_value);
    }
    tcc_free(reloc);
}

/* transputer_optimize_jumps removed — was opt-in (T4_OPTIMIZE_JUMPS env),
 * not needed until binary size optimization becomes a priority. */

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

static int tp_patch_boot_bss_load_points(uint8_t *image, size_t total_size,
                                         uint32_t base_addr, TCCState *s1,
                                         Section *symtab_sec,
                                         int strict_boot_asserts,
                                         int link_debug,
                                         uint32_t *out_bss_start,
                                         uint32_t *out_bss_stop,
                                         int *out_bss_valid)
{
    ElfW(Sym) *bss_start_load;
    ElfW(Sym) *bss_stop_load;
    Section *bss_sec = NULL;
    uint32_t bss_runtime_start = 0;
    int si;

    *out_bss_valid = 0;
    bss_start_load = find_sym_by_name(symtab_sec, "__bss_start_load_point");
    bss_stop_load = find_sym_by_name(symtab_sec, "__bss_stop_load_point");

    for (si = 1; si < s1->nb_sections; si++) {
        Section *s = s1->sections[si];
        if ((s->sh_flags & SHF_ALLOC) && s->sh_type != SHT_NOBITS) {
            uint32_t sec_size = s->data_offset ? s->data_offset : s->sh_size;
            uint32_t sec_end = s->sh_addr + sec_size;
            if (sec_end > bss_runtime_start)
                bss_runtime_start = sec_end;
        }
        if (strcmp(s->name, ".bss") == 0 && (s->sh_flags & SHF_ALLOC))
            bss_sec = s;
    }

    if (bss_sec && bss_start_load && bss_stop_load &&
        bss_start_load->st_shndx > 0 && bss_start_load->st_shndx < s1->nb_sections &&
        bss_stop_load->st_shndx > 0 && bss_stop_load->st_shndx < s1->nb_sections) {
        uint32_t bss_start_addr = bss_runtime_start;
        uint32_t bss_size = bss_sec->sh_type == SHT_NOBITS ? bss_sec->data_offset : bss_sec->sh_size;
        uint32_t bss_stop_addr = bss_start_addr + bss_size;
        uint32_t bss_start_load_addr = s1->sections[bss_start_load->st_shndx]->sh_addr
                                       + bss_start_load->st_value;
        uint32_t bss_stop_load_addr = s1->sections[bss_stop_load->st_shndx]->sh_addr
                                      + bss_stop_load->st_value;

        if (tp_patch_abs_ldc_value(image, total_size, base_addr, bss_start_load_addr,
                                   bss_start_addr, "__bss_start_load_point") < 0 ||
            tp_patch_abs_ldc_value(image, total_size, base_addr, bss_stop_load_addr,
                                   bss_stop_addr, "__bss_stop_load_point") < 0) {
            return -1;
        }

        if (link_debug) {
            fprintf(stderr, "LDC patched __bss_start_load_point: bss_start=0x%08X\n",
                    bss_start_addr);
            fprintf(stderr, "LDC patched __bss_stop_load_point: bss_stop=0x%08X (size=%u bytes)\n",
                    bss_stop_addr, bss_size);
        }
        *out_bss_start = bss_start_addr;
        *out_bss_stop = bss_stop_addr;
        *out_bss_valid = 1;
        return 1;
    }

    if (strict_boot_asserts) {
        fprintf(stderr, "ERROR: Could not patch BSS load points (.bss section or symbols not found)\n");
        return -1;
    }
    if (bss_start_load || bss_stop_load)
        fprintf(stderr, "Warning: Could not patch BSS load points (.bss section or symbols not found)\n");
    return 0;
}

static int tp_patch_boot_init_stack(uint8_t *image, size_t total_size,
                                    uint32_t base_addr, TCCState *s1,
                                    Section *symtab_sec, int link_debug)
{
    uint32_t load_point_addr = 0;
    uint32_t init_stack_addr = 0;
    uint32_t workspace_addr = 0;
    int have_load_point;
    int have_init_stack;

    have_load_point = tp_find_defined_symbol_addr(s1, symtab_sec,
                                                  "__init_stack_load_point",
                                                  &load_point_addr);
    have_init_stack = tp_find_defined_symbol_addr(s1, symtab_sec,
                                                  "init_stack",
                                                  &init_stack_addr);
    if (have_load_point == 0 || have_init_stack == 0) {
        if (link_debug)
            fprintf(stderr, "Warning: Could not find __init_stack_load_point or init_stack symbols\n");
        return 0;
    }
    if (have_load_point < 0 || have_init_stack < 0) {
        if (link_debug)
            fprintf(stderr, "Warning: Invalid section index for init_stack symbols\n");
        return 0;
    }

    workspace_addr = init_stack_addr + 0x2000 - 64;
    if (tp_patch_abs_ldc_value(image, total_size, base_addr, load_point_addr,
                               workspace_addr, "__init_stack_load_point") < 0)
        return -1;
    if (tp_verify_abs_ldc_value(image, total_size, base_addr, s1, symtab_sec,
                                "__init_stack_load_point", workspace_addr,
                                "init_stack workspace") < 0)
        return -1;
    tp_dump_abs_ldc_bytes(image, total_size, base_addr, s1, symtab_sec,
                          "__init_stack_load_point", "init_stack workspace",
                          link_debug);

    if (link_debug) {
        fprintf(stderr, "LDC patched __init_stack_load_point: init_stack=0x%08X workspace=0x%08X\n",
                init_stack_addr, workspace_addr);
    }
    return 1;
}

/*
 * Populate the __initcall_start[] table with sorted initcall function pointers.
 *
 * TCC creates __initcall_FOOlevel symbols (one per initcall) scattered in .data.
 * This function collects them, sorts by level ordinal, and writes the function
 * pointers into the __initcall_start[] array defined in initcall_table.c, whose
 * capacity is taken from the array's symbol size.  It then patches
 * __early_initcall_end and __initcall_end surrogates to point at the correct
 * offsets within the array.
 */
static int tp_initcall_level_ordinal(const char *suffix)
{
    /* Order must match vmlinux.lds.h INIT_CALLS definition */
    static const struct { const char *s; int ord; } levels[] = {
        { "rootfs", 12 }, { "early", 0 },
        { "7s", 16 }, { "6s", 14 }, { "5s", 11 }, { "4s", 9 },
        { "3s", 7 },  { "2s", 5 },  { "1s", 3 },
        { "7", 15 },  { "6", 13 },  { "5", 10 },  { "4", 8 },
        { "3", 6 },   { "2", 4 },   { "1", 2 },   { "0", 1 },
    };
    int i;
    for (i = 0; i < (int)(sizeof(levels) / sizeof(levels[0])); i++) {
        size_t slen = strlen(levels[i].s);
        size_t nlen = strlen(suffix);
        if (nlen >= slen && strcmp(suffix + nlen - slen, levels[i].s) == 0)
            return levels[i].ord;
    }
    return -1;
}

typedef struct {
    uint32_t fn_addr;
    int ordinal;
    uint32_t link_order;
} InitcallEntry;

typedef struct {
    uint32_t table_addr;
    uint32_t level_addr[8];
    uint32_t early_end_addr;
    uint32_t end_addr;
    int entry_count;
    int capacity;       /* slots in __initcall_start[] */
} InitcallLayout;

static int initcall_entry_cmp(const void *a, const void *b)
{
    const InitcallEntry *ea = (const InitcallEntry *)a;
    const InitcallEntry *eb = (const InitcallEntry *)b;
    if (ea->ordinal != eb->ordinal)
        return ea->ordinal - eb->ordinal;
    if (ea->link_order < eb->link_order)
        return -1;
    if (ea->link_order > eb->link_order)
        return 1;
    return 0;
}

static int tp_populate_initcall_table(uint8_t *image, size_t total_size,
                                       uint32_t base_addr, TCCState *s1,
                                       Section *symtab_sec,
                                       const InitcallLayout *layout,
                                       int link_debug)
{
    InitcallEntry *entries;
    int entry_count = 0;
    int i, nsyms;
    ElfW(Sym) *sym;
    const char *strtab;

    entries = tcc_malloc(layout->capacity * sizeof(*entries));

    /* Scan all symbols for __initcall_* entries */
    nsyms = symtab_sec->data_offset / sizeof(ElfW(Sym));
    strtab = (const char *)symtab_sec->link->data;

    for (i = 0; i < nsyms && entry_count < layout->capacity; i++) {
        const char *name;
        const char *suffix;
        int ordinal;
        uint32_t sym_addr, fn_ptr;
        size_t off;

        sym = &((ElfW(Sym) *)symtab_sec->data)[i];
        name = strtab + sym->st_name;

        if (strncmp(name, "__initcall_", 11) != 0)
            continue;

        /* Skip boundary/table symbols */
        if (strcmp(name, "__initcall_start") == 0 ||
            strcmp(name, "__initcall_end") == 0 ||
            strcmp(name, "__early_initcall_end") == 0)
            continue;

        /* The suffix after __initcall_ is "fnname" + "level" concatenated.
         * Parse level from the known suffixes at the end. */
        suffix = name + 11;  /* skip "__initcall_" */
        ordinal = tp_initcall_level_ordinal(suffix);
        if (ordinal < 0) {
            if (link_debug)
                fprintf(stderr, "[INITCALL] skipping %s (unknown level)\n", name);
            continue;
        }

        /* Read the function pointer value from the image at the symbol address */
        if (sym->st_shndx <= 0 || sym->st_shndx >= s1->nb_sections)
            continue;
        sym_addr = s1->sections[sym->st_shndx]->sh_addr + sym->st_value;
        off = sym_addr - base_addr;
        if (off + 4 > total_size)
            continue;

        fn_ptr = image[off] | (image[off+1] << 8) |
                 (image[off+2] << 16) | (image[off+3] << 24);

        if (fn_ptr == 0) {
            fprintf(stderr, "ERROR: initcall %s resolved to a NULL function\n", name);
            tcc_free(entries);
            return -1;
        }

        entries[entry_count].fn_addr = fn_ptr;
        entries[entry_count].ordinal = ordinal;
        entries[entry_count].link_order = sym_addr;
        entry_count++;

        if (link_debug >= 2)
            fprintf(stderr, "[INITCALL]   %s → fn=0x%08x ord=%d\n",
                    name, fn_ptr, ordinal);
    }

    if (entry_count != layout->entry_count) {
        fprintf(stderr,
                "ERROR: initcall count changed across relocation (%d before, %d after)\n",
                layout->entry_count, entry_count);
        tcc_free(entries);
        return -1;
    }

    /* Sort by level, preserving original link order within each level. */
    qsort(entries, entry_count, sizeof(entries[0]), initcall_entry_cmp);

    /* Write sorted function pointers into __initcall_start[] */
    for (i = 0; i < entry_count; i++) {
        uint32_t slot_addr = layout->table_addr + i * 4;
        if (tp_patch_u32_checked(image, base_addr, total_size, slot_addr,
                                  entries[i].fn_addr, "__initcall_start[]") < 0) {
            tcc_free(entries);
            return -1;
        }
    }
    tcc_free(entries);

    fprintf(stderr, "[INITCALL] Populated %d entries\n", entry_count);
    fprintf(stderr, "[INITCALL] __initcall_start=0x%08x __early_initcall_end=0x%08x "
            "__initcall_end=0x%08x\n", layout->table_addr,
            layout->early_end_addr, layout->end_addr);

    return 0;
}

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

static int tp_patch_linker_surrogates(uint8_t *image, size_t total_size,
                                      uint32_t base_addr, TCCState *s1,
                                      Section *symtab_sec, int link_debug)
{
    uint32_t stext_addr = 0xFFFFFFFF;
    uint32_t etext_addr = 0;
    uint32_t edata_addr = 0;
    uint32_t end_addr = 0;
    uint32_t fp_init_bootmem_node_addr = 0;
    uint32_t fp_free_bootmem_node_addr = 0;
    uint32_t fp_tp_vfs_caches_init_addr = 0;
    uint32_t fp_reserve_bootmem_node_addr = 0;
    uint32_t fp_alloc_bootmem_node_nopanic_addr = 0;
    int si;
    int ls;
    struct {
        const char *sym_name;
        uint32_t value;
    } patches[] = {
        { "__linker_stext", 0 },
        { "__linker_etext", 0 },
        { "__linker_edata", 0 },
        { "__linker_end",   0 },
        { "__fp_init_bootmem_node", 0 },
        { "__fp_free_bootmem_node", 0 },
        { "__fp_reserve_bootmem_node", 0 },
        { "__fp_alloc_bootmem_node_nopanic", 0 },
        { "__fp_tp_vfs_caches_init", 0 },
    };

    for (si = 1; si < s1->nb_sections; si++) {
        Section *ss = s1->sections[si];
        uint32_t sec_size;
        uint32_t sec_end;

        if (!(ss->sh_flags & SHF_ALLOC))
            continue;

        sec_size = (ss->sh_type == SHT_NOBITS) ? ss->data_offset : ss->sh_size;
        sec_end = ss->sh_addr + sec_size;
        if (sec_end > end_addr)
            end_addr = sec_end;
        if (ss->sh_flags & SHF_EXECINSTR) {
            if (ss->sh_addr < stext_addr)
                stext_addr = ss->sh_addr;
            if (sec_end > etext_addr)
                etext_addr = sec_end;
        }
        /*
         * edata must cover ALL non-BSS allocated sections — not just
         * writable ones.  .rodata (read-only string literals, const data)
         * is SHF_ALLOC but NOT SHF_WRITE.  If we only tracked SHF_WRITE
         * sections, the BSS zeroing in head.S would start too early and
         * wipe .rodata data including "proc", "sysfs", etc.
         */
        if (ss->sh_type != SHT_NOBITS) {
            if (sec_end > edata_addr)
                edata_addr = sec_end;
        }
    }

    tp_find_symbol_addr(s1, symtab_sec, "init_bootmem_node", &fp_init_bootmem_node_addr);
    tp_find_symbol_addr(s1, symtab_sec, "free_bootmem_node", &fp_free_bootmem_node_addr);
    tp_find_symbol_addr(s1, symtab_sec, "reserve_bootmem_node", &fp_reserve_bootmem_node_addr);
    tp_find_symbol_addr(s1, symtab_sec, "__alloc_bootmem_node_nopanic", &fp_alloc_bootmem_node_nopanic_addr);
    tp_find_symbol_addr(s1, symtab_sec, "tp_vfs_caches_init_wrapper", &fp_tp_vfs_caches_init_addr);

    patches[0].value = stext_addr;
    patches[1].value = etext_addr;
    patches[2].value = edata_addr;
    patches[3].value = end_addr;
    patches[4].value = fp_init_bootmem_node_addr;
    patches[5].value = fp_free_bootmem_node_addr;
    patches[6].value = fp_reserve_bootmem_node_addr;
    patches[7].value = fp_alloc_bootmem_node_nopanic_addr;
    patches[8].value = fp_tp_vfs_caches_init_addr;

    for (ls = 0; ls < (int)(sizeof(patches) / sizeof(patches[0])); ls++) {
        ElfW(Sym) *data_sym = find_sym_by_name(symtab_sec, patches[ls].sym_name);
        Section *data_sec;
        uint32_t data_addr;
        uint32_t val = patches[ls].value;

        if (!data_sym || data_sym->st_shndx <= 0 || data_sym->st_shndx >= s1->nb_sections)
            continue;

        data_sec = s1->sections[data_sym->st_shndx];
        data_addr = data_sec->sh_addr + data_sym->st_value;

        if (val == 0 && strncmp(patches[ls].sym_name, "__fp_", 5) == 0) {
            fprintf(stderr, "WARNING: surrogate %s resolved to 0x00000000\n",
                    patches[ls].sym_name);
        }
        if (tp_patch_u32_checked(image, base_addr, total_size, data_addr,
                                 val, patches[ls].sym_name) < 0) {
            return -1;
        }
        if (link_debug)
            fprintf(stderr, "Data patched %s = 0x%08X\n", patches[ls].sym_name, val);
    }

    return 0;
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

static void tp_debug_reloc_near_start_kernel(Section *symtab_sec, ElfW(Sym) *sym,
                                             uint32_t start_kernel_addr, addr_t addr,
                                             int type, addr_t val, uint8_t *ptr,
                                             int link_debug)
{
    if (start_kernel_addr > 0 && addr >= start_kernel_addr && addr < start_kernel_addr + 64) {
        if (link_debug) {
            fprintf(stderr, " !!! Relocation at 0x%08x affects start_kernel (offset +%d)\n",
                    (unsigned int)addr, (int)(addr - start_kernel_addr));
            fprintf(stderr, " Type: %d, Sym: %s, Value: 0x%08x, Before: %08x",
                    type, tp_reloc_symbol_name(symtab_sec, sym),
                    (unsigned int)val, read32le(ptr));
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

/* ------------------------------------------------------------------ */
/* Identical code folding (ICF)                                        */
/*                                                                     */
/* TCC never inlines, so every translation unit that includes a        */
/* `static inline` helper emits its own out-of-line copy.  Measured on */
/* the 7.35 MB of STB_LOCAL function bodies in vmlinux.a that is 966   */
/* copies of const_folio_flags, 1167 of __this_cpu_preempt_check and   */
/* so on.  This pass runs after all input sections have been merged,   */
/* finds byte-and-relocation-identical local functions inside a single */
/* output section, keeps one copy and redirects the rest.              */
/*                                                                     */
/* Safety rules, in order of importance:                               */
/*  - only STB_LOCAL STT_FUNC symbols fold.  A global symbol has an    */
/*    address other objects may compare, and we cannot see them all.   */
/*  - a function referenced by anything other than an IP-relative      */
/*    relocation has had its address taken (R_TRANSPUTER_32 into a     */
/*    dispatch table, R_TRANSPUTER_LDC_ABS into a register).  Two such */
/*    functions must keep distinct addresses, so they never fold.      */
/*  - two functions fold only when their relocations agree at the same */
/*    relative offsets AND name the same post-merge symbol index.      */
/*    Because tcc_load_object_file() unifies global symbols into one   */
/*    index, an equal index means literally the same target; a local   */
/*    target from another TU has a different index and simply blocks   */
/*    the fold.  No name comparison is needed.                         */
/*  - the walkback used by R_TRANSPUTER_LDC_ABS and the IPREL forms    */
/*    scans forward from r_offset over prefix bytes of the very same   */
/*    instruction, so it is unaffected by moving whole functions.  Call */
/*    slots are 6 bytes (+/-16 MB, any target in an image) unless the  */
/*    compiler narrowed one for a callee close behind it; such a       */
/*    callee is kept like an address-taken one (tp_icf_fold), so no    */
/*    redirected call can overflow its slot.                           */
/* ------------------------------------------------------------------ */

struct tp_icf_fn {
    uint32_t start;
    uint32_t size;
    uint32_t hash;
    int rel_first;      /* first entry in the section's sorted reloc index */
    int rel_count;
    int rep;            /* representative function, -1 when it is its own */
    int ok;             /* still a fold candidate */
};

struct tp_icf_rem {
    uint32_t start;     /* removed range, old offsets */
    uint32_t size;
    uint32_t rep_start; /* old offset of the surviving copy */
    uint32_t before;    /* bytes removed by earlier ranges */
};

static ElfW_Rel *tp_icf_sort_rel;

static int tp_icf_relidx_cmp(const void *a, const void *b)
{
    int ia = *(const int *)a, ib = *(const int *)b;
    addr_t oa = tp_icf_sort_rel[ia].r_offset, ob = tp_icf_sort_rel[ib].r_offset;

    if (oa < ob) return -1;
    if (oa > ob) return 1;
    return ia - ib;
}

static int tp_icf_start_cmp(const void *a, const void *b)
{
    const struct tp_icf_fn *fa = a, *fb = b;

    if (fa->start < fb->start) return -1;
    if (fa->start > fb->start) return 1;
    if (fa->size < fb->size) return -1;
    if (fa->size > fb->size) return 1;
    return 0;
}

static struct tp_icf_fn *tp_icf_sort_fn;

static int tp_icf_hash_cmp(const void *a, const void *b)
{
    const struct tp_icf_fn *fa = &tp_icf_sort_fn[*(const int *)a];
    const struct tp_icf_fn *fb = &tp_icf_sort_fn[*(const int *)b];

    if (fa->hash < fb->hash) return -1;
    if (fa->hash > fb->hash) return 1;
    if (fa->size < fb->size) return -1;
    if (fa->size > fb->size) return 1;
    if (fa->start < fb->start) return -1;
    if (fa->start > fb->start) return 1;
    return 0;
}

static int tp_icf_enabled(void)
{
    const char *e = getenv("T4_ICF");

    if (e && *e)
        return !(e[0] == '0' && e[1] == '\0');
    return 1;
}

/* Index of the last function with start <= off, or -1. */
static int tp_icf_find_fn(struct tp_icf_fn *fns, int n, uint32_t off)
{
    int lo = 0, hi = n - 1, res = -1;

    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (fns[mid].start <= off) { res = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    return res;
}

/* Bytes removed strictly before a kept offset. */
static uint32_t tp_icf_removed_before(struct tp_icf_rem *rem, int n, uint32_t off)
{
    int lo = 0, hi = n - 1, res = -1;

    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (rem[mid].start < off) { res = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    if (res < 0)
        return 0;
    return rem[res].before + rem[res].size;
}

/* Index of the removed range containing off, or -1. */
static int tp_icf_find_rem(struct tp_icf_rem *rem, int n, uint32_t off)
{
    int lo = 0, hi = n - 1, res = -1;

    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (rem[mid].start <= off) { res = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    if (res >= 0 && off < rem[res].start + rem[res].size)
        return res;
    return -1;
}

static uint32_t tp_icf_map(struct tp_icf_rem *rem, int nrem, uint32_t off)
{
    int k = tp_icf_find_rem(rem, nrem, off);

    if (k >= 0)
        off = rem[k].rep_start + (off - rem[k].start);
    return off - tp_icf_removed_before(rem, nrem, off);
}

static uint32_t tp_icf_hash_fn(const uint8_t *body, uint32_t size,
                               ElfW_Rel *rel, const int *relidx,
                               int first, int count, uint32_t start)
{
    uint32_t h = 2166136261u;
    uint32_t i;
    int j;

    h = (h ^ size) * 16777619u;
    for (i = 0; i < size; i++)
        h = (h ^ body[i]) * 16777619u;
    for (j = 0; j < count; j++) {
        ElfW_Rel *r = &rel[relidx[first + j]];
        uint32_t v = (uint32_t)(r->r_offset - start);
        uint32_t w = (uint32_t)r->r_info;
        int b;
        for (b = 0; b < 4; b++) h = (h ^ ((v >> (b * 8)) & 0xff)) * 16777619u;
        for (b = 0; b < 4; b++) h = (h ^ ((w >> (b * 8)) & 0xff)) * 16777619u;
    }
    return h;
}

static int tp_icf_same(struct tp_icf_fn *a, struct tp_icf_fn *b,
                       const uint8_t *data, ElfW_Rel *rel, const int *relidx)
{
    int j;

    if (a->size != b->size || a->rel_count != b->rel_count)
        return 0;
    if (memcmp(data + a->start, data + b->start, a->size) != 0)
        return 0;
    for (j = 0; j < a->rel_count; j++) {
        ElfW_Rel *ra = &rel[relidx[a->rel_first + j]];
        ElfW_Rel *rb = &rel[relidx[b->rel_first + j]];
        if (ra->r_offset - a->start != rb->r_offset - b->start)
            return 0;
        if (ra->r_info != rb->r_info)
            return 0;
    }
    return 1;
}

static int tp_icf_addr_taken_type(int type)
{
    /* IP-relative forms are branches and calls; everything else that can
     * reference a function materialises its address. */
    switch (type) {
    case R_TRANSPUTER_IPREL32:
    case R_TRANSPUTER_IPREL16:
    case R_TRANSPUTER_IPREL8:
        return 0;
    default:
        return 1;
    }
}

/* Fold one output section.  Returns the number of bytes removed. */
static uint32_t tp_icf_fold_section(TCCState *s1, int seci,
                                    const uint8_t *addr_taken,
                                    int link_debug, int *out_folded)
{
    Section *s = s1->sections[seci];
    Section *sr = s->reloc;
    ElfW(Sym) *syms = (ElfW(Sym) *)symtab_section->data;
    int nsym = (int)(symtab_section->data_offset / sizeof(ElfW(Sym)));
    ElfW_Rel *rel = sr ? (ElfW_Rel *)sr->data : NULL;
    int nrel = sr ? (int)(sr->data_offset / sizeof(ElfW_Rel)) : 0;
    struct tp_icf_fn *fns = NULL;
    struct tp_icf_rem *rem = NULL;
    int *relidx = NULL, *order = NULL;
    int nfn = 0, nrem = 0, i, k;
    uint32_t removed = 0, size = (uint32_t)s->data_offset;
    uint8_t *ndata = NULL;

    if (size == 0 || s->data == NULL)
        return 0;

    /* 1. Candidate functions: local STT_FUNC, non-empty, in range. */
    fns = tcc_malloc(sizeof(*fns) * (size_t)(nsym + 1));
    for (i = 1; i < nsym; i++) {
        ElfW(Sym) *sym = &syms[i];
        if (sym->st_shndx != seci)
            continue;
        if (ELFW(ST_TYPE)(sym->st_info) != STT_FUNC)
            continue;
        if (ELFW(ST_BIND)(sym->st_info) != STB_LOCAL)
            continue;
        if (sym->st_size == 0 || sym->st_value + sym->st_size > size)
            continue;
        if (addr_taken[i])
            continue;
        fns[nfn].start = (uint32_t)sym->st_value;
        fns[nfn].size = (uint32_t)sym->st_size;
        fns[nfn].rep = -1;
        fns[nfn].ok = 1;
        fns[nfn].hash = 0;
        fns[nfn].rel_first = 0;
        fns[nfn].rel_count = 0;
        nfn++;
    }
    if (nfn < 2)
        goto done;
    qsort(fns, (size_t)nfn, sizeof(*fns), tp_icf_start_cmp);

    /* Collapse aliases (several local names on one body) and reject any
     * pair that overlaps or disagrees about the body length. */
    k = 0;
    for (i = 1; i < nfn; i++) {
        if (fns[i].start == fns[k].start) {
            if (fns[i].size != fns[k].size)
                fns[k].ok = 0;
            continue;
        }
        if (fns[i].start < fns[k].start + fns[k].size) {
            fns[k].ok = 0;   /* overlapping bodies: leave both alone */
            fns[i].ok = 0;
        }
        fns[++k] = fns[i];
    }
    nfn = k + 1;

    /* 2. Any other symbol landing on or inside a body disqualifies it: a
     * global alias must keep its address, and an interior label means the
     * body is entered from somewhere we cannot see. */
    for (i = 1; i < nsym; i++) {
        ElfW(Sym) *sym = &syms[i];
        int f;
        if (sym->st_shndx != seci)
            continue;
        f = tp_icf_find_fn(fns, nfn, (uint32_t)sym->st_value);
        if (f < 0 || sym->st_value >= fns[f].start + fns[f].size)
            continue;
        if (sym->st_value != fns[f].start) {
            fns[f].ok = 0;
            continue;
        }
        if (ELFW(ST_TYPE)(sym->st_info) != STT_FUNC ||
            ELFW(ST_BIND)(sym->st_info) != STB_LOCAL ||
            sym->st_size != fns[f].size || addr_taken[i])
            fns[f].ok = 0;
    }

    /* 3. Attach relocations. */
    if (nrel > 0) {
        relidx = tcc_malloc(sizeof(int) * (size_t)nrel);
        for (i = 0; i < nrel; i++)
            relidx[i] = i;
        tp_icf_sort_rel = rel;
        qsort(relidx, (size_t)nrel, sizeof(int), tp_icf_relidx_cmp);
        k = 0;
        for (i = 0; i < nfn; i++) {
            int c = 0;
            while (k < nrel && rel[relidx[k]].r_offset < fns[i].start)
                k++;
            fns[i].rel_first = k;
            while (k + c < nrel &&
                   rel[relidx[k + c]].r_offset < fns[i].start + fns[i].size)
                c++;
            fns[i].rel_count = c;
        }
    }

    /* 4. Hash and group. */
    for (i = 0; i < nfn; i++) {
        if (!fns[i].ok)
            continue;
        fns[i].hash = tp_icf_hash_fn(s->data + fns[i].start, fns[i].size,
                                     rel, relidx, fns[i].rel_first,
                                     fns[i].rel_count, fns[i].start);
    }
    order = tcc_malloc(sizeof(int) * (size_t)nfn);
    for (i = 0; i < nfn; i++)
        order[i] = i;
    tp_icf_sort_fn = fns;
    qsort(order, (size_t)nfn, sizeof(int), tp_icf_hash_cmp);
    for (i = 0; i < nfn; ) {
        int j = i;
        while (j < nfn && fns[order[j]].hash == fns[order[i]].hash &&
               fns[order[j]].size == fns[order[i]].size)
            j++;
        /* Equal hash and length: confirm byte-for-byte before folding. */
        for (k = i; k < j; k++) {
            int a = order[k], m;
            if (!fns[a].ok || fns[a].rep >= 0)
                continue;
            for (m = k + 1; m < j; m++) {
                int b = order[m];
                if (!fns[b].ok || fns[b].rep >= 0)
                    continue;
                if (tp_icf_same(&fns[a], &fns[b], s->data, rel, relidx))
                    fns[b].rep = a;
            }
        }
        i = j;
    }

    /* 5. Removal plan, in address order. */
    rem = tcc_malloc(sizeof(*rem) * (size_t)nfn);
    for (i = 0; i < nfn; i++) {
        if (fns[i].rep < 0)
            continue;
        rem[nrem].start = fns[i].start;
        rem[nrem].size = fns[i].size;
        rem[nrem].rep_start = fns[fns[i].rep].start;
        rem[nrem].before = removed;
        removed += fns[i].size;
        nrem++;
    }
    if (nrem == 0) {
        removed = 0;
        goto done;
    }

    /* 6. Rewrite the section, then every symbol value and relocation
     * offset that referred to it. */
    ndata = tcc_malloc(size - removed ? size - removed : 1);
    {
        uint32_t src = 0, dst = 0;
        for (i = 0; i < nrem; i++) {
            uint32_t n = rem[i].start - src;
            memcpy(ndata + dst, s->data + src, n);
            dst += n;
            src = rem[i].start + rem[i].size;
        }
        memcpy(ndata + dst, s->data + src, size - src);
    }
    for (i = 1; i < nsym; i++) {
        if (syms[i].st_shndx != seci)
            continue;
        syms[i].st_value = tp_icf_map(rem, nrem, (uint32_t)syms[i].st_value);
    }
    if (nrel > 0) {
        int w = 0;
        for (i = 0; i < nrel; i++) {
            uint32_t off = (uint32_t)rel[i].r_offset;
            if (tp_icf_find_rem(rem, nrem, off) >= 0)
                continue;   /* the bytes it patched are gone */
            rel[w] = rel[i];
            rel[w].r_offset = off - tp_icf_removed_before(rem, nrem, off);
            w++;
        }
        sr->data_offset = (unsigned long)w * sizeof(ElfW_Rel);
        sr->sh_size = sr->data_offset;
    }
    memcpy(s->data, ndata, size - removed);
    s->data_offset = size - removed;
    s->sh_size = s->data_offset;
    *out_folded += nrem;
    if (link_debug || tp_env_enabled("T4_ICF_STATS"))
        fprintf(stderr, "  ICF %-16s %u -> %u bytes (%d bodies folded)\n",
                s->name, size, size - removed, nrem);

done:
    tcc_free(ndata);
    tcc_free(rem);
    tcc_free(order);
    tcc_free(relidx);
    tcc_free(fns);
    return removed;
}

/* References through a section symbol or an instruction addend may enter
 * the middle of a function. Keep the target section's bytes together and
 * do not peephole across potential entry points there. */
static uint8_t *tp_fixed_layout_sections(TCCState *s1)
{
    ElfW(Sym) *syms = (ElfW(Sym) *)symtab_section->data;
    int nsym = symtab_section->data_offset / sizeof(*syms);
    uint8_t *fixed = tcc_mallocz(s1->nb_sections);
    int i;

    for (i = 1; i < s1->nb_sections; i++) {
        Section *sr = s1->sections[i]->reloc;
        ElfW_Rel *r;
        if (!sr)
            continue;
        for_each_elem(sr, 0, r, ElfW_Rel) {
            int si = ELFW(R_SYM)(r->r_info);
            int type = ELFW(R_TYPE)(r->r_info);
            /* A narrow field embedded in text is data. For a PC-relative
             * field its addend may also measure back to an earlier label. */
            if (tp_data_layout_reloc(type))
                fixed[i] = 1;
            if (si <= 0 || si >= nsym || syms[si].st_shndx >= s1->nb_sections)
                continue;
            if (ELFW(ST_TYPE)(syms[si].st_info) == STT_SECTION
                || type == R_TRANSPUTER_INSN || tp_data_layout_reloc(type))
                fixed[syms[si].st_shndx] = 1;
        }
    }
    return fixed;
}

static void tp_icf_fold(TCCState *s1, const uint8_t *fixed, int link_debug)
{
    ElfW(Sym) *syms = (ElfW(Sym) *)symtab_section->data;
    int nsym = (int)(symtab_section->data_offset / sizeof(ElfW(Sym)));
    uint8_t *addr_taken;
    uint32_t total = 0;
    int folded = 0, i;

    if (!tp_icf_enabled())
        return;
    addr_taken = tcc_mallocz((size_t)nsym + 1);

    /* Note which symbols have their address materialised.
     *
     * A call slot narrower than the usual 6 bytes was sized by the compiler
     * for a callee close behind it in the same section (tp_call_slot_width).
     * Folding that callee away would send the call to the surviving copy,
     * possibly in another object and out of the slot's reach, so treat it
     * like an address-taken function: it keeps its body. */
    for (i = 1; i < s1->nb_sections; i++) {
        Section *ts = s1->sections[i];
        Section *sr = ts->reloc;
        ElfW_Rel *rel, *rel_end;
        if (!sr || !sr->data || !strcmp(ts->name, ".note.tprelax"))
            continue;           /* the list for tp_link_relax, not a use */
        rel = (ElfW_Rel *)sr->data;
        rel_end = (ElfW_Rel *)(sr->data + sr->data_offset);
        for (; rel < rel_end; rel++) {
            int type = ELFW(R_TYPE)(rel->r_info);
            int si = (int)ELFW(R_SYM)(rel->r_info);
            if (si <= 0 || si >= nsym)
                continue;
            if (tp_icf_addr_taken_type(type))
                addr_taken[si] = 1;
            else if (type == R_TRANSPUTER_IPREL32 && ts->data) {
                unsigned long w = rel->r_offset;
                while (w < ts->data_offset && tp_is_prefix_byte(ts->data[w]))
                    w++;
                if (w + 1 - rel->r_offset < 6)
                    addr_taken[si] = 1;
            }
        }
    }

    for (i = 1; i < s1->nb_sections; i++) {
        Section *s = s1->sections[i];
        if ((s->sh_flags & (SHF_ALLOC | SHF_EXECINSTR)) !=
            (SHF_ALLOC | SHF_EXECINSTR))
            continue;
        if (s->sh_type == SHT_NOBITS || s->data_offset == 0 || !s->data)
            continue;
        if (fixed[i]) {
            if (link_debug)
                fprintf(stderr, "  ICF %-16s skipped (fixed layout)\n",
                        s->name);
            continue;
        }
        if (strcmp(s->name, ".text.start") == 0)
            continue;   /* boot trampoline: layout is contractual */
        total += tp_icf_fold_section(s1, i, addr_taken, link_debug, &folded);
    }
    if (total && (link_debug || tp_env_enabled("T4_ICF_STATS")))
        fprintf(stderr, "transputer_simple_link: ICF folded %d duplicate "
                "function bodies, %u bytes\n", folded, total);
    tcc_free(addr_taken);
}

/* ------------------------------------------------------------------ */
/* Call relaxation after ICF                                          */
/*                                                                    */
/* A call to another function, or forward in the same object, keeps   */
/* its 6-byte slot through compilation: only at the link, once ICF    */
/* has picked the copy a call reaches, is its distance known.  Within */
/* one output section the distances are final before layout, so each  */
/* call slot whose target is in the same section shrinks to the width */
/* its displacement needs, and the section closes up.                 */
/*                                                                    */
/* Only code known to survive being taken apart may change: functions */
/* the compiler listed in .note.tprelax (no inline asm, no switch     */
/* jump table, no debug info) that pass the checks in tp_lr_function. */
/* In those, j/cj are re-encoded too, and pfix 0 no-ops between       */
/* instructions go.  Everything else in the section - assembler code, */
/* the other compiled functions - is a block that moves as a whole    */
/* and keeps its offset modulo the section alignment, so its internal */
/* IP-relative references and its .align padding stay valid.          */
/* ------------------------------------------------------------------ */

enum { TP_LR_FIXED, TP_LR_NOP, TP_LR_BRANCH, TP_LR_BLOCK };

struct tp_lr_item {
    uint32_t old_off, old_len;
    uint32_t new_off, new_len;      /* new_off: past any block padding */
    uint32_t target;                /* TP_LR_BRANCH: old offset */
    uint8_t kind, opcode;
};

struct tp_lr {
    struct tp_lr_item *it;
    int n, cap;
    const uint8_t *data;
    uint32_t size;
    const int *rel_at;          /* index of the relocation at an offset */
    const int32_t *call_to;     /* per relocation: same-section target */
    const uint8_t *sym_at;      /* a symbol is defined at this offset */
    const uint8_t *ref_in;      /* data refers to sym+addend here */
};

static struct tp_lr_item *tp_lr_add(struct tp_lr *lr, int kind,
                                    uint32_t off, uint32_t len)
{
    struct tp_lr_item *it;

    if (lr->n == lr->cap) {
        lr->cap = lr->cap ? lr->cap * 2 : 1024;
        lr->it = tcc_realloc(lr->it, sizeof(*lr->it) * (size_t)lr->cap);
    }
    it = &lr->it[lr->n++];
    memset(it, 0, sizeof(*it));
    it->kind = (uint8_t)kind;
    it->old_off = off;
    it->old_len = it->new_len = len;
    return it;
}

/* Index of the item starting at old offset `off` among it[lo..hi), or -1. */
static int tp_lr_find(const struct tp_lr_item *it, int lo, int hi, uint32_t off)
{
    int end = hi;

    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (it[mid].old_off < off)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < end && it[lo].old_off == off ? lo : -1;
}

/* Decode the function at [start, end) into items.  Returns 0, leaving no
 * items behind, when it cannot be taken apart safely. */
static int tp_lr_function(struct tp_lr *lr, uint32_t start, uint32_t end)
{
    const uint8_t *data = lr->data;
    int first = lr->n, i;
    uint32_t p = start, k;
    int prev_ldc = -1;          /* operand of an immediately preceding ldc */
    struct { uint32_t from, to; } *spans = NULL;
    int nspans = 0;

    while (p < end) {
        int r = lr->rel_at[p], op, opd, len;
        struct tp_lr_item *it;

        if (r != -1) {
            /* A relocated placeholder: its prefixes and opcode, rewritten
             * as a whole by tp_apply_relocations. */
            uint32_t q = p;
            while (q < end && tp_is_prefix_byte(data[q]))
                q++;
            if (q >= end)
                goto bad;
            for (k = p + 1; k <= q; k++)
                if (lr->rel_at[k] != -1)
                    goto bad;
            it = tp_lr_add(lr, TP_LR_FIXED, p, q + 1 - p);
            if (r >= 0 && lr->call_to[r] >= 0
                && (data[q] >> 4) == TP_OP_CALL) {
                it->kind = TP_LR_BRANCH;
                it->opcode = TP_OP_CALL;
                it->target = (uint32_t)lr->call_to[r];
            }
            prev_ldc = -1;
            p = q + 1;
            continue;
        }
        if (data[p] == ((TP_OP_PFIX << 4) | 0)) {
            /* At an instruction boundary Oreg is 0: a no-op */
            tp_lr_add(lr, TP_LR_NOP, p, 1);
            p++;
            continue;
        }
        len = tp_decode_instr_at(data, end, p, &op, &opd);
        if (len == 0)
            goto bad;
        for (k = p + 1; k < p + (uint32_t)len; k++)
            if (lr->rel_at[k] != -1)
                goto bad;
        it = tp_lr_add(lr, TP_LR_FIXED, p, (uint32_t)len);
        if (op == TP_OP_J || op == TP_OP_CJ) {
            int64_t t = (int64_t)p + len + opd;
            if (t < start || t > end)
                goto bad;
            it->kind = TP_LR_BRANCH;
            it->opcode = (uint8_t)op;
            it->target = (uint32_t)t;
        } else if (op == TP_OP_OPR && opd == TP_OPR_LDPI) {
            /* `ldc d; ldpi` (an indirect call's return address): the d
             * bytes after it must keep their length. */
            if (prev_ldc < 0 || p + len + (uint32_t)prev_ldc > end)
                goto bad;
            spans = tcc_realloc(spans, sizeof(*spans) * (size_t)(nspans + 1));
            spans[nspans].from = p + len;
            spans[nspans].to = p + len + (uint32_t)prev_ldc;
            nspans++;
        } else if (op == TP_OP_OPR
                   && (opd == TP_OPR_LEND || opd == TP_OPR_STARTP)) {
            goto bad;           /* also IP-relative, with unknown reach */
        }
        prev_ldc = op == TP_OP_LDC ? opd : -1;
        p += (uint32_t)len;
    }

    /* Branch targets, ldpi spans and symbols must sit on item starts;
     * nothing may refer into the body with an addend. */
    for (i = first; i < lr->n; i++) {
        struct tp_lr_item *it = &lr->it[i];
        if (it->kind == TP_LR_BRANCH && it->opcode != TP_OP_CALL
            && it->target != end
            && tp_lr_find(lr->it, first, lr->n, it->target) < 0)
            goto bad;
    }
    for (k = start + 1; k < end; k++) {
        if (lr->ref_in[k])
            goto bad;
        if (lr->sym_at[k] && tp_lr_find(lr->it, first, lr->n, k) < 0)
            goto bad;
    }
    for (i = 0; i < nspans; i++) {
        int a = tp_lr_find(lr->it, first, lr->n, spans[i].from);
        int b = spans[i].to == end ? lr->n
                : tp_lr_find(lr->it, first, lr->n, spans[i].to);
        if (a < 0 || b < 0)
            goto bad;
        for (; a < b; a++) {
            if (lr->it[a].kind == TP_LR_BRANCH)
                goto bad;
            lr->it[a].kind = TP_LR_FIXED;   /* keep the bytes as they are */
        }
    }
    tcc_free(spans);
    return 1;

bad:
    tcc_free(spans);
    lr->n = first;
    return 0;
}

/* Bytes between functions: padding between objects (0 or pfix 0, with
 * nothing defined or relocated in it) goes; anything else is a block. */
static void tp_lr_gap(struct tp_lr *lr, uint32_t a, uint32_t b)
{
    uint32_t k;

    for (k = a; k < b; k++)
        if ((lr->data[k] != 0 && lr->data[k] != ((TP_OP_PFIX << 4) | 0))
            || lr->rel_at[k] != -1 || lr->sym_at[k] || lr->ref_in[k])
            break;
    if (k < b) {
        tp_lr_add(lr, TP_LR_BLOCK, a, b - a);
        return;
    }
    for (k = a; k < b; k++)
        tp_lr_add(lr, TP_LR_NOP, k, 1);
}

/* Place the items at their current lengths; a block keeps its offset
 * modulo `align`.  Returns the new size of the section. */
static uint32_t tp_lr_layout(struct tp_lr *lr, uint32_t align)
{
    uint32_t off = 0;
    int i;

    for (i = 0; i < lr->n; i++) {
        struct tp_lr_item *it = &lr->it[i];
        if (it->kind == TP_LR_BLOCK)
            off += (it->old_off - off) & (align - 1);
        it->new_off = off;
        off += it->new_len;
    }
    return off;
}

/* The new offset of old offset t under the current layout, or -1. */
static int64_t tp_lr_newpos(const struct tp_lr *lr, uint32_t t, uint32_t total)
{
    const struct tp_lr_item *it = lr->it;
    int lo = 0, hi = lr->n - 1, res = -1;

    if (t == lr->size)
        return total;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (it[mid].old_off <= t) {
            res = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    if (res < 0)
        return -1;
    if (t == it[res].old_off)
        return it[res].new_off;
    if ((it[res].kind == TP_LR_FIXED || it[res].kind == TP_LR_BLOCK)
        && t < it[res].old_off + it[res].old_len)
        return it[res].new_off + (t - it[res].old_off);
    return -1;
}

/* Displacement of branch item i to its target, from the end of the item. */
static int64_t tp_lr_disp(const struct tp_lr *lr, int i, uint32_t total)
{
    const struct tp_lr_item *it = &lr->it[i];

    return tp_lr_newpos(lr, it->target, total)
           - ((int64_t)it->new_off + it->new_len);
}

static int tp_lr_range_cmp(const void *a, const void *b)
{
    const uint32_t *ra = a, *rb = b;

    if (ra[0] != rb[0])
        return ra[0] < rb[0] ? -1 : 1;
    if (ra[1] != rb[1])
        return ra[1] < rb[1] ? -1 : 1;
    return 0;
}

/* Relax one executable section.  Returns the bytes saved. */
static uint32_t tp_lr_section(TCCState *s1, int seci, const uint8_t *marked,
                              int *out_fns, int *out_kept)
{
    Section *s = s1->sections[seci];
    Section *sr = s->reloc;
    ElfW(Sym) *syms = (ElfW(Sym) *)symtab_section->data;
    int nsym = (int)(symtab_section->data_offset / sizeof(ElfW(Sym)));
    ElfW_Rel *rel = sr ? (ElfW_Rel *)sr->data : NULL;
    int nrel = sr ? (int)(sr->data_offset / sizeof(ElfW_Rel)) : 0;
    uint32_t size = (uint32_t)s->data_offset, align, total = 0, k;
    struct tp_lr lr;
    uint32_t (*fns)[2];
    int nfn = 0, i, j, pass, ok = 0;
    int *rel_at;
    int32_t *call_to, *map = NULL;
    uint8_t *sym_at, *ref_in, *out = NULL;

    memset(&lr, 0, sizeof(lr));
    align = s->sh_addralign > 1 ? (uint32_t)s->sh_addralign : 1;
    rel_at = tcc_malloc(sizeof(int) * (size_t)(size + 1));
    call_to = tcc_malloc(sizeof(int32_t) * (size_t)(nrel + 1));
    sym_at = tcc_mallocz((size_t)size + 1);
    ref_in = tcc_mallocz((size_t)size + 1);
    for (k = 0; k <= size; k++)
        rel_at[k] = -1;

    /* Relocations here.  A call to a function defined in this section is a
     * branch to it: tp_apply_relocations repairs a reference by name, but
     * only ever away from an undefined or duplicated symbol. */
    for (i = 0; i < nrel; i++) {
        uint32_t o = (uint32_t)rel[i].r_offset;
        int si = (int)ELFW(R_SYM)(rel[i].r_info);
        call_to[i] = -1;
        if (o >= size)
            continue;
        rel_at[o] = rel_at[o] == -1 ? i : -2;
        if (ELFW(R_TYPE)(rel[i].r_info) == R_TRANSPUTER_IPREL32
            && si > 0 && si < nsym && syms[si].st_shndx == seci
            && syms[si].st_value < size)
            call_to[i] = (int32_t)syms[si].st_value;
    }

    /* Symbols here, and data words pointing at sym+addend in here */
    for (i = 1; i < nsym; i++)
        if (syms[i].st_shndx == seci && syms[i].st_value <= size)
            sym_at[syms[i].st_value] = 1;
    for (j = 1; j < s1->nb_sections; j++) {
        Section *ts = s1->sections[j];
        ElfW_Rel *r, *r_end;
        if (!ts->reloc || !ts->data || !(ts->sh_flags & SHF_ALLOC))
            continue;
        r_end = (ElfW_Rel *)(ts->reloc->data + ts->reloc->data_offset);
        for (r = (ElfW_Rel *)ts->reloc->data; r < r_end; r++) {
            int si = (int)ELFW(R_SYM)(r->r_info);
            uint32_t a;
            if (ELFW(R_TYPE)(r->r_info) != R_TRANSPUTER_32 || si <= 0
                || si >= nsym || syms[si].st_shndx != seci
                || r->r_offset + 4 > ts->data_offset)
                continue;
            a = read32le(ts->data + r->r_offset);
            if (a && syms[si].st_value + a < size)
                ref_in[syms[si].st_value + a] = 1;
        }
    }

    /* The functions that may be taken apart, in address order */
    fns = tcc_malloc(sizeof(*fns) * (size_t)(nsym + 1));
    for (i = 1; i < nsym; i++) {
        ElfW(Sym) *sym = &syms[i];
        if (marked[i] && sym->st_shndx == seci
            && ELFW(ST_TYPE)(sym->st_info) == STT_FUNC && sym->st_size
            && sym->st_value + sym->st_size <= size) {
            fns[nfn][0] = (uint32_t)sym->st_value;
            fns[nfn][1] = (uint32_t)(sym->st_value + sym->st_size);
            nfn++;
        }
    }
    qsort(fns, (size_t)nfn, sizeof(*fns), tp_lr_range_cmp);
    for (i = 0, j = 0; i < nfn; i++) {
        if (j && fns[i][0] == fns[j - 1][0] && fns[i][1] == fns[j - 1][1])
            continue;                       /* an alias */
        if (j && fns[i][0] < fns[j - 1][1]) {
            fns[j - 1][1] = fns[j - 1][0];  /* overlap: neither */
            continue;
        }
        fns[j][0] = fns[i][0];
        fns[j][1] = fns[i][1];
        j++;
    }
    nfn = j;

    /* Items, in address order */
    lr.data = s->data;
    lr.size = size;
    lr.rel_at = rel_at;
    lr.call_to = call_to;
    lr.sym_at = sym_at;
    lr.ref_in = ref_in;
    k = 0;
    for (i = 0; i < nfn; i++) {
        if (fns[i][1] == fns[i][0])
            continue;
        if (fns[i][0] > k)
            tp_lr_gap(&lr, k, fns[i][0]);
        if (tp_lr_function(&lr, fns[i][0], fns[i][1])) {
            (*out_fns)++;
        } else {
            tp_lr_add(&lr, TP_LR_BLOCK, fns[i][0], fns[i][1] - fns[i][0]);
            (*out_kept)++;
        }
        k = fns[i][1];
    }
    if (k < size)
        tp_lr_gap(&lr, k, size);

    /* A call whose target is inside a re-encoded instruction stays put */
    total = tp_lr_layout(&lr, align);
    for (i = 0; i < lr.n; i++)
        if (lr.it[i].kind == TP_LR_BRANCH
            && tp_lr_newpos(&lr, lr.it[i].target, total) < 0)
            lr.it[i].kind = TP_LR_FIXED;

    /* Shrink while anything does.  Then, since the padding in front of a
     * block can grow as the code before it shrinks, widen whatever no
     * longer fits until nothing needs to. */
    for (pass = 0; pass < 32; pass++) {
        int changed = 0;
        total = tp_lr_layout(&lr, align);
        for (i = 0; i < lr.n; i++) {
            struct tp_lr_item *it = &lr.it[i];
            uint32_t want = it->new_len;
            if (it->kind == TP_LR_NOP) {
                want = 0;
            } else if (it->kind == TP_LR_BRANCH) {
                int64_t t = tp_lr_newpos(&lr, it->target, total);
                while (want > 1
                       && tp_encode_len(it->opcode,
                              (int)(t - ((int64_t)it->new_off + want - 1)))
                          <= (int)want - 1)
                    want--;
            }
            if (want < it->new_len) {
                it->new_len = want;
                changed = 1;
            }
        }
        if (!changed)
            break;
    }
    for (pass = 0; pass < 64; pass++) {
        int changed = 0;
        total = tp_lr_layout(&lr, align);
        for (i = 0; i < lr.n; i++) {
            struct tp_lr_item *it = &lr.it[i];
            int need;
            if (it->kind != TP_LR_BRANCH)
                continue;
            need = tp_encode_len(it->opcode, (int)tp_lr_disp(&lr, i, total));
            if (need > (int)it->new_len) {
                it->new_len = (uint32_t)need;
                changed = 1;
            }
        }
        if (!changed)
            break;
    }
    if (pass == 64 || total >= size)
        goto done;

    /* Emit, and map every old offset something can refer to */
    out = tcc_malloc(total);
    memset(out, (TP_OP_PFIX << 4) | 0, total);
    map = tcc_malloc(sizeof(int32_t) * (size_t)(size + 1));
    for (k = 0; k <= size; k++)
        map[k] = -1;
    for (i = 0; i < lr.n; i++) {
        struct tp_lr_item *it = &lr.it[i];
        map[it->old_off] = (int32_t)it->new_off;
        if (it->kind == TP_LR_BRANCH) {
            uint8_t buf[16];
            int len = tp_encode_instr(buf, it->opcode,
                                      (int)tp_lr_disp(&lr, i, total));
            if (len > (int)it->new_len)
                goto done;
            memcpy(out + it->new_off + it->new_len - len, buf, len);
        } else if (it->kind != TP_LR_NOP) {
            memcpy(out + it->new_off, s->data + it->old_off, it->old_len);
            for (k = 1; k < it->old_len; k++)
                map[it->old_off + k] = (int32_t)(it->new_off + k);
        }
    }
    map[size] = (int32_t)total;
    for (i = 0; i < nrel; i++)
        if (rel[i].r_offset < size && map[rel[i].r_offset] < 0)
            goto done;
    for (i = 1; i < nsym; i++)
        if (syms[i].st_shndx == seci
            && (syms[i].st_value > size || map[syms[i].st_value] < 0))
            goto done;

    /* Commit */
    for (i = 0; i < nrel; i++)
        if (rel[i].r_offset < size)
            rel[i].r_offset = (uint32_t)map[rel[i].r_offset];
    for (i = 1; i < nsym; i++) {
        ElfW(Sym) *sym = &syms[i];
        uint32_t v, e;
        if (sym->st_shndx != seci)
            continue;
        v = (uint32_t)sym->st_value;
        e = v + (uint32_t)sym->st_size;
        if (sym->st_size && e <= size && map[e] >= 0)
            sym->st_size = (uint32_t)(map[e] - map[v]);
        sym->st_value = (uint32_t)map[v];
    }
    memcpy(s->data, out, total);
    s->data_offset = total;
    s->sh_size = total;
    ok = 1;

done:
    tcc_free(out);
    tcc_free(map);
    tcc_free(lr.it);
    tcc_free(fns);
    tcc_free(ref_in);
    tcc_free(sym_at);
    tcc_free(call_to);
    tcc_free(rel_at);
    return ok ? size - total : 0;
}

static void tp_link_relax(TCCState *s1, const uint8_t *fixed, int link_debug)
{
    ElfW(Sym) *syms = (ElfW(Sym) *)symtab_section->data;
    int nsym = (int)(symtab_section->data_offset / sizeof(ElfW(Sym)));
    const char *e = tp_env("T4_LINK_RELAX");
    uint8_t *marked, *marked_sections;
    Section *list = NULL;
    uint32_t saved = 0;
    int i, fns = 0, kept = 0;

    if (e && e[0] == '0' && e[1] == '\0')
        return;
    for (i = 1; i < s1->nb_sections; i++)
        if (!strcmp(s1->sections[i]->name, ".note.tprelax"))
            list = s1->sections[i];
    if (!list || !list->reloc)
        return;

    marked = tcc_mallocz((size_t)nsym + 1);
    marked_sections = tcc_mallocz(s1->nb_sections);
    for (i = 0; i < (int)(list->reloc->data_offset / sizeof(ElfW_Rel)); i++) {
        int si = (int)ELFW(R_SYM)(((ElfW_Rel *)list->reloc->data)[i].r_info);
        if (si > 0 && si < nsym) {
            ElfW(Sym) *sym = &syms[si];
            marked[si] = 1;
            if (ELFW(ST_TYPE)(sym->st_info) == STT_FUNC && sym->st_size
                && sym->st_shndx > 0 && sym->st_shndx < s1->nb_sections)
                marked_sections[sym->st_shndx] = 1;
        }
    }
    for (i = 1; i < s1->nb_sections; i++) {
        Section *s = s1->sections[i];
        uint32_t before = (uint32_t)s->data_offset, n;
        if ((s->sh_flags & (SHF_ALLOC | SHF_EXECINSTR))
            != (SHF_ALLOC | SHF_EXECINSTR)
            || s->sh_type == SHT_NOBITS || !s->data || !before
            || fixed[i] || !marked_sections[i] || !strcmp(s->name, ".text.start"))
            continue;
        /* Unmarked sections can only become one unchanged block. Avoid
         * their byte-sized maps and scans of all incoming relocations. */
        n = tp_lr_section(s1, i, marked, &fns, &kept);
        saved += n;
        if (n && (link_debug || tp_env_enabled("T4_RELAX_STATS")))
            fprintf(stderr, "  relax %-16s %u -> %u bytes\n", s->name,
                    before, before - n);
    }
    if (link_debug || tp_env_enabled("T4_RELAX_STATS"))
        fprintf(stderr, "transputer_simple_link: relaxed %d functions "
                "(%d kept whole), %u bytes\n", fns, kept, saved);
    tcc_free(marked);
    tcc_free(marked_sections);
}

static void tp_optimize_code_sections(TCCState *s1, const uint8_t *fixed, int link_debug)
{
    int i;

    if (link_debug)
        fprintf(stderr, "transputer_simple_link: Optimizing code sections...\n");
    for (i = 1; i < s1->nb_sections; i++) {
        Section *s = s1->sections[i];
        if ((s->sh_flags & (SHF_ALLOC | SHF_EXECINSTR)) == (SHF_ALLOC | SHF_EXECINSTR) &&
            !fixed[i] && s->data_offset > 0 && s->data != NULL) {
            if (link_debug) {
                fprintf(stderr, " Optimizing section: %s (size=%lu bytes)\n",
                        s->name, (unsigned long)s->data_offset);
            }
            transputer_peephole_optimize(s1, s);
        }
    }
    if (link_debug)
        fprintf(stderr, "transputer_simple_link: Optimization complete\n");
}

/* Linux compares scheduler-class pointers directly, so these input sections
 * must be contiguous and in strict priority order.  A normal GNU ld script
 * provides SCHED_DATA for this; the Transputer simple linker performs its own
 * layout and therefore has to preserve the same contract explicitly. */
static int tp_sched_class_rank(const char *name)
{
    static const char *const ordered[] = {
        "__stop_sched_class",
        "__dl_sched_class",
        "__rt_sched_class",
        "__fair_sched_class",
        "__ext_sched_class",
        "__idle_sched_class",
    };
    int i;

    for (i = 0; i < (int)(sizeof(ordered) / sizeof(ordered[0])); i++) {
        if (strcmp(name, ordered[i]) == 0)
            return i;
    }
    return -1;
}

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

/*
 * Define Linux initcall boundaries before applying relocations.  The
 * Transputer linker uses a real, contiguous __initcall_start[] table, but the
 * input linker script cannot assign symbols to offsets within it.  Turning
 * the boundary symbols into SHN_ABS symbols here lets the ordinary relocation
 * pass resolve every pointer and LDC reference in one place.
 */
static int tp_define_initcall_boundaries(TCCState *s1, Section *symtab_sec,
                                         InitcallLayout *layout,
                                         int link_debug)
{
    static const char *const level_names[8] = {
        "__initcall0_start", "__initcall1_start",
        "__initcall2_start", "__initcall3_start",
        "__initcall4_start", "__initcall5_start",
        "__initcall6_start", "__initcall7_start",
    };
    /* First ordinal belonging to each Linux initcall level.  The sync
     * variants share their base level; rootfs is between fs and device. */
    static const int level_ordinals[8] = { 1, 2, 4, 6, 8, 10, 13, 15 };
    int counts[17] = { 0 };
    ElfW(Sym) *table_sym;
    ElfW(Sym) *sym;
    const char *strtab;
    int i, nsyms;
    int total = 0;

    memset(layout, 0, sizeof(*layout));
    table_sym = find_sym_by_name(symtab_sec, "__initcall_start");
    if (!table_sym)
        return 0;
    if (table_sym->st_shndx <= 0 || table_sym->st_shndx >= s1->nb_sections) {
        fprintf(stderr, "ERROR: __initcall_start has invalid section %d\n",
                table_sym->st_shndx);
        return -1;
    }
    layout->table_addr = (uint32_t)s1->sections[table_sym->st_shndx]->sh_addr +
                         (uint32_t)table_sym->st_value;
    /* The kernel sizes the table; an object without a symbol size predates
     * that and had the original fixed 128 slots. */
    layout->capacity = table_sym->st_size ? (int)(table_sym->st_size / 4) : 128;

    nsyms = symtab_sec->data_offset / sizeof(ElfW(Sym));
    strtab = (const char *)symtab_sec->link->data;
    for (i = 0; i < nsyms; i++) {
        const char *name;
        int ordinal;

        sym = &((ElfW(Sym) *)symtab_sec->data)[i];
        name = strtab + sym->st_name;
        if (strncmp(name, "__initcall_", 11) != 0 ||
            strcmp(name, "__initcall_start") == 0 ||
            strcmp(name, "__initcall_end") == 0 ||
            strcmp(name, "__early_initcall_end") == 0)
            continue;
        ordinal = tp_initcall_level_ordinal(name + 11);
        if (ordinal < 0)
            continue;
        if (sym->st_shndx <= 0 || sym->st_shndx >= s1->nb_sections) {
            fprintf(stderr, "ERROR: initcall entry %s has invalid section %d\n",
                    name, sym->st_shndx);
            return -1;
        }
        if (++total > layout->capacity) {
            fprintf(stderr, "ERROR: initcall table overflow (%d entries, capacity %d)\n",
                    total, layout->capacity);
            return -1;
        }
        counts[ordinal]++;
    }

    layout->entry_count = total;
    layout->early_end_addr = layout->table_addr + counts[0] * 4;
    layout->end_addr = layout->table_addr + total * 4;
    for (i = 0; i < 8; i++) {
        int ord;
        int before = 0;
        for (ord = 0; ord < level_ordinals[i]; ord++)
            before += counts[ord];
        layout->level_addr[i] = layout->table_addr + before * 4;
    }

    tp_define_abs_symbol(symtab_sec, "__early_initcall_end",
                         layout->early_end_addr);
    for (i = 0; i < 8; i++)
        tp_define_abs_symbol(symtab_sec, level_names[i], layout->level_addr[i]);
    tp_define_abs_symbol(symtab_sec, "__initcall_end", layout->end_addr);

    if (link_debug) {
        fprintf(stderr,
                "[INITCALL] pre-reloc table=0x%08x entries=%d early_end=0x%08x end=0x%08x\n",
                layout->table_addr, total, layout->early_end_addr,
                layout->end_addr);
        for (i = 0; i < 8; i++)
            fprintf(stderr, "[INITCALL]   %s=0x%08x\n",
                    level_names[i], layout->level_addr[i]);
    }
    return 0;
}

static int tp_define_sched_class_boundaries(TCCState *s1,
                                            Section *symtab_sec,
                                            int link_debug)
{
    uint32_t highest = 0;
    uint32_t lowest = 0;
    int rank;
    int i;

    for (rank = 0; rank < 6; rank++) {
        for (i = 1; i < s1->nb_sections; i++) {
            Section *s = s1->sections[i];
            uint32_t end;

            if (tp_sched_class_rank(s->name) != rank ||
                !(s->sh_flags & SHF_ALLOC))
                continue;
            if (!highest)
                highest = (uint32_t)s->sh_addr;
            end = (uint32_t)s->sh_addr + (uint32_t)s->data_offset;
            if (lowest && (uint32_t)s->sh_addr != lowest) {
                fprintf(stderr, "ERROR: scheduler class sections are not contiguous\n");
                return -1;
            }
            lowest = end;
        }
    }

    if (!highest)
        return 0;
    tp_define_abs_symbol(symtab_sec, "__sched_class_highest", highest);
    tp_define_abs_symbol(symtab_sec, "__sched_class_lowest", lowest);
    if (link_debug) {
        fprintf(stderr, " Scheduler classes: highest=0x%08x lowest=0x%08x\n",
                highest, lowest);
    }
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
                         : s1->tp_t414 ? 0x80000048u : 0x80000070u;
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
