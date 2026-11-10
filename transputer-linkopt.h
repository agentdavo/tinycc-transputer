/*
 *  Transputer linker: the passes that shrink code before layout.
 *
 *  Code, not declarations: transputer-link.c includes this once.  The link
 *  driver calls three functions of it, in this order, on the merged input
 *  sections, before addresses are assigned and relocations applied:
 *
 *    tp_optimize_code_sections()   peephole: no-op sequences to pfix 0
 *    tp_icf_fold()                 identical code folding
 *    tp_link_relax()               shortest encodings for calls and jumps
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
    const char *e = tp_knob(TPK_T4_ICF);

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
    if (link_debug || tp_knob_on(TPK_T4_ICF_STATS))
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
    if (total && (link_debug || tp_knob_on(TPK_T4_ICF_STATS)))
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
    const char *e = tp_knob(TPK_T4_LINK_RELAX);
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
        if (n && (link_debug || tp_knob_on(TPK_T4_RELAX_STATS)))
            fprintf(stderr, "  relax %-16s %u -> %u bytes\n", s->name,
                    before, before - n);
    }
    if (link_debug || tp_knob_on(TPK_T4_RELAX_STATS))
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
