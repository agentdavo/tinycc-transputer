/*
 *  Transputer linker: what it knows about a Linux kernel image.
 *
 *  Code, not declarations: transputer-link.c includes this once.  A kernel
 *  is linked without a linker script, so the things a script would have
 *  done are done here, by symbol and section name; a link that has none of
 *  those names is not affected.
 *
 *    boot words        __bss_*_load_point and __init_stack_load_point, the
 *                      constants head.S loads before any relocation exists
 *    initcalls         the sorted __initcall_start[] table and its bounds
 *    scheduler classes contiguous, in priority order, with their bounds
 *    surrogates        __linker_* and bootmem function-pointer symbols
 */

static void tp_define_abs_symbol(Section *symtab_sec, const char *name,
                                 uint32_t value);

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
