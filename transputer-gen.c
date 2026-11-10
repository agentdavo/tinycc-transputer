/*
 * Transputer code generator for TCC
 *
 * Based on arm-gen.c and x86_64-gen.c patterns
 * INMOS Transputer T414/T800 architecture
 */

#ifdef TARGET_DEFS_ONLY

#define NB_REGS 15
#define CONFIG_TCC_ASM
extern int kernel_mode;
#define NB_ASM_REGS 14

/* Generic register classes */
#define RC_INT     0x0001
#define RC_FLOAT   0x0002

/* Specific registers (class bits) */
#define RC_IST0    0x0004 /* A */
#define RC_IST1    0x0008 /* B */
#define RC_IST2    0x0010 /* C */
#define RC_FST0    0x0020 /* FA */
#define RC_FST1    0x0040 /* FB */
#define RC_FST2    0x0080 /* FC */
#define RC_WREG    0x0100
#define RC_IRET    0x0200 /* integer return reg class */
#define RC_IRE2    0x0400 /* 2nd integer return reg class */

/* Register numbers
 *
 * IMPORTANT: On Transputer, there are no general-purpose registers in the
 * traditional sense. The hardware has only a 3-deep evaluation stack (A, B, C).
 * TCC's register abstraction is mapped to workspace slots:
 *
 *   TREG_N -> Workspace slot N (accessed via LDL N / STL N)
 *
 * This means when code does Tp_STL(r) with a register index r, it stores to
 * workspace slot r. This mapping is implicit throughout the backend.
 *
 * The evaluation stack (TREG_A/B/C) is handled specially - values are pushed/
 * popped rather than stored to fixed slots.
 */
#define TREG_A     0  /* Hardware AReg (top of eval stack) */
#define TREG_B     1  /* Hardware BReg */
#define TREG_C     2  /* Hardware CReg (bottom of 3-deep stack) */
#define TREG_FA    3  /* Floating point register (workspace slot 3) */
#define TREG_FB    4  /* Floating point register (workspace slot 4) */
#define TREG_FC    5  /* Floating point register (workspace slot 5) */
#define TREG_W0    6  /* General workspace slot 6 */
#define TREG_R0    7  /* Return value slot (workspace slot 7) */
#define TREG_R1    8  /* Secondary return slot (workspace slot 8) */

/* Return register numbers - values stored here, then loaded to AReg for RET */
#define REG_IRET   TREG_R0  /* Integer return: workspace slot 7 */
#define REG_IRE2   TREG_R1  /* Secondary return: workspace slot 8 */
#define REG_FRET   TREG_FA  /* Float return: workspace slot 3 */

#define RC_FRET    RC_FST0
/* A double is two words, REG_FRET and REG_FRET+1 (R2_RET).  Without this,
 * RC2_TYPE() put the high word of a double pinned to RC_FRET - a ?: whose
 * arms must meet in one place, a return value - in an integer register, while
 * gv() loads constants and lvalues as the slot pair r/r+1: one arm of
 * `c ? (double)i : -128.0` left its high word where the other never looked. */
#define RC_FRE2    RC_FST1

#define INVERT_FUNC_PARAMS
#define PTR_SIZE 4
#define LDOUBLE_SIZE  8
#define LDOUBLE_ALIGN 8
#define MAX_ALIGN     4

/* long double is REAL64, as on ARM: have the front end type it as
   VT_DOUBLE|VT_LONG and keep its constants in c.d, which is what the
   back end reads (a VT_LDOUBLE constant lives in the host's c.ld). */
#define TCC_USING_DOUBLE_FOR_LDOUBLE 1

#define TCC_TARGET_NATIVE_STRUCT_COPY
ST_FUNC void gen_struct_copy(int size);

// #define TCC_TARGET_BUILTIN_MEMSET  // Disabled - implementation is broken

#define TCC_TARGET_TRANSPUTER 1
ST_FUNC void g(int c);
ST_FUNC void gen_le16(int v);
ST_FUNC void gen_le32(int c);

#undef CONFIG_TCC_BCHECK

#else /* !TARGET_DEFS_ONLY */

#define USING_GLOBALS
#include "tcc.h"
#include "transputer-opcodes.h"
#include "transputer-enc.h"  /* Unified prefix encoding */
#include "transputer-knobs.h"

/* Forward declarations */
static void transputer_func_reset(void);
static void tp_gen_tvx_unary(int opr);
static void tp_gen_tvx_binary(int opr);
static void tp_gen_vec2(int opr);
static void tp_gen_vec3(int opr);
static void emit_instr(int opc, int v);
static void tp_push_sv(SValue *sv);
static void tp_push_float_sv(SValue *sv);
static void tp_store_fpu_result_to_slot(int slot, int bt);
static int tp_soft_float(void);
static int tp_t414(void);
static int tp_tvx(void);
static int tp_try_inline_local_zero_memset(int nb_args);
static int tp_try_inline_memcpy(int nb_args);
static int tp_push_sv_is_pure(const SValue *sv);

/* TP_NO_CALLOPT=1 restores the pre-2026-08-05 call sequence: the
 * unconditional four-slot return capture and the spill/reload argument
 * staging.  Purely a bisection handle - it makes the two call-path changes
 * switchable without rebuilding a different tree. */
static int tp_no_callopt(void)
{
    static int v = -1;
    if (v < 0)
        v = tp_knob(TPK_TP_NO_CALLOPT) ? 1 : 0;
    return v;
}
static int tp_case_cmp(uint64_t a, uint64_t b, int is_unsigned);
static uint64_t tp_switch_total_selectors(struct switch_t *sw, int is_unsigned);
static void tp_switch_emit_instr(int opcode, int operand);
static void tp_switch_emit_fixed_slot(int opcode, int operand, int slot_size);
static void tp_switch_patch_fixed_slot(int offset, int opcode, int operand, int slot_size);
static void tp_builtin_return_void(void);
static void tp_gen_flag_result(int opr);
static void tp_builtin_word_result(void);
static void tp_emit_1op(int opcode);
static void tp_emit_2op(int opcode, int b_idx, int a_idx);
static void tp_emit_3op(int opcode, int c_idx, int b_idx, int a_idx);
struct tp_call_words {
    SValue *words;
    int count;
    int cap;
};
static void tp_gen_tvx_dot4(int opr);
static void tp_call_words_reserve(struct tp_call_words *cw, int extra_words);
static void tp_expand_struct_call_words(struct tp_call_words *cw, SValue *arg, int arg_index, int callarg_debug);
static void tp_expand_scalar_call_words(struct tp_call_words *cw, SValue *arg, int arg_index, int callarg_debug);
static void tp_expand_call_words(struct tp_call_words *cw, int nb_args, int callarg_debug);
static int tp_stage_call_words(struct tp_call_words *cw, int nb_args,
                               int callarg_debug, int *abc_slot);
static void tp_emit_call_target(int call_word_count, int indirect_fn_slot,
                                int abc_slot, int callarg_debug);

/* Forward declarations */
static void tp_pool_reset(void);
static void tp_ldc_pooled(int v);
static void Tp_STL(int v);
static void Tp_LDL(int v);
static void abcreg_push(int count);
static int func_start_ind = -1;  /* where the function being compiled begins */

/* DUP optimization: track what's in AReg after tp_push_sv().
 * If next push wants the same value, emit DUP instead of reloading.
 * tp_areg_tag encodes what's in AReg:
 *   0 = unknown/invalidated
 *   positive = workspace slot loaded via LDL (slot number + 1, to distinguish from 0)
 *   TP_AREG_TAG_LOCAL_BASE + offset = local lval loaded via emit_local_op LDL
 * Invalidated by any instruction that modifies AReg other than tp_push_sv. */
#define TP_AREG_TAG_NONE          0
#define TP_AREG_TAG_REG_BASE      1      /* tag = slot + TP_AREG_TAG_REG_BASE */
#define TP_AREG_TAG_LOCAL_BASE    0x10000 /* tag = offset + TP_AREG_TAG_LOCAL_BASE */
static int tp_areg_tag = TP_AREG_TAG_NONE;

/* ADC is the *error-checking* add: it sets the transputer Error flag on
 * signed overflow (the emulator's `-se` then stops the run).  SUM is the
 * unchecked one.  Folding `x + const` into a single ADC is therefore only
 * sound when a signed overflow cannot legitimately occur:
 *
 *   - signed integers: overflow is undefined behaviour in C, so trapping is
 *     permitted and the one-instruction form is kept;
 *   - pointers: transputer addresses run from MinInt (0x80000000) upwards,
 *     i.e. they are *negative* as signed values, so adding a small positive
 *     displacement moves toward zero and can never overflow.  Kept, which
 *     preserves the struct-field-addressing win that motivated ADC folding;
 *   - unsigned integers: wrapping is well defined and completely routine
 *     (hashes, checksums, PRNGs, ring buffers), so ADC must not be used.
 *     `LDC k; SUM` costs one extra instruction and does not trap.
 *
 * Repro for the bug this prevents:
 *     volatile unsigned g = 0x7FFFFFF0u;  unsigned x = g;  x = x + 12345u;
 */
static int tp_adc_would_trap(void)
{
    int t = vtop[-1].type.t;
    int bt = t & VT_BTYPE;

    if (bt == VT_PTR)
        return 0;                       /* addresses never overflow upward */
    return (t & VT_UNSIGNED) != 0;
}

/* TP_NO_LDNLP=1 keeps `adc` for pointer displacements, for A/B measurement. */
static int tp_no_ldnlp(void)
{
    static int v = -1;
    if (v < 0)
        v = tp_knob(TPK_TP_NO_LDNLP) ? 1 : 0;
    return v;
}

/* Pointer + constant byte displacement: how many words, or 0 if `ldnlp` does
 * not apply.
 *
 * `ldnlp n` is AReg + 4n - no memory access, no error check - and the T9000
 * grouper charges it to stage 2, the address-calculation stage, which sits at
 * 0.5% occupancy over a kernel corpus.  `adc n` computes the same address on
 * stage 4, the single-issue ALU stage that bounds the whole machine.
 *
 * A variable add already runs on stage 2 as `bsub`, but the constant form was
 * left on `adc` because `ldc n; bsub` is two instructions.  That overlooked
 * `ldnlp`, which is one instruction and needs no `ldc` - so the constant form
 * moves too, and the encoding is never longer: `ldnlp n` carries n where
 * `adc 4n` carries 4n, two nibbles fewer for every displacement above 15
 * words.
 *
 * Only for pointers.  On a signed integer `adc` is the error-*checking* add and
 * dropping the check would change behaviour under `t4 -se`; addresses cannot
 * meaningfully overflow, which is why tp_adc_would_trap already exempts them. */
static int tp_ptr_add_is_addr;

static int tp_addr_disp_words(int bytes)
{
    if (tp_no_ldnlp())
        return 0;
    if (!tp_ptr_add_is_addr && (vtop[-1].type.t & VT_BTYPE) != VT_PTR)
        return 0;
    if (bytes == 0 || (bytes & 3))
        return 0;
    return bytes >> 2;
}

/* Set around the final `+` of a pointer computation (tp_ptradd_addr).
 *
 * `bsub` and `sum` compute the same thing (AReg + BReg, commutative), but the
 * T9000 grouper charges `bsub` to stage 2 (address calculations, 2 per cycle,
 * measurably spare) and `sum` to stage 4 (ALU, 1 per cycle, the narrowest
 * resource in the machine).  This flag first moved pointer adds to `bsub`;
 * every unchecked add now uses it (Tp_SUM), since it is also a byte shorter.
 *
 * What the flag still decides is the constant form: a pointer plus a whole
 * number of words becomes `ldnlp`, also stage 2 (tp_addr_disp_words), where an
 * integer keeps the error-checking `adc`. */

static void tp_areg_invalidate(void) { tp_areg_tag = TP_AREG_TAG_NONE; }

#include "transputer-hist.h"

static int tp_fpu_take_resident(SValue *sv, int is_db);

/* What the environment asks to see of a relocation as tccgen.c's greloca()
 * emits it: T4_IPREL_NONFUNC_WARN, T4_IPREL_DIAG, T4_RELOC_LOG (with
 * T4_RELOC_SYM and T4_RELOC_TYPE) and T4_SYM_TRACE. */
ST_FUNC void tp_greloca_trace(Section *s, Sym *sym, unsigned long offset,
                              int type)
{
    static int reloc_type_filter = -2;
    const char *reloc_sym_filter = tp_knob(TPK_T4_RELOC_SYM);

    if (reloc_type_filter == -2) {
        const char *env = tp_knob(TPK_T4_RELOC_TYPE);
        reloc_type_filter = env && *env ? (int)strtol(env, NULL, 0) : -1;
    }

    /* Bug detection: IPREL32 should only be used for function calls */
    if (type == R_TRANSPUTER_IPREL32 && sym) {
        int bt = sym->type.t & VT_BTYPE;
        /* IPREL32 is only valid for function calls (bt == VT_FUNC).
         * If we're emitting IPREL32 for a non-function symbol, that's a bug! */
        if (bt != VT_FUNC && tp_knob_on(TPK_T4_IPREL_NONFUNC_WARN)) {
            const char *fname = file ? file->filename : "<unknown>";
            const char *func = funcname ? funcname : "<no func>";
            fprintf(stderr,
                    "BUG_IPREL32_NONFUNCTION: sym=%s bt=0x%x func=%s file=%s line=%d section=%s offset=0x%lx\n",
                    get_tok_str(sym->v, NULL), bt, func, fname,
                    file ? file->line_num : -1,
                    s ? s->name : "<no-section>", offset);
        }
        if (tp_knob_on(TPK_T4_IPREL_DIAG)) {
            const char *fname = file ? file->filename : "<unknown>";
            const char *func = funcname ? funcname : "<no func>";
            uint8_t op = 0;
            if (s && s->data && offset < s->data_offset)
                op = ((uint8_t *)s->data)[offset];
            fprintf(stderr,
                    "IPREL_DIAG: sym=%s bt=0x%x func=%s file=%s line=%d section=%s offset=0x%lx op=0x%02x\n",
                    get_tok_str(sym->v, NULL), bt, func, fname,
                    file ? file->line_num : -1,
                    s ? s->name : "<no-section>", offset, op);
        }
    }
    if ((type == R_TRANSPUTER_IPREL32 || type == R_TRANSPUTER_LDC_ABS) && sym && tp_knob_on(TPK_T4_RELOC_LOG)) {
        const char *name = get_tok_str(sym->v, NULL);
        if ((!reloc_sym_filter || strstr(name, reloc_sym_filter)) &&
            (reloc_type_filter < 0 || reloc_type_filter == type)) {
            const char *fname = file ? file->filename : "<unknown>";
            const char *func = funcname ? funcname : "<no func>";
            uint8_t op = 0;
            if (s && s->data && offset < s->data_offset)
                op = ((uint8_t *)s->data)[offset];
            fprintf(stderr,
                    "RELOC_EMIT: sym=%s type=%d func=%s file=%s line=%d section=%s offset=0x%lx op=0x%02x\n",
                    name, type, func, fname, file ? file->line_num : -1,
                    s ? s->name : "<no-section>", offset, op);
        }
    }
    if (sym && tp_knob_on(TPK_T4_SYM_TRACE)) {
        const char *name = get_tok_str(sym->v, NULL);
        if (name && strstr(name, tp_knob(TPK_T4_SYM_TRACE))) {
            const char *fname = file ? file->filename : "<unknown>";
            const char *func = funcname ? funcname : "<no func>";
            uint8_t op = 0;
            int bt = sym->type.t & VT_BTYPE;
            if (s && s->data && offset < s->data_offset)
                op = ((uint8_t *)s->data)[offset];
            fprintf(stderr,
                    "SYM_RELOC: sym=%s bt=0x%x type=%d func=%s file=%s line=%d section=%s offset=0x%lx op=0x%02x\n",
                    name, bt, type, func, fname, file ? file->line_num : -1,
                    s ? s->name : "<no-section>", offset, op);
        }
    }
}

/* Fold `ADC k` into the word offset of the LDNL/STNL that follows it.
 *
 * LDNL/STNL already take a word displacement from the base in AReg, and on
 * T9000 that displacement is resolved by the address-calculation stage, which
 * is otherwise idle — whereas the ADC occupies the contended ALU stage. So
 * `ADC 4; LDNL 0` and `LDNL 1` compute the same address, but the second is
 * both one instruction shorter and one ALU slot cheaper.
 *
 * Only valid when the ADC is the very last thing emitted (nothing may sit
 * between it and the access) and the byte offset is a whole number of words.
 * Returns the word offset to use, having rolled the ADC back, or 0. */
static int tp_fold_adc_into_nonlocal(int base_words)
{
    int words;
    const struct tp_hist_ent *last;

    /* Never rewind under nocode_wanted: emission is suppressed, so `ind` is
     * frozen and the trackers may describe *live* pre-region code.  Folding
     * would delete that code while the replacement is itself suppressed. */
    if (nocode_wanted)
        return base_words;
    last = tp_hist_last_instr();
    if (!last || (last->opc != TP_OP_ADC && last->opc != TP_OP_LDNLP))
        return base_words;
    if (last->off < func_start_ind)
        return base_words;
    if (last->val == 0)
        return base_words;
    if (last->opc == TP_OP_LDNLP) {
        /* LDNLP already carries a word displacement.  Folding it away is still
         * a win: the access resolves the displacement itself, so this removes
         * an instruction as well as a stage-2 slot. */
        words = base_words + last->val;
    } else {
        if (last->val & 3)
            return base_words;
        words = base_words + (last->val >> 2);
    }
    /* Keep the folded form to encodings that are no longer than what we
     * removed; a huge displacement would just move the prefix bytes around. */
    if (words < 0 || words > 4095)
        return base_words;

    ind = last->off;
    cur_text_section->data_offset = ind;
    tp_hist_rollback(ind);
    return words;
}

/* Is workspace-slot register r referenced by any live vstack entry other than
 * the one at `except`? If not, a store to it is dead. */
static int tp_reg_live_on_vstack(int r, SValue *except)
{
    SValue *p;
    for (p = vstack; p <= vtop; p++) {
        if (p == except)
            continue;
        if ((p->r & VT_VALMASK) == r)
            return 1;
        if ((p->r2 & VT_VALMASK) == r)
            return 1;
    }
    return 0;
}

/* Push slot r onto the eval stack, cancelling an immediately preceding dead
 * `STL r` rather than emitting `LDL r` after it. `STL r; LDL r` is a pure
 * memory round trip: the store writes r and the load reads it straight back,
 * leaving the eval stack exactly as it was. When nothing else can observe
 * slot r, both instructions can simply go away and the value stays in AReg.
 * Returns 1 if the pair was elided. */
static int tp_push_reg_or_cancel_stl(int r, SValue *except)
{
    /* See tp_fold_adc_into_nonlocal: no rollback while emission is off. */
    int roll_to = tp_hist_last_stl(r);

    if (!nocode_wanted && roll_to >= 0 && roll_to >= func_start_ind &&
        !tp_reg_live_on_vstack(r, except)) {
        ind = roll_to;
        cur_text_section->data_offset = ind;
        /* The instruction before the cancelled store is once again the last
         * thing emitted: a preceding ADC can still be folded into the
         * LDNL/STNL that follows. */
        tp_hist_rollback_keep_last(ind);
        tp_areg_invalidate();
        abcreg_push(1);
        return 1;
    }
    Tp_LDL(r);
    return 0;
}

static int func_has_switch_slots = 0;

/* Debug output control - all env vars cached once in transputer_init() */
static int tp_debug_enabled = -1;  /* -1 = not checked yet */
static int tp_genopi_debug = 0;
static int tp_load_debug = 0;
static int tp_instr_debug = 0;
static int tp_emit_local_debug = 0;
static const char *tp_gen_sym_filter = NULL;  /* T4_GEN_SYM substring filter */
static int tp_store_debug = 0;
static int tp_structcopy_debug = 0;
static int tp_callarg_debug = 0;
static int tp_opf_debug = 0;
static int tp_vt_local_debug = 0;
static int tp_debug_check(void) {
    return tp_debug_enabled > 0;
}

static void tp_builtin_return_void(void)
{
    vpushi(0);
    vtop->type.t = VT_VOID;
}

/* A jump table as it is emitted: `ldl idx; ldc 5; prod; ldc 2; ldpi; bsub;
 * gcall`, then one five-byte `j` for each index (front-padded with pfix 0,
 * the width of every other jump placeholder).  gcall's return address is
 * dropped: the stack is empty at a case label.  tp_relax_function() then
 * gives the entries the common width their displacements need, and when
 * that is four bytes indexes them with wsub: `ldl idx; ldc 2; ldpi; wsub;
 * gcall`, 11 T800 cycles. */
#define TP_SWITCH_JUMP_SLOT_SIZE 5
/* The tables of the function being compiled: section offsets of the
 * dispatch's `ldc width` and of the first entry. */
struct tp_swtab {
    int scale, table, entries;
};
static struct tp_swtab *swtab_log;
static int swtab_count, swtab_cap;

static void swtab_add(int scale, int table, int entries)
{
    if (swtab_count == swtab_cap) {
        swtab_cap = swtab_cap ? swtab_cap * 2 : 8;
        swtab_log = tcc_realloc(swtab_log, swtab_cap * sizeof(*swtab_log));
    }
    swtab_log[swtab_count].scale = scale;
    swtab_log[swtab_count].table = table;
    swtab_log[swtab_count].entries = entries;
    swtab_count++;
}
#define TP_SWITCH_MAX_SPAN      2048

static int tp_case_cmp(uint64_t a, uint64_t b, int is_unsigned)
{
    if (is_unsigned)
        return a < b ? -1 : a > b;
    return (int64_t)a < (int64_t)b ? -1 : (int64_t)a > (int64_t)b;
}

static void tp_switch_emit_instr(int opcode, int operand)
{
    uint8_t buf[12];
    int len, i;

    len = tp_encode_instr(buf, opcode, operand);
    for (i = 0; i < len; ++i)
        g(buf[i]);
}

static void tp_switch_emit_fixed_slot(int opcode, int operand, int slot_size)
{
    uint8_t buf[12];
    int len, i, pad;

    func_has_switch_slots = 1;
    len = tp_encode_instr(buf, opcode, operand);
    if (len > slot_size)
        tcc_error("transputer switch slot too small");
    /* Front-pad with PFIX 0 (safe NOP).  Do not use J 0 — on real silicon it
     * is a timeslice point, and for offset math PC_after must sit after the
     * real instruction at the end of the slot. */
    pad = slot_size - len;
    for (i = 0; i < pad; ++i)
        g((TP_OP_PFIX << 4) | 0);
    for (i = 0; i < len; ++i)
        g(buf[i]);
}

static void tp_switch_patch_fixed_slot(int offset, int opcode, int operand, int slot_size)
{
    uint8_t buf[12];
    int len, i, pad;

    func_has_switch_slots = 1;
    len = tp_encode_instr(buf, opcode, operand);
    if (len > slot_size)
        tcc_error("transputer switch patch overflow");
    if (offset < 0 || offset + slot_size > cur_text_section->data_offset)
        tcc_error("transputer switch patch out of bounds");

    pad = slot_size - len;
    for (i = 0; i < pad; ++i)
        cur_text_section->data[offset + i] = (TP_OP_PFIX << 4) | 0;
    for (i = 0; i < len; ++i)
        cur_text_section->data[offset + pad + i] = buf[i];
}

static uint64_t tp_switch_total_selectors(struct switch_t *sw, int is_unsigned)
{
    uint64_t total = 0;
    int i;

    for (i = 0; i < sw->n; ++i) {
        struct case_t *p = sw->p[i];
        uint64_t count;

        if (is_unsigned) {
            uint32_t a = (uint32_t)p->v1, b = (uint32_t)p->v2;
            count = (uint64_t)b - (uint64_t)a + 1;
        } else {
            int32_t a = (int32_t)p->v1, b = (int32_t)p->v2;
            count = (uint64_t)((int64_t)b - (int64_t)a + 1);
        }
        total += count;
        if (total > (uint64_t)TP_SWITCH_MAX_SPAN + 1)
            break;
    }
    return total;
}

ST_FUNC int gen_switch_jump_table(struct switch_t *sw, int *pdsym)
{
    int is_unsigned, entries, i, d;
    uint32_t min_u, max_u;
    uint64_t span_u64, total_u64;
    int *targets = NULL;
    int *entry_offs = NULL;
    int scale_off, table_off, miss_ind;
    uint32_t base_u;
    int switch_t;
    int idx_reg;

    if (sw->n <= 0)
        return 0;

    switch_t = sw->sv.type.t & VT_BTYPE;
    if (switch_t == VT_LLONG)
        return 0;

    is_unsigned = (sw->sv.type.t & VT_UNSIGNED) != 0;
    min_u = (uint32_t)sw->p[0]->v1;
    max_u = (uint32_t)sw->p[sw->n - 1]->v2;

    span_u64 = is_unsigned
        ? (uint64_t)max_u - (uint64_t)min_u
        : (uint64_t)((int64_t)(int32_t)max_u - (int64_t)(int32_t)min_u);
    if (span_u64 > TP_SWITCH_MAX_SPAN)
        return 0;

    entries = (int)(span_u64 + 1);
    if (entries <= 6)
        return 0;

    total_u64 = tp_switch_total_selectors(sw, is_unsigned);
    if (total_u64 <= 6)
        return 0;
    if (total_u64 * 3 < (uint64_t)entries)  /* density >= 33% (was 50%) */
        return 0;

    /* A table that starts just above zero is indexed by the value itself:
     * a few unused entries cost less than a subtraction at every dispatch. */
    base_u = min_u;
    if (min_u != 0 && min_u <= 16 && (is_unsigned || (int32_t)max_u > 0)) {
        base_u = 0;
        entries = (int)max_u + 1;
    }

    targets = tcc_malloc(entries * sizeof(*targets));
    entry_offs = tcc_malloc(entries * sizeof(*entry_offs));
    for (i = 0; i < entries; ++i)
        targets[i] = -1;

    for (i = 0; i < sw->n; ++i) {
        struct case_t *p = sw->p[i];
        if (is_unsigned) {
            uint32_t v, v2 = (uint32_t)p->v2;
            for (v = (uint32_t)p->v1; ; ++v) {
                int idx = (int)((uint64_t)v - (uint64_t)base_u);
                if (idx < 0 || idx >= entries)
                    tcc_error("transputer switch index out of bounds");
                targets[idx] = p->ind;
                if (v == v2)
                    break;
            }
        } else {
            int64_t v, v2 = (int32_t)p->v2, min_s = (int32_t)base_u;
            for (v = (int32_t)p->v1; tp_case_cmp(v, v2, 0) <= 0; ++v) {
                int idx = (int)(v - min_s);
                if (idx < 0 || idx >= entries)
                    tcc_error("transputer switch index out of bounds");
                targets[idx] = p->ind;
            }
        }
    }

    d = *pdsym;

    /* The index, x - base as an unsigned word, and one unsigned test for
     * both ends of the range: a value below the base wraps to a large one.
     * Unsigned so that the subtraction cannot set the Error flag.  No test
     * when the default is `__builtin_unreachable()`: the program promises
     * a case. */
    vdup();
    vtop->type.t = VT_INT | VT_UNSIGNED;
    if (base_u != 0) {
        vpush64(VT_INT | VT_UNSIGNED, base_u);
        gen_op('-');
    }
    if (!sw->def_unreachable) {
        vdup();
        vpush64(VT_INT | VT_UNSIGNED, (uint32_t)entries);
        gen_op(TOK_ULT);
        d = gvtst(1, d);
    }

    gv(RC_INT);
    idx_reg = vtop->r & VT_VALMASK;
    if (idx_reg < VT_CONST)
        tp_switch_emit_instr(TP_OP_LDL, idx_reg);
    else if (idx_reg == VT_CONST && !(vtop->r & VT_SYM))
        tp_switch_emit_instr(TP_OP_LDC, vtop->c.i);
    else
        tcc_error("transputer switch: unsupported idx value form");

    scale_off = ind;
    tp_switch_emit_instr(TP_OP_LDC, TP_SWITCH_JUMP_SLOT_SIZE);
    tp_switch_emit_instr(TP_OP_OPR, TP_OPR_PROD);
    /* the table starts two bytes after ldpi: bsub, gcall */
    tp_switch_emit_instr(TP_OP_LDC, 2);
    tp_switch_emit_instr(TP_OP_OPR, TP_OPR_LDPI);
    tp_switch_emit_instr(TP_OP_OPR, TP_OPR_BSUB);
    tp_switch_emit_instr(TP_OP_OPR, TP_OPR_GCALL);

    table_off = ind;
    for (i = 0; i < entries; ++i) {
        entry_offs[i] = ind;
        tp_switch_emit_fixed_slot(TP_OP_J, 0, TP_SWITCH_JUMP_SLOT_SIZE);
    }

    miss_ind = gind();
    d = gjmp(d);

    for (i = 0; i < entries; ++i) {
        int target = targets[i] >= 0 ? targets[i] : miss_ind;
        tp_switch_patch_fixed_slot(entry_offs[i], TP_OP_J,
                                   target - (entry_offs[i] + TP_SWITCH_JUMP_SLOT_SIZE),
                                   TP_SWITCH_JUMP_SLOT_SIZE);
    }
    swtab_add(scale_off, table_off, entries);

    tcc_free(entry_offs);
    tcc_free(targets);

    vpop();
    *pdsym = d;
    return 1;
}

static void tp_debug_log_vt_local_normalize(const SValue *sv, const char *side)
{
    int bt;

    if (!tp_vt_local_debug)
        return;

    bt = sv->type.t & VT_BTYPE;
    fprintf(stderr,
            "[VT_LOCAL_NORMALIZE] func=%s side=%s r=0x%x c.i=%ld bt=0x%x type=0x%x file=%s line=%d\n",
            funcname ? funcname : "<unknown>",
            side,
            sv->r,
            (long)sv->c.i,
            bt,
            sv->type.t,
            file ? file->filename : "<unknown>",
            file ? file->line_num : -1);
}

ST_FUNC void transputer_init(struct TCCState *s) {
    const char *env;
    (void)s;

    /* Cache all debug env vars once at init time */
    env = tp_knob(TPK_T4_DEBUG);
    tp_debug_enabled = (env && *env) ? 1 : 0;
    env = tp_knob(TPK_T4_GENOPI_DEBUG);
    tp_genopi_debug = env && *env;
    env = tp_knob(TPK_T4_LOAD_DEBUG);
    tp_load_debug = env && *env;
    env = tp_knob(TPK_T4_INSTR_DEBUG);
    tp_instr_debug = env && *env;
    env = tp_knob(TPK_T4_EMIT_LOCAL_DEBUG);
    tp_emit_local_debug = env && *env ? atoi(env) : 0;
    env = tp_knob(TPK_T4_GEN_SYM);
    tp_gen_sym_filter = (env && *env) ? env : NULL;
    env = tp_knob(TPK_T4_STORE_DEBUG);
    tp_store_debug = env && *env;
    env = tp_knob(TPK_T4_STRUCTCOPY_DEBUG);
    tp_structcopy_debug = env && *env;
    env = tp_knob(TPK_T4_CALLARG_DEBUG);
    tp_callarg_debug = env && *env;
    env = tp_knob(TPK_T4_OPF_DEBUG);
    tp_opf_debug = env && *env;
    env = tp_knob(TPK_T4_VT_LOCAL_DEBUG);
    tp_vt_local_debug = env && *env;
    if (!tp_vt_local_debug) {
        env = tp_knob(TPK_TCC_TRANSPUTER_DEBUG_VT);
        tp_vt_local_debug = env && *env;
    }

    /* Initialize transputer backend */
    /* Reset function state for safety in case previous compilation aborted */
    transputer_func_reset();
}

ST_DATA const int reg_classes[NB_REGS] = {
    RC_IST0, RC_IST1, RC_IST2,
    RC_FLOAT | RC_FST0, RC_FLOAT | RC_FST1, RC_FLOAT | RC_FST2,
    RC_WREG,
    RC_IRET, RC_IRE2, RC_INT, RC_INT, RC_INT, RC_INT, RC_INT, RC_INT
};

ST_DATA const char * const target_machine_defs =
    "__transputer__\0"
    "__TRANSPUTER__\0"
    /* 16-bit float feature-test macros.  There is no ISO C feature-test
       macro for either format (neither is in any C standard), so these
       follow the de-facto compiler-extension convention.  The types work
       on every part (soft conversions without TVX); __TVX__ and __MXFP4__
       (the TVX instructions and the MX FP4 intrinsics) are predefined in
       tccpp.c only under -mt9000tvx. */
    "__BF16__\0"       /* the __bf16 type is available (as GCC/Clang)    */
    "__FP16__\0"       /* the __fp16 (binary16) type is available        */
    "__FLOAT16__\0"    /* _Float16 (C23 Annex H binary16) is available   */
    ;

/*
 * STRICT WORKSPACE MEMORY MAP:
 *   [0 .. 14]  : TCC Pseudo-registers (TREG_A=0 ... reg 14), accessed via STL r
 *   [15 .. 19] : Backend Scratch Slots (isolated from registers)
 *   [20 .. N]  : Local Variables and Call Spills
 *
 * Previously FRAME_SLOT_SCRATCH was 0, overlapping TREG_A's workspace slot,
 * and FRAME_SLOT_MIN_LOCALS was 4, causing locals to be patched into offsets
 * 12-14 which overlap RC_INT registers 12-14 in workspace.
 */
#define TP_SCRATCH_BASE         NB_REGS  /* 15 — first slot above all registers */
#define FRAME_SLOT_SCRATCH      (TP_SCRATCH_BASE + 0)  /* Reserved for ALT/outword/postnormsn */
#define FRAME_SLOT_CARRY        (TP_SCRATCH_BASE + 1)  /* Carry/borrow between ADDC1↔ADDC2, SUBC1↔SUBC2 */
#define FRAME_SLOT_SCRATCH2     (TP_SCRATCH_BASE + 2)  /* Second scratch for 64-bit stack rearrangement */
#define FRAME_SLOT_CALL_BASE    (TP_SCRATCH_BASE + 2)  /* Alias: pointer to original call frame */
#define FRAME_SLOT_SPILL_A      (TP_SCRATCH_BASE + 0)  /* Areg spill slot (reuses SCRATCH during calls) */
#define FRAME_SLOT_SPILL_B      (TP_SCRATCH_BASE + 1)  /* Breg spill slot (reuses CARRY during calls) */
#define FRAME_SLOT_SPILL_C      (TP_SCRATCH_BASE + 2)  /* Creg spill slot (reuses SCRATCH2 during calls) */
/* Dedicated 64-bit temp pair for float/double operand preservation. */
#define FRAME_SLOT_DBL_TMP      (TP_SCRATCH_BASE + 3)  /* Uses slots +3 and +4 */
/* Locals start after all registers + scratch slots. */
#define FRAME_SLOT_MIN_LOCALS   (TP_SCRATCH_BASE + 5)  /* = 20 */

/*
 * Returning a builtin's result.
 *
 * `vtop->r = <reg>` means "this value lives in workspace slot <reg>", not
 * "in the hardware AReg" - load() materialises it with LDL <reg> (see the
 * `v < VT_CONST` case in load()).  So a builtin whose instruction leaves a
 * result in AReg must park it:
 *
 *      vtop->r = REG_IRET;
 *      Tp_STL(REG_IRET);
 *
 * Marking the value TREG_A instead is a trap: TREG_A is slot 0, nothing
 * ever stores to it, and the reload fetches whatever slot 0 happens to
 * hold.  For an ALT that is the Temp slot, so `__disc` returned
 * NoneSelected_o (0xffffffff) and every guard read as ready; for
 * `__ldtimer`/`__mint`/`__crcbyte` it was simply stale workspace.
 *
 * REG_IRET is slot 7, which also keeps the result clear of Temp - required
 * here, because disc/diss/dist must be able to read Temp after an earlier
 * disable instruction has already produced a result.
 */

/* Global state */
static int abcreg_stack_depth = 0;  /* ABC integer stack depth */
static int fpureg_stack_depth = 0;  /* FPU stack depth */
/* Maximum number of stacked outgoing call words (ABI word 3+) seen in the
 * current function. This is reserved in the frame so gfunc_call can stage
 * outgoing stack arguments at Wptr[0..n-1] without clobbering locals. */
static int func_max_outgoing_words = 0;
/* Most stacked argument words (ABI word 3+) any one call in the current
 * function stores at Wptr[0..n-1].  The callee reads them in place, which
 * the scheduler cannot see, so it treats every call as reading those slots. */
static int func_max_stack_words = 0;
/* Highest fixed workspace slot actually accessed (register or scratch).
 * Used to compute dynamic frame size instead of always reserving 20 slots. */
static int func_max_slot_used = -1;
/* While tp_stage_call_words materialises a call word, the workspace slots
 * already holding other words of the same call: [tp_staged_lo[k],
 * tp_staged_hi[k]).  emit_instr refuses any fixed-slot access to them. */
static int tp_staging_push;
static int tp_staged_lo[3], tp_staged_hi[3];
/* Set when inline asm was assembled into the current function.  Inline asm
 * encodes workspace slots itself (transputer-asm.c never goes through
 * emit_instr), so func_max_slot_used cannot see what it touches and the
 * reserved region must fall back to the static worst case. */
static int func_uses_inline_asm = 0;
/* Words reserved below the locals in the current function's frame, i.e. the
 * slot number the first local lands on.  Computed in gfunc_epilog and read by
 * the evaluation-stack scheduler, which must only treat slots below it as
 * pseudo-registers - everything at or above it is a local. */
static int func_frame_reserved = FRAME_SLOT_MIN_LOCALS;
/* Total size of the current function's frame in workspace words, i.e. the
 * magnitude of its prologue AJW.  Slots [0, func_frame_words) are the
 * function's own; incoming arguments sit above them.  Set in gfunc_epilog,
 * read by the evaluation-stack scheduler. */
static int func_frame_words;
extern const char *funcname;
static int func_start_nsym;   /* symtab size when the function began */
static int func_start_nrel;   /* its code section's relocation count then */
static Sym *func_start_sym;   /* the function itself */

static int tp_trace_func_enabled = -1;
static char tp_trace_func_name[64];

static int tp_trace_func_active(void) {
    const char *env;
    if (tp_trace_func_enabled == -1) {
        env = tp_knob(TPK_TCC_TRACE_FUNC);
        if (env && *env) {
            strncpy(tp_trace_func_name, env, sizeof(tp_trace_func_name) - 1);
            tp_trace_func_name[sizeof(tp_trace_func_name) - 1] = '\0';
            tp_trace_func_enabled = 1;
        } else {
            tp_trace_func_enabled = 0;
        }
    }
    if (!tp_trace_func_enabled || !funcname || !*funcname)
        return 0;
    return strcmp(funcname, tp_trace_func_name) == 0;
}

static void tp_trace_emit_bytes(const char *tag, int start_off, int end_off) {
    int len = end_off - start_off;
    int i;
    if (!tp_trace_func_active() || !cur_text_section || len <= 0)
        return;
    if (start_off < 0 || end_off > cur_text_section->data_offset)
        return;
    fprintf(stderr, "[tcc] %s func=%s ind=0x%x bytes:", tag, funcname, start_off);
    for (i = 0; i < len; i++) {
        fprintf(stderr, " %02x", cur_text_section->data[start_off + i]);
    }
    fprintf(stderr, "\n");
}

/*
 * FRAME HANDLING SYSTEM
 * TCC generates code in one pass. It calculates local variable offsets (loc)
 * relative to the stack frame base. On Transputer, we use 'ajw -N' to allocate
 * stack, meaning the base pointer (Wptr) moves.
 *
 * We cannot emit 'ldl (FrameSize + loc)' because FrameSize is unknown until
 * the function epilog.
 *
 * Solution: Emit fixed-size placeholder instructions for all local accesses
 * and patch them in gfunc_epilog.
 */

struct local_patch {
    int instr_offset;   /* offset in section buffer */
    int tcc_offset;     /* TCC's internal offset (usually negative) */
    int opcode;         /* The opcode used (LDL, STL, LDLP) */
    int placeholder_size; /* 2 or 3 bytes reserved at emit time */
    int proven_nonvolatile_read; /* direct read of an unescaped, nonvolatile int */
    struct local_patch *next;
};

struct jump_patch_meta {
    int instr_offset;                 /* placeholder offset in section */
    int target_addr;                  /* resolved target address (set by gsym_addr) */
    int instr_len;                    /* encoded instruction length (set by gsym_addr) */
    unsigned char is_conditional;     /* 1 for CJ placeholder, 0 for J */
    unsigned char patched;            /* 1 once gsym_addr patched it */
};

static struct local_patch *local_patches = NULL;
/* Tail of local_patches, so the pool can append in O(1). Entries must be
 * appended, not prepended: tp_drop_local_patches_from scans the leading run
 * and relies on it holding the most recently emitted offsets. */
static struct local_patch *local_patches_tail = NULL;
/* The function's jump placeholders, sorted by instr_offset so that
 * jump_patch_find can binary-search them: a linear list made every gjmp and
 * every chain link a scan, quadratic in a function's jumps.  Code is emitted
 * forwards, so a new placeholder nearly always goes on the end. */
static struct jump_patch_meta *jump_patches;
static int jump_patch_count, jump_patch_cap;

static int func_prologue_ajw_offset = -1;  /* -1 = not set, >=0 = valid offset */

/* The AJW placeholders of the early returns' own epilogues (gfunc_leave). */
static int *leave_ajw;
static int leave_ajw_count, leave_ajw_cap;

/* Local and prologue-AJW placeholders whose real operand needs more bytes
 * than were reserved for it: a frame over 4095 words.  gfunc_epilog leaves
 * the opcode with a zero operand in the slot, and tp_relax_function() widens
 * it to the full encoding when it lays the function out again. */
#define MAX_WIDE_ENTRIES 4096
struct wide_entry {
    int offset;     /* byte offset in section of the opcode byte */
    int opcode;
    int operand;
};
static struct wide_entry wide_log[MAX_WIDE_ENTRIES];
static int wide_log_count = 0;

/* Direct calls emitted narrower than TP_CALL_SLOT, because the callee was
 * already earlier in this section (tp_call_slot_width).  tp_relax_function()
 * re-encodes them as branches to that fixed target instead of pinning them,
 * so they follow the code in between when it shrinks or widens. */
struct near_call {
    int offset;     /* section offset of the slot (the relocation) */
    int target;     /* section offset of the callee */
};
static struct near_call *near_call_log;
static int near_call_count, near_call_cap;

static void near_call_add(int offset, int target)
{
    if (near_call_count == near_call_cap) {
        near_call_cap = near_call_cap ? near_call_cap * 2 : 32;
        near_call_log = tcc_realloc(near_call_log,
                                    near_call_cap * sizeof(*near_call_log));
    }
    near_call_log[near_call_count].offset = offset;
    near_call_log[near_call_count].target = target;
    near_call_count++;
}

/* The values `loc` held whenever a local was accessed (tcc offsets), for
 * tp_slot_remap.  No local object straddles one: objects are only ever
 * allocated below the current `loc`, so every past value is a boundary
 * between them.  Allocations with no access in between fall into one block,
 * which only means the layout moves them together.  `loc` rising again (the
 * transputer inline experiment in tccgen.c rolls it back) breaks that, and
 * the function is then left as laid out. */
static int *tp_loc_marks;
static int tp_loc_marks_count, tp_loc_marks_cap, tp_loc_marks_bad;

static void tp_loc_mark(void)
{
    if (tp_loc_marks_count) {
        int last = tp_loc_marks[tp_loc_marks_count - 1];
        if (loc == last)
            return;
        if (loc > last)
            tp_loc_marks_bad = 1;
    }
    if (tp_loc_marks_count == tp_loc_marks_cap) {
        tp_loc_marks_cap = tp_loc_marks_cap ? tp_loc_marks_cap * 2 : 64;
        tp_loc_marks = tcc_realloc(tp_loc_marks,
                                   tp_loc_marks_cap * sizeof(*tp_loc_marks));
    }
    tp_loc_marks[tp_loc_marks_count++] = loc;
}

static void wide_log_add(int offset, int opcode, int operand) {
    if (wide_log_count == MAX_WIDE_ENTRIES)
        tcc_error("function '%s': too many workspace accesses beyond "
                  "the 4095-word reach of a local placeholder",
                  funcname ? funcname : "?");
    wide_log[wide_log_count].offset = offset;
    wide_log[wide_log_count].opcode = opcode;
    wide_log[wide_log_count].operand = operand;
    wide_log_count++;
}

/* Index of the first placeholder at or after instr_offset. */
static int jump_patch_index(int instr_offset)
{
    int lo = 0, hi = jump_patch_count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (jump_patches[mid].instr_offset < instr_offset)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

static struct jump_patch_meta *jump_patch_find(int instr_offset)
{
    int i = jump_patch_index(instr_offset);
    if (i < jump_patch_count && jump_patches[i].instr_offset == instr_offset)
        return &jump_patches[i];
    return NULL;
}

static struct jump_patch_meta *jump_patch_upsert(int instr_offset, int is_conditional)
{
    int i = jump_patch_index(instr_offset);
    struct jump_patch_meta *p;
    if (i == jump_patch_count || jump_patches[i].instr_offset != instr_offset) {
        if (jump_patch_count == jump_patch_cap) {
            jump_patch_cap = jump_patch_cap ? jump_patch_cap * 2 : 64;
            jump_patches = tcc_realloc(jump_patches,
                                       jump_patch_cap * sizeof(*jump_patches));
        }
        /* Only a placeholder re-emitted after a rollback lands mid-array. */
        memmove(jump_patches + i + 1, jump_patches + i,
                (jump_patch_count - i) * sizeof(*jump_patches));
        jump_patch_count++;
        jump_patches[i].instr_offset = instr_offset;
    }
    p = &jump_patches[i];
    p->is_conditional = is_conditional ? 1 : 0;
    return p;
}

static void jump_patch_add(int instr_offset, int is_conditional)
{
    struct jump_patch_meta *p = jump_patch_upsert(instr_offset, is_conditional);
    p->target_addr = 0;
    p->instr_len = 0;
    p->patched = 0;
}

/* Reset per function */
static void transputer_func_reset(void) {
    struct local_patch *p = local_patches;

    while(p) {
        struct local_patch *next = p->next;
        tcc_free(p);
        p = next;
    }
    local_patches = NULL;
    local_patches_tail = NULL;
    jump_patch_count = 0;
    swtab_count = 0;
    tp_hist_clear();
    tp_fpr_forget();
    func_prologue_ajw_offset = -1;  /* Reset prologue offset */
    leave_ajw_count = 0;
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;
    func_max_outgoing_words = 0;
    func_max_stack_words = 0;
    func_max_slot_used = -1;
    tp_staging_push = 0;
    func_uses_inline_asm = 0;
    func_frame_reserved = FRAME_SLOT_MIN_LOCALS;
    func_frame_words = 0;
    func_has_switch_slots = 0;
    tp_areg_invalidate();
    tp_pool_reset();
    wide_log_count = 0;
    near_call_count = 0;
    tp_loc_marks_count = 0;
    tp_loc_marks_bad = 0;
}

/*************************************************************/
/* Instruction emission */
/*************************************************************/

/* Emit Transputer instruction with nibble operand (handles Oreg properly) */
/* TCC_TRANSPUTER_FPLDNLI=0 keeps `wsub; fpldnlsn' apart, for an A/B run. */
static int tp_fpu_indexed_ok(void)
{
    static int ok = -1;

    if (ok < 0) {
        const char *e = tp_knob(TPK_TCC_TRANSPUTER_FPLDNLI);
        ok = !(e && *e == '0');
    }
    return ok;
}

/******************************************************/
/* Basic byte emit helpers                            */
/******************************************************/
ST_FUNC void g(int c)
{
    int ind1;
    static int last_byte = -1;
    static int nfix_opr_log_count = 0;
    static int nfix_opr_warn;
    static int prefix_count = 0;
    static int call_log_count = 0;
    static int call_log_limit;
    static int call_log_short;
    static const char *call_log_filter;
    static int env_read;
    if (nocode_wanted)
        return;
    if (!cur_text_section)
        return;
    /* Read the trace knobs once: this runs for every byte emitted. */
    if (!env_read) {
        const char *env;
        env = tp_knob(TPK_T4_NFIX_OPR_WARN);
        nfix_opr_warn = env && *env ? atoi(env) : 0;
        env = tp_knob(TPK_T4_CALL_EMIT_TRACE);
        call_log_limit = env && *env ? atoi(env) : 0;
        env = tp_knob(TPK_T4_CALL_EMIT_SHORT);
        call_log_short = env && *env ? atoi(env) : 0;
        call_log_filter = tp_knob(TPK_T4_CALL_EMIT_FILTER);
        env_read = 1;
    }
    ind1 = ind + 1;
    if (ind1 > cur_text_section->data_allocated)
        section_realloc(cur_text_section, ind1);
    cur_text_section->data[ind] = c;
    if (nfix_opr_warn && nfix_opr_log_count < 64
        && last_byte >= 0
        && ((last_byte & 0xF0) == 0x60)
        && ((c & 0xF0) == 0xF0)) {
        const char *fname = file ? file->filename : "<unknown>";
        const char *func = funcname ? funcname : "<no func>";
        fprintf(stderr, "WARN: g(): NFIX->OPR sequence 0x%02X 0x%02X at offset=0x%x file=%s func=%s\n",
                last_byte, c, ind, fname, func);
        nfix_opr_log_count++;
    }
    /* Note: 0xF3 is a valid Transputer instruction byte (OPR 3).
     * When preceded by PFIX 3 (0x23), it encodes OPR 0x33 = XOR.
     * This is used legitimately in unsigned comparison code.
     * Do NOT intercept or replace this byte! */
    ind = ind1;
    /* Keep data_offset in sync with ind */
    if (ind > cur_text_section->data_offset)
        cur_text_section->data_offset = ind;
    last_byte = c;

    if ((c & 0xF0) == 0x20 || (c & 0xF0) == 0x60) {
        prefix_count++;
    } else {
        if (call_log_limit > 0 && call_log_count < call_log_limit) {
            int opc = c & 0xF0;
            if (opc == 0x90 || opc == 0x00 || opc == 0xA0) {
                const char *fname = file ? file->filename : "<unknown>";
                const char *func = funcname ? funcname : "<no func>";
                fprintf(stderr,
                        "CALL_EMIT: byte=0x%02X prefixes=%d offset=0x%x file=%s func=%s\n",
                        c, prefix_count, ind - 1, fname, func);
                call_log_count++;
            }
        } else if (call_log_short > 0 && call_log_count < call_log_short) {
#define TP_IPREL_NIBBLES 9

            int opc = c & 0xF0;
            if ((opc == 0x90 || opc == 0x00 || opc == 0xA0) &&
                prefix_count < (TP_IPREL_NIBBLES - 1)) {
                const char *fname = file ? file->filename : "<unknown>";
                const char *func = funcname ? funcname : "<no func>";
                if (call_log_filter && *call_log_filter) {
                    if (!strstr(fname, call_log_filter) &&
                        (!func || !strstr(func, call_log_filter))) {
                        prefix_count = 0;
                        return;
                    }
                }
                fprintf(stderr,
                        "CALL_EMIT_SHORT: byte=0x%02X prefixes=%d offset=0x%x file=%s func=%s\n",
                        c, prefix_count, ind - 1, fname, func);
                call_log_count++;
            }
        }
        prefix_count = 0;
    }
}

ST_FUNC void gen_le16(int v)
{
    g(v);
    g(v >> 8);
}

ST_FUNC void gen_le32(int v)
{
    g(v);
    g(v >> 8);
    g(v >> 16);
    g(v >> 24);
}

/* Classify only the direct workspace opcodes; callers apply their own
 * operand bounds and account for any indirect access following LDLP. */
static int tp_is_direct_workspace_op(int opc)
{
    return opc == TP_OP_LDL || opc == TP_OP_STL || opc == TP_OP_LDLP;
}

static void emit_instr(int opc, int v) {
    uint8_t buf[12];  /* Max instruction length */
    int len;
    int start_off = ind;
    int i;

    if (tp_instr_debug) {
        fprintf(stderr, "[INSTR_DEBUG] func=%s opc=%d v=%d ind=0x%x nocode=%d\n",
                funcname ? funcname : "<unknown>", opc, v, ind, nocode_wanted);
    }
    if (tp_trace_func_active()) {
        fprintf(stderr, "[tcc] emit_instr func=%s opc=%d v=%d ind=0x%x\n",
            funcname, opc, v, ind);
    }

    /* DUP safety: any instruction emission invalidates the AReg content tag.
     * The tag is only re-set explicitly at the end of tp_push_sv(). */
    tp_areg_invalidate();

    /* Deferred STL disabled — too many code paths read from workspace
     * slots without going through tp_push_sv. */

    /* Track highest fixed workspace slot used for dynamic frame sizing.
     * Only counts direct register/scratch slot accesses (non-negative operands).
     * Local variable accesses go through emit_local_op() and are NOT counted here. */
    if (tp_is_direct_workspace_op(opc) &&
        v >= 0 && v < FRAME_SLOT_MIN_LOCALS) {
        /* LDLP of a fixed slot is always followed by an indirect access
         * (LDNL/STNL, FPLDNLDB/FPSTNLDB, ...) that can reach slot+1 for the
         * 64-bit forms.  Count the pair so the reserved region covers it. */
        int top = (opc == TP_OP_LDLP) ? v + 1 : v;
        if (top > func_max_slot_used)
            func_max_slot_used = top;
    }
    /* A call word being materialised must not read or overwrite one that is
     * already staged (see tp_stage_push). */
    if (tp_staging_push && !nocode_wanted && v >= 0 &&
        tp_is_direct_workspace_op(opc)) {
        for (i = 0; i < 3; i++)
            if (v >= tp_staged_lo[i] && v < tp_staged_hi[i])
                tcc_internal_error("call word staged in a slot the next one uses");
    }

    /* `wsub; fpldnlsn' is `fpldnlsni', and `wsubdb; fpldnldb' is
     * `fpldnldbi' (TCWG 7.4, T9000 ISM): the indexed loads take the base in
     * A and the index in B, as wsub and wsubdb do.  The wsub must be the
     * newest instruction in the history window, which a label empties, so
     * nothing can jump between the two; a label at the wsub itself still
     * lands on the indexed load, with A and B as before. */
    if (opc == TP_OP_OPR && (v == TP_FPU_LDNLSN || v == TP_FPU_LDNLDB) &&
        !nocode_wanted && tp_hist_n > 0 && tp_hist[0].end == ind &&
        tp_hist[0].kind == TP_HIST_INSTR && tp_hist[0].opc == TP_OP_OPR &&
        tp_hist[0].val == (v == TP_FPU_LDNLSN ? TP_OPR_WSUB : TP_OPR_WSUBDB) &&
        tp_hist[0].off >= func_start_ind && tp_fpu_indexed_ok()) {
        ind = tp_hist[0].off;
        cur_text_section->data_offset = ind;
        tp_hist_rollback(ind);
        start_off = ind;
        v = v == TP_FPU_LDNLSN ? TP_OPR_FPLDNLSNI : TP_OPR_FPLDNLDBI;
    }

    /* Use unified encoder from transputer-enc.h */
    len = tp_encode_instr(buf, opc, v);

    /* Respect no-code parsing contexts (sizeof/typeof/constant-fold walks). */
    if (nocode_wanted) {
        /* ...but do not let the peephole trackers see through the gap.
         *
         * Every rule that rewrites already-emitted code decides it is safe by
         * checking that nothing has been emitted since - and it tests that
         * by the newest entry of the window ending at `ind`.  While emission is suppressed `ind`
         * does not move, so a whole parenthesised expression can go by without
         * disturbing the trackers, and a rule then matches an instruction from
         * *before* the suppressed region against one from after it.
         *
         * tp_push_reg_or_cancel_stl() is the one that bites: in
         *
         *     (1 ? p->cpus : (void *)sizeof(chk(p->cpus)))
         *
         * the `stl` materialising the live operand was cancelled against an
         * `ldl` emitted after the sizeof, so the result was read from a
         * workspace slot nothing had written.  `to_cpumask()` in the kernel is
         * exactly this shape, which made cpu_capacity_span() return the struct
         * base instead of its ->cpus member; __set_bit then OR'd a bit into a
         * list pointer and asym_cpu_capacity_scan() looped forever. */
        tp_hist_clear();
        return;
    }

    /* Emit all bytes from the encoder */
    for (i = 0; i < len; i++) {
        g(buf[i]);
    }
    tp_hist_record(TP_HIST_INSTR, opc, v, start_off, ind);
    tp_trace_emit_bytes("emit_instr", start_off, ind);
}

/*************************************************************/
/* Stack tracking */
/*************************************************************/

static void abcreg_push(int count) {
    abcreg_stack_depth += count;
}

static void abcreg_pop(int count) {
    abcreg_stack_depth -= count;
    if (abcreg_stack_depth < 0) {
        abcreg_stack_depth = 0;
    }
}

static void fpureg_push(int count) {
    fpureg_stack_depth += count;
}

static void fpureg_pop(int count) {
    fpureg_stack_depth -= count;
    if (fpureg_stack_depth < 0) {
        fpureg_stack_depth = 0;
    }
}

/*************************************************************/
/* Basic Transputer instructions */
/*************************************************************/

/* Specific wrappers */
static void Tp_LDC(int v) {
    if ((uint32_t)v == 0x80000000u) {
        /* mint (opr 0x42, two bytes) loads 0x80000000 (MinInt) - 6 bytes
         * shorter than the 8-byte prefix chain ldc would need. */
        emit_instr(TP_OP_OPR, TP_OPR_MINT);
    } else {
        emit_instr(TP_OP_LDC, v);
    }
    abcreg_push(1);
    tp_areg_invalidate();
}
static void Tp_OPR(int v) {
    const char *opr_name = "UNKNOWN";

    /* Secondary function is an integer Oreg operand (ISM §6.1.2): single-
     * byte ops are 0x00..0xFF; multi-nibble forms (LDDEVID 0x17C, TVX 0x1C0+,
     * …) are encoded with PFIX/NFIX via emit_instr. Rejecting v > 0xFF used
     * to silently drop those secondaries → fetch of the next byte as a bare
     * primary, which can look like IllegalInstruction on the RTL core. */
    if (v < 0)
        tcc_internal_error("negative secondary function code");

    /* 0xF3 is TP_OPR_DEVSS (T9000). Keep historical block so a mistaken
     * constant is not emitted as a bare secondary on T800-oriented builds. */
    if (v == 0xF3)
        tcc_internal_error("opr 0xf3 (devss) emitted through Tp_OPR");

    /* Debug critical OPR instructions */
    switch (v) {
        case TP_OPR_RET:    opr_name = "RET"; break;
        case TP_OPR_GCALL:  opr_name = "GCALL"; break;
        case TP_OPR_REV:    opr_name = "REV"; break;
        case TP_OPR_ADD:    opr_name = "ADD"; break;
        case TP_OPR_SUB:    opr_name = "SUB"; break;
        case TP_OPR_MUL:    opr_name = "MUL"; break;
        case TP_OPR_DIV:    opr_name = "DIV"; break;
    }

    /* Log critical operations (only when T4_DEBUG=1) */
    if (tp_debug_check() && (v == TP_OPR_RET || v == TP_OPR_GCALL)) {
        fprintf(stderr, "[OPR_DEBUG] func=%s opr=%s (0x%x) ind=0x%x\n",
                funcname ? funcname : "<unknown>", opr_name, v, ind);
    }

    emit_instr(TP_OP_OPR, v);
}
static void Tp_ADC(int v) { emit_instr(TP_OP_ADC, v); }
static void Tp_LDL(int v) { emit_instr(TP_OP_LDL, v); abcreg_push(1); }
static void Tp_LDLP(int v) { emit_instr(TP_OP_LDLP, v); abcreg_push(1); }
static void Tp_LDNL(int v) { emit_instr(TP_OP_LDNL, tp_fold_adc_into_nonlocal(v)); }
static void Tp_STL(int v) { emit_instr(TP_OP_STL, v); abcreg_pop(1); tp_areg_invalidate(); }
static void Tp_STNL(int v) { emit_instr(TP_OP_STNL, tp_fold_adc_into_nonlocal(v)); abcreg_pop(2); }
static void Tp_REV(void)  { emit_instr(TP_OP_OPR, TP_OPR_REV); tp_areg_invalidate(); }
static void Tp_DUP(void)  { emit_instr(TP_OP_OPR, TP_OPR_DUP); abcreg_push(1); } /* T800: AReg→BReg, BReg→CReg, AReg unchanged */
static void Tp_POP(void)  { emit_instr(TP_OP_OPR, TP_OPR_POP); abcreg_pop(1); tp_areg_invalidate(); }
static void Tp_OPR_UNARY(int v) { emit_instr(TP_OP_OPR, v); }
static void Tp_OPR_BINARY(int v) { emit_instr(TP_OP_OPR, v); abcreg_pop(1); tp_areg_invalidate(); }
static void Tp_LB(void) { Tp_OPR_UNARY(TP_OPR_LB); }
/* sb/ss consume BOTH operands (address and value) and leave AReg = CReg,
 * so they pop 2 - not 1 as Tp_OPR_BINARY assumes.  The peephole's own
 * instruction model already had this right (delta = -2, see the
 * TP_OPR_SB/TP_OPR_SS case in tp_opr_effect); the eval-stack tracker did
 * not, so every sb left abcreg_stack_depth one too high.  That was latent
 * while only volatile byte stores used sb; routing all byte and 16-bit
 * stores through it made it live, and it corrupted the argument setup of
 * a following call (c-testsuite 09003). */
static void Tp_SB(void) { emit_instr(TP_OP_OPR, TP_OPR_SB); abcreg_pop(2); tp_areg_invalidate(); }
static void Tp_AND(void) { Tp_OPR_BINARY(TP_OPR_AND); }
static void Tp_OR(void) { Tp_OPR_BINARY(TP_OPR_OR); }
static void Tp_XOR(void) { Tp_OPR_BINARY(TP_OPR_XOR); }
/* Unchecked AReg + BReg.  `bsub` is the same commutative addition as `sum`
 * (t4/p.c), in one byte where `sum` (opr 0x52) needs a prefix, and the
 * T9000 grouper charges it to the spare address stage instead of the ALU
 * (see tp_ptr_add_is_addr). */
static void Tp_SUM(void) { Tp_OPR_BINARY(TP_OPR_BSUB); }
static void Tp_DIFF(void) { Tp_OPR_BINARY(TP_OPR_DIFF); }
static void Tp_SHL(void) { Tp_OPR_BINARY(TP_OPR_SHL); }
static void Tp_SHR(void) { Tp_OPR_BINARY(TP_OPR_SHR); }
static void Tp_GT(void) { Tp_OPR_BINARY(TP_OPR_GT); }
static void Tp_XWORD(void) { Tp_OPR_BINARY(TP_OPR_XWORD); }
static void Tp_BSUB(void) { Tp_OPR_BINARY(TP_OPR_BSUB); }
static void Tp_WSUB(void) { Tp_OPR_BINARY(TP_OPR_WSUB); }
static void Tp_WSUBDB(void) { Tp_OPR_BINARY(TP_OPR_WSUBDB); }

/*
 * IMS T9000-only narrow-access opcodes, gated on -mt9000.
 *
 *   ls     0xCA  AReg := zero_extend16(*(uint16 *)AReg)
 *   lsx    0xF9  AReg := sign_extend16(*(int16  *)AReg)
 *   ss     0xC8  *(uint16 *)AReg := BReg; AReg := CReg   (pops 2)
 *   lbx    0xB9  AReg := sign_extend8(*(int8 *)AReg)
 *   xbword 0xB8  AReg := sign_extend8(AReg)
 *   xsword 0xF8  AReg := sign_extend16(AReg)
 *   ssub   0xC1  AReg := AReg + 2*BReg                   (pops 1)
 *
 * ls/ss/lsx address a 16-bit unit and require a 16-bit aligned address;
 * see tp_t9000_half_ok()/tp_t9000_half_ptr_ok() for where we are willing to
 * assume that.  The generic two-byte-load/RMW paths stay for everything else.
 */
/* Bit mask for bisecting the individual -mt9000 idioms (TP_T9K_MASK):
   1 = 16-bit loads, 2 = 16-bit stores, 4 = signed byte load,
   8 = sign extends, 16 = ssub, 32 = T9000 FPU (fpge, fpmulby2/divby2,
   fprz/fprn, fpr32tor64/fpr64tor32), 64 = gtu.  Default: all on. */
#define TP_T9K_LOAD16   1
#define TP_T9K_STORE16  2
#define TP_T9K_LOAD8    4
#define TP_T9K_SEXT     8
#define TP_T9K_SSUB     16
#define TP_T9K_FPU      32   /* fpge, fpmulby2/divby2, fprz/rn, fpr32/64 cvts */
#define TP_T9K_GTU      64
/* Does the part compiled for have `f` (TP_EF_*)? */
static int tp_isa_has(unsigned f)
{
    return tcc_state && tp_has(tcc_state, f);
}

static int tp_t9k_mask = -1;
static int tp_have_t9000_feat(int feat) {
    if (!tp_isa_has(TP_EF_T9000))
        return 0;
    if (tp_t9k_mask < 0) {
        const char *e = tp_knob(TPK_TP_T9K_MASK);
        tp_t9k_mask = (e && *e) ? (int)strtol(e, NULL, 0) : 0x7f;
    }
    return (tp_t9k_mask & feat) != 0;
}

/* -mt425 and -mt414: no FPU, so floating point is calls to the soft-float
 * helpers in the part's transputer-libtcc1. */
static int tp_soft_float(void)
{
    return tcc_state && !tp_has(tcc_state, TP_EF_FPU);
}

/* -mt414: the IMS T414 lacks what the T425 and the T800 added - dup and
 * wsubdb among what the code generator emits (Norcroft xpuops.h). */
static int tp_t414(void)
{
    return tcc_state && !tp_has(tcc_state, TP_EF_T800OPS);
}

/* -mt9000tvx: the only target with the TVX extension.  Everywhere else the
 * 16-bit float conversions call the helpers in lib/transputer/transputer-f16.c. */
static int tp_tvx(void)
{
    return tp_isa_has(TP_EF_TVX);
}

/* -mt800 and -mt414: the original T800 and the T414 lack what the T805
 * and the T425 added to them - pop and lddevid among what the code
 * generator emits (xpuops.h marks pop "T425 only"). */
static int tp_no_t805_ops(void)
{
    return tcc_state && !tp_has(tcc_state, TP_EF_T805OPS);
}
static void Tp_LS(void)     { Tp_OPR_UNARY(TP_OPR_LS); }
static void Tp_LSX(void)    { Tp_OPR_UNARY(TP_OPR_LSX); }
static void Tp_LBX(void)    { Tp_OPR_UNARY(TP_OPR_LBX); }
static void Tp_XSWORD(void) { Tp_OPR_UNARY(TP_OPR_XSWORD); }
static void Tp_XBWORD(void) { Tp_OPR_UNARY(TP_OPR_XBWORD); }
static void Tp_SS(void)     { emit_instr(TP_OP_OPR, TP_OPR_SS); abcreg_pop(2); tp_areg_invalidate(); }
static void Tp_SSUB(void)   { Tp_OPR_BINARY(TP_OPR_SSUB); }

/*
 * T9000 protection / trap secondaries (ISM App.B nfix encodings — gold OReg):
 *   selth   → 60 F9  (OReg = −7)
 *   tret    → 60 FB  (OReg = −5)
 *   goprot  → 60 FA  (OReg = −6)
 *   syscall → 60 F8  (OReg = −8)
 *   ldth    → 2C F2  (OReg = +0xC2, pfix form)
 * Raw nfix0;opr bytes — do not use tp_encode_instr (that pfix-builds App.B hex).
 */
static void __attribute__((unused)) Tp_SELTH(void)
{
	g(0x60);
	g(0xF9);
	tp_areg_invalidate();
}
static void __attribute__((unused)) Tp_TRET(void)
{
	g(0x60);
	g(0xFB);
	abcreg_pop(1);
	tp_areg_invalidate();
}
static void __attribute__((unused)) Tp_GOPROT(void)
{
	g(0x60);
	g(0xFA);
	abcreg_pop(2);
	tp_areg_invalidate();
}
static void __attribute__((unused)) Tp_SYSCALL(void)
{
	g(0x60);
	g(0xF8);
	tp_areg_invalidate();
}
static void __attribute__((unused)) Tp_LDTH(void)
{
	g(0x2C);
	g(0xF2);
	abcreg_push(1);
}

/* ls/ss/lsx address memory in 16-bit units: the byte address must be even.
 *
 * ISM 4.4 "Unaligned address detection" says a 16-bit-object instruction
 * requires bit 0 of the address to be 0, and that Unalign is either trapped or
 * *silently repaired by clearing bit 0*.  This core takes the second option and
 * does not trap: t9_core.sv drives st_strb from st_log[1] alone and ld_half
 * likewise ignores address bit 0.  An odd address therefore reads/writes the
 * containing aligned halfword with no diagnostic — silent corruption, not a
 * fault.  So every ls/lsx/ss must be gated on a *proof* of evenness.
 *
 * Two proofs are available:
 *
 *  - Known-offset form (frame locals, and struct members reached by a constant
 *    displacement from the frame or from a symbol): the displacement's parity
 *    decides, provided the base is even.  Workspace slots are word aligned and
 *    the frame base is word aligned, so an even displacement is an even
 *    address — but only if the object was not laid out by packed rules, which
 *    is what VT_UNALIGNED records.
 *
 *  - Unknown-address form (a `short *` dereference).  ISO C requires a short
 *    lvalue to be naturally aligned, so absent evidence to the contrary this is
 *    the same assumption ARMv5/SPARC make.  The evidence to the contrary is
 *    exactly VT_UNALIGNED: tccgen's '.'/'->' handling sets it when the member
 *    comes from a struct of alignment 1 (__attribute__((packed)),
 *    #pragma pack(1)), sits at an odd member offset, or is reached through an
 *    object already so marked.  Such accesses go back to the byte-pair
 *    synthesis / read-modify-write, which is correct at any alignment.
 *
 * `r` is the SValue's r field (flags included), not just VT_VALMASK. */
static int tp_t9000_half_align_known(int r) {
    return !(r & VT_UNALIGNED);
}
static int tp_t9000_half_ok(int byte_off, int r) {
    return tp_have_t9000_feat(TP_T9K_LOAD16)
        && tp_t9000_half_align_known(r)
        && ((byte_off & 1) == 0);
}
static int tp_t9000_half_ptr_ok(int r, long long disp) {
    /* A constant byte displacement folded onto a symbol or pointer is itself
       decisive when odd, whatever else we believe about the base. */
    if (disp & 1)
        return 0;
    return tp_have_t9000_feat(TP_T9K_LOAD16) && tp_t9000_half_align_known(r);
}
static int tp_t9000_store16_ok(int r, long long disp) {
    if (disp & 1)
        return 0;
    return tp_have_t9000_feat(TP_T9K_STORE16) && tp_t9000_half_align_known(r);
}

/*
 * Emit a patchable local variable access.
 * We reserve 5 bytes: PFIX 0; PFIX 0; PFIX 0; PFIX 0; OP 0
 * This allows offsets up to ~1M words (4MB frames).
 * The epilog patches these with the actual offset, padding shorter
 * encodings with PFIX 0 (safe NOP) at the front.
 * TCC offsets are in bytes, Transputer needs words.
 */
static void emit_local_op(int op, int tcc_offset_bytes) {
    struct local_patch *p;
    static int emit_local_counter = 0;

    /*
     * Must respect nocode_wanted.  Previously this forced nocode off so
     * placeholder bytes always hit the text section — which broke
     * CODE_OFF after `return`: leave_scope already ran cleanups, then
     * prev_scope/block_cleanup tried to emit them again under CODE_OFF.
     * emit_instr suppressed the CALL, but emit_local_op still wrote the
     * LDLP of the cleanup object, clobbering AReg (the return value)
     * immediately before AJW/RET.  Symptom: `return 0` after a cleanup
     * guard returned a stack pointer (e.g. register_filesystem).
     */
    if (nocode_wanted)
        return;

    tp_loc_mark();
    p = tcc_malloc(sizeof(struct local_patch));
    p->instr_offset = ind;
    p->tcc_offset = tcc_offset_bytes;
    p->opcode = op;
    p->proven_nonvolatile_read = 0;
    p->next = local_patches;
    if (!local_patches)
        local_patches_tail = p;
    local_patches = p;

    if (tp_emit_local_debug) {
        fprintf(stderr, "[EMIT_LOCAL_OP_%d] func=%s op=%s tcc_off=%d ind=0x%x\n",
                emit_local_counter++,
                funcname ? funcname : "<unknown>",
                op == TP_OP_LDL ? "LDL" : (op == TP_OP_STL ? "STL" :
                 (op == TP_OP_LDLP ? "LDLP" : "UNKNOWN")),
                tcc_offset_bytes, (unsigned int)ind);
    }

    /* 3-byte placeholder: PFIX 0, PFIX 0, OP 0.
     * Supports offsets up to 4095 words (16KB frame).
     * Adaptive 2/3 sizing was attempted but caused kernel PERCPU init hang
     * (loc-based frame estimation was inaccurate for some code patterns). */
    g((TP_OP_PFIX << 4) | 0);
    g((TP_OP_PFIX << 4) | 0);
    g((op << 4) | 0);
    p->placeholder_size = 3;
    tp_hist_record(TP_HIST_LOCAL, op, tcc_offset_bytes, p->instr_offset, ind);
}

/* Discard local_patch records for code that has been rolled back. The list is
 * built by prepending, so the entries to drop are exactly the leading run with
 * instr_offset >= off. Leaving them behind would make the epilogue patch bytes
 * that now belong to different instructions. */
static void tp_drop_local_patches_from(int off)
{
    while (local_patches && local_patches->instr_offset >= off) {
        struct local_patch *p = local_patches;
        local_patches = p->next;
        tcc_free(p);
    }
}

/* ------------------------------------------------------------------------
 * Loop constant pool
 *
 * A group can retire at most one stage-4 ALU operation, so cycles are bounded
 * below by the count of ldc/adc/eqc/OPR-arithmetic executed - and `ldc` is
 * 8.8% of the instruction stream.  Stages 1-3 (two local reads, two address
 * calculations, two non-local reads per group) are essentially never full, so
 * an `ldc k` that becomes `ldl slot` is not merely one instruction traded for
 * another: it vacates the one contended stage for one that is idle.  In a
 * counted loop the condition's `ldc` is re-materialised every iteration, which
 * makes it one of the loop's handful of stage-4 ops.
 *
 * This is not loop-invariant code motion and needs none of its analysis: a
 * literal is invariant by construction.  All that is required is a point that
 * dominates the loop to initialise a slot from, and a slot nothing else
 * writes.  tccgen knows the first syntactically (the loop preheader, before
 * the entry jump of the rotated layout) and `loc` supplies the second.
 *
 * Single-pass ordering is handled the way the rest of this backend handles it:
 * reserve at the preheader, backfill at the loop close.  Reference sites are
 * *not* placeholders - they emit the ordinary `ldc k`, which is already
 * correct, and are only overwritten at close if pooling was approved.  So
 * every failure mode (a peephole rewrote the site, code was rolled back, a VLA
 * scope rewound `loc`) degrades to exactly the code generated today.
 *
 * Admission requires the constant's LDC encoding to be at least 3 bytes, which
 * is the width of a local-access placeholder.  The rewrite is then byte-for-
 * byte the same length - the saved prefix bytes become PFIX 0 padding - so no
 * reference site ever grows.  Small constants (1-2 byte LDC) are left alone:
 * they would cost fetch bandwidth, which the grouper charges at 4 bytes/cycle.
 * ---------------------------------------------------------------------- */

/* Bytes reserved in the preheader, shared by all entries of one loop nest.
 * Each entry costs its LDC encoding plus a 3-byte STL placeholder, so 16 bytes
 * holds two typical (<= 4096) constants.  Executed once per loop entry. */
#define TP_POOL_RESERVE_DEFAULT 32
#define TP_POOL_MAX_ENTRIES     6
#define TP_POOL_MAX_REFS        512
#define TP_POOL_MIN_LDC_BYTES   3   /* = local placeholder width */

struct tp_pool_entry {
    int value;
    int loc_off;    /* TCC frame offset (negative), or 0 if unallocated */
};

struct tp_pool_ref {
    int off;        /* section offset of the emitted LDC */
    int len;        /* its encoded length */
    int entry;      /* index into tp_pool[] */
};

static int tp_loop_depth;
static int tp_pool_active;
static Section *tp_pool_section;
static int tp_pool_reserve_off;
static int tp_pool_reserve_bytes;
static int tp_pool_bytes_used;
static int tp_pool_n;
static int tp_pool_loc_min;
static struct tp_pool_entry tp_pool[TP_POOL_MAX_ENTRIES];
static struct tp_pool_ref tp_pool_refs[TP_POOL_MAX_REFS];
static int tp_pool_nrefs;

/* TP_POOL=0 disables the pool; TP_POOL=<n> sets the per-loop reservation in
 * bytes.  Both exist so the change can be A/B measured without rebuilding a
 * different tree, the same way TP_NO_CALLOPT works. */
static int tp_pool_reserve_size(void)
{
    static int v = -1;
    if (v < 0) {
        const char *e = tp_knob(TPK_TP_POOL);
        v = e ? atoi(e) : TP_POOL_RESERVE_DEFAULT;
        if (v < 0)
            v = 0;
        if (v > 64)
            v = 64;
    }
    return v;
}

/* TP_POOL_DEBUG=1 reports what each loop hoisted. */
static int tp_pool_debug(void)
{
    static int v = -1;
    if (v < 0)
        v = tp_knob(TPK_TP_POOL_DEBUG) ? 1 : 0;
    return v;
}

static void tp_pool_reset(void)
{
    tp_loop_depth = 0;
    tp_pool_active = 0;
    tp_pool_section = NULL;
    tp_pool_reserve_off = -1;
    tp_pool_reserve_bytes = 0;
    tp_pool_bytes_used = 0;
    tp_pool_n = 0;
    tp_pool_nrefs = 0;
    tp_pool_loc_min = 0;
}

/* Append (not prepend) a local-access placeholder record for bytes already
 * written at `off`.  The list's leading run is what tp_drop_local_patches_from
 * scans for rollback, and it assumes descending instr_offset; these records
 * describe code well behind `ind`, so they must not sit at the head. */
static void tp_local_patch_append(int off, int op, int tcc_offset_bytes)
{
    struct local_patch *p = tcc_malloc(sizeof(struct local_patch));

    p->instr_offset = off;
    p->tcc_offset = tcc_offset_bytes;
    p->opcode = op;
    p->proven_nonvolatile_read = 0;
    p->placeholder_size = 3;
    p->next = NULL;

    if (local_patches_tail)
        local_patches_tail->next = p;
    else
        local_patches = p;
    local_patches_tail = p;
}

/* Write a 3-byte local-access placeholder (PFIX 0, PFIX 0, OP 0) into already
 * emitted code and register it for the epilogue offset patch. */
static void tp_write_local_placeholder(int off, int op, int tcc_offset_bytes)
{
    unsigned char *d = cur_text_section->data + off;
    d[0] = (TP_OP_PFIX << 4) | 0;
    d[1] = (TP_OP_PFIX << 4) | 0;
    d[2] = (unsigned char)((op << 4) | 0);
    tp_local_patch_append(off, op, tcc_offset_bytes);
}

/* Called at a loop preheader.  Only the outermost loop of a nest opens a pool:
 * its reservation dominates every inner loop, so hoisting all the way out
 * costs one initialisation per nest rather than one per inner entry. */
ST_FUNC void tp_loop_pool_open(void)
{
    int n, i;

    if (tp_loop_depth++ != 0)
        return;
    if (nocode_wanted || !cur_text_section)
        return;
    n = tp_pool_reserve_size();
    if (n < TP_POOL_MIN_LDC_BYTES + 3)
        return;

    tp_pool_section = cur_text_section;
    tp_pool_reserve_off = ind;
    tp_pool_reserve_bytes = n;
    tp_pool_bytes_used = 0;
    tp_pool_n = 0;
    tp_pool_nrefs = 0;
    tp_pool_loc_min = loc;
    tp_pool_active = 1;

    /* PFIX 0 decodes as Oreg |= 0 and leaves Oreg at its reset value, so the
     * unused tail is a no-op ahead of whatever follows, including the entry
     * jump's own prefix chain. */
    for (i = 0; i < n; i++)
        g((TP_OP_PFIX << 4) | 0);
    tp_areg_invalidate();
}

/* A label, `case` or `default` has been defined inside the loop.  Control can
 * reach it from outside the loop (a goto, or Duff's device), so the preheader
 * no longer dominates the body and a slot initialised there may be read before
 * it is written.  Abandon the pool: the reference sites still hold the LDC
 * they were emitted with, and the reservation is PFIX 0, which the branch
 * relaxation pass folds away. */
ST_FUNC void tp_loop_pool_unsafe(void)
{
    tp_pool_active = 0;
    tp_pool_n = 0;
    tp_pool_nrefs = 0;
}

/* Record an `ldc v` just emitted at [off, off+len) as a pooling candidate.
 * Returns nothing: the instruction stands as emitted either way. */
static void tp_pool_note_ldc(int off, int len, int v)
{
    int i;

    if (!tp_pool_active || nocode_wanted)
        return;
    if (len < TP_POOL_MIN_LDC_BYTES)
        return;
    if (tp_pool_nrefs >= TP_POOL_MAX_REFS)
        return;

    for (i = 0; i < tp_pool_n; i++)
        if (tp_pool[i].value == v)
            goto found;

    /* New constant: it must fit the reservation alongside its STL. */
    if (tp_pool_n >= TP_POOL_MAX_ENTRIES)
        return;
    if (tp_pool_bytes_used + len + 3 > tp_pool_reserve_bytes)
        return;
    loc = (loc - 4) & -4;
    tp_pool[tp_pool_n].value = v;
    tp_pool[tp_pool_n].loc_off = loc;
    tp_pool_bytes_used += len + 3;
    if (loc < tp_pool_loc_min)
        tp_pool_loc_min = loc;
    i = tp_pool_n++;

found:
    tp_pool_refs[tp_pool_nrefs].off = off;
    tp_pool_refs[tp_pool_nrefs].len = len;
    tp_pool_refs[tp_pool_nrefs].entry = i;
    tp_pool_nrefs++;
}

/* Emit `ldc v` and, inside a pooled loop, register it as a candidate. */
static void tp_ldc_pooled(int v)
{
    int off = ind;
    int len;

    Tp_LDC(v);
    len = ind - off;
    /* Tp_LDC emits MINT for 0x80000000 and nothing at all under nocode_wanted;
     * only a genuine LDC of the expected width is a candidate. */
    if (len > 0 && !nocode_wanted && len == tp_encode_len(TP_OP_LDC, v) &&
        (uint32_t)v != 0x80000000u)
        tp_pool_note_ldc(off, len, v);
}

/* Called at the close of the outermost loop of a nest.  Backfills the
 * reservation with `ldc k; stl slot` and rewrites the reference sites to
 * `ldl slot`, left-padded with PFIX 0 to the width the LDC already occupied.
 * Any inconsistency simply leaves the emitted code as it is. */
ST_FUNC void tp_loop_pool_close(void)
{
    int cursor, i, hoisted = 0;

    if (--tp_loop_depth > 0)
        return;
    if (tp_loop_depth < 0)
        tp_loop_depth = 0;
    if (!tp_pool_active)
        return;
    tp_pool_active = 0;

    if (tp_pool_n == 0 || tp_pool_nrefs == 0)
        return;
    if (tp_pool_section != cur_text_section)
        return;
    /* The reservation must still be live code ahead of `ind`. */
    if (tp_pool_reserve_off < 0 ||
        tp_pool_reserve_off + tp_pool_reserve_bytes > ind ||
        tp_pool_reserve_off + tp_pool_reserve_bytes > cur_text_section->data_offset)
        return;
    /* A VLA scope closing inside the loop rewinds `loc`, which would hand the
     * pool's slots to a later declaration. */
    if (loc > tp_pool_loc_min)
        return;

    cursor = tp_pool_reserve_off;
    for (i = 0; i < tp_pool_n; i++) {
        uint8_t buf[12];
        int len = tp_encode_instr(buf, TP_OP_LDC, tp_pool[i].value);
        if (cursor + len + 3 > tp_pool_reserve_off + tp_pool_reserve_bytes)
            return;                 /* accounting disagreed - emit nothing */
        memcpy(cur_text_section->data + cursor, buf, len);
        tp_write_local_placeholder(cursor + len, TP_OP_STL, tp_pool[i].loc_off);
        cursor += len + 3;
    }

    for (i = 0; i < tp_pool_nrefs; i++) {
        struct tp_pool_ref *rf = &tp_pool_refs[i];
        uint8_t buf[12];
        int len, j;

        if (rf->off < 0 || rf->off + rf->len > ind ||
            rf->off + rf->len > cur_text_section->data_offset)
            continue;
        /* The site must still hold the exact LDC that was recorded: a peephole
         * may have rewritten or rolled back the code since. */
        len = tp_encode_instr(buf, TP_OP_LDC, tp_pool[rf->entry].value);
        if (len != rf->len)
            continue;
        if (memcmp(cur_text_section->data + rf->off, buf, len) != 0)
            continue;
        hoisted++;
        for (j = 0; j < len - 3; j++)
            cur_text_section->data[rf->off + j] = (TP_OP_PFIX << 4) | 0;
        tp_write_local_placeholder(rf->off + len - 3, TP_OP_LDL,
                                   tp_pool[rf->entry].loc_off);
    }

    if (tp_pool_debug())
        fprintf(stderr, "[POOL] %s: %d constant(s) hoisted, %d/%d site(s) rewritten\n",
                funcname ? funcname : "<unknown>", tp_pool_n, hoisted, tp_pool_nrefs);
}

/* Emit byte-accurate address of a local/param slot into AReg.
 * local patching is word-based, so split byte offset into:
 *   word_base (multiple of 4) + byte_remainder (0..3). */
static void emit_local_addr(int tcc_offset_bytes) {
    int word_base = tcc_offset_bytes & ~3;
    int byte_rem = tcc_offset_bytes - word_base;

    emit_local_op(TP_OP_LDLP, word_base);
    abcreg_push(1);
    if (byte_rem)
        Tp_ADC(byte_rem);
}

/*************************************************************/
/* Register allocation */
/*************************************************************/

/* Get primitive type size */
static int get_primitive_type_size(int bt) {
    switch (bt) {
    case VT_BYTE:
    case VT_BOOL:
        return 1;
    case VT_SHORT:
    case VT_BF16:
    case VT_F16:
    case VT_FLOAT16:
        return 2;
    case VT_INT:
    case VT_FLOAT:
    case VT_PTR:
    case VT_FUNC:
        return 4;
    case VT_LLONG:
    case VT_DOUBLE:
    case VT_LDOUBLE:
        return 8;
    default:
        return 4;
    }
}

static int tp_is_float_bt(int bt) {
    return bt == VT_FLOAT || bt == VT_DOUBLE || bt == VT_LDOUBLE;
}

/* XWORD (0x3A, base instruction set) sign-extends a narrow value to a word.
 * It was #ifdef'd out and USE_XWORD was never #define'd anywhere in the tree,
 * so every -mt800 sign extension took the 6-instruction XOR/DIFF fallback.
 * The kernel object has 11512 sites of the 8-bit form and 1209 of the 16-bit.
 *
 * The emulator (t4/p.c, case 0x3a) enforces the ACWG preconditions
 * "bitcount(AReg) = 1 /\ 2*AReg > BReg".  The preceding `LDC mask; AND` is
 * what guarantees the second one, so it MUST be kept - it is not redundant
 * even where the value is known narrow, because gen_cvt_csti reaches these
 * helpers with an arbitrary int.  4 instructions instead of 6. */
#define USE_XWORD 1

/* Sign-extend the low 8 or 16 bits of AReg.  XWORD takes AReg=half_range,
 * BReg=value, which must be in [0, 2*half_range): `narrow` says it already
 * is - a byte straight from lb, or a halfword built from two - so the
 * T805 sequence can skip the mask.  The T9000's xbword/xsword ignore the
 * high bits and subsume the mask either way. */
static void tp_sign_extend(int bits, int narrow) {
    int half = 1 << (bits - 1);

    if (tp_have_t9000_feat(TP_T9K_SEXT)) {
        if (bits == 16)
            Tp_XSWORD();
        else
            Tp_XBWORD();
        return;
    }
    if (!narrow) {
        Tp_LDC((1 << bits) - 1);
        Tp_AND();
    }
#ifdef USE_XWORD
    Tp_LDC(half);
    Tp_XWORD();
#else
    Tp_LDC(half);
    Tp_XOR();
    Tp_LDC(half);
    Tp_DIFF();
#endif
}

static void tp_sign_extend_16_in_areg(void) { tp_sign_extend(16, 0); }
static void tp_sign_extend_8_in_areg(void) { tp_sign_extend(8, 0); }

/* A halfword from the byte address in A on a part without ls (T800, T805,
 * T425, T414): the high byte first and shifted up, then the low byte ORed
 * in, so only the address is parked (in `slot`): 9 instructions where
 * parking the low byte too took 11.  The stack never holds more than the
 * old sequence held, one word above whatever was under the address. */
static void tp_load_half_bytes(int slot)
{
    Tp_STL(slot);
    Tp_LDL(slot);
    Tp_ADC(1);
    Tp_LB();
    Tp_LDC(8);
    Tp_SHL();                   /* A = high byte << 8 */
    Tp_LDL(slot);
    Tp_LB();
    Tp_OR();
}

/* A halfword local at frame byte offset `off`, the same parts.  At an even
 * offset it is one half of a workspace word, so the word is read and the
 * half taken out: ldl w; ldc #ffff; and for the low one, ldl w; ldc 16;
 * shr for the high one (8 and 21 T800 cycles where two lb and a shift cost
 * 27 or more, and no ldlp the post-pass must treat as an escaping address).
 * The bytes of an odd (packed) one are read high first, nothing parked. */
static void tp_load_local_half(int off, int is_unsigned)
{
    int rem = off & 3;

    if (rem == 0 || rem == 2) {
        emit_local_op(TP_OP_LDL, off - rem);
        abcreg_push(1);
        if (rem == 2) {
            Tp_LDC(16);
            Tp_SHR();
            if (!is_unsigned)
                tp_sign_extend(16, 1);
        } else if (is_unsigned) {
            Tp_LDC(0xffff);
            Tp_AND();
        } else
            tp_sign_extend(16, 0);
        return;
    }
    emit_local_addr(off + 1);
    Tp_LB();
    Tp_LDC(8);
    Tp_SHL();
    emit_local_addr(off);
    Tp_LB();
    Tp_OR();
    if (!is_unsigned)
        tp_sign_extend(16, 1);
}

/* Push a floating-point SValue onto the T800 FPU stack (FA/FB/FC). */
/*
 * Words at an address that may not be word aligned.
 *
 * A member of a packed or #pragma pack(N < 4) struct can sit at any byte
 * (VT_UNALIGNED, set by tccgen's member access), and so can a local at a
 * frame byte offset that is not a multiple of four.  ldnl/stnl and ldl/stl
 * ignore the low two address bits, so `p->k` with k at offset 1 read bytes
 * 0..3 and a store wrote over the bytes before it.  lb and sb have no
 * alignment precondition (ISM 4.4): such a word is four bytes, little
 * endian, and a long long or double two such words.
 *
 * The address is parked in a word the access may use: a scratch slot
 * (`slot` >= 0) or a frame temporary at tcc offset `off`.  The callers use
 * a temporary of their own (tp_park_new): the scratch slots can hold a
 * call's staged arguments while one of them is loaded, which emit_instr's
 * staging check rejects.
 */
static int tp_park_new(int bytes)
{
    loc = (loc - bytes) & -4;
    return loc;
}

static int tp_word_unaligned(const SValue *sv)
{
    if (!(sv->r & VT_LVAL))
        return 0;
    if ((sv->r & VT_VALMASK) == VT_LOCAL)
        return (sv->c.i & 3) != 0;
    return (sv->r & VT_UNALIGNED) != 0;
}

static void tp_park_op(int op, int slot, int off)
{
    if (slot >= 0) {
        if (op == TP_OP_LDL) Tp_LDL(slot); else Tp_STL(slot);
    } else {
        emit_local_op(op, off);
        if (op == TP_OP_LDL) abcreg_push(1); else abcreg_pop(1);
    }
}

/* AReg = the word in bytes addr+k .. addr+k+3, the address parked.  Two
 * stack places above whatever is below. */
static void tp_bytes_load(int slot, int off, int k)
{
    int b;
    for (b = 3; b >= 0; b--) {
        tp_park_op(TP_OP_LDL, slot, off);
        if (k + b)
            Tp_ADC(k + b);
        Tp_LB();
        if (b < 3)
            Tp_OR();
        if (b) {
            Tp_LDC(8);
            Tp_SHL();
        }
    }
}

/* Bytes addr+k .. addr+k+3 = the word pushed by `push_val` (a workspace
 * slot, or a frame word at tcc offset `voff` when vslot < 0). */
static void tp_bytes_store(int slot, int off, int k, int vslot, int voff)
{
    int b;
    for (b = 0; b < 4; b++) {
        tp_park_op(TP_OP_LDL, vslot, voff);
        if (b) {
            Tp_LDC(8 * b);
            Tp_SHR();
        }
        tp_park_op(TP_OP_LDL, slot, off);
        if (k + b)
            Tp_ADC(k + b);
        Tp_SB();
    }
}

/* Emit a full-width relocatable address without changing stack tracking.
 * Callers account for the pushed word and apply any symbol addend. */
static void tp_emit_sym_reloc(const SValue *sv, const char *kind, int bt)
{
    int i;

    greloc(cur_text_section, sv->sym, ind, R_TRANSPUTER_LDC_ABS);
    if (kind && tp_gen_sym_filter) {
        const char *name = get_tok_str(sv->sym->v, NULL);
        if (name && strstr(name, tp_gen_sym_filter))
            fprintf(stderr,
                    "GEN_RELOC: kind=%s sym=%s type=%d bt=0x%x file=%s line=%d\n",
                    kind, name, R_TRANSPUTER_LDC_ABS, bt,
                    file ? file->filename : "<unknown>",
                    file ? file->line_num : -1);
    }
    for (i = 0; i < 7; i++)
        g((TP_OP_PFIX << 4) | 0);
    g((TP_OP_LDC << 4) | 0);
}

/* Emit code leaving the ADDRESS of a float operand's storage in AReg,
 * so an FPU non-local load (plain or fused add/mul) can read it
 * directly.  Handles every SValue
 * form gv() accepts for RC_FLOAT except the const-0.0 fast path, which
 * has no address: returns 0 for that one case (caller should emit
 * fpldzerosn/db instead). */
static int tp_emit_float_addr(SValue *sv) {
    int fr = sv->r;
    int v = fr & VT_VALMASK;
    int bt = sv->type.t & VT_BTYPE;
    int use_db = (bt == VT_DOUBLE || bt == VT_LDOUBLE);

    if (fr & VT_LVAL) {
        if (v == VT_LOCAL) {
            emit_local_addr(sv->c.i);
        } else if (v == VT_LLOCAL) {
            emit_local_op(TP_OP_LDL, sv->c.i);
            abcreg_push(1);
        } else if (v == VT_CONST) {
            if (fr & VT_SYM) {
                tp_emit_sym_reloc(sv, NULL, 0);
                abcreg_push(1);
                if (sv->c.i)
                    Tp_ADC(sv->c.i);
            } else {
                Tp_LDC(sv->c.i);
            }
        } else if (v < VT_CONST) {
            /* Mirror load(): a negative c.i marks the canonical spilled
             * copy of the address, which is the stable source after
             * compare/jump chains.  Otherwise the address sits in the
             * pseudo-register; cancel its parking STL when possible. */
            if (sv->c.i < 0) {
                emit_local_op(TP_OP_LDL, sv->c.i);
                abcreg_push(1);
            } else {
                tp_push_reg_or_cancel_stl(v, sv);
            }
        }
        if (tp_word_unaligned(sv)) {
            /* The FPU loads from an aligned address: copy the bytes to a
             * frame temporary of the access's own, words at t and t+4,
             * the address parked at t+8 (no scratch slot: this can run
             * while a call's arguments are being staged). */
            int t = tp_park_new(12);
            tp_park_op(TP_OP_STL, -1, t + 8);
            tp_bytes_load(-1, t + 8, 0);
            tp_park_op(TP_OP_STL, -1, t);
            if (use_db) {
                tp_bytes_load(-1, t + 8, 4);
                tp_park_op(TP_OP_STL, -1, t + 4);
            }
            emit_local_addr(t);
        }
    } else {
        if (v == VT_CONST) {
            if (fr & VT_SYM) {
                tp_emit_sym_reloc(sv, NULL, 0);
                abcreg_push(1);
                if (sv->c.i)
                    Tp_ADC(sv->c.i);
            } else {
                /* +0.0 has a dedicated FPU load - no address.  Test the
                 * bits: -0.0 compares equal to it but keeps its sign. */
                if (use_db) {
                    union { double d; uint64_t u; } u;
                    u.d = sv->c.d;
                    if (u.u == 0)
                        return 0;
                    Tp_LDC((uint32_t)u.u);
                    Tp_STL(FRAME_SLOT_SCRATCH);
                    Tp_LDC((uint32_t)(u.u >> 32));
                    Tp_STL(FRAME_SLOT_SCRATCH + 1);
                } else {
                    union { float f; uint32_t u; } u;
                    u.f = sv->c.f;
                    if (u.u == 0)
                        return 0;
                    Tp_LDC((uint32_t)u.u);
                    Tp_STL(FRAME_SLOT_SCRATCH);
                }
                Tp_LDLP(FRAME_SLOT_SCRATCH);
            }
        } else if (v == VT_LOCAL) {
            emit_local_addr(sv->c.i);
        } else if (v == VT_LLOCAL) {
            emit_local_op(TP_OP_LDL, sv->c.i);
            abcreg_push(1);
        } else if (v < VT_CONST) {
            Tp_LDLP(v);
        }
    }
    return 1;
}

static void tp_push_float_sv(SValue *sv) {
    int bt = sv->type.t & VT_BTYPE;
    int use_db = (bt == VT_DOUBLE || bt == VT_LDOUBLE);

    if (!tp_emit_float_addr(sv)) {
        /* Fast-path: T800 has dedicated zero-load FPU instructions */
        emit_instr(TP_OP_OPR, use_db ? TP_OPR_FPLDZERODB : TP_OPR_FPLDZEROSN);
        fpureg_push(1);
        return;
    }
    emit_instr(TP_OP_OPR, use_db ? TP_FPU_LDNLDB : TP_FPU_LDNLSN);
    abcreg_pop(1);
    fpureg_push(1);
}

/* Store FA (top of FPU stack) into a workspace slot, keeping workspace-backed values coherent. */
static void tp_store_fpu_result_to_slot(int slot, int bt) {
    int use_db = (bt == VT_DOUBLE || bt == VT_LDOUBLE);
    if (slot < 0 || slot >= VT_CONST) {
        fpureg_pop(1);
        return;
    }

    Tp_LDLP(slot);
    emit_instr(TP_OP_OPR, use_db ? TP_FPU_STNLDB : TP_FPU_STNLSN);
    abcreg_pop(1);
    fpureg_pop(1);
}

ST_FUNC void load(int r, SValue *sv) {
    int fr = sv->r;
    int v = fr & VT_VALMASK;
    int bt = sv->type.t & VT_BTYPE;
    int size = get_primitive_type_size(bt);
    int is_unsigned = (sv->type.t & VT_UNSIGNED) != 0 || bt == VT_BF16 || bt == VT_F16
                      || bt == VT_FLOAT16;
    int byte_off;

    static int load_counter = 0;

    if (tp_load_debug) {
        fprintf(stderr, "[LOAD_DEBUG_%d] func=%s fr=0x%x v=0x%x bt=0x%x size=%d r=%d lval=%d sym=%p sv->c.i=%ld sym->c=%d\n",
                load_counter++,
                funcname ? funcname : "<unknown>",
                fr, v, bt, size, r, (fr & VT_LVAL) != 0,
                sv->sym, (long)sv->c.i, (fr & VT_SYM) && sv->sym ? sv->sym->c : -999);
    }

    /* Only VT_SYM makes a constant relocatable.  Without it, sv->sym is a
     * back-pointer at most (enum constants), or not a pointer at all: it
     * shares storage with cmp_op/cmp_r, so a comparison folded to 0/1, or the
     * high word gv() splits off a 64-bit value, carries their bits.  Turning
     * such a "symbol" into a relocation crashed the compiler (`x && 2LL`) or
     * relocated a constant against whatever Sym the stale pointer hit. */
    if (fr & VT_LVAL) {
        if (v == VT_LOCAL) {
            if (tp_debug_check() && sv->sym && (sv->sym->type.t & VT_STATIC)) {
                fprintf(stderr, "[STATIC_LOCAL_DEBUG] Static variable accessed as LOCAL: %s type=0x%x r=0x%x c.i=%ld\n",
                        get_tok_str(sv->sym->v, NULL), sv->sym->type.t, sv->r, (long)sv->c.i);
            }
            /* VT_LVAL | VT_LOCAL: Load value from local workspace slot.
             * On Transputer, LDL loads the WORD directly from workspace.
             * This is different from other architectures where locals are
             * byte-addressed in memory. No dereference (LB/LDNL) needed.
             * Only sign/zero extension for smaller types. */
            byte_off = sv->c.i;
            if (size == 1) {
                emit_local_addr(byte_off);
                if (!is_unsigned && tp_have_t9000_feat(TP_T9K_LOAD8)) {
                    Tp_LBX();            /* load byte extended */
                } else {
                    Tp_LB();                    /* zero-extends */
                    if (!is_unsigned)
                        tp_sign_extend(8, 1);
                }
                Tp_STL(r);
                return;
            } else if (size == 2 && tp_t9000_half_ok(byte_off, sv->r)) {
                /* T9000: one instruction instead of two byte loads,
                   a shift, an OR and the extension dance. */
                emit_local_addr(byte_off);
                if (is_unsigned) Tp_LS(); else Tp_LSX();
                Tp_STL(r);
                return;
            } else if (size == 2) {
                tp_load_local_half(byte_off, is_unsigned);
                Tp_STL(r);
                return;
            } else if (byte_off & 3) {
                /* a member of a packed local: LDL would round the offset
                 * down to its word (tp_word_unaligned) */
                int t = tp_park_new(4);
                emit_local_addr(byte_off);
                tp_park_op(TP_OP_STL, -1, t);
                tp_bytes_load(-1, t, 0);
                Tp_STL(r);
                if (size == 8) {
                    tp_bytes_load(-1, t, 4);
                    Tp_STL(r + 1);
                }
                return;
            }
            emit_local_op(TP_OP_LDL, byte_off);
            if (size == 8) {
                Tp_STL(r); /* Save Low */
                emit_local_op(TP_OP_LDL, byte_off + 4);  /* Load high word */
                Tp_STL(r+1);
                return;
            }
            Tp_STL(r);
        } else {
            /* VT_CONST or register: We have an ADDRESS that needs dereferencing */
            if (v == VT_CONST) {
                if (sv->r & VT_SYM) {
                    /* Use R_TRANSPUTER_LDC_ABS for loading absolute data addresses.
                     * Eight nibbles (7 PFIX + LDC) cover a full 32-bit address. */
                    tp_emit_sym_reloc(sv, "load_lval", bt);
                    /* Add byte offset from symbol (e.g. arr[1] has c.i=1) */
                    if (sv->c.i)
                        Tp_ADC(sv->c.i);
                } else {
                    Tp_LDC(sv->c.i);
                }
            } else if (v == VT_LLOCAL) {
                /* VT_LLOCAL: lvalue address was saved to local slot sv->c.i.
                 * This happens when an lvalue pointer needed to be spilled
                 * to make room for other operations (e.g., RHS evaluation).
                 * Load the saved address from the local slot. */
                emit_local_op(TP_OP_LDL, sv->c.i);
            } else {
                /* Some lvalue addresses survive control-flow in both a
                 * workspace "register" copy and a canonical spilled local
                 * slot (sv->c.i). The spilled slot is the stable source after
                 * compare/jump chains; prefer it when present. */
                if ((fr & VT_LVAL) && sv->c.i < 0) {
                    emit_local_op(TP_OP_LDL, sv->c.i);
                } else {
                    /* Address is in register v (workspace slot). The address
                     * was typically just computed and parked there, so try to
                     * cancel that store rather than reload it — that is what
                     * turns `ADC k; STL r; LDL r; LDNL 0` into `ADC k; LDNL 0`,
                     * which the ADC fold then collapses to `LDNL k/4`: the
                     * T9000 idiom where the displacement rides on the
                     * address-calculation stage instead of the ALU. */
                    tp_push_reg_or_cancel_stl(v, sv);
                }
            }

            /* A word or two of a packed member: from its bytes, the address
             * parked as the halfword path below parks it. */
            if ((size == 4 || size == 8) && (sv->r & VT_UNALIGNED)) {
                int t = tp_park_new(4);
                tp_park_op(TP_OP_STL, -1, t);
                tp_bytes_load(-1, t, 0);
                Tp_STL(r);
                if (size == 8) {
                    tp_bytes_load(-1, t, 4);
                    Tp_STL(r + 1);
                }
                return;
            }

            /* Memory access - dereference the address */
            switch (size) {
            case 1:
                if (!is_unsigned && tp_have_t9000_feat(TP_T9K_LOAD8)) {
                    Tp_LBX();
                    break;
                }
                Tp_LB();                    /* zero-extends */
                if (!is_unsigned)
                    tp_sign_extend(8, 1);
                break;
            case 2:
                if (tp_t9000_half_ptr_ok(sv->r,
                                         (v == VT_CONST) ? (long long)sv->c.i : 0)) {
                    if (is_unsigned) Tp_LS(); else Tp_LSX();
                    break;
                }
                /* Addresses are byte-addressed. A 16-bit field can sit at an
                 * arbitrary byte offset within a word (for example the kernel's
                 * struct printf_spec.s16 precision at offset 6). Rebuild the
                 * halfword from two byte loads instead of using LDNL 0. */
                tp_load_half_bytes(FRAME_SLOT_SCRATCH);

                /* two zero-extended bytes: already 0..0xFFFF */
                if (!is_unsigned)
                    tp_sign_extend(16, 1);
                break;
            case 4:
                Tp_LDNL(0);
                break;
            case 8:
                /* T800 DUP: duplicate address before first dereference so we
                 * don't need to reload it (which costs 1-9 bytes) for the
                 * high word.  Stack: AReg=addr → DUP → AReg=addr, BReg=addr.
                 * After LDNL 0: AReg=low, BReg=addr.  STL r saves low,
                 * shifts addr back to AReg.  LDNLP 1; LDNL 0 loads high. */
                if (tp_t414()) {
                    /* no dup: park the address in the high word's slot */
                    Tp_STL(r + 1);
                    Tp_LDL(r + 1);
                    Tp_LDNL(0);
                    Tp_STL(r);
                    Tp_LDL(r + 1);
                    Tp_LDNL(1);
                    Tp_STL(r + 1);
                    return;
                }
                Tp_DUP();                     /* AReg=addr, BReg=addr */
                Tp_LDNL(0);                  /* AReg=low, BReg=addr */
                Tp_STL(r);                    /* save low; AReg←addr (slid from BReg) */
                Tp_LDNL(1);                  /* LDNL 1: load high word directly at offset */
                Tp_STL(r+1);
                return;
            }
            Tp_STL(r);
        }
    } else {
        /* Immediate / Register value */
        if (v == VT_CONST) {
            if (size == 8 && !(sv->r & VT_SYM)) {
                uint64_t bits;
                if (bt == VT_DOUBLE || bt == VT_LDOUBLE) {
                    union { double d; uint64_t u; } u;
                    u.d = sv->c.d;
                    bits = u.u;
                } else {
                    bits = sv->c.i;
                }
                Tp_LDC((uint32_t)bits);
                Tp_STL(r);
                Tp_LDC((uint32_t)(bits >> 32));
                Tp_STL(r + 1);
                return;
            }
            if (sv->r & VT_SYM) {
                /* Load absolute symbol value into Areg. Reserve full-width
                 * prefix space so the linker can patch large addresses. */
                tp_emit_sym_reloc(sv, "load_imm", bt);
                /* Add byte offset from symbol (e.g. &arr[1] has c.i=1) */
                if (sv->c.i)
                    Tp_ADC(sv->c.i);
            } else {
                if (bt == VT_FLOAT) {
                    union { float f; uint32_t u; } bits;
                    bits.f = sv->c.f;
                    tp_ldc_pooled(bits.u);
                } else {
                    tp_ldc_pooled(sv->c.i);
                }
            }
            Tp_STL(r);
        } else if (v == VT_LOCAL) {
            /* VT_LOCAL without VT_LVAL is the ADDRESS of a local object
             * (e.g. result of '&local'), not the local's stored value. */
            if (tp_load_debug) {
                fprintf(stderr, "[LOAD_VT_LOCAL_ADDR] offset=%ld (sv->c.i=%ld, sym->c=%d) func=%s r=%d sym=%p\n",
                        (long)sv->c.i, (long)sv->c.i,
                        sv->sym ? sv->sym->c : -999,
                        funcname ? funcname : "<unknown>", r, sv->sym);
            }
            emit_local_addr(sv->c.i);
            Tp_STL(r);
        } else if (v == VT_LLOCAL) {
            /* Address spilled in a local slot - load pointer value. */
            emit_local_op(TP_OP_LDL, sv->c.i);
            Tp_STL(r);
        } else if (v == VT_CMP) {
            /* VT_CMP means a prior comparison left a result in AReg.
             * Some ops (GT, EQC) produce strict 0/1. Others (DIFF for
             * TOK_NE) may produce arbitrary nonzero values.
             * The cmp_op field tells us the comparison semantics:
             *   cmp_op & 1 == 1 (odd, e.g. TOK_NE=0x95): non-zero=true
             *   cmp_op & 1 == 0 (even, e.g. TOK_EQ=0x94): zero=true
             * When materializing as a value, normalize to 0/1 with
             * double EQC 0: nonzero→0→1, zero→1→0. */
            if (sv->cmp_op >= 2 && !(sv->cmp_op & 1)) {
                /* Even op (e.g. TOK_EQ): invert the 0/1 value.
                 * EQC 0: AReg = (AReg == 0) ? 1 : 0 */
                emit_instr(TP_OP_EQC, 0);
            } else {
                /* Odd op (e.g. TOK_NE): normalize arbitrary nonzero to 1.
                 * Double EQC 0: nonzero→0→1, zero→1→0. */
                emit_instr(TP_OP_EQC, 0);
                emit_instr(TP_OP_EQC, 0);
            }
            Tp_STL(r);
        } else if (v == VT_JMP || v == VT_JMPI) {
            /* Convert pending jump-encoded boolean into 0/1 in register slot.
             * Mirrors other backends: default value, skip, patch alt value. */
            int t = v & 1;
            int j;
            Tp_LDC(t);
            Tp_STL(r);
            j = gjmp(0);
            gsym(sv->c.i);
            Tp_LDC(t ^ 1);
            Tp_STL(r);
            gsym(j);
        } else if (v != r) {
            Tp_LDL(v);
            Tp_STL(r);
            if (size == 8) {
                /* Use r2 for the high-word source when available.
                 * After lbuild(), r2 may differ from v+1 (e.g. r=11, r2=9).
                 * Falling back to v+1 only when r2 is not a valid register. */
                int hi_src = (sv->r2 < VT_CONST) ? sv->r2 : v + 1;
                Tp_LDL(hi_src);
                Tp_STL(r + 1);
            }
        }
    }
}

/* ---- store-side address displacement ------------------------------------
 *
 * store() runs after the whole right-hand side has been emitted, so by then
 * the destination address has already been computed, offset with ADC and
 * parked in a pseudo-register, with the RHS sitting on top of it:
 *
 *     ldl 22 ; adc 12 ; stl 9 ; ldl 21 ; stl 10 ; ldl 10 ; ldl 9 ; stnl 0
 *
 * The ADC is no longer adjacent to the STNL, so neither the dead-store
 * canceller nor the ADC-into-displacement fold can reach back to it — that is
 * the whole reason field writes never picked up the LDNL/STNL displacement
 * idiom that field reads now use.
 *
 * The shape is what makes this fixable. The address part
 * (`ldl 22 ; adc 12 ; stl 9`) is self-contained: it leaves the eval stack
 * exactly as it found it, its only effect is on pseudo-register 9, and the
 * value part is very often a short run of pure pushes. So the two runs can
 * simply be swapped, and once the address part is last both existing folds
 * apply to it:
 *
 *     ldl 21 ; stl 10 ; ldl 10 ; ldl 22 ; stnl 3
 *
 * That removes the ADC (freeing the one ALU slot a T9000 group gets) and the
 * `stl 9` (freeing both a stage-5 write and a stage-2 address calculation),
 * while keeping the value-before-address order that avoids a REV.
 *
 * The swap is performed by rolling the tail back and re-emitting it, never by
 * moving bytes, which is the same discipline the existing folds use: a raw
 * g() anywhere in the window (relocation or jump placeholder) or a branch
 * target inside it breaks the recorded history chain and the rewrite is
 * skipped. */

/* Pushes that can be replayed verbatim and leave exactly one value on the
 * eval stack. */
static int tp_hist_is_pure_push(const struct tp_hist_ent *h)
{
    if (h->kind == TP_HIST_LOCAL)
        return h->opc == TP_OP_LDL || h->opc == TP_OP_LDLP;
    return h->opc == TP_OP_LDL || h->opc == TP_OP_LDLP || h->opc == TP_OP_LDC;
}

/* Everything the value run is allowed to contain: side-effect free, replayable
 * verbatim, and independent of where in the stream it sits. Jumps, calls and
 * OPRs are deliberately excluded. */
static int tp_hist_is_replayable(const struct tp_hist_ent *h)
{
    if (tp_hist_is_pure_push(h))
        return 1;
    if (h->kind != TP_HIST_INSTR)
        return 0;
    return h->opc == TP_OP_LDNL || h->opc == TP_OP_LDNLP || h->opc == TP_OP_ADC;
}

/* Does replaying this instruction read pseudo-register slot `r`? A local
 * access never can: locals are patched above every fixed slot the function
 * names (tp_frame_reserved_words), so clear of any pseudo-register it uses. */
static int tp_hist_reads_reg(const struct tp_hist_ent *h, int r)
{
    if (h->kind != TP_HIST_INSTR)
        return 0;
    return (h->opc == TP_OP_LDL || h->opc == TP_OP_LDLP) && h->val == r;
}

static void tp_hist_replay(const struct tp_hist_ent *h)
{
    if (h->kind == TP_HIST_LOCAL) {
        emit_local_op(h->opc, h->val);
        abcreg_push(1);
        return;
    }
    switch (h->opc) {
    case TP_OP_LDL:  Tp_LDL(h->val); break;
    case TP_OP_LDLP: Tp_LDLP(h->val); break;
    case TP_OP_LDC:  Tp_LDC(h->val); break;
    case TP_OP_ADC:  Tp_ADC(h->val); break;
    default:         emit_instr(h->opc, h->val); break;   /* LDNL/LDNLP */
    }
}

/* Returns 1 having emitted the complete store, 0 to fall through to the
 * ordinary path. */
static int tp_store_swap_addr_value(int r, SValue *v, int vr, int is_lval)
{
    struct tp_hist_ent val_run[TP_HIST_N];
    struct tp_hist_ent base;
    int i, k, a_stl, a_base, nval;
    int disp = 0;
    int roll_to;

    if (nocode_wanted || tp_hist_n < 4)
        return 0;
    if (r < 0 || r >= NB_REGS || vr < 0 || vr >= NB_REGS || r == vr)
        return 0;
    /* A spilled lvalue address is reloaded from its local slot, not from a
     * pseudo-register, so there is nothing to swap. */
    if (is_lval && v->c.i < 0)
        return 0;

    /* The value's own spill must be the very last thing emitted. */
    if (tp_hist[0].kind != TP_HIST_INSTR || tp_hist[0].opc != TP_OP_STL ||
        tp_hist[0].val != r || tp_hist[0].end != ind)
        return 0;

    /* Walk back through the value run until the address spill shows up. */
    for (i = 1; i < tp_hist_n; i++) {
        if (tp_hist[i].end != tp_hist[i - 1].off)
            return 0;                       /* untracked bytes in between */
        if (tp_hist[i].kind == TP_HIST_INSTR && tp_hist[i].opc == TP_OP_STL) {
            if (tp_hist[i].val != vr)
                return 0;
            break;
        }
        if (!tp_hist_is_replayable(&tp_hist[i]))
            return 0;
        if (tp_hist_reads_reg(&tp_hist[i], vr))
            return 0;                       /* value reads the address slot */
    }
    if (i >= tp_hist_n || i < 2)
        return 0;
    a_stl = i;
    nval = a_stl - 1;

    /* The field offset, if any, and then the base pointer push. */
    k = a_stl + 1;
    if (k >= tp_hist_n || tp_hist[k].end != tp_hist[k - 1].off)
        return 0;
    if (tp_hist[k].kind == TP_HIST_INSTR &&
        (tp_hist[k].opc == TP_OP_ADC || tp_hist[k].opc == TP_OP_LDNLP)) {
        /* A pointer displacement now arrives as LDNLP (stage 2) rather than
         * ADC (stage 4); it carries words where ADC carried bytes.  Without
         * this the store-side fold silently stopped firing and every such
         * store kept an extra instruction. */
        int off = tp_hist[k].val;
        if (tp_hist[k].opc == TP_OP_LDNLP) {
            if (off <= 0 || off > 4095)
                return 0;
            disp = off;
        } else {
            if (off <= 0 || (off & 3) || (off >> 2) > 4095)
                return 0;
            disp = off >> 2;
        }
        if (++k >= tp_hist_n || tp_hist[k].end != tp_hist[k - 1].off)
            return 0;
    }
    a_base = k;
    if (!tp_hist_is_pure_push(&tp_hist[a_base]))
        return 0;
    /* The base is now read after the value's own STL rather than before it. */
    if (tp_hist_reads_reg(&tp_hist[a_base], r))
        return 0;
    if (tp_hist[a_base].off < func_start_ind)
        return 0;
    /* Nothing may still expect to find the address in slot vr: the rewritten
     * sequence never writes it. */
    if (tp_reg_live_on_vstack(vr, v))
        return 0;

    for (i = 0; i < nval; i++)
        val_run[i] = tp_hist[a_stl - 1 - i];     /* oldest first */
    base = tp_hist[a_base];

    roll_to = base.off;
    ind = roll_to;
    cur_text_section->data_offset = ind;
    tp_drop_local_patches_from(roll_to);
    tp_hist_clear();
    tp_areg_invalidate();

    /* Value first: STNL wants AReg = address, BReg = value, and pushing the
     * value first produces that order without a REV. */
    for (i = 0; i < nval; i++)
        tp_hist_replay(&val_run[i]);
    Tp_STL(r);
    /* Usually the assignment's result still names slot r, so this reloads it;
     * when nothing can observe the slot the pair cancels outright. */
    tp_push_reg_or_cancel_stl(r, NULL);
    tp_hist_replay(&base);
    Tp_STNL(disp);
    return 1;
}

/* True when gen_store_addr() below would reach tp_push_reg_or_cancel_stl()
   *and* that call would cancel the STL that has just parked the address in a
   workspace slot.  Mirrors gen_store_addr's dispatch and
   tp_push_reg_or_cancel_stl's own precondition.

   Callers use this to decide operand order: cancelling the STL is worth more
   than saving a REV (it removes an instruction outright rather than trading
   one for another), so when a cancel is available the address must still be
   pushed first. */
static int tp_store_addr_cancels_stl(SValue *v, int vr, int is_lval)
{
    if (vr == VT_CONST || vr == VT_LOCAL || vr == VT_LLOCAL)
        return 0;
    if (is_lval && v->c.i < 0)
        return 0;
    return !nocode_wanted
        && tp_hist_last_stl(vr) >= 0 && tp_hist_last_stl(vr) >= func_start_ind
        && !tp_reg_live_on_vstack(vr, v);
}

static void gen_store_addr(SValue *v, int bt, int vr, int is_lval)
{
    if (vr == VT_CONST) {
        if (v->r & VT_SYM) {
            /* Absolute data address: emit LDC with relocation. */
            tp_emit_sym_reloc(v, "store_addr", bt);
            /* Add byte offset from symbol (e.g. arr[1] has c.i=1) */
            if (v->c.i)
                Tp_ADC(v->c.i);
        } else {
            Tp_LDC(v->c.i);
        }
        } else if (vr == VT_LOCAL) {
            if (tp_debug_check() && v->sym && (v->sym->type.t & VT_STATIC)) {
                fprintf(stderr, "[STATIC_STORE_DEBUG] Static variable stored as LOCAL: %s type=0x%x r=0x%x c.i=%ld\n",
                        get_tok_str(v->sym->v, NULL), v->sym->type.t, v->r, (long)v->c.i);
            }
            /* gen_store_addr should ONLY generate the destination ADDRESS into AReg.
             * The actual store is done by the caller (store() function).
             *
             * VT_LVAL | VT_LOCAL: Local slot IS the destination (VT_LVAL = "this is an lvalue").
             * Use LDLP to get the address of the local slot.
             *
             * VT_LOCAL without VT_LVAL: Local slot contains a POINTER to the destination.
             * Load the pointer value with LDL.
             */
            {
                if (tp_store_debug) {
                    fprintf(stderr, "[GEN_STORE_ADDR] func=%s VT_LOCAL is_lval=%d c.i=%ld -> %s\n",
                            funcname ? funcname : "<unknown>",
                            is_lval, (long)v->c.i,
                            is_lval ? "LDLP (get address of local)" : "LDL (load pointer value)");
                }
            }
            if (is_lval) {
                /* VT_LVAL means "this is the memory location to store to".
                 * Use LDLP to get the address of the local workspace slot. */
                emit_local_addr(v->c.i);
            } else {
                /* No VT_LVAL - the local slot contains a pointer to the destination.
                 * Load the pointer value with LDL. */
                emit_local_op(TP_OP_LDL, v->c.i);
            }
    } else if (vr == VT_LLOCAL) {
        /* VT_LLOCAL: lvalue address was saved to local slot v->c.i.
         * This happens when an lvalue pointer needed to be spilled
         * to make room for other operations (e.g., RHS evaluation).
         * Load the saved address from the local slot. */
        emit_local_op(TP_OP_LDL, v->c.i);
    } else {
        /* As in load(), prefer the canonical spilled lvalue address when the
         * SValue still carries a local spill slot for it. */
        if (is_lval && v->c.i < 0) {
            emit_local_op(TP_OP_LDL, v->c.i);
        } else {
            /* Address is in register vr — same cancellation as in load(). */
            tp_push_reg_or_cancel_stl(vr, v);
        }
    }
}

ST_FUNC void store(int r, SValue *v) {
    int bt = v->type.t & VT_BTYPE;
    int vr = v->r & VT_VALMASK;
    int is_lval = (v->r & VT_LVAL) != 0;
    int size = get_primitive_type_size(bt);
    int value_in_areg = 0;

    if (tp_store_debug) {
        fprintf(stderr, "[STORE] r=%d vr=0x%x is_lval=%d size=%d bt=0x%x c.i=%ld sym=%p r_flags=0x%x\n",
                r, vr, is_lval, size, bt, (long)v->c.i,
                (v->r & VT_SYM) ? (void*)v->sym : NULL, v->r);
    }
    if (tp_trace_func_active() && size <= 2) {
        fprintf(stderr,
                "[tcc] store_trace func=%s r=%d vr=0x%x is_lval=%d size=%d bt=0x%x c.i=%ld depth=%d flags=0x%x\n",
                funcname ? funcname : "<unknown>",
                r, vr, is_lval, size, bt, (long)v->c.i, abcreg_stack_depth, v->r);
    }

    /* A word of a packed member, or of a local at a misaligned offset
     * (tp_word_unaligned): four byte stores.  Like the paths below this
     * writes the one word in r - a long long's second word comes in a second
     * call at +4 - except a value still on the FPU stack, written whole as
     * the FPU path below writes it. */
    if (is_lval && (size == 4 || size == 8) && tp_word_unaligned(v)) {
        int t = tp_park_new(12);            /* address t+8, value t, t+4 */
        int vslot = r, voff = 0, two = 0;
        if (r == TREG_FA && tp_is_float_bt(bt) && fpureg_stack_depth > 0) {
            emit_local_addr(t);             /* to an aligned temporary first */
            emit_instr(TP_OP_OPR, size == 8 ? TP_FPU_STNLDB : TP_FPU_STNLSN);
            abcreg_pop(1);
            fpureg_pop(1);
            vslot = -1;
            voff = t;
            two = size == 8;
        } else if (r == TREG_A || r == TREG_B || r == TREG_C) {
            tp_park_op(TP_OP_STL, -1, t);
            vslot = -1;
            voff = t;
        }
        if ((v->r & VT_VALMASK) == VT_CONST && v->sym && !(v->r & VT_SYM)) {
            v->r |= VT_SYM;
            vr = v->r & VT_VALMASK;
        }
        gen_store_addr(v, bt, vr, is_lval);
        tp_park_op(TP_OP_STL, -1, t + 8);
        tp_bytes_store(-1, t + 8, 0, vslot, voff);
        if (two)
            tp_bytes_store(-1, t + 8, 4, -1, voff + 4);
        abcreg_stack_depth = 0;
        return;
    }

    /* Float registers are stack-like; write through to memory explicitly. */
    if ((r == TREG_FA || r == TREG_FB || r == TREG_FC) && tp_is_float_bt(bt)) {
        int is_db = (bt == VT_DOUBLE || bt == VT_LDOUBLE);
        if (r == TREG_FA && fpureg_stack_depth > 0) {
            gen_store_addr(v, bt, vr, is_lval);
            emit_instr(TP_OP_OPR, is_db ? TP_FPU_STNLDB : TP_FPU_STNLSN);
            fpureg_pop(1);
            abcreg_stack_depth = 0;
            return;
        }
        if (is_db) {
            /* Fallback: value already materialized in workspace slot(s).
             *
             * Store ONLY the word in r.  store() stores one register: for a
             * two-word value the generic code calls us again with r2 and the
             * address advanced by PTR_SIZE (save_reg_upstack: "Each backend's
             * store() handles one word at a time"; vstore() narrows the type
             * to a single word and calls us twice).
             *
             * Writing both words here made the caller's second store() emit a
             * *second* pair one word further on, running past the end of the
             * spill temporary and overwriting the next local.  That silently
             * zeroed the second of two int->float conversions passed as call
             * arguments - `f((float)i, (float)j)` - because the spill of the
             * first argument trampled the source of the second.
             *
             * It also assumed r2 == r + 1, which the 64-bit local path below
             * documents as wrong after lbuild.
             *
             * Value pushed first: gen_store_addr is a single push (plus at
             * most a unary ADC), so the value rides down to BReg and STNL
             * needs no REV — same ordering the word-store path uses. */
            Tp_LDL(r);
            gen_store_addr(v, bt, vr, is_lval);
            Tp_STNL(0);
            abcreg_stack_depth = 0;
            return;
        }
        /* float32 in a workspace slot: fall through to the generic
         * word-store path below — a float reg is a workspace slot like any
         * other, and that path pushes the value first (no REV) and can fold
         * an ADC into the STNL displacement. */
    }

    /* Check if value is on hardware evaluation stack.
     * TREG_A/B/C are explicit eval-stack registers. In practice, gv()
     * often leaves the current single-word RHS in AReg even when the
     * associated pseudo-register slot has not been synchronized yet.
     * Narrow non-local stores are especially sensitive to reloading a
     * stale workspace slot after control-flow-heavy RHS evaluation. */
    if (r == TREG_A || r == TREG_B || r == TREG_C) {
        value_in_areg = 1;
    }

    if ((v->r & VT_VALMASK) == VT_CONST && v->sym && !(v->r & VT_SYM)) {
        /* Ensure symbol constants reserve full LDC placeholder space. */
        v->r |= VT_SYM;
        vr = v->r & VT_VALMASK;
    }

    /*
     * Local scalar lvalues live in workspace words. Storing them through
     * LDLP+SB/halfword sequences can corrupt temporaries and upper bytes:
     * SB only writes 1 byte, leaving upper bytes as garbage. Later LDL
     * loads the full 32-bit word including garbage, corrupting values
     * passed as function arguments.
     * Store directly with STL for ALL local scalars (byte, short, int).
     * AReg already contains the properly promoted int value.
     */
    if (is_lval && vr == VT_LOCAL && size == 4 && ((v->c.i & 3) == 0)) {
        if (!value_in_areg)
            Tp_LDL(r);
        emit_local_op(TP_OP_STL, v->c.i);
        abcreg_pop(1);
        return;
    }
    /* 64-bit local stores: only store the one word in register r.
     * The caller (save_reg_upstack, vstore) handles the second word
     * separately via a second store(r2, ...) call — this avoids the
     * r+1 assumption which breaks when r2 != r+1 (e.g. after lbuild). */
    if (is_lval && vr == VT_LOCAL && size == 8 && ((v->c.i & 3) == 0)) {
        if (value_in_areg) {
            emit_local_op(TP_OP_STL, v->c.i);       /* store word from AReg */
        } else {
            Tp_LDL(r);
            emit_local_op(TP_OP_STL, v->c.i);        /* store word */
        }
        abcreg_stack_depth = 0;
        return;
    }

    /* Word store, value still in a workspace slot: STNL wants AReg=address,
     * BReg=value. Pushing the value *before* the address produces exactly that
     * order; emitting the address first forces a REV to swap them, which cost
     * 47.5k instructions across the kernel. Every gen_store_addr path is a
     * single push (optionally followed by a unary ADC), so a value parked in
     * BReg is left undisturbed. */
    if (size == 4 && !value_in_areg) {
        /* First try to swap the address computation past the value so the
         * field offset can ride on the STNL displacement instead of an ADC. */
        static int swap_off = -1;
        if (swap_off < 0)
            swap_off = tp_knob(TPK_TP_NO_STORE_SWAP) ? 1 : 0;
        if (!swap_off && tp_store_swap_addr_value(r, v, vr, is_lval))
            return;
        Tp_LDL(r);
        gen_store_addr(v, bt, vr, is_lval);
        Tp_STNL(0);
        return;
    }

    /* sb and ss want AReg = address, BReg = value.  When the value is still
     * parked in a workspace slot the historical order (address first, then
     * LDL r) leaves them the wrong way round and costs a REV on every narrow
     * store; pushing the value first produces the right order directly.  The
     * precondition is the one the word store above already relies on: every
     * gen_store_addr path is a single push, optionally followed by a unary
     * ADC, so a value sitting in BReg is left undisturbed.  Nothing is
     * re-ordered in the C sense - both pushes are side-effect-free reads of
     * things already evaluated.
     *
     * Restricted to the cases that end in one sb/ss: the two-sb 16-bit
     * fallback below parks the address first, as it needs it twice.  And
     * skipped when gen_store_addr could instead cancel the STL that parked the
     * address, which saves a whole instruction rather than trading one. */
    if (!value_in_areg
        && (size == 1
            || (size == 2
                && tp_t9000_store16_ok(v->r,
                                       (vr == VT_CONST) ? (long long)v->c.i : 0)))
        && !tp_store_addr_cancels_stl(v, vr, is_lval)) {
        Tp_LDL(r);                              /* AReg = value */
        gen_store_addr(v, bt, vr, is_lval);     /* AReg = addr, BReg = value */
        if (size == 1)
            Tp_SB();
        else
            Tp_SS();
        return;
    }

    /* Generate Destination Address - pushes address onto stack */
    gen_store_addr(v, bt, vr, is_lval);

    /* Store logic */
    switch (size) {
    case 1: /* Byte store */
        /* SB: byte[AReg] = BReg & 0xFF - needs AReg=addr, BReg=value.
         *
         * This used to be a ~37-instruction word read/modify/write for
         * non-volatile lvalues, justified in-source as "keeping packed struct
         * u8 fields coherent".  That reasoning is backwards: the RMW reads
         * four bytes, changes one and writes all four back, so it *clobbers*
         * the three neighbouring bytes if anything else touched them in
         * between - strictly less coherent than a single-byte store, and not
         * atomic either.  ISM p.408 gives sb exactly the required semantics
         * ("store least significant byte of Breg into the byte of memory
         * addressed by Areg"), with AccessViolation as its only error signal:
         * no alignment precondition and no coherence caveat.  The volatile
         * path had been using sb all along.  106 -> 32 bytes on
         * `void f(char*a,int i,char v){a[i]=v;}`. */
        if (value_in_areg) {
            /* After gen_store_addr: AReg=addr, BReg=value - already ordered */
            Tp_SB();
        } else {
            /* After gen_store_addr: AReg=addr.  LDL(r) pushes the value, so
             * AReg=value, BReg=addr; REV restores AReg=addr, BReg=value. */
            Tp_LDL(r);
            Tp_REV();
            Tp_SB();
        }
        break;
    case 2:
        if (tp_t9000_store16_ok(v->r,
                                (vr == VT_CONST) ? (long long)v->c.i : 0)) {
            /* ss: *(uint16 *)AReg = BReg.  Same operand order as SB, so
             * mirror the volatile-byte path exactly.  Replaces a ~55
             * instruction two-byte read/modify/write. */
            if (value_in_areg) {
                Tp_SS();          /* AReg=addr, BReg=value already */
            } else {
                Tp_LDL(r);
                Tp_REV();
                Tp_SS();
            }
            break;
        }
        /* Two byte stores, low byte first (little-endian).  sb has no
         * alignment precondition, so this is right for a halfword at any
         * byte offset - including 2 mod 4, which ldnl/stnl would silently
         * round down to the containing word.  It replaces a two-byte
         * read/modify/write of the containing words (181 -> ~15 bytes),
         * which also rewrote the neighbouring bytes behind the program's
         * back. */
        Tp_STL(FRAME_SLOT_SCRATCH);             /* park the address */
        if (value_in_areg) {
            Tp_STL(FRAME_SLOT_SCRATCH + 1);     /* park the value */
            r = FRAME_SLOT_SCRATCH + 1;
        }
        Tp_LDL(r);
        Tp_LDL(FRAME_SLOT_SCRATCH);
        Tp_SB();                                /* byte[addr] = value */
        Tp_LDL(r);
        Tp_LDC(8);
        Tp_SHR();
        Tp_LDL(FRAME_SLOT_SCRATCH);
        Tp_ADC(1);
        Tp_SB();                                /* byte[addr+1] = value >> 8 */
        break;
    case 4:
        if (value_in_areg) {
            /* Before gen_store_addr: AReg = value
             * After gen_store_addr: AReg = address, BReg = value (pushed down)
             * STNL 0: stores BReg to [AReg] - no REV needed! */
            Tp_STNL(0);
        } else {
            Tp_LDL(r);
            Tp_REV();
            Tp_STNL(0);
        }
        break;
    case 8:
        /* Two-word (64-bit) value: store ONLY the word held in r.
         *
         * store() stores one register, exactly as the generic code expects -
         * vstore() narrows the type to a single word and calls us twice, and
         * save_reg_upstack() calls us with r then r2 at +4 ("Each backend's
         * store() handles one word at a time").  Writing both words here
         * meant the caller's second store() wrote a *second* pair, running
         * one word past the temporary and corrupting the neighbouring local
         * - which silently zeroed the second of two int->float conversions
         * passed as call arguments.
         *
         * It also hardcoded r+1, the very assumption the 64-bit local path
         * above documents as wrong when r2 != r+1 (e.g. after lbuild). */
        if (value_in_areg) {
            /* AReg = address, BReg = value (gen_store_addr pushed it down) */
            Tp_STNL(0);
        } else {
            Tp_LDL(r);
            Tp_REV();
            Tp_STNL(0);
        }
        break;
    }
}

/* NOTE: greloc() is defined in tccgen.c (generic implementation) */

/*************************************************************/
/* Required TinyCC backend functions (minimal stubs) */
/*************************************************************/

ST_FUNC int gfunc_sret(CType *vt, int variadic, CType *ret, int *ret_align, int *regsize) {
    /* Struct return handling */
    *ret_align = 4;
    *regsize = 4;
    return 0;  /* Return on stack */
}

#define TP_JMP_CHAIN_FLAG_COND 0x80000000u
/* Jump placeholder: 4 bytes chain + padding + 1 opcode byte.
 * 5 bytes supports offsets up to ±65K (covers most functions).
 * Was 9 bytes, wasting 4 bytes of PFIX 0 per jump. */
#define TP_JMP_PLACEHOLDER_SIZE 5

ST_FUNC int gjmp(int t)
{
    int r;
    uint32_t next = (uint32_t)t & ~TP_JMP_CHAIN_FLAG_COND;
    int start_off;

    /* If code emission disabled, don't emit - just return chain pointer */
    if (nocode_wanted)
        return t;

    r = ind;
    start_off = r;
    /* Emit placeholder bytes with a 32-bit chain pointer up front. */
    gen_le32(next);
    /* Marker opcode byte so gsym_addr can tell J vs CJ. */
    g((TP_OP_J << 4) | 0);
    jump_patch_add(r, 0);
    tp_trace_emit_bytes("gjmp", start_off, ind);
    return r;
}

ST_FUNC void gjmp_addr(int a) {
    /* Jump to absolute address.
     *
     * Transputer J semantics: new_IPtr = IPtr_after_J + operand
     * where IPtr_after_J = ind + instruction_length.
     * So: operand = target - (ind + instr_len) = target - ind - instr_len
     *
     * The instruction length depends on the operand (negative values need
     * NFIX prefix), so we iterate until the length stabilizes. On oscillation
     * force the longer encoding and front-pad with PFIX 0 so PC_after matches.
     */
    int offset, instr_len, new_len, iter;
    int start = ind;
    int natural_len, front_pad, i;

    /* Initial estimate assuming 1-byte instruction */
    instr_len = 1;
    offset = a - ind - instr_len;

    /* Iterate until length stabilizes. Use larger encoding on oscillation. */
    {
        int prev_len = 0;
        for (iter = 0; iter < 8; iter++) {
            offset = a - ind - instr_len;
            new_len = tp_encode_len(TP_OP_J, offset);
            if (new_len == instr_len)
                break;
            if (new_len == prev_len) {
                /* Oscillating: use the larger length */
                instr_len = (new_len > instr_len) ? new_len : instr_len;
                offset = a - ind - instr_len;
                break;
            }
            prev_len = instr_len;
            instr_len = new_len;
        }
    }

    natural_len = tp_encode_len(TP_OP_J, offset);
    front_pad = instr_len - natural_len;
    if (front_pad < 0)
        front_pad = 0;

    if (tp_genopi_debug) {
        fprintf(stderr, "[GJMP_ADDR] func=%s target=0x%x ind=0x%x offset=%d instr_len=%d natural=%d front_pad=%d\n",
                funcname ? funcname : "?", a, start, offset, instr_len, natural_len, front_pad);
    }
    for (i = 0; i < front_pad; i++)
        g(0x20);
    emit_instr(TP_OP_J, offset);
}

ST_FUNC int gjmp_cond(int op, int t) {
    /* Conditional jump placeholder: chain pointer + padding */
    int r;
    uint32_t next = (uint32_t)t & ~TP_JMP_CHAIN_FLAG_COND;
    int start_off;

    /* If code emission disabled, don't emit - just return chain pointer */
    if (nocode_wanted)
        return t;

    /* gvtst passes op^inv. Use LSB as inversion flag. */
    if (tp_genopi_debug) {
        fprintf(stderr, "[GJMP_COND] func=%s op=0x%x (op&1=%d → %s) ind=0x%x\n",
                funcname ? funcname : "?", op, op & 1,
                (op & 1) ? "EQC 0 (invert)" : "no invert", ind);
    }
    if (op & 1) {
        emit_instr(TP_OP_EQC, 0);
    }
    r = ind;
    start_off = r;
    gen_le32(next | TP_JMP_CHAIN_FLAG_COND);
    /* Marker opcode byte so gsym_addr can tell J vs CJ. */
    g((TP_OP_CJ << 4) | 0);
    jump_patch_add(r, 1);
    abcreg_pop(1);             /* CJ pops condition */
    tp_trace_emit_bytes("gjmp_cond", start_off, ind);
    return r;
}

ST_FUNC int gjmp_append(int n, int t)
{
    void *p;
    int section_size;
    /* Insert vtop->c jump list in t */
    if (n) {
        uint32_t n1 = n, n2, raw;
        section_size = cur_text_section->data_offset;

        /* A link outside the section means the chain is corrupt; following
           or truncating it would leave a jump unpatched. */
        if (n1 + 4 > (uint32_t)section_size)
            tcc_internal_error("jump chain outside the section");

        /* Walk the chain to find the last jump */
        p = cur_text_section->data + n1;
        raw = read32le(p);
        n2 = raw & ~TP_JMP_CHAIN_FLAG_COND;
        while (n2) {
            if (n2 + 4 > (uint32_t)section_size)
                tcc_internal_error("jump chain outside the section");
            n1 = n2;
            p = cur_text_section->data + n1;
            raw = read32le(p);
            n2 = raw & ~TP_JMP_CHAIN_FLAG_COND;
        }
        /* Patch the last jump to point to t */
        write32le(p, ((uint32_t)t & ~TP_JMP_CHAIN_FLAG_COND) | (raw & TP_JMP_CHAIN_FLAG_COND));
        t = n;
    }
    return t;
}

ST_FUNC void gsym_addr(int t, int a)
{
    uint32_t n;
    uint8_t *ptr;
    int offset, saved_ind, saved_nocode;
    int section_size;
    struct jump_patch_meta *meta;

    /* Code at `a` is now a branch target; nothing may be rolled back over it. */
    tp_hist_clear();

    /* Control-flow joins cannot safely rely on the previous virtual ABC/FPU
     * depth. The hardware stack is transient and this backend spills values
     * through workspace across branches. Reset the virtual stack model when
     * materializing a jump target so later stores/loads do not consume stale
     * depth information from the predecessor path. */
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;

    /* Handle jump chaining: walk the chain and patch all jumps */
    while (t) {
        uint32_t raw;
        uint32_t next;
        int is_cond_marker;
        int is_cond_jump;
        int opcode;
        int instr_len, new_len;

        section_size = cur_text_section->data_offset;
        if (t < 0 || t + TP_JMP_PLACEHOLDER_SIZE > section_size)
            tcc_internal_error("jump chain outside the section");
        meta = jump_patch_find(t);
        if (meta && meta->patched) {
            break;  /* Avoid repatching nodes already materialized. */
        }

        ptr = cur_text_section->data + t;
        n = read32le(ptr); /* Read next jump in chain (or 0) */
        raw = n;
        next = n & ~TP_JMP_CHAIN_FLAG_COND;
        is_cond_marker = 0;

        if (t + TP_JMP_PLACEHOLDER_SIZE - 1 < section_size) {
            is_cond_marker = (cur_text_section->data[t + TP_JMP_PLACEHOLDER_SIZE - 1] & 0xF0)
                == (TP_OP_CJ << 4);
        }
        is_cond_jump = meta ? meta->is_conditional
                            : ((raw & TP_JMP_CHAIN_FLAG_COND) || is_cond_marker);

        if (next != 0 && next >= (uint32_t)section_size)
            tcc_internal_error("jump chain outside the section");

        /* Calculate offset for a jump that starts at position t */
        saved_ind = ind;
        ind = t;

        /* CRITICAL: Must clear nocode_wanted for the entire patching operation.
         * emit_instr() clears it internally, but we also need to pad with g() which
         * checks nocode_wanted. If nocode_wanted is set, g() returns without writing
         * or incrementing ind, causing an infinite padding loop. */
        saved_nocode = nocode_wanted;
        nocode_wanted = 0;

        /* Use emit_instr to generate the proper jump encoding.
         *
         * CRITICAL FIX (2026-01-17): Transputer jump offsets are relative to the
         * byte AFTER the instruction (PC after execution), not the start.
         * So: target = PC_after + offset
         * Therefore: offset = target - PC_after = target - (start + instr_len)
         *
         * We need to iterate because the offset value affects instruction length,
         * and instruction length affects the offset calculation.
         */
        opcode = is_cond_jump ? TP_OP_CJ : TP_OP_J;

        /* Initial estimate: assume 1-byte instruction. */
        instr_len = 1;
        offset = a - t - instr_len;

        /* Iterate until length stabilizes.
         * Edge case: when the offset is at the boundary between N-byte and
         * (N+1)-byte encoding, the length oscillates (shorter offset needs
         * longer encoding, which changes the offset back). When this happens,
         * use the LONGER encoding and front-pad with PFIX 0 so that
         * PC_after == t + instr_len (required for offset correctness).
         *
         * Example of the failure mode without front-pad:
         *   a = t+17, offset=16 needs 2 bytes, offset=15 needs 1 byte.
         *   Picking offset=15 but emitting only 1 byte lands at t+16, not t+17
         *   — mid-instruction, skipping the real target (seen as hex/loop
         *   bodies printing return addresses instead of the intended value). */
        {
            int prev_len = 0;
            int oscillated = 0;
            int iter;
            for (iter = 0; iter < 8; iter++) {
                offset = a - t - instr_len;
                new_len = tp_encode_len(opcode, offset);
                if (new_len == instr_len)
                    break;
                if (new_len == prev_len) {
                    /* Oscillating: force the larger length. */
                    instr_len = (new_len > instr_len) ? new_len : instr_len;
                    offset = a - t - instr_len;
                    oscillated = 1;
                    break;
                }
                prev_len = instr_len;
                instr_len = new_len;
            }
            (void)oscillated;
        }

        /* Emit exactly instr_len bytes: front-pad with PFIX 0 if the natural
         * encoding of `offset` is shorter. PFIX 0 leaves OReg at 0 so the
         * final J/CJ nibble is still `offset`. */
        {
            int natural_len = tp_encode_len(opcode, offset);
            int front_pad = instr_len - natural_len;
            int i;
            if (front_pad < 0)
                front_pad = 0;
            if (tp_genopi_debug) {
                fprintf(stderr,
                    "[tcc] gsym_addr t=0x%x a=0x%x offset=%d instr_len=%d natural=%d front_pad=%d cond=%d func=%s\n",
                    t, a, offset, instr_len, natural_len, front_pad,
                    is_cond_jump, funcname ? funcname : "?");
            }
            for (i = 0; i < front_pad; i++)
                g(0x20);
            emit_instr(opcode, offset);
        }
        {
            int emitted_len = ind - t;
            if (tp_trace_func_active()) {
                fprintf(stderr,
                    "[tcc] gsym_addr func=%s n=0x%x a=0x%x offset=%d raw=0x%x cond=%d marker=%d emitted_len=%d\n",
                    funcname, n, a, offset, raw,
                    (raw & TP_JMP_CHAIN_FLAG_COND) != 0, is_cond_marker, emitted_len);
            }
            /* Prefer the planned width when we front-padded; fall back to what
             * was actually written if something went wrong. */
            if (emitted_len != instr_len)
                instr_len = emitted_len;
            /* Past the slot it has overwritten the code that follows. */
            if (emitted_len > TP_JMP_PLACEHOLDER_SIZE)
                tcc_error("branch from 0x%x to 0x%x needs %d bytes, more than "
                          "its %d-byte slot", t, a, emitted_len,
                          TP_JMP_PLACEHOLDER_SIZE);
        }

        /* Pad remaining placeholder bytes with PFIX 0 (0x20) — safe NOP that
         * cannot trigger timeslice descheduling (unlike J 0 = 0x00). */
        {
            while (ind < t + TP_JMP_PLACEHOLDER_SIZE) {
                g(0x20);
            }
        }

        ind = saved_ind;  /* Restore ind */
        nocode_wanted = saved_nocode;  /* Restore nocode_wanted */

        /* Move to next jump in chain.  Look the entry up again rather than
         * trust `meta` across the emission: the array moves when it grows. */
        if (meta) {
            meta = jump_patch_find(t);
            meta->patched = 1;
            meta->target_addr = a;
            meta->instr_len = instr_len;
        }
        t = next;
    }
}

/* NOTE: gsym() is defined in tccgen.c (generic implementation) */

ST_FUNC void gen_fill_nops(int bytes) {
    /* Fill with NOPs using 'pfix 0' (0x20).
     *
     * CRITICAL FIX (2026-01-17): The previous implementation used 'ldc 0'
     * which pushes to the hardware evaluation stack. The Transputer stack
     * is only 3 deep (A, B, C), so multiple NOPs would overflow it.
     *
     * 'pfix 0' shifts OReg left by 4 and ORs 0 — a no-op when OReg is 0.
     * Multiple pfix 0 keep OReg at 0. Unlike 'j 0' (0x00), pfix 0 cannot
     * trigger timeslice descheduling (INMOS TCWG Section 5.6.2).
     */
    while (bytes > 0) {
        g(0x20);  /* pfix 0: true NOP, no timeslice deschedule */
        bytes--;
    }
}

/* Variable-length arrays.
 *
 * TCC's VLA model is a frame pointer plus a stack pointer that the
 * allocation moves.  The transputer has only Wptr, which is both: every
 * local is an ldl/stl at a fixed offset from it, so moving Wptr re-bases
 * every later local access (and the array lands on top of locals 0..N).
 * Wptr therefore never moves.  VLAs come from the running process's arena
 * (lib/transputer/transputer-vla.c), and the stack-pointer save, adjust and restore
 * that tccgen asks for operate on the arena's top instead:
 *
 *   gen_vla_sp_save     before a scope's first VLA: slot = __tp_vla_mark()
 *   gen_vla_alloc       __tp_vla_alloc(size, align): base in A, new top in B
 *   gen_vla_result      VLA's slot = base
 *   gen_vla_sp_save     right after: slot = new top (from the same call)
 *   gen_vla_sp_restore  __tp_vla_release(slot)
 *
 * The arena grows upward, so the base is not the saved top: tccgen keeps it
 * in a slot of its own through gen_vla_result, as it does for Win64.
 *
 * Each step is an ordinary call.  gfunc_call() spills whatever the vstack
 * holds first, which matters for the restores: a `return` or the end of a
 * statement expression releases with the value still on the vstack. */

/* gen_vla_result() ran: the top to save is in REG_IRE2 from the alloc. */
static int tp_vla_top_in_ire2;

static void tp_vla_store_reg(int addr, int r)
{
    vset(&char_pointer_type, VT_LOCAL | VT_LVAL, addr);
    vpushi(0);
    vtop->type = char_pointer_type;
    vtop->r = r;
    vstore();
    vpop();
}

ST_FUNC void gen_vla_sp_save(int addr) {
    if (tp_vla_top_in_ire2) {
        tp_vla_top_in_ire2 = 0;
        if (!nocode_wanted)
            tp_vla_store_reg(addr, REG_IRE2);
        return;
    }
    if (nocode_wanted)
        return;
    vpush_helper_func(tok_alloc_const("__tp_vla_mark"));
    gfunc_call(0);
    tp_vla_store_reg(addr, REG_IRET);
}

ST_FUNC void gen_vla_sp_restore(int addr) {
    if (nocode_wanted)
        return;
    vpush_helper_func(tok_alloc_const("__tp_vla_release"));
    vset(&char_pointer_type, VT_LOCAL | VT_LVAL, addr);
    gfunc_call(1);
}

ST_FUNC void gen_vla_alloc(CType *type, int align) {
    /* vstack top: the size in bytes */
    if (nocode_wanted) {
        vpop();
        return;
    }
    vpushi(align);
    vpush_helper_func(tok_alloc_const("__tp_vla_alloc"));
    vrott(3);
    gfunc_call(2);
}

ST_FUNC void gen_vla_result(int addr) {
    tp_vla_top_in_ire2 = 1;
    if (!nocode_wanted)
        tp_vla_store_reg(addr, REG_IRET);
}

ST_FUNC void gen_struct_copy(int size) {
    /* Called from vstore() for struct assignment when TCC_TARGET_NATIVE_STRUCT_COPY is defined.
     * At entry, vstack has: [dest_copy, dest_ptr, src_ptr].
     * Copy bytes from src_ptr to dest_ptr and keep dest_copy as expression result. */
    SValue sv_src = vtop[0];
    SValue sv_dest = vtop[-1];
    {
        if (tp_structcopy_debug) {
            fprintf(stderr,
                    "[STRUCT_COPY] func=%s size=%d src{r=0x%x v=0x%x c.i=%ld lval=%d bt=0x%x} "
                    "dst{r=0x%x v=0x%x c.i=%ld lval=%d bt=0x%x}\n",
                    funcname ? funcname : "<unknown>", size,
                    sv_src.r, sv_src.r & VT_VALMASK, (long)sv_src.c.i,
                    (sv_src.r & VT_LVAL) != 0, sv_src.type.t & VT_BTYPE,
                    sv_dest.r, sv_dest.r & VT_VALMASK, (long)sv_dest.c.i,
                    (sv_dest.r & VT_LVAL) != 0, sv_dest.type.t & VT_BTYPE);
        }
    }

    /* A one-word struct whose two addresses are word aligned - a union of an
     * int and a float, say - is a load and a store: `ldl s; ldnl k; ldl d;
     * stnl k`, 8 cycles where `ldc 4; move` with its two addresses costs
     * about 18.  The ray tracer's shadeNode copies such a W seven times a
     * call.  vstore() only retyped the operands as pointers, so type.ref is
     * still the struct, whose alignment is ref->r; VT_UNALIGNED marks a
     * member that a packed or #pragma pack container could leave short of
     * it. */
    if (size == 4 &&
        sv_src.type.ref && sv_src.type.ref->r >= 4 &&
        sv_dest.type.ref && sv_dest.type.ref->r >= 4 &&
        !(sv_src.r & VT_UNALIGNED) && !(sv_dest.r & VT_UNALIGNED)) {
        tp_push_sv(&sv_src);
        Tp_LDNL(0);
        tp_push_sv(&sv_dest);
        Tp_STNL(0);
        abcreg_stack_depth = 0;
        vtop -= 2;
        return;
    }

    /* Otherwise T800 MOVE.
     * TCWG defines MOVE as a byte-counted block transfer, so it is safe for
     * packed and unaligned struct fields where LDNL/STNL are not.
     * Struct assignment guarantees non-overlapping source and destination. */
    tp_push_sv(&sv_src);    /* src address -> CReg */
    tp_push_sv(&sv_dest);   /* dest address -> BReg */
    Tp_LDC(size);           /* byte count -> AReg */
    Tp_OPR(TP_OPR_MOVE);    /* MOVE: copy size bytes from src to dest */
    abcreg_pop(3);          /* MOVE consumes all 3 stack values */

    abcreg_stack_depth = 0;

    /* Keep assignment result (dest_copy), drop src_ptr and dest_ptr. */
    vtop--;
    vtop--;
}

/* -mt425/-mt414 soft float.  The T425 and T414 have no FPU, so every
 * floating-point operation becomes a call to the libgcc-named helper that
 * lib/transputer/transputer-softfp.c (in transputer-libtcc1-t425.a and -t414.a)
 * provides.  Float values already live in workspace words, and a float or
 * double is passed and returned exactly like an int or a long long, so
 * these are ordinary calls.  Call `name` on the top `nargs` vstack entries
 * and leave its result, of type t, in their place. */
static void tp_softfp_call(const char *name, int nargs, int t)
{
    int bt = t & VT_BTYPE;

    vpush_helper_func(tok_alloc_const(name));
    vrott(nargs + 1);
    gfunc_call(nargs);
    vpushi(0);
    vtop->type.t = t;
    if (is_float(t)) {
        vtop->r = REG_FRET;
        vtop->r2 = (bt == VT_DOUBLE || bt == VT_LDOUBLE) ? REG_FRET + 1
                                                          : VT_CONST;
    } else {
        vtop->r = REG_IRET;
        vtop->r2 = bt == VT_LLONG ? REG_IRE2 : VT_CONST;
    }
}

/* "sf" for float, "df" for double (and long double, which is REAL64) */
static const char *tp_softfp_mode(int t)
{
    return (t & VT_BTYPE) == VT_FLOAT ? "sf" : "df";
}

static void tp_softfp_opf(int op)
{
    int t = vtop[-1].type.t & (VT_BTYPE | VT_LONG);
    const char *f;
    char name[16];

    switch (op) {
    case '+': f = "add"; break;
    case '-': f = "sub"; break;
    case '*': f = "mul"; break;
    case '/': f = "div"; break;
    case TOK_EQ: f = "eq"; break;
    case TOK_NE: f = "ne"; break;
    case TOK_LT: f = "lt"; break;
    case TOK_LE: f = "le"; break;
    case TOK_GT: f = "gt"; break;
    case TOK_GE: f = "ge"; break;
    default: tcc_error("unsupported float op 0x%x", op);
    }
    if (op == '+' || op == '-' || op == '*' || op == '/') {
        snprintf(name, sizeof name, "__%s%s3", f, tp_softfp_mode(t));
        tp_softfp_call(name, 2, t);
    } else {
        /* The relation holds when __<op>sf2(a, b) <op> 0, NaN included:
         * __eq/ne/lt/le return 1 for an unordered pair, __gt/ge -1. */
        snprintf(name, sizeof name, "__%s%s2", f, tp_softfp_mode(t));
        tp_softfp_call(name, 2, VT_INT);
        vpushi(0);
        gen_op(op);
    }
}

static void tp_softfp_itof(int t)
{
    int sbt = vtop->type.t & (VT_BTYPE | VT_UNSIGNED);
    const char *from = "si";
    char name[24];

    if ((sbt & VT_BTYPE) == VT_LLONG) {
        /* tccgen sends unsigned long long to __floatundi?f itself */
        from = "di";
    } else {
        if (sbt == (VT_INT | VT_UNSIGNED))
            from = "unsi";
        /* a char, short or _Bool is loaded widened; pass it as an int */
        gv(RC_INT);
        vtop->type.t = VT_INT;
    }
    snprintf(name, sizeof name, "__float%s%s", from, tp_softfp_mode(t));
    tp_softfp_call(name, 1, t & (VT_BTYPE | VT_LONG));
}

static void tp_softfp_ftoi(int t)
{
    const char *mode = tp_softfp_mode(vtop->type.t);
    char name[24];

    if ((t & VT_BTYPE) == VT_LLONG)     /* unsigned goes to __fixuns?fdi */
        snprintf(name, sizeof name, "__fix%sdi", mode);
    else
        snprintf(name, sizeof name, "__fix%s%ssi",
                 (t & VT_UNSIGNED) ? "uns" : "", mode);
    tp_softfp_call(name, 1, t);
}

static void tp_softfp_ftof(int t)
{
    int sbt = vtop->type.t & VT_BTYPE;
    int dbt = t & VT_BTYPE;

    if (sbt == VT_FLOAT && dbt != VT_FLOAT)
        tp_softfp_call("__extendsfdf2", 1, t & (VT_BTYPE | VT_LONG));
    else if (sbt != VT_FLOAT && dbt == VT_FLOAT)
        tp_softfp_call("__truncdfsf2", 1, VT_FLOAT);
    else
        vtop->type.t = t;           /* double <-> long double: both REAL64 */
}

/* Round the REAL64 in FA to REAL32. */
static void tp_fpu_r64tor32(void) {
    if (tp_have_t9000_feat(TP_T9K_FPU)) {
        emit_instr(TP_OP_OPR, TP_OPR_FPR64TOR32);
    } else {
        Tp_LDC(TP_FPENTRY_R64TOR32);
        emit_instr(TP_OP_OPR, TP_OPR_FPENTRY);
        abcreg_pop(1);
    }
}

ST_FUNC void gen_cvt_itof(int t) {
    int dbt = t & VT_BTYPE;
    int is_db = (dbt == VT_DOUBLE || dbt == VT_LDOUBLE);
    int sbt = vtop->type.t & (VT_BTYPE | VT_UNSIGNED);
    SValue sv;

    if (tp_soft_float()) {
        tp_softfp_itof(t);
        return;
    }

    /* The result is written to REG_FRET, which also holds float function-call
     * return values. If another vstack operand (e.g. the divisor in
     * `int / float_call()`) still lives in REG_FRET, spill it first — otherwise
     * both operands alias REG_FRET and e.g. `a / f()` computes a/a.
     * A double result also takes REG_FRET+1 (TREG_FB), where a pending
     * float may live - evict that too, as gen_opf does for its pair. */
    save_reg(REG_FRET);
    if (is_db)
        save_reg_upstack(REG_FRET + 1, 0);

    if ((sbt & VT_BTYPE) == VT_LLONG) {
        /* Signed 64-bit (tccgen sends unsigned long long to __floatundi?f,
         * which in turn uses this path).  The ISM's (11.11.3) sequence:
         * value = hi * 2^32 + (unsigned)lo, where both halves load exactly
         * from their workspace slots, expinc32 scales hi exactly, and the
         * add rounds once.  On T9000 fpadddbsn rounds straight to REAL32;
         * the T800 rounds to REAL64 first, so its float result can be one
         * ulp off, and only when |value| > 2^53. */
        int lo, hi;
        gv(RC_INT);
        lo = vtop->r & VT_VALMASK;
        hi = vtop->r2 & VT_VALMASK;
        abcreg_stack_depth = 0;
        fpureg_stack_depth = 0;
        Tp_LDLP(lo);
        emit_instr(TP_OP_OPR, TP_OPR_FPB32TOR64);
        abcreg_pop(1);
        fpureg_push(1);
        Tp_LDLP(hi);
        emit_instr(TP_OP_OPR, TP_OPR_FPI32TOR64);
        abcreg_pop(1);
        fpureg_push(1);
        if (tp_have_t9000_feat(TP_T9K_FPU)) {
            emit_instr(TP_OP_OPR, TP_OPR_FPEXPINC32);
        } else {
            Tp_LDC(TP_FPENTRY_EXPINC32);
            emit_instr(TP_OP_OPR, TP_OPR_FPENTRY);
            abcreg_pop(1);
        }
        if (!is_db && tp_have_t9000_feat(TP_T9K_FPU)) {
            emit_instr(TP_OP_OPR, TP_OPR_FPADDDBSN);
            fpureg_pop(1);
        } else {
            emit_instr(TP_OP_OPR, TP_FPU_ADD);
            fpureg_pop(1);
            if (!is_db)
                tp_fpu_r64tor32();
        }
        goto store;
    }

    /* Source integer is workspace-backed; push it explicitly. */
    sv = vtop[0];
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;
    /* The FPU reads memory itself.  A word-aligned local lvalue or an
     * already materialized workspace value needs no scratch copy. */
    {
        int v = sv.r & VT_VALMASK;
        if ((sbt & VT_BTYPE) == VT_INT && !(sv.r & VT_MUSTCAST) &&
            (sv.r & VT_LVAL) && v == VT_LOCAL &&
            !(sv.r & VT_UNALIGNED) && !(sv.c.i & 3)) {
            emit_local_addr(sv.c.i);
        } else if ((sbt & VT_BTYPE) == VT_INT &&
                   !(sv.r & (VT_LVAL | VT_MUSTCAST)) &&
                   v >= 0 && v < VT_CONST) {
            Tp_LDLP(v);
        } else {
            tp_push_sv(&sv);
            Tp_STL(FRAME_SLOT_SCRATCH);
            Tp_LDLP(FRAME_SLOT_SCRATCH);
        }
    }

    if (sbt == (VT_INT | VT_UNSIGNED)) {
        /* fpi32tor* would read bit 31 as a sign.  fpb32tor64 loads the
         * word as unsigned, exactly; a float result is rounded once. */
        emit_instr(TP_OP_OPR, TP_OPR_FPB32TOR64);
        abcreg_pop(1);
        fpureg_push(1);
        if (!is_db)
            tp_fpu_r64tor32();
        goto store;
    }

    /* T800 integer->float converters load INT32 from address in AReg.  They
     * round to nearest: every FP instruction resets the rounding mode to
     * that (ISM 11.6), so no fpurn is needed first. */
    emit_instr(TP_OP_OPR, is_db ? TP_FPU_I32TOR64 : TP_FPU_I32TOR32);
    abcreg_pop(1);
    fpureg_push(1);

store:
    /* Keep float values workspace-backed like the rest of the backend. */
    tp_store_fpu_result_to_slot(REG_FRET, dbt);
    vtop->r = REG_FRET;
    vtop->r2 = is_db ? (REG_FRET + 1) : VT_CONST;
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;
}

ST_FUNC void gen_cvt_ftoi(int t) {
    SValue sv = vtop[0];

    if (tp_soft_float()) {
        tp_softfp_ftoi(t);
        return;
    }

    if ((t & VT_BTYPE) == VT_LLONG || t == (VT_INT | VT_UNSIGNED)) {
        /* fprtoi32 covers only int32.  This is the ISM's (11.11.2)
         * double-word sequence:
         *     fprz; fpint; fpdup; ldlp lo; fpstnli32;
         *     fpexpdec32; ldlp hi; fpstnli32
         * fpstnli32 floors to int64 and stores the low word, so once fpint
         * has truncated, it gives the low word exactly and floor(x / 2^32)
         * the high word.  unsigned int is the low word alone, which is
         * right for all of [0, 2^32), beyond fprtoi32's range. */
        int is_ll = (t & VT_BTYPE) == VT_LLONG;

        save_reg(REG_IRET);
        if (is_ll)
            save_reg(REG_IRE2);
        abcreg_stack_depth = 0;
        fpureg_stack_depth = 0;
        tp_push_float_sv(&sv);
        if (tp_have_t9000_feat(TP_T9K_FPU)) {
            emit_instr(TP_OP_OPR, TP_OPR_FPRZ);
        } else {
            Tp_LDC(TP_FPENTRY_RZ);
            emit_instr(TP_OP_OPR, TP_OPR_FPENTRY);  /* fpurz */
            abcreg_pop(1);
        }
        emit_instr(TP_OP_OPR, TP_OPR_FPINT);
        /* The T9000's fprz holds until fprn (the T800's fpurz governs the
         * next FP instruction only): put round-to-nearest back for the
         * code after the conversion. */
        if (tp_have_t9000_feat(TP_T9K_FPU))
            emit_instr(TP_OP_OPR, TP_OPR_FPRN);
        if (is_ll) {
            emit_instr(TP_OP_OPR, TP_OPR_FPDUP);
            fpureg_push(1);
        }
        Tp_LDLP(REG_IRET);
        emit_instr(TP_OP_OPR, TP_OPR_FPSTNLI32);
        abcreg_pop(1);
        fpureg_pop(1);
        if (is_ll) {
            if (tp_have_t9000_feat(TP_T9K_FPU)) {
                emit_instr(TP_OP_OPR, TP_OPR_FPEXPDEC32);
            } else {
                Tp_LDC(TP_FPENTRY_EXPDEC32);
                emit_instr(TP_OP_OPR, TP_OPR_FPENTRY);  /* fpuexpdec32 */
                abcreg_pop(1);
            }
            Tp_LDLP(REG_IRE2);
            emit_instr(TP_OP_OPR, TP_OPR_FPSTNLI32);
            abcreg_pop(1);
            fpureg_pop(1);
        }
        vtop->r = REG_IRET;
        vtop->r2 = is_ll ? REG_IRE2 : VT_CONST;
        abcreg_stack_depth = 0;
        fpureg_stack_depth = 0;
        return;
    }

    /* The result is written to REG_IRET, which also holds integer
     * function-call return values.  If another vstack operand still lives
     * there - `icall() + (int)f` evaluates the call first, leaving its
     * pending result named REG_IRET - writing the conversion into the same
     * slot makes both operands alias it and the expression computes
     * conv OP conv.  Exactly the REG_FRET bug fixed in gen_cvt_itof above. */
    save_reg(REG_IRET);

    /* Source float is workspace-backed; materialize FA from vtop first. */
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;
    tp_push_float_sv(&sv);

    /* C casts float->int truncate toward zero.  T800 FPU default is
     * round-to-nearest.  Use fpurz then fprtoi32 to round and convert
     * to int32 in one step, then fpstnli32 to store.
     * Pattern from Helios t8sim.a real32toint32:
     *   fpurz; fprtoi32; fpstnli32 */
    if (tp_have_t9000_feat(TP_T9K_FPU)) {
        emit_instr(TP_OP_OPR, TP_OPR_FPRZ);  /* direct: set round-to-zero */
    } else {
        Tp_LDC(TP_FPENTRY_RZ);
        emit_instr(TP_OP_OPR, TP_OPR_FPENTRY);  /* fpurz */
        abcreg_pop(1);
    }
    emit_instr(TP_OP_OPR, TP_OPR_FPRTOI32); /* round and convert to int32 */
    /* The T800's fpurz applies to fprtoi32 alone, but the T9000's fprz
     * holds until fprn: without this every FP operation after an (int)
     * conversion rounded toward zero. */
    if (tp_have_t9000_feat(TP_T9K_FPU))
        emit_instr(TP_OP_OPR, TP_OPR_FPRN);
    Tp_LDLP(FRAME_SLOT_SCRATCH);
    emit_instr(TP_OP_OPR, TP_OPR_FPSTNLI32);
    abcreg_pop(1);
    fpureg_pop(1);
    Tp_LDL(FRAME_SLOT_SCRATCH);
    Tp_STL(REG_IRET);
    vtop->r = REG_IRET;
    vtop->r2 = VT_CONST;
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;
}

ST_FUNC void gen_cvt_ftof(int t) {
    int sbt = vtop->type.t & VT_BTYPE;
    int dbt = t & VT_BTYPE;
    int dst_is_db = (dbt == VT_DOUBLE || dbt == VT_LDOUBLE);
    SValue sv = vtop[0];

    if (tp_soft_float()) {
        tp_softfp_ftof(t);
        return;
    }

    /* Same aliasing hazard as gen_cvt_itof/gen_cvt_ftoi: the result lands in
     * REG_FRET(+1), which may still hold a pending float call result on the
     * vstack (`dcall() + (double)f32var`).  Spill it first - both words of
     * a double result, as gen_cvt_itof does. */
    save_reg(REG_FRET);
    if (dst_is_db)
        save_reg_upstack(REG_FRET + 1, 0);

    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;
    tp_push_float_sv(&sv);

    if (dst_is_db && sbt == VT_FLOAT) {
        /* float32 -> float64 */
        if (tp_have_t9000_feat(TP_T9K_FPU)) {
            emit_instr(TP_OP_OPR, TP_OPR_FPR32TOR64);  /* direct, exact */
        } else {
            Tp_LDC(TP_FPENTRY_R32TOR64);
            emit_instr(TP_OP_OPR, TP_OPR_FPENTRY);
            abcreg_pop(1);
        }
    } else if (!dst_is_db && (sbt == VT_DOUBLE || sbt == VT_LDOUBLE)) {
        /* float64 -> float32 rounds to nearest, the default mode after
         * any FP instruction (ISM 11.6) */
        tp_fpu_r64tor32();
    }

    tp_store_fpu_result_to_slot(REG_FRET, dbt);
    vtop->r = REG_FRET;
    vtop->r2 = dst_is_db ? (REG_FRET + 1) : VT_CONST;
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;
}

#if PTR_SIZE == 8
ST_FUNC void gen_cvt_sxtw(void) {
    /* Sign extend word - Transputer is 32-bit, may not need */
    /* Stub */
}
#endif

/* char/short to int conversion with truncation and optional sign extension.
 * Called from tccgen.c when narrowing cast (int->signed char, int->short, etc.)
 * needs explicit truncation and sign extension.
 *
 * After gv(RC_INT), value is in AReg (and workspace slot r).
 * For unsigned: mask with AND to truncate.
 * For signed: tp_sign_extend_*_in_areg() handles AND + sign extend. */
ST_FUNC void gen_cvt_csti(int t) {
    int r = gv(RC_INT);
    int slot = r & VT_VALMASK;

    /* Reload from workspace - always-STL means value is in workspace, not AReg */
    Tp_LDL(slot);

        if ((t & VT_BTYPE) == VT_BYTE) {
        if (t & VT_UNSIGNED) {
            Tp_LDC(0xFF);
            Tp_AND();
        } else {
            tp_sign_extend_8_in_areg(); /* includes AND 0xFF */
        }
    } else if ((t & VT_BTYPE) == VT_SHORT) {
        if (t & VT_UNSIGNED) {
            Tp_LDC(0xFFFF);
            Tp_AND();
        } else {
            tp_sign_extend_16_in_areg(); /* includes AND 0xFFFF */
        }
    }

    Tp_STL(slot);  /* store back to workspace */
}



/* transputer_store_const_local_ptr was deleted here: it had no callers, and
 * its local-slot math predated the dynamic frame policy (it hardcoded
 * FRAME_SLOT_MIN_LOCALS and added a word count to a byte address), so any
 * future caller would have miscompiled silently. */

/*************************************************************/
/* Assembly support (called by tccgen.c) */
/*************************************************************/

ST_FUNC void gen_expr32(ExprValue *pe) {
    /*
     * Generate 32-bit expression for assembly .long/.word directives.
     * This emits a 32-bit value with an optional relocation.
     *
     * CRITICAL: Must call gen_le32() to emit bytes and advance ind,
     * otherwise multiple .long directives will all get the same address!
     */
    if (pe->sym) {
        /* Emit relocation for symbol reference */
        greloc(cur_text_section, pe->sym, ind,
               pe->pcrel ? R_TRANSPUTER_PC_RELATIVE : R_TRANSPUTER_32);
    }
    /* Always emit 4 bytes and advance instruction pointer */
    gen_le32(pe->v + (pe->pcrel ? ind : 0));
}

ST_FUNC void ggoto(void) {
    int target_r;

    /* Indirect jump target expression is on vtop. */
    gv(RC_INT);
    target_r = vtop->r & VT_VALMASK;

    /* Build a temporary RET frame:
     *   [0] target IPtr
     *   [1..3] restored A/B/C (unused here -> 0)
     * RET then jumps to target and restores Wptr (+4 words), giving
     * us an indirect jump without perturbing the current call frame. */
    Tp_LDL(target_r);
    emit_instr(TP_OP_AJW, -4);
    emit_instr(TP_OP_STL, 0);
    Tp_LDC(0); emit_instr(TP_OP_STL, 1);
    Tp_LDC(0); emit_instr(TP_OP_STL, 2);
    Tp_LDC(0); emit_instr(TP_OP_STL, 3);
    Tp_OPR(TP_OPR_RET);

    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;
    vtop--;
}

ST_FUNC int asm_parse_regvar(int t) {
    /* Parse register variable in assembly stub */
    return -1;  /* Register not found */
}

ST_FUNC void asm_compute_constraints(ASMOperand *operands, int nb_operands,
                                     int nb_outputs, const uint8_t *clobber_regs, int *pout_reg) {
    /* The transputer is a stack machine with no named registers, so full
     * register-level inline asm is not supported.  We DO support the common
     * idioms that appear in uClibc/libc: an empty (or no-op) template with
     * matching-constraint operands, e.g.
     *     __asm__("" : "=r"(out) : "0"(in))   // out = in, defeats optimization
     * The critical correctness requirement is that OUTPUT operands must be
     * assigned — leaving them uninitialized produces layout-dependent garbage
     * (this is exactly what hung uClibc's exit(): not_null_ptr() returned a
     * garbage nonzero, causing a call through a NULL __exit_cleanup pointer).
     *
     * Here we only resolve matching constraints (tie each such input to its
     * output); the actual copy happens in asm_gen_code(). */
    int i;

    for (i = 0; i < nb_operands; i++) {
        operands[i].input_index = -1;
        operands[i].ref_index = -1;
        operands[i].reg = -1;
    }
    for (i = nb_outputs; i < nb_operands; i++) {
        ASMOperand *op = &operands[i];
        const char *str = op->constraint;

        /* A matching constraint is a digit naming an output operand. */
        if (str[0] >= '0' && str[0] <= '9') {
            int k = str[0] - '0';
            if (k >= 0 && k < nb_outputs) {
                op->ref_index = k;
                operands[k].input_index = i;
            }
        }
    }
    if (pout_reg)
        *pout_reg = -1;
}

ST_FUNC void subst_asm_operand(CString *add_str, SValue *sv, int modifier) {
    /* Operand substitution into the asm template text is unsupported (the
     * supported idioms use empty templates with no %N references). */
}

ST_FUNC void asm_gen_code(ASMOperand *operands, int nb_operands,
                         int nb_outputs, int is_output,
                         uint8_t *clobber_regs, int out_reg) {
    /* Input-load pass does nothing: supported templates are no-ops, and the
     * value transfer is handled entirely on the output pass below. */
    if (!is_output)
        return;

    /* Output pass: assign each output operand.  For a matching-constraint
     * output (tied to an input), copy the input's value into the output
     * lvalue — this realizes the __asm__("":"=r"(out):"0"(in)) idiom as
     * out = in and, crucially, never leaves the output uninitialized. */
    {
        int i;
        for (i = 0; i < nb_outputs; i++) {
            ASMOperand *op = &operands[i];
            if (op->input_index >= 0 && op->input_index < nb_operands) {
                SValue *in_vt = operands[op->input_index].vt;
                int r;

                vpushv(in_vt);      /* copy the tied input onto the vstack */
                r = gv(RC_INT);     /* materialize it into a register      */
                vtop--;             /* drop the copy; r still holds it     */
                store(r, op->vt);   /* out = in                            */
            }
        }
    }
}

ST_FUNC void asm_clobber(uint8_t *clobber_regs, const char *str) {
    /* Handle assembly clobber list stub */
}

/*************************************************************/
/* Transputer builtin functions (called by tccgen.c) */
/*************************************************************/

/* Channel operations
 *
 * After parse_builtin_params("eee"), vstack has:
 *   vtop[0]  = arg3 (last parsed)
 *   vtop[-1] = arg2
 *   vtop[-2] = arg1
 *
 * To load into hardware regs A, B, C where A=arg1, B=arg2, C=arg3:
 *   Load arg3 first (becomes C after subsequent loads)
 *   Load arg2 (becomes B)
 *   Load arg1 (becomes A)
 *
 * gv(RC_INT) loads vtop into AReg (emitting LDL/LDC), pushing previous A->B->C
 */

static void gen_builtin_outbyte(void) {
    /* __outbyte(chan, byte): A=byte(1), B=chan(0) */
    tp_emit_2op(TP_OPR_OUTBYTE, 0, 1);
}

static void gen_builtin_outword(void) {
    /* __outword(chan, word): A=word(1), B=chan(0) */
    tp_emit_2op(TP_OPR_OUTWORD, 0, 1);
}

/* ALT guard enable/disable.
 *
 * These take their operands in fixed eval-stack positions (ISM
 * SS7.6, and the emulator agrees - transputer/t4/p.c cases 0x2E/0x2F/0x30
 * and 0x47/0x48/0x49):
 *
 *   enbc  AReg=guard  BReg=channel
 *   enbt  AReg=guard  BReg=time
 *   enbs  AReg=guard
 *   disc  AReg=code   BReg=guard   CReg=channel  -> AReg=selected
 *   dist  AReg=code   BReg=guard   CReg=time     -> AReg=selected
 *   diss  AReg=code   BReg=guard                 -> AReg=selected
 *
 * `code` is the branch offset an occam compiler would stash in Temp for
 * altend to jump through.  A C caller has no jump table, so it passes the
 * guard index and reads the boolean result instead - only the first ready
 * guard reports true, which is exactly the arbitration ALT provides.
 *
 * The previous versions of these took no channel operand at all (enbc took
 * one, in AReg where the guard belongs), which made every ALT built from C
 * enable the wrong thing.  That is why link multiplexing could not be
 * expressed here.
 */

/* Memory operations */
static void gen_builtin_move(void) {
    /* MOVE: __move(dest, src, length)
     * vstack: vtop[0]=length, vtop[-1]=src, vtop[-2]=dest
     * MOVE expects eval stack: CReg=src, BReg=dest, AReg=length
     * idx mapping: arg0=dest, arg1=src, arg2=length -> C=1, B=0, A=2 */
    tp_emit_3op(TP_OPR_MOVE, 1, 0, 2);
}

/*
 * Helper: materialize 3 vstack args into workspace slots, then push
 * them onto the eval stack in the order needed for a 3-operand instruction.
 *
 * vstack layout:  vtop[0]=arg2, vtop[-1]=arg1, vtop[-2]=arg0
 * Eval stack result:  CReg=first, BReg=second, AReg=third
 *
 * The caller specifies which vstack index maps to C, B, A positions.
 * For MOVE:        C=src(1), B=dest(0), A=len(2)  -> c_idx=1, b_idx=0, a_idx=2
 * For MOVE2DINIT:  C=src_stride(2), B=dest_stride(1), A=length(0)
 * For MOVE2DALL:   C=src(2), B=dest(1), A=width(0)
 *
 * Note that AReg takes *different* quantities for the two 2D instructions -
 * move2dinit wants the row count, move2dall the row width in bytes (ISM
 * §7.15.1, pp.386-387).  The comments here previously had them the other way
 * round, and fbwm.c was written against the comments, so every 2D blit came
 * out transposed.
 */
/*
 * Helper: materialize the single vstack arg and push it onto the eval stack.
 *
 * gv() alone is not enough.  On this backend gv(RC_INT) means "the value now
 * lives in a workspace slot", not "the value is in the hardware AReg" - so
 * the old `vpushv(&vtop[0]); gv(RC_INT); vtop--;` idiom emitted the
 * instruction with nothing staged at all, and it operated on whatever the
 * eval stack happened to hold.  For __runp that meant scheduling a garbage
 * descriptor.  tp_push_sv() is what actually emits the LDL.
 */
/* opr with a negative operation - the T9000 VCP instructions (ISM App.B):
 * nfix k; opr x leaves OReg = (~k << 4) | x.  emit_instr() treats an opr
 * operand as unsigned, so these need their own bytes, like Tp_SELTH(). */
static void tp_emit_opr(int opcode)
{
    if (opcode >= 0) {
        emit_instr(TP_OP_OPR, opcode);
        return;
    }
    g((TP_OP_NFIX << 4) | (~((unsigned)opcode >> 4) & 0xF));
    g((TP_OP_OPR << 4) | (opcode & 0xF));
}

static int tp_operand_is_simple(const SValue *sv);
static void tp_push_simple(const SValue *sv);
static void tp_push_sym_addr(const SValue *sv);

/* Can a builtin's argument be pushed when the instruction needs it, with
 * one instruction (or an address and an adc) and one stack slot, and no
 * code before?  Then it need not be parked in a slot first: `__in(4, c,
 * &w)` was `ldlp w; stl 9; <c>; ldc 4; stl 11; ldl 9; rev; ldl 11; in`
 * where occam emits `ldlp w; ldl c; ldc 4; in`. */
static int tp_builtin_arg_direct(const SValue *sv)
{
    int v = sv->r & VT_VALMASK;

    if (sv->r & (VT_MUSTCAST | VT_MUSTBOUND | VT_BOUNDED) ||
        is_float(sv->type.t))
        return 0;
    if (tp_operand_is_simple(sv))
        return 1;               /* constant, word local, register */
    if (sv->r & VT_LVAL)
        return 0;
    if (v == VT_LOCAL)
        return !(sv->r & VT_SYM);              /* &local */
    return v == VT_CONST && (sv->r & VT_SYM);  /* &global */
}

static void tp_builtin_push(SValue *sv)
{
    if (!tp_builtin_arg_direct(sv))
        tp_push_sv(sv);
    else if (tp_operand_is_simple(sv))
        tp_push_simple(sv);
    else if ((sv->r & VT_VALMASK) == VT_LOCAL) {
        emit_local_addr(sv->c.i);
        abcreg_push(1);
    } else
        tp_push_sym_addr(sv);
}

static void tp_emit_1op(int opcode) {
    if (!tp_builtin_arg_direct(vtop))
        gv(RC_INT);
    tp_builtin_push(&vtop[0]);
    vpop();

    tp_emit_opr(opcode);
    abcreg_pop(1);
}

/*
 * Helper: materialize 2 vstack args, then push them onto the eval stack in
 * the order needed for a 2-operand instruction.
 *
 * vstack layout:  vtop[0]=arg1, vtop[-1]=arg0
 * Eval stack result:  BReg=first pushed, AReg=second pushed
 */
static void tp_emit_2op(int opcode, int b_idx, int a_idx) {
    SValue sv[2];

    /* Materialize arg1 (vtop), then arg0 - each only if it cannot be
     * pushed directly.  Read both back afterwards: a gv() may spill the
     * other's register, which rewrites its vstack entry. */
    if (!tp_builtin_arg_direct(vtop))
        gv(RC_INT);
    vswap();
    if (!tp_builtin_arg_direct(vtop))
        gv(RC_INT);
    sv[0] = vtop[0];
    sv[1] = vtop[-1];

    vpop();
    vpop();

    /* Push onto eval stack: B first (sinks deeper), then A */
    tp_builtin_push(&sv[b_idx]);
    tp_builtin_push(&sv[a_idx]);

    tp_emit_opr(opcode);
    abcreg_pop(2);
}

static void tp_emit_3op(int opcode, int c_idx, int b_idx, int a_idx) {
    SValue sv[3];

    /* Materialize arg2 (vtop), arg1, arg0 as before, skipping those that
     * can be pushed directly, and read all three back at the end. */
    if (!tp_builtin_arg_direct(vtop))
        gv(RC_INT);
    vswap();
    if (!tp_builtin_arg_direct(vtop))
        gv(RC_INT);
    vrotb(3);
    if (!tp_builtin_arg_direct(vtop))
        gv(RC_INT);
    /* [arg0, arg1, arg2] -> vswap -> [arg0, arg2, arg1] -> vrotb(3) ->
     * [arg2, arg1, arg0] */
    sv[0] = vtop[0];
    sv[1] = vtop[-1];
    sv[2] = vtop[-2];

    vpop();
    vpop();
    vpop();

    /* Push onto eval stack: C first (sinks deepest), then B, then A */
    tp_builtin_push(&sv[c_idx]);
    tp_builtin_push(&sv[b_idx]);
    tp_builtin_push(&sv[a_idx]);

    tp_emit_opr(opcode);
    abcreg_pop(3);
}

/* Emit a zero-operand instruction whose result is returned as a word. */
static void tp_gen_flag_result(int opr)
{
    emit_instr(TP_OP_OPR, opr);
    tp_builtin_word_result();
}

static void tp_builtin_word_result(void)
{
    abcreg_push(1);
    vpushi(0);
    vtop->r = REG_IRET;
    Tp_STL(REG_IRET);
}

/* The FPU error flag, which the FPU sets on an invalid operation,
 * overflow, division by zero or a non-finite fpint.  T800 and later. */
static void tp_need_fpu(int tok)
{
    if (tp_soft_float())
        tcc_error("%s is an FPU instruction, not available with -m%s",
                  get_tok_str(tok, NULL), tp_target_name(tcc_state));
}

/* fpchkerr: copy the FP error flag into the Error flag */
static void gen_builtin_fpchkerr(void)
{
    tp_need_fpu(TOK___fpchkerr);
    emit_instr(TP_OP_OPR, TP_FPU_CHKERR);
}

/* fptesterr: 1 if the FP error flag was clear, 0 if set; clears it */
static void gen_builtin_fptesterr(void)
{
    tp_need_fpu(TOK___fptesterr);
    tp_gen_flag_result(TP_OPR_FPTESTERR);
}

/* Set or clear the FP error flag: fpuseterr/fpuclrerr through fpentry on
 * the T800, fpseterr/fpclrerr on the T9000. */
static void tp_gen_fp_errflag(int tok, int t9k_opr, int fpentry_code)
{
    tp_need_fpu(tok);
    if (tp_have_t9000_feat(TP_T9K_FPU)) {
        emit_instr(TP_OP_OPR, t9k_opr);
    } else {
        Tp_LDC(fpentry_code);
        emit_instr(TP_OP_OPR, TP_OPR_FPENTRY);
        abcreg_pop(1);
    }
}
static void gen_builtin_fpseterr(void)
{
    tp_gen_fp_errflag(TOK___fpseterr, TP_OPR_FPSETERR, TP_FPENTRY_USETERR);
}
static void gen_builtin_fpclrerr(void)
{
    tp_gen_fp_errflag(TOK___fpclrerr, TP_OPR_FPCLRERR, TP_FPENTRY_UCLRERR);
}

/* ------------------------------------------------------------------
 * Bit manipulation builtins (T800)
 * ------------------------------------------------------------------ */

static void gen_builtin_norm(void) {
    /* NORM normalizes a 64-bit value BReg:AReg.
     * For 32-bit CLZ: put value in BReg, 0 in AReg.
     * Input:  BReg=high32 (our value), AReg=low32 (0)
     * Output: CReg=shift count (leading zeros of 64-bit pair)
     *         BReg=shifted high, AReg=shifted low
     * For 32-bit value in BReg with AReg=0, CReg = clz(value). */
    gv(RC_INT);
    tp_push_sv(&vtop[0]);    /* AReg = value */
    Tp_LDC(0);               /* AReg = 0, BReg = value */
    emit_instr(TP_OP_OPR, TP_OPR_NORM);
    /* Now: CReg = shift count (CLZ of 64-bit pair), BReg = shifted hi, AReg = shifted lo.
     * For 32-bit CLZ: value in BReg, 0 in AReg → result is CLZ of value.
     * Note: __norm(0) returns 64 (full 64-bit pair is zero). Callers needing
     * 32-bit CLZ semantics should use: r = __norm(x); if (r > 32) r = 32; */
    /* We need CReg. Discard AReg and BReg: */
    Tp_STL(FRAME_SLOT_SCRATCH);   /* store AReg (shifted lo), pop */
    Tp_STL(FRAME_SLOT_SCRATCH);   /* store BReg (shifted hi), pop */
    /* Now AReg = CReg (the shift count = leading zeros of 64-bit BReg:AReg) */
    vtop->r = REG_IRET;
    Tp_STL(REG_IRET);
}

static void gen_builtin_bitcnt(void) {
    /* BITCNT: AReg = popcount(AReg) + BReg.
     * We want popcount(x), so load 0 into BReg first (accumulator). */
    if (tp_t414()) {
        /* the T414 has no bitcnt: lib/builtin.c's __builtin_popcount */
        tp_softfp_call("__builtin_popcount", 1, VT_INT);
        return;
    }
    gv(RC_INT);
    tp_push_sv(&vtop[0]);
    Tp_LDC(0);           /* BReg = x, AReg = 0 (accumulator base) */
    Tp_REV();             /* AReg = x, BReg = 0 */
    emit_instr(TP_OP_OPR, TP_OPR_BITCNT);
    /* AReg = popcount(x) + 0 = popcount(x) */
    abcreg_pop(1);
    vtop->r = REG_IRET;
    Tp_STL(REG_IRET);
}

static void gen_builtin_bitrevword(void) {
    /* BITREVWORD: AReg = bit-reversed AReg (all 32 bits) */
    gv(RC_INT);
    tp_push_sv(&vtop[0]);
    emit_instr(TP_OP_OPR, TP_OPR_BITREVWORD);
    vtop->r = REG_IRET;
    Tp_STL(REG_IRET);
}

static void gen_builtin_bitrevnbits(void) {
    /* BITREVNBITS: reverse bottom N bits of AReg.
     * Args: __bitrevnbits(value, nbits) → BReg=value, AReg=nbits */
    gv2(RC_INT, RC_INT);
    tp_push_sv(&vtop[-1]);  /* value → BReg */
    tp_push_sv(&vtop[0]);   /* nbits → AReg */
    emit_instr(TP_OP_OPR, TP_OPR_BITREVNBITS);
    abcreg_pop(1);
    vtop--;
    vtop->r = REG_IRET;
    Tp_STL(REG_IRET);
}

static void gen_builtin_lddevid(void) {
    emit_instr(TP_OP_OPR, TP_OPR_LDDEVID);
    abcreg_push(1);
    Tp_STL(REG_IRET);
    vpushi(0);
    vtop->r = REG_IRET;
}

/*************************************************************/
/* TVX - Transputer Vector eXtension (MXFP4 / bfloat16)      */
/*************************************************************/

/* __bf16 -> binary32.  vtop holds the 16-bit pattern in an integer slot;
 * BF16TOR32 turns it into a binary32 bit pattern, which we then reinterpret
 * as a float by loading it through the FPU. */
ST_FUNC void gen_cvt_bf16_to_float(void)
{
    SValue sv = vtop[0];

    if (!tp_tvx()) {
        /* bfloat16 is the top half of a binary32 */
        save_reg(REG_FRET);
        abcreg_stack_depth = 0;
        tp_push_sv(&sv);
        Tp_LDC(16);
        Tp_SHL();
        Tp_STL(REG_FRET);
        vtop->r = REG_FRET;
        vtop->r2 = VT_CONST;
        abcreg_stack_depth = 0;
        return;
    }

    save_reg(REG_FRET);
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;

    tp_push_sv(&sv);
    emit_instr(TP_OP_OPR, TP_OPR_BF16TOR32);
    Tp_STL(FRAME_SLOT_SCRATCH);

    Tp_LDLP(FRAME_SLOT_SCRATCH);
    emit_instr(TP_OP_OPR, TP_FPU_LDNLSN);
    abcreg_pop(1);
    fpureg_push(1);

    tp_store_fpu_result_to_slot(REG_FRET, VT_FLOAT);
    vtop->r = REG_FRET;
    vtop->r2 = VT_CONST;
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;
}

/* binary32 -> __bf16.  Spill FA to memory to get at the bit pattern, then
 * R32TOBF16 rounds it to nearest-even. */
ST_FUNC void gen_cvt_f16_to_float(void)
{
    SValue sv = vtop[0];

    if (!tp_tvx()) {
        vtop->type.t = VT_INT | VT_UNSIGNED;
        tp_softfp_call("__extendhfsf2", 1, VT_FLOAT);
        return;
    }

    save_reg(REG_FRET);
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;

    tp_push_sv(&sv);
    emit_instr(TP_OP_OPR, TP_OPR_F16TOR32);
    Tp_STL(FRAME_SLOT_SCRATCH);

    Tp_LDLP(FRAME_SLOT_SCRATCH);
    emit_instr(TP_OP_OPR, TP_FPU_LDNLSN);
    abcreg_pop(1);
    fpureg_push(1);

    tp_store_fpu_result_to_slot(REG_FRET, VT_FLOAT);
    vtop->r = REG_FRET;
    vtop->r2 = VT_CONST;
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;
}

ST_FUNC void gen_cvt_float_to_f16(void)
{
    SValue sv;

    if (!tp_tvx()) {
        tp_softfp_call("__truncsfhf2", 1, VT_INT);
        return;
    }

    /* the result goes to REG_IRET: spill a pending call result there */
    save_reg(REG_IRET);
    sv = vtop[0];
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;

    tp_push_float_sv(&sv);
    tp_store_fpu_result_to_slot(FRAME_SLOT_SCRATCH, VT_FLOAT);

    Tp_LDL(FRAME_SLOT_SCRATCH);
    emit_instr(TP_OP_OPR, TP_OPR_R32TOF16);
    Tp_STL(REG_IRET);

    vtop->r = REG_IRET;
    vtop->r2 = VT_CONST;
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;
}

ST_FUNC void gen_cvt_float_to_bf16(void)
{
    SValue sv;

    if (!tp_tvx()) {
        tp_softfp_call("__truncsfbf2", 1, VT_INT);
        return;
    }

    /* the result goes to REG_IRET: spill a pending call result there */
    save_reg(REG_IRET);
    sv = vtop[0];
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;

    tp_push_float_sv(&sv);
    tp_store_fpu_result_to_slot(FRAME_SLOT_SCRATCH, VT_FLOAT);

    Tp_LDL(FRAME_SLOT_SCRATCH);
    emit_instr(TP_OP_OPR, TP_OPR_R32TOBF16);
    Tp_STL(REG_IRET);

    vtop->r = REG_IRET;
    vtop->r2 = VT_CONST;
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;
}

/* A TVX op's result is a word of its own, whatever the operand was: keeping
 * the operand's type let a bf16 bit pattern held in an unsigned short narrow
 * __bf16tof32's 32-bit result to 16 bits when it was spilled. */
static void tp_tvx_word_result(int t) {
    vtop->type.t = t;
    vtop->r = REG_IRET;
    vtop->r2 = VT_CONST;
    Tp_STL(REG_IRET);
}

/* Unary word->word TVX op: AReg = f(AReg). */
static void tp_gen_tvx_unary(int opr) {
    gv(RC_INT);
    tp_push_sv(&vtop[0]);
    emit_instr(TP_OP_OPR, opr);
    /* Net eval-stack effect is zero: one operand consumed, one result. */
    tp_tvx_word_result(VT_INT | VT_UNSIGNED);
}

/* Binary word,word->word TVX op: AReg = f(BReg, AReg).
 * BF16PACK receives the high half in BReg and the low half in AReg. */
static void tp_gen_tvx_binary(int opr) {
    gv2(RC_INT, RC_INT);
    tp_push_sv(&vtop[-1]);  /* first arg  -> BReg */
    tp_push_sv(&vtop[0]);   /* second arg -> AReg */
    emit_instr(TP_OP_OPR, opr);
    abcreg_pop(1);
    vtop--;
    tp_tvx_word_result(VT_INT | VT_UNSIGNED);
}

/* Unary FPU operation on FAreg: __fpsqrt(x), __fpabs(x).  The result keeps
 * the operand's precision - the FPU takes its width from the length of the
 * value that was loaded, so one code path serves float and double.
 *
 * The T9000 has these as direct secondaries (fpsqrt 0xD3, fpabs 0xDB, ISM
 * 11.7.2).  The T800 reaches the same operations through fpentry, where abs
 * is a single entry and square root is the documented
 * first/step/step/last sequence - so both targets get the hardware
 * operation instead of a call into libm.
 */
/* Load the top n (1 or 2) vstack floats onto the FPU stack, the deeper one
 * first so that it ends in FBreg.  The result will go to REG_FRET. */
static void tp_fpu_operands(int n)
{
    int i, bt = vtop->type.t & VT_BTYPE;

    /* The result lands in REG_FRET(+1), which may still name a pending
     * float call result on the vstack (`__fpsqrt(fcall())`).  Same
     * aliasing hazard gen_cvt_itof documents - spill first. */
    save_reg(REG_FRET);
    if (bt == VT_DOUBLE || bt == VT_LDOUBLE)
        save_reg_upstack(REG_FRET + 1, 0);

    /* Re-read the vstack: the spills above rewrite its entries.  A spilled
     * operand becomes a LOCAL/LLOCAL lvalue, which stays directly
     * addressable, so the FPU can still load it without a gv(). */
    for (i = 0; i < n; i++) {
        SValue *sv = vtop - i;
        int v = sv->r & VT_VALMASK;
        if (!((v < VT_CONST || v == VT_CONST || v == VT_LOCAL || v == VT_LLOCAL)
              && !(sv->r & VT_MUSTBOUND))) {
            if (i)
                vswap();
            gv(RC_FLOAT);
            if (i)
                vswap();
        }
    }
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;
    for (i = n - 1; i >= 0; i--)
        tp_push_float_sv(&vtop[-i]);
}

/* The FPU result in FAreg becomes vtop, of basic type bt, in REG_FRET. */
static void tp_fpu_result(int bt)
{
    tp_store_fpu_result_to_slot(REG_FRET, bt);
    vtop->r = REG_FRET;
    vtop->r2 = (bt == VT_DOUBLE || bt == VT_LDOUBLE) ? (REG_FRET + 1)
                                                     : VT_CONST;
    vtop->sym = NULL;
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;
}

/* A T800 FPU operation reached through fpentry: ldc code; fpentry */
static void tp_fpentry(int code)
{
    Tp_LDC(code);
    emit_instr(TP_OP_OPR, TP_OPR_FPENTRY);
    abcreg_pop(1);
}

static void tp_gen_fpu_unary(int t9k_opr, int fpentry_code, int is_sqrt)
{
    int bt = vtop->type.t & VT_BTYPE;
    int is_db = (bt == VT_DOUBLE || bt == VT_LDOUBLE);

    if (tp_soft_float()) {
        int hi;

        if (is_sqrt) {
            tp_softfp_call(is_db ? "__sqrtdf2" : "__sqrtsf2", 1,
                           vtop->type.t & (VT_BTYPE | VT_LONG));
            return;
        }
        /* |x| clears the sign: the top bit of the word in the last slot,
         * as for unary minus in gen_opf */
        gv(RC_FLOAT);
        hi = (vtop->r & VT_VALMASK) + (is_db ? 1 : 0);
        abcreg_stack_depth = 0;
        fpureg_stack_depth = 0;
        Tp_LDL(hi);
        Tp_LDC(0x7fffffff);
        Tp_AND();
        Tp_STL(hi);
        abcreg_stack_depth = 0;
        return;
    }

    /* A single-use arithmetic result can still be in FA.  Take back its
     * unobserved store before save_reg() turns the operand into a spill.
     * Other live return-slot values must still go through the normal path:
     * the unary result will overwrite REG_FRET (and its high word). */
    if (is_sqrt && !tp_reg_live_on_vstack(REG_FRET, vtop) &&
        (!is_db || !tp_reg_live_on_vstack(REG_FRET + 1, vtop)) &&
        tp_fpu_take_resident(vtop, is_db)) {
        abcreg_stack_depth = 0;
        fpureg_stack_depth = 1;
    } else {
        tp_fpu_operands(1);
    }

    if (tp_have_t9000_feat(TP_T9K_FPU)) {
        emit_instr(TP_OP_OPR, t9k_opr);
    } else if (is_sqrt) {
        /* first, steps, last: two steps for single length and five for
         * double (TCWG 7.5.4) */
        int i, steps = is_db ? 5 : 2;
        tp_fpentry(TP_FPENTRY_SQRTFIRST);
        for (i = 0; i < steps; i++)
            tp_fpentry(TP_FPENTRY_SQRTSTEP);
        tp_fpentry(TP_FPENTRY_SQRTLAST);
    } else {
        tp_fpentry(fpentry_code);
    }
    tp_fpu_result(bt);
}

static void gen_builtin_fpsqrt(void) { tp_gen_fpu_unary(TP_OPR_FPSQRT, TP_FPENTRY_SQRTLAST, 1); }
static void gen_builtin_fpabs(void)  { tp_gen_fpu_unary(TP_OPR_FPABS,  TP_FPENTRY_ABS,      0); }

/* Round to an integer value, keeping the precision: __fpint (to nearest,
 * like rint), __fpintrp (toward +inf, ceil), __fpintrm (toward -inf,
 * floor), __fpintrz (toward zero, trunc).  An integer argument becomes a
 * double.  The T800's rounding modes last for the next FPU instruction
 * only (TCWG 7.5.2), so fpurm; fpint is a floor; the T9000's are sticky,
 * so fprm; fpint; fprn puts round-to-nearest back for the code around it.
 * fpint of an infinity or a NaN sets the FP error flag. */
static void tp_gen_fpu_round(int tok, int t9k_mode, int fpentry_mode)
{
    tp_need_fpu(tok);
    if (!is_float(vtop->type.t)) {
        CType t;
        t.t = VT_DOUBLE;
        t.ref = NULL;
        gen_cast(&t);
    }
    {
        int bt = vtop->type.t & VT_BTYPE;
        tp_fpu_operands(1);
        if (tp_have_t9000_feat(TP_T9K_FPU)) {
            if (t9k_mode)
                emit_instr(TP_OP_OPR, t9k_mode);
            emit_instr(TP_OP_OPR, TP_OPR_FPINT);
            if (t9k_mode)
                emit_instr(TP_OP_OPR, TP_OPR_FPRN);
        } else {
            if (fpentry_mode)
                tp_fpentry(fpentry_mode);
            emit_instr(TP_OP_OPR, TP_OPR_FPINT);
        }
        tp_fpu_result(bt);
    }
}
static void gen_builtin_fpint(void)   { tp_gen_fpu_round(TOK___fpint, 0, 0); }
static void gen_builtin_fpintrp(void) { tp_gen_fpu_round(TOK___fpintrp, TP_OPR_FPRP, TP_FPENTRY_RP); }
static void gen_builtin_fpintrm(void) { tp_gen_fpu_round(TOK___fpintrm, TP_OPR_FPRM, TP_FPENTRY_RM); }
static void gen_builtin_fpintrz(void) { tp_gen_fpu_round(TOK___fpintrz, TP_OPR_FPRZ, TP_FPENTRY_RZ); }

/* __fprem(x, y): the IEEE remainder x REM y, exact, in double unless both
 * are float.  One instruction on the T9000; on the T800 the TCWG 7.5.4
 * loop, each step leaving "done" in A for the cj:
 *      fpremfirst; eqc 0; cj next; loop: fpremstep; cj loop; next:
 * It uses all three FPU registers and destroys the integer stack. */
static void gen_builtin_fprem(void)
{
    CType t;
    int bt;

    tp_need_fpu(TOK___fprem);
    t.ref = NULL;
    t.t = ((vtop[-1].type.t & VT_BTYPE) == VT_FLOAT
           && (vtop[0].type.t & VT_BTYPE) == VT_FLOAT) ? VT_FLOAT : VT_DOUBLE;
    gen_cast(&t);
    vswap();
    gen_cast(&t);
    vswap();
    bt = t.t;
    tp_fpu_operands(2);                 /* FBreg = x, FAreg = y */
    if (tp_have_t9000_feat(TP_T9K_FPU)) {
        emit_instr(TP_OP_OPR, TP_OPR_FPREM);
    } else {
        int next, loop;
        emit_instr(TP_OP_OPR, TP_OPR_FPREMFIRST);
        abcreg_push(1);
        next = gjmp_cond(1, 0);
        loop = ind;
        emit_instr(TP_OP_OPR, TP_OPR_FPREMSTEP);
        abcreg_push(1);
        gsym_addr(gjmp_cond(0, 0), loop);
        gsym(next);
        tp_areg_invalidate();
    }
    vtop--;
    vtop->type = t;
    tp_fpu_result(bt);
}

/* -mt425/-mt414: a 16-bit float conversion without TVX, bits in and out */
static void tp_soft_tvx_unary(const char *helper)
{
    vtop->type.t = VT_INT | VT_UNSIGNED;
    tp_softfp_call(helper, 1, VT_INT | VT_UNSIGNED);
}

/* Software TVX.  Without -mt9000tvx, a TVX builtin with a C version in
 * lib/transputer/transputer-tvx*.c (every runtime archive has them) becomes a call to
 * it, which gives the instruction's result bit for bit; the others stay
 * compile errors.  One letter per argument and for the result: u a word (an
 * unsigned or a pointer), i an int, f a float, v nothing. */
static const struct tp_soft_tvx_desc {
    int tok;
    const char *helper;
    const char *args;
    char ret;
} tp_soft_tvx_descs[] = {
    { TOK___bf16pack,    "__tvx_bf16pack",    "uu",   'u' },
    { TOK___bf16unpkh,   "__tvx_bf16unpkh",   "u",    'u' },
    { TOK___mxfp4tof32,  "__tvx_mxfp4tof32",  "u",    'u' },
    { TOK___f32tomxfp4,  "__tvx_f32tomxfp4",  "u",    'u' },
    { TOK___mxe8m0tof32, "__tvx_mxe8m0tof32", "u",    'u' },
    { TOK___mxe8m0of,    "__tvx_mxe8m0of",    "u",    'u' },
    { TOK___mxfp4unpk8,  "__tvx_mxfp4unpk8",  "uu",   'v' },
    { TOK___f32tomxfp4x8,"__tvx_f32tomxfp4x8","u",    'u' },
    { TOK___bf16mul2,    "__tvx_bf16mul2",    "uu",   'u' },
    { TOK___bf16add2,    "__tvx_bf16add2",    "uu",   'u' },
    { TOK___mxfp4dot,    "__tvx_mxfp4dot",    "uuf",  'f' },
    { TOK___bf16dot,     "__tvx_bf16dot",     "uuuf", 'f' },
    { TOK___mxfp4gemv,   "__tvx_mxfp4gemv",   "uuuf", 'f' },
    { TOK___mxfp4gemvbf, "__tvx_mxfp4gemvbf", "uuuf", 'f' },
    { TOK___q8dot,       "__tvx_q8dot",       "uuuf", 'f' },
    { TOK___q8dotbf,     "__tvx_q8dotbf",     "uuuf", 'f' },
    { TOK___iq1mdot,     "__tvx_iq1mdot",     "uuuf", 'f' },
    { TOK___iq2xxsdot,   "__tvx_iq2xxsdot",   "uuuf", 'f' },
    { TOK___iq3xxsdot,   "__tvx_iq3xxsdot",   "uuuf", 'f' },
    { TOK___qsetfmt,     "__tvx_qsetfmt",     "i",    'v' },
    { TOK___qdot,        "__tvx_qdot",        "uuuf", 'f' },
    { 0 }
};

static const struct tp_soft_tvx_desc *tp_find_soft_tvx(int tok)
{
    const struct tp_soft_tvx_desc *s;

    for (s = tp_soft_tvx_descs; s->tok; ++s)
        if (s->tok == tok)
            return s;
    return NULL;
}

/* Call s's helper on the builtin's parsed arguments, the top of the vstack,
 * and leave its result (or a void value) in their place. */
static void tp_soft_tvx_call(const struct tp_soft_tvx_desc *s)
{
    int n = (int)strlen(s->args), i;
    CType t;

    /* Cast each argument to its parameter type: the last is on top; cast
     * it and rotate it to the bottom, n times, which restores the order. */
    t.ref = NULL;
    for (i = n - 1; i >= 0; i--) {
        t.t = s->args[i] == 'f' ? VT_FLOAT
            : s->args[i] == 'i' ? VT_INT : VT_INT | VT_UNSIGNED;
        gen_cast(&t);
        vrott(n);
    }
    if (s->ret == 'v') {
        vpush_helper_func(tok_alloc_const(s->helper));
        vrott(n + 1);
        gfunc_call(n);
        tp_builtin_return_void();
    } else {
        tp_softfp_call(s->helper, n,
                       s->ret == 'f' ? VT_FLOAT : VT_INT | VT_UNSIGNED);
    }
}

static void gen_builtin_bf16tof32(void)
{
    if (!tp_tvx()) {            /* bfloat16 is the top half of a binary32 */
        gv(RC_INT);
        tp_push_sv(&vtop[0]);
        Tp_LDC(16);
        Tp_SHL();
        tp_tvx_word_result(VT_INT | VT_UNSIGNED);
        return;
    }
    tp_gen_tvx_unary(TP_OPR_BF16TOR32);
}

static void gen_builtin_f32tobf16(void)
{
    if (!tp_tvx())
        tp_soft_tvx_unary("__truncsfbf2");
    else
        tp_gen_tvx_unary(TP_OPR_R32TOBF16);
}

/* Shared tail for accumulating dot products: the result is left in
 * FAreg, which we spill to the float return slot like every other float value
 * in this backend. */
static void tp_gen_tvx_dot_finish(int nb_args) {
    tp_store_fpu_result_to_slot(REG_FRET, VT_FLOAT);
    vtop -= nb_args;
    vpushi(0);
    vtop->type.t = VT_FLOAT;
    vtop->r = REG_FRET;
    vtop->r2 = VT_CONST;
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;
}

static void gen_builtin_mxfp4dot(void) {
    /* float __mxfp4dot(const void *block, const float *act, float acc)
     * MXFP4DOT wants AReg = block pointer, BReg = activation pointer, and the
     * accumulator on top of the FPU stack. */
    SValue blk = vtop[-2];
    SValue act = vtop[-1];
    SValue acc = vtop[0];

    save_reg(REG_FRET);
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;

    tp_push_float_sv(&acc);   /* FAreg = acc */
    tp_push_sv(&act);         /* act -> BReg once blk is pushed */
    tp_push_sv(&blk);         /* blk -> AReg */
    emit_instr(TP_OP_OPR, TP_OPR_MXFP4DOT);
    abcreg_pop(2);

    tp_gen_tvx_dot_finish(3);
}

/* Four-argument dots take the accumulator in FA, argument 0 in A,
 * argument 1 in B and argument 2 in C. */
static void tp_gen_tvx_dot4(int opr) {
    SValue blk = vtop[-3];
    SValue act = vtop[-2];
    SValue n   = vtop[-1];
    SValue acc = vtop[0];

    save_reg(REG_FRET);
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;

    tp_push_float_sv(&acc);
    tp_push_sv(&n);
    tp_push_sv(&act);
    tp_push_sv(&blk);
    emit_instr(TP_OP_OPR, opr);
    abcreg_pop(3);

    tp_gen_tvx_dot_finish(4);
}

/* void __mxfp4unpk8(unsigned packed, float *dst) - 8 lanes, one instruction */
static void gen_builtin_mxfp4unpk8(void) {
    gv2(RC_INT, RC_INT);
    tp_push_sv(&vtop[0]);   /* dst    -> BReg */
    tp_push_sv(&vtop[-1]);  /* packed -> AReg */
    emit_instr(TP_OP_OPR, TP_OPR_MXFP4UNPK8);
    abcreg_pop(2);
    vtop -= 2;
}

/* void __vsetlen(int n) - set the length for subsequent vector ops */
static void gen_builtin_vsetlen(void) {
    gv(RC_INT);
    tp_push_sv(&vtop[0]);
    emit_instr(TP_OP_OPR, TP_OPR_VSETLEN);
    abcreg_pop(1);
    vtop--;
}

/* Three-pointer vector op: A = dst, B = src1, C = src2. */
static void tp_gen_vec3(int opr) {
    SValue d = vtop[-2], a = vtop[-1], b = vtop[0];
    abcreg_stack_depth = 0;
    tp_push_sv(&b);     /* src2 -> CReg */
    tp_push_sv(&a);     /* src1 -> BReg */
    tp_push_sv(&d);     /* dst  -> AReg */
    emit_instr(TP_OP_OPR, opr);
    abcreg_pop(3);
    vtop -= 3;
    abcreg_stack_depth = 0;
}

/* Two-pointer vector op: A = dst, B = src. */
static void tp_gen_vec2(int opr) {
    SValue d = vtop[-1], a = vtop[0];
    abcreg_stack_depth = 0;
    tp_push_sv(&a);     /* src -> BReg */
    tp_push_sv(&d);     /* dst -> AReg */
    emit_instr(TP_OP_OPR, opr);
    abcreg_pop(2);
    vtop -= 2;
    abcreg_stack_depth = 0;
}


/* Block rotation (BRQ).  All three take (dst, src, sign_mask) and get their
 * element count from __vsetlen(), like the other vector kernels.  The sign
 * mask is one bit per lane of the 32-element block, so it fits a single word;
 * pass 0 for a plain (non-randomised) Hadamard.
 *
 *   void __vhad32  (float *dst,             const float *src,    unsigned m);
 *   void __mxqrot  (tvx_mxfp4_block *dst,   const float *src,    unsigned m);
 *   void __mxqrotbf(tvx_mxfp4_block *dst,   const unsigned short *src, unsigned m);
 */

/* TVX-GFX - transform & project (T&L geometry).  See transputer/t4/tvx_gfx.h.
 * Like tp_gen_vec3 but the instruction leaves a result (the outcode) in
 * AReg: three operands consumed, one word produced. */
static void tp_gen_vec3_ret(int opr) {
    SValue d = vtop[-2], a = vtop[-1], b = vtop[0];
    abcreg_stack_depth = 0;
    tp_push_sv(&b);     /* -> CReg */
    tp_push_sv(&a);     /* -> BReg */
    tp_push_sv(&d);     /* -> AReg */
    emit_instr(TP_OP_OPR, opr);
    abcreg_pop(2);
    vtop -= 2;
    tp_tvx_word_result(VT_INT);     /* not the destination pointer's type */
}

/* void __ftsetvp(const float vp[5])                {sx,sy,ox,oy,guard}
 * int  __ftransp(float *dst, const float *xyz, const float *mtx4x4)
 * int  __ftranspb(float *dst, const float *xyz, const float *mtx4x4)
 *                                     (vertex count from __vsetlen)
 * void __fsincos(float *dst, unsigned bams)        dst = {sin, cos}
 * unsigned __frsqrtb(unsigned bits)                bit-domain 1/sqrt   */
static void gen_builtin_ftsetvp(void) {
    gv(RC_INT);
    tp_push_sv(&vtop[0]);
    emit_instr(TP_OP_OPR, TP_OPR_FTSETVP);
    abcreg_pop(1);
    vtop--;
}
static void gen_builtin_ftransp(void)  { tp_gen_vec3_ret(TP_OPR_FTRANSP); }
static void gen_builtin_ftranspb(void) { tp_gen_vec3_ret(TP_OPR_FTRANSPB); }
/* unsigned __frecipb(unsigned bits);
 * void __fsincosb(float *dst_pairs, const unsigned short *bams)
 *                                        (angle count from __vsetlen) */
/* void __vsetstride(unsigned stride_bytes, unsigned stream_mask);
 * int  __ftcull(unsigned char *dst, const float *xyz, const void *param)
 * int  __tskin(float *dst, const void *joints, const void *param)
 *                                        (vertex count from __vsetlen) */
static void gen_builtin_ftcull(void)   { tp_gen_vec3_ret(TP_OPR_FTCULL); }
static void gen_builtin_tskin(void)    { tp_gen_vec3_ret(TP_OPR_TSKIN); }
/* void __vsetfmt(unsigned fmt);  0 = f32 xyz, 1 = bf16, 2 = f16 */
static void gen_builtin_vsetfmt(void) {
    gv(RC_INT);
    tp_push_sv(&vtop[0]);
    emit_instr(TP_OP_OPR, TP_OPR_VSETFMT);
    abcreg_pop(1);
    vtop--;
}
/* void __ftsetlit(const float lit[5])      {Lx,Ly,Lz,ambient,intensity}
 * int  __flitb(float *records, const float *normals, const float *mtx3x3)
 * int  __qgemm(float *dst, const void *param, unsigned nblocks_per_row)
 *                       param = {acts ptr, weights ptr}; rows = __vsetlen
 * void __vblend8(unsigned *dst, const unsigned *src, unsigned alpha_ctl) */
static void gen_builtin_ftsetlit(void) {
    gv(RC_INT);
    tp_push_sv(&vtop[0]);
    emit_instr(TP_OP_OPR, TP_OPR_FTSETLIT);
    abcreg_pop(1);
    vtop--;
}
static void gen_builtin_flitb(void)   { tp_gen_vec3_ret(TP_OPR_FLITB); }
static void gen_builtin_qgemm(void)   { tp_gen_vec3_ret(TP_OPR_QGEMM); }
/* void __vfill(void *dst, unsigned pattern, unsigned nbytes)
 * void __vfill2d(void *dst, unsigned pattern, unsigned width_bytes)
 *                          (rows/stride latched by __move2dinit)
 * unsigned __csumb(const void *src, unsigned init, unsigned nbytes)
 * void __vblend2d(unsigned *dst, const unsigned *src, unsigned ctl)
 *                          (rows/strides from __move2dinit, px/row
 *                           from __vsetlen)
 * void __stvxstate(void *save15words) */
static void gen_builtin_csumb(void)    { tp_gen_vec3_ret(TP_OPR_CSUM); }
/* unsigned __csumcpy(void *dst, const void *src, unsigned nbytes)
 * void __vq8k(void *dst_q8_K_blocks, const void *src)
 * void __vq80(void *dst_q8_0_blocks, const void *src)
 *                    (count from __vsetlen, source format from __vsetfmt;
 *                     __vsetfmt(3) then makes __qgemm take these blocks -
 *                     Q8_K for K- and i-quant weights, Q8_0 for the rest)
 * void __vcvt(void *dst, const void *src, unsigned dst_fmt)
 *                    (count from __vsetlen, source format from __vsetfmt;
 *                     dst_fmt 0 f32, 1 bf16, 2 f16, round-to-nearest-even)
 * void __vxfer(t9_xfer *list, unsigned n)
 *                    (n VCP transfers started at once, one deschedule
 *                     until all are done; each input's length comes back
 *                     in its entry - see include/t9000.h) */
static void gen_builtin_csumcpy(void)  { tp_gen_vec3_ret(TP_OPR_CSUMCPY); }

/* T9000 configuration space and virtual channel processor (ISM 12.8-12.9,
 * HRM 11 and 15).  `chan` is a virtual channel word - input #80000040+8n,
 * output +4 - and `vl` the virtual link's input channel.  Register usage
 * follows the ISM: A takes the first argument, B the second, C the third.
 *   unsigned __ldconf(addr)            stconf(addr, value)
 *   __initvlcb(vl, pktbuf)             __sethdr(vl, link, hdroffset)
 *   __writehdr(vl, len, hdr)           __readhdr(vl, len, dst)
 *   __setchmode(chan, active)          __stopch(chan)
 *   void *__swapbfr(vl, newbuf)        - returns the old packet buffer
 *   __vout(len, chan, src)             unsigned __vin(max, chan, dst)
 *   unsigned __ldchstatus(chan)        unsigned __chantype(chan)
 * __vin returns the length received, through ldcnt; on an overrun it
 * returns LengthError.p with the Error flag set. */
static void tp_t9k_result(void)
{
    abcreg_push(1);
    vpushi(0);
    tp_tvx_word_result(VT_INT | VT_UNSIGNED);
}
static void gen_builtin_ldconf(void)     { tp_emit_1op(TP_OPR_LDCONF); tp_t9k_result(); }
static void gen_builtin_swapbfr(void)
{
    tp_emit_2op(TP_OPR_SWAPBFR, 1, 0);
    tp_t9k_result();
    vtop->type.t = VT_PTR;
    vtop->type.ref = char_pointer_type.ref;
}
static void gen_builtin_vin(void)
{
    /* vin may deschedule; ldcnt then reads the count it left in the
     * process's workspace */
    tp_emit_3op(TP_OPR_VIN, 2, 1, 0);
    emit_instr(TP_OP_OPR, TP_OPR_LDCNT);
    tp_t9k_result();
}
static void gen_builtin_ldchstatus(void) { tp_emit_1op(TP_OPR_LDCHSTATUS); tp_t9k_result(); }
static void gen_builtin_chantype(void)   { tp_emit_1op(TP_OPR_CHANTYPE); tp_t9k_result(); }

/* T9000 semaphores (ISM 8.6).  A semaphore is three words: a count, then
 * the front and back of the queue of processes waiting on it.
 *   __semwait(sem)     take a count, or join the queue and deschedule
 *   __semsignal(sem)   wake the first waiter, or add a count
 *
 * Resource channels (ISM 8.8, 12.7): many users output to one server,
 * which picks the next with grant.  An RDS is three words (the server's
 * descriptor while it waits, the front and back of the queue of claimed
 * channels); an internal resource channel is the channel word, then
 * rc.Ptr and rc.Id.
 *   __mkrc(chan, rds, id)          mark chan as part of rds, identified by id
 *   __unmkrc(chan)                 back to an ordinary channel
 *   __grant(rds, idp)              *idp = id of the next claimed channel,
 *                                  descheduling until there is one
 *   __enbg(guard, rds)             ALT guard: enable, like __enbc
 *   int __disg(code, guard, rds)   ALT guard: disable, like __disc
 *   __irdsq(rds, front, back)      put a queue of channels at the front
 *   void *__erdsq(rds, void **back) take the whole queue: its front,
 *                                  its back through *back
 *   unsigned __ldresptr(chan)      read rc.Ptr; __stresptr(chan, v) write it
 * They take a T9000 (the descriptor table puts them after __vxfer). */
static void gen_builtin_disg(void)
{
    /* as __disc: A=code(0), B=guard(1), C=rds(2) -> selected */
    tp_emit_3op(TP_OPR_DISG, 2, 1, 0);
    abcreg_push(1);
    vpushi(0);
    vtop->r = REG_IRET;
    Tp_STL(REG_IRET);
}
static void gen_builtin_erdsq(void)
{
    /* erdsq leaves the queue's front in A and its back in B.  Park the
     * front in a scratch slot while the back is stored, since the vstack
     * may hold the back pointer in REG_IRET. */
    SValue rds, backp;

    gv2(RC_INT, RC_INT);
    rds = vtop[-1];
    backp = vtop[0];
    vtop -= 2;
    tp_push_sv(&rds);
    tp_emit_opr(TP_OPR_ERDSQ);          /* one operand, two results */
    abcreg_push(1);
    tp_areg_invalidate();
    Tp_STL(FRAME_SLOT_SCRATCH);         /* front; A = back */
    tp_push_sv(&backp);
    Tp_STNL(0);                         /* *backp = back */
    Tp_LDL(FRAME_SLOT_SCRATCH);
    vpushi(0);
    tp_tvx_word_result(VT_PTR);
    vtop->type.ref = char_pointer_type.ref;
}
static void gen_builtin_ldresptr(void)   { tp_emit_1op(TP_OPR_LDRESPTR); tp_t9k_result(); }
static void gen_builtin_stvxstate(void) {
    gv(RC_INT);
    tp_push_sv(&vtop[0]);
    emit_instr(TP_OP_OPR, TP_OPR_STVXSTATE);
    abcreg_pop(1);
    vtop--;
}

static void gen_builtin_f16tof32(void)
{
    if (!tp_tvx())
        tp_soft_tvx_unary("__extendhfsf2");
    else
        tp_gen_tvx_unary(TP_OPR_F16TOR32);
}

static void gen_builtin_f32tof16(void)
{
    if (!tp_tvx())
        tp_soft_tvx_unary("__truncsfhf2");
    else
        tp_gen_tvx_unary(TP_OPR_R32TOF16);
}

/* ---------------------------------------------------------------------
 * T9 FPU Temps T0-T7.
 *
 * The Temps are a flat, indexed file of eight 64-bit FP slots that sits
 * beside - not on top of - the classic 3-deep FA/FB/FC stack.  They are the
 * operand store the T&L microcode reads from: fdot4/fmvmul4 take their two
 * matrix-row halves from T[i], T[i+1], and fpoly3 takes its three Horner
 * coefficients from T[i..i+2].  Nothing in the C type system describes a
 * register file, so these builtins expose them directly for hand-written
 * kernels, exactly the way gen_builtin_vsetstride() and friends expose the
 * TVX latches.
 *
 * Register conventions come straight from ip/cpu/rtl/t9_fpu_bridge.sv:
 *
 *   tld/tst/txf/tclr   index in A[2:0]; A is NOT popped by the hardware
 *   f2ldm/f4ldm/f2ldma A = address, B[2:0] = index; neither is popped
 *   f2stm              A = address, B[2:0] = index; BOTH are popped
 *
 * The index left behind in A is harmless: every one of these helpers resets
 * abcreg_stack_depth, so the backend re-materialises anything it needs
 * afterwards, and a stale AReg only ever gets shifted down and dropped.
 */

/* Push a small constant Temp index.  The index must be a compile-time
 * constant: it lands in the instruction stream as an ldc, and letting it be
 * a runtime value would defeat the whole point (the caller is naming a
 * specific architectural register). */
static int tp_temp_index_const(SValue *sv, const char *who)
{
    if ((sv->r & (VT_VALMASK | VT_LVAL | VT_SYM)) != VT_CONST) {
        tcc_error("%s: Temp index must be a constant expression", who);
        return 0;
    }
    return (int)(sv->c.i & 7);
}

/* tld/tst/txf/tclr: `ldc i; opr <op>`. */
static void tp_gen_temp_idx_op(int opr, int idx)
{
    Tp_LDC(idx);
    emit_instr(TP_OP_OPR, opr);
    /* The hardware does not pop the index, so do not tell the tracker it
     * did.  Callers reset abcreg_stack_depth instead. */
}

/* void __tclr(int i)   -- mark T[i] empty */
static void gen_builtin_tclr(void) {
    int idx = tp_temp_index_const(&vtop[0], "__tclr");
    abcreg_stack_depth = 0;
    tp_gen_temp_idx_op(TP_OPR_TCLR, idx);
    vtop--;
    abcreg_stack_depth = 0;
}

/* void __tst(float v, int i)  -- T[i] <- v  (v is pushed to FA first) */
static void gen_builtin_tst(void) {
    SValue v = vtop[-1];
    int idx = tp_temp_index_const(&vtop[0], "__tst");
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;
    tp_push_float_sv(&v);
    tp_gen_temp_idx_op(TP_OPR_TST, idx);
    /* tst leaves FA valid; drop our bookkeeping copy of it. */
    fpureg_pop(1);
    vtop -= 2;
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;
}

/* float __tld(int i)  -- FA <- T[i], spilled to the float return slot */
static void gen_builtin_tld(void) {
    int idx = tp_temp_index_const(&vtop[0], "__tld");
    save_reg(REG_FRET);
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;
    tp_gen_temp_idx_op(TP_OPR_TLD, idx);
    fpureg_push(1);
    tp_store_fpu_result_to_slot(REG_FRET, VT_FLOAT);
    vtop -= 1;
    vpushi(0);
    vtop->type.t = VT_FLOAT;
    vtop->r = REG_FRET;
    vtop->r2 = VT_CONST;
    vtop->sym = NULL;
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;
}

/* f2ldm / f4ldm / f2ldma: A = address, B = Temp index. */
static void tp_gen_temp_fill(int opr)
{
    SValue p = vtop[-1];
    int idx = tp_temp_index_const(&vtop[0], "__f2ldm/__f4ldm/__f2ldma");
    abcreg_stack_depth = 0;
    Tp_LDC(idx);        /* -> BReg */
    tp_push_sv(&p);     /* -> AReg */
    emit_instr(TP_OP_OPR, opr);
    vtop -= 2;
    abcreg_stack_depth = 0;
}

/* void __f2ldm (const void *p, int i)   T[i]        <- {p[1], p[0]}
 * void __f4ldm (const void *p, int i)   T[i],T[i+1] <- p[0..3]
 * void __f2ldma(const void *p, int i)   async form of __f2ldm
 *
 * __f2ldma returns immediately and the fill runs on the private pool; the
 * next Temps-class instruction fences on it (is_fp_fill_fence).  Issue it
 * as far ahead of the consuming __tld/__fmvmul4 as the source allows or it
 * degenerates into a plain __f2ldm plus a stall. */
static void gen_builtin_f2ldm(void)  { tp_gen_temp_fill(TP_OPR_F2LDM); }
static void gen_builtin_f4ldm(void)  { tp_gen_temp_fill(TP_OPR_F4LDM); }
static void gen_builtin_f2ldma(void) { tp_gen_temp_fill(TP_OPR_F2LDMA); }

/* void __f2stm(void *p, int i)   {p[1], p[0]} <- T[i]
 * Mirrors __f2ldm's operand order.  This one really does pop both A and B. */
static void gen_builtin_f2stm(void) {
    SValue p = vtop[-1];
    int idx = tp_temp_index_const(&vtop[0], "__f2stm");
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;
    Tp_LDC(idx);        /* -> BReg */
    tp_push_sv(&p);     /* -> AReg */
    emit_instr(TP_OP_OPR, TP_OPR_F2STM);
    abcreg_pop(2);
    vtop -= 2;
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;
}

/* float __fmvmul4(int rowbase, int vbase)
 *
 *   FA <- dot4( T[rowbase], T[rowbase+1] , T[vbase], T[vbase+1] )
 *
 * fdot4/fmvmul4 read the matrix row from T[rowbase], T[rowbase+1] but take
 * the vertex from FA (= {x,y}) and FB (= {z,w}), and they consume both
 * (stack_action DYADIC, fb/fc invalidated).  So the vertex has to be
 * re-pushed for every row.  Keeping it in a Temp pair makes that two `tld`s
 * instead of four loads and two `f2pack`s - which is the whole reason the
 * Temps and the dot microcode were designed together.
 *
 * Note the push order: `tld vbase+1` first so it sinks to FB. */
static void tp_gen_mvmul4(int opr, int rowbase, int vbase)
{
    tp_gen_temp_idx_op(TP_OPR_TLD, (vbase + 1) & 7);  /* -> FA, sinks to FB */
    tp_gen_temp_idx_op(TP_OPR_TLD, vbase & 7);        /* -> FA */
    tp_gen_temp_idx_op(opr, rowbase & 7);
}

static void gen_builtin_fmvmul4(void) {
    int rowbase = tp_temp_index_const(&vtop[-1], "__fmvmul4");
    int vbase   = tp_temp_index_const(&vtop[0],  "__fmvmul4");
    save_reg(REG_FRET);
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;
    tp_gen_mvmul4(TP_OPR_FMVMUL4, rowbase, vbase);
    fpureg_push(1);
    tp_store_fpu_result_to_slot(REG_FRET, VT_FLOAT);
    vtop -= 2;
    vpushi(0);
    vtop->type.t = VT_FLOAT;
    vtop->r = REG_FRET;
    vtop->r2 = VT_CONST;
    vtop->sym = NULL;
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;
}

/* void __fmvmul4st(int rowbase, int vbase, float *dst)
 *
 * Same dot product, stored straight to *dst.  Going through the float return
 * slot costs an extra fpstnlsn/fpldnlsn pair per component, which on a vec4
 * transform is eight workspace touches per vertex - the exact traffic the
 * Temps are supposed to remove.  This form keeps the result on FA until it
 * reaches memory. */
static void gen_builtin_fmvmul4st(void) {
    SValue dst  = vtop[0];
    int rowbase = tp_temp_index_const(&vtop[-2], "__fmvmul4st");
    int vbase   = tp_temp_index_const(&vtop[-1], "__fmvmul4st");
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;
    tp_gen_mvmul4(TP_OPR_FMVMUL4, rowbase, vbase);
    tp_push_sv(&dst);
    emit_instr(TP_OP_OPR, TP_FPU_STNLSN);
    abcreg_pop(1);
    vtop -= 3;
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;
}

/* float __fpoly3(float x, int cbase)  -- Horner with c0..c2 in T[cbase..+2] */
static void gen_builtin_fpoly3(void) {
    SValue x = vtop[-1];
    int cbase = tp_temp_index_const(&vtop[0], "__fpoly3");
    save_reg(REG_FRET);
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;
    tp_push_float_sv(&x);
    tp_gen_temp_idx_op(TP_OPR_FPOLY3, cbase);
    tp_store_fpu_result_to_slot(REG_FRET, VT_FLOAT);
    vtop -= 2;
    vpushi(0);
    vtop->type.t = VT_FLOAT;
    vtop->r = REG_FRET;
    vtop->r2 = VT_CONST;
    vtop->sym = NULL;
    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;
}

/* void __qsetfmt(int ggml_type_id) */
static void gen_builtin_qsetfmt(void) {
    gv(RC_INT);
    tp_push_sv(&vtop[0]);
    emit_instr(TP_OP_OPR, TP_OPR_QSETFMT);
    abcreg_pop(1);
    vtop--;
}

static void gen_builtin_csub0(void) {
    /* CSUB0: Check subscript from 0.
     * __csub0(index, bound) → sets error flag if index >= bound (unsigned).
     * Returns index (unchanged). Useful for array bounds checking.
     * CSUB0: AReg=index, BReg=bound → if (uint)AReg >= (uint)BReg: SetError
     * After: AReg=index (unchanged), BReg=old CReg. */
    gv2(RC_INT, RC_INT);
    tp_push_sv(&vtop[-1]);  /* bound → pushed first, drops to BReg */
    tp_push_sv(&vtop[0]);   /* index → AReg */
    emit_instr(TP_OP_OPR, TP_OPR_CSUB0);
    abcreg_pop(1);
    vtop--;
    vtop->r = REG_IRET;
    Tp_STL(REG_IRET);
}

static void gen_builtin_fmul(void) {
    /* FMUL: Fractional multiply.
     * __fmul(a, b) → (a * b) >> 31 (signed, round-to-nearest).
     * Operands interpreted as fractions in [-1, +1).
     * FMUL: AReg = (BReg * AReg) >> 31, BReg = old CReg. */
    gv2(RC_INT, RC_INT);
    tp_push_sv(&vtop[-1]);  /* a → pushed first, drops to BReg */
    tp_push_sv(&vtop[0]);   /* b → AReg */
    emit_instr(TP_OP_OPR, TP_OPR_FMUL);
    abcreg_pop(1);
    vtop--;
    vtop->r = REG_IRET;
    Tp_STL(REG_IRET);
}

/* memcpy(dest, src, n) -> the T800 MOVE instruction.
 *
 * MOVE (OPR 0x4A) is a byte-counted block transfer: dest = BReg, src = CReg,
 * count = AReg (t4/p.c:5800-5804), so the operands are pushed src, dest,
 * count and the 3-deep evaluation stack is used exactly.  This is the same
 * emission gen_struct_copy() already uses for struct assignment.
 *
 * Because MOVE takes the count from a register this works for a variable
 * length too - there is no constant-size restriction.  MOVE is in the base
 * instruction set (no BADCODE guard in the emulator, unlike the move2d*
 * family), so no target gating is needed, and it no-ops for count <= 0, which
 * makes memcpy(d, s, 0) well behaved.
 *
 * NOT applied to memmove: MOVE's behaviour on overlapping regions is
 * undefined (ISM), and the T9000 is explicitly licensed to use word-granular
 * accesses that would corrupt a backward overlap.
 */
static int tp_try_inline_memcpy(int nb_args)
{
    SValue *fn_sv;
    SValue sv_dest, sv_src, sv_len;
    const char *name;

    if (nb_args != 3)
        return 0;
    if (!cur_text_section)
        return 0;

    fn_sv = &vtop[-nb_args];
    if (!(fn_sv->r & VT_SYM) || !fn_sv->sym)
        return 0;

    name = get_tok_str(fn_sv->sym->v, NULL);
    if (!name)
        return 0;
    if (strcmp(name, "memcpy") != 0 &&
        strcmp(name, "__aeabi_memcpy") != 0 &&
        strcmp(name, "__builtin_memcpy") != 0)
        return 0;

    save_regs(nb_args + 1);

    sv_dest = vtop[-2];
    sv_src  = vtop[-1];
    sv_len  = vtop[0];

    /* MOVE needs all three operands live at once, in C/B/A.  That only works
     * if materialising each one is a *pure push* - reads nothing already on
     * the evaluation stack, touches no backend scratch slot.  A narrow lvalue
     * argument, for instance, rebuilds a halfword through FRAME_SLOT_SCRATCH
     * and several stack levels, which would quietly destroy the src and dest
     * already sitting in B and C; the MOVE then runs with a garbage address
     * or length.
     *
     * This was latent for as long as the kernel defined memcpy as a static
     * inline, because no call ever reached this inliner.  Routing memcpy
     * through MOVE exposed it as a boot hang immediately after "VFS: Finished
     * mounting rootfs".  Anything not provably pure falls back to a real call
     * to arch/transputer/lib/memcpy.S, which does the same MOVE from its own
     * frame where the operands are plain locals. */
    if (!tp_push_sv_is_pure(&sv_dest) || !tp_push_sv_is_pure(&sv_src) ||
        !tp_push_sv_is_pure(&sv_len))
        return 0;                 /* gfunc_call's own save_regs still stands */

    abcreg_stack_depth = 0;
    tp_areg_invalidate();     /* no DUP shortcut against a stale AReg tag */
    tp_push_sv(&sv_src);      /* src   -> CReg */
    tp_push_sv(&sv_dest);     /* dest  -> BReg */
    tp_push_sv(&sv_len);      /* count -> AReg */
    Tp_OPR(TP_OPR_MOVE);
    abcreg_pop(3);            /* MOVE consumes all three */
    abcreg_stack_depth = 0;

    /* memcpy returns dest; keep the standard return slot coherent. */
    tp_push_sv(&sv_dest);
    Tp_STL(REG_IRET);
    abcreg_stack_depth = 0;

    vtop -= nb_args;
    vtop--;

    /* No REG_IRE2, and no mirror into the float return slots: memcpy returns
     * void *, so R_RET() names REG_IRET alone and nothing can read the others.
     * Matches the return-type-driven capture in gfunc_call(). */
    return 1;
}

static int tp_try_inline_local_zero_memset(int nb_args)
{
    SValue *fn_sv;
    SValue *dest_sv;
    SValue *val_sv;
    SValue *size_sv;
    const char *name;
    int offset;
    int size;

    if (nb_args != 3)
        return 0;

    /* Can't inline if cur_text_section isn't set up (e.g. during
       compound literal initialization in nested declarator context) */
    if (!cur_text_section)
        return 0;

    fn_sv = &vtop[-nb_args];
    if (!(fn_sv->r & VT_SYM) || !fn_sv->sym)
        return 0;

    name = get_tok_str(fn_sv->sym->v, NULL);
    if (!name || (strcmp(name, "memset") != 0 && strcmp(name, "__aeabi_memset") != 0))
        return 0;

    dest_sv = &vtop[-2];
    val_sv = &vtop[-1];
    size_sv = &vtop[0];

    if ((dest_sv->r & VT_VALMASK) != VT_LOCAL || (dest_sv->r & (VT_LVAL | VT_SYM)))
        return 0;
    if ((val_sv->r & VT_VALMASK) != VT_CONST || (val_sv->r & VT_SYM) || val_sv->c.i != 0)
        return 0;
    if ((size_sv->r & VT_VALMASK) != VT_CONST || (size_sv->r & VT_SYM))
        return 0;

    offset = dest_sv->c.i;
    size = size_sv->c.i;
    /* Each word costs `ldc 0; stl n` (2-3 bytes); a call is about 20.
       Past 16 words the call is smaller, and it is still a call. */
    if (size < 0 || size > 64)
        return 0;

    save_regs(nb_args + 1);

    /* memset returns the destination pointer. */
    emit_local_addr(offset);
    Tp_STL(REG_IRET);
    abcreg_stack_depth = 0;

    while (size > 0 && (offset & 3) != 0) {
        Tp_LDC(0);                    /* sb stores BReg at AReg */
        emit_local_addr(offset);
        Tp_SB();
        abcreg_stack_depth = 0;
        offset++;
        size--;
    }

    while (size >= 4) {
        Tp_LDC(0);
        emit_local_op(TP_OP_STL, offset);
        abcreg_stack_depth = 0;
        offset += 4;
        size -= 4;
    }

    while (size > 0) {
        Tp_LDC(0);
        emit_local_addr(offset);
        Tp_SB();
        abcreg_stack_depth = 0;
        offset++;
        size--;
    }

    /* Pop args and function like the regular call path. */
    vtop -= nb_args;
    vtop--;

    /* No REG_IRE2 and no mirror into the float return slots: memset
     * returns void * (int if undeclared), which R_RET() names REG_IRET
     * alone - as in tp_try_inline_memcpy(). */
    return 1;
}

// void gen_builtin_memset(void) {
//     /* memset(dest, value, size) - inline implementation for Transputer */
//     /* Stack: [dest, value, size] */
// 
//     /* For now, implement simple case where value == 0 (which is what init_putz uses) */
//     /* dest is in vtop[-2], value in vtop[-1], size in vtop[0] */
// 
//     /* Get size */
//     vpushv(&vtop[0]);
//     gv(RC_INT);
// 
//     /* Check if size == 0, if so skip */
//     Tp_DUP();  /* Duplicate size */
//     Tp_EQC(0);
//     gsym_addr(0, 1);  /* Conditional jump to end if size == 0 */
// 
//     /* Get dest */
//     vpushv(&vtop[-2]);
//     gv(RC_INT);
// 
//     /* Get value (should be 0) */
//     vpushv(&vtop[-1]);
//     gv(RC_INT);
// 
//     /* For value == 0, we can use a simple loop with SB */
//     /* Areg = value (0), Breg = dest */
// 
//     /* Start loop */
//     int loop_start = ind;
// 
//     /* Check if size > 0 */
//     Tp_DUP();  /* Duplicate size */
//     Tp_GT();   /* Compare size > 0? Wait, this compares Areg > Breg */
//     /* Actually, let's use a simpler approach - just loop size times */
// 
//     /* For simplicity, let's implement a byte-by-byte loop */
//     /* This is similar to the assembly memset.S */
// 
//     /* Save registers: we need to save dest, value, size */
//     /* But since we're emitting inline, let's use a simple loop */
// 
//     /* For Transputer, the most efficient is to use MOVE2DZERO if value == 0 */
//     /* But MOVE2DZERO needs a source. For memset to 0, we can use dest as source */
//     /* assuming it contains zeros, but that's not guaranteed. */
// 
//     /* Better approach: emit a loop that does SB (store byte) */
// 
//     /* Areg = value, Breg = dest */
//     /* Loop: while size > 0 { SB; dest++; size--; } */
// 
//     /* Set up loop */
//     /* Areg = size, save it */
//     Tp_STL(3);  /* Save size to local 3 */
// 
//     /* Areg = dest */
//     vpop();     /* Pop size from stack */
//     Tp_STL(1);  /* Save dest to local 1 */
// 
//     /* Areg = value */
//     vpop();     /* Pop dest from stack */
//     Tp_STL(2);  /* Save value to local 2 */
// 
//     /* Now set up for loop */
//     /* Areg = value, Breg = dest */
//     Tp_LDL(2);  /* Areg = value */
//     Tp_LDL(1);  /* Areg = dest, Breg = value */
// 
//     /* Loop start */
//     int memset_loop = ind;
// 
//     /* Check if size == 0 */
//     Tp_LDL(3);  /* Areg = size */
//     Tp_EQC(0);
//     gsym_addr(0, 1);  /* Jump to done if size == 0 */
// 
//     /* Store byte: SB stores low byte of Areg to [Breg] */
//     Tp_SB();    /* Store byte */
// 
//     /* Increment dest */
//     Tp_LDL(1);  /* Areg = dest */
//     Tp_ADC(1);  /* Areg += 1 */
//     Tp_STL(1);  /* Save back */
// 
//     /* Decrement size */
//     Tp_LDL(3);  /* Areg = size */
//     Tp_ADC(-1); /* Areg -= 1 */
//     Tp_STL(3);  /* Save back */
// 
//     /* Jump back to loop */
//     gsym_addr(memset_loop, 0);
// 
//     /* Done */
//     gsym_set_addr(jump_done, ind);
// 
//     /* Clean up locals and return dest */
//     vpushv(&vtop[-2]);  /* Return dest */
//     gv(RC_INT);
// 
//     vtop -= 3;  /* Pop all 3 parameters */
// }

/*************************************************************/
/* Builtin dispatch: the one entry tccgen.c calls.  Every generator above
 * is reached only through this table. */
/*************************************************************/

typedef void (*tp_builtin_gen_fn)(void);
enum {
    TP_BUILTIN_ANY,
    TP_BUILTIN_T425,
    TP_BUILTIN_T805,
    TP_BUILTIN_FPU,
    TP_BUILTIN_TVX,
    TP_BUILTIN_T9000
};

struct tp_builtin_desc {
    int tok;
    const char *args;
    tp_builtin_gen_fn gen;
    unsigned char returns_void;
    short simple_op; /* simple instruction */
    short vector_op; /* TVX vector instruction */
    unsigned char cpu;
    short scalar_op; /* TVX word or four-argument accumulating instruction */
};

static const struct tp_builtin_desc *tp_find_builtin_desc(int tok)
{
    static const struct tp_builtin_desc descs[] = {
        { TOK___out,         "eee", NULL, 1, TP_OPR_OUT, 0, TP_BUILTIN_ANY },
        { TOK___in,          "eee", NULL, 1, TP_OPR_IN, 0, TP_BUILTIN_ANY },
        { TOK___outbyte,     "ee",  gen_builtin_outbyte,     1, 0, 0, TP_BUILTIN_ANY },
        { TOK___outword,     "ee",  gen_builtin_outword,     1, 0, 0, TP_BUILTIN_ANY },
        { TOK___tin,         "e",   NULL, 1, TP_OPR_TIN, 0, TP_BUILTIN_ANY },
        /* RESETCH returns the previous channel word: NotProcess_p if idle,
         * otherwise the descriptor of the process blocked on it. */
        { TOK___resetch,     "e",   NULL,     0, TP_OPR_RESETCH, 0, TP_BUILTIN_ANY },
        { TOK___move,        "eee", gen_builtin_move,        1, 0, 0, TP_BUILTIN_ANY },
        { TOK___move2dinit,    "eee", NULL, 1, TP_OPR_MOVE2DINIT, 0, TP_BUILTIN_T425 },
        { TOK___move2dall,     "eee", NULL, 1, TP_OPR_MOVE2DALL, 0, TP_BUILTIN_T425 },
        { TOK___move2dnonzero, "eee", NULL, 1, TP_OPR_MOVE2DNONZERO, 0, TP_BUILTIN_T425 },
        { TOK___move2dzero,    "eee", NULL, 1, TP_OPR_MOVE2DZERO, 0, TP_BUILTIN_T425 },
        { TOK___crcbyte,     "eee", NULL,     0, TP_OPR_CRCBYTE, 0, TP_BUILTIN_T425 },
        { TOK___crcword,     "eee", NULL,     0, TP_OPR_CRCWORD, 0, TP_BUILTIN_T425 },
        { TOK___ldtimer,     "",    NULL,     0, TP_OPR_LDTIMER, 0, TP_BUILTIN_ANY },
        { TOK___sttimer,     "e",   NULL, 1, TP_OPR_STTIMER, 0, TP_BUILTIN_ANY },
        { TOK___mint,        "",    NULL,        0, TP_OPR_MINT, 0, TP_BUILTIN_ANY },
        { TOK___ldpri,       "",    NULL,       0, TP_OPR_LDPRI, 0, TP_BUILTIN_ANY },
        { TOK___startp,      "ee",  NULL, 1, TP_OPR_STARTP, 0, TP_BUILTIN_ANY },
        { TOK___endp,        "e",   NULL, 1, TP_OPR_ENDP, 0, TP_BUILTIN_ANY },
        { TOK___runp,        "e",   NULL, 1, TP_OPR_RUNP, 0, TP_BUILTIN_ANY },
        { TOK___stopp, "", NULL, 1, TP_OPR_STOPP, 0, TP_BUILTIN_ANY },
        { TOK___alt, "", NULL, 1, TP_OPR_ALT, 0, TP_BUILTIN_ANY },
        { TOK___altwt, "", NULL, 1, TP_OPR_ALTWT, 0, TP_BUILTIN_ANY },
        { TOK___altend, "", NULL, 1, TP_OPR_ALTEND, 0, TP_BUILTIN_ANY },
        { TOK___talt, "", NULL, 1, TP_OPR_TALT, 0, TP_BUILTIN_ANY },
        { TOK___taltwt, "", NULL, 1, TP_OPR_TALTWT, 0, TP_BUILTIN_ANY },
        { TOK___enbc,        "ee",  NULL, 1, TP_OPR_ENBC, 0, TP_BUILTIN_ANY },
        { TOK___enbs,        "e",   NULL, 1, TP_OPR_ENBS, 0, TP_BUILTIN_ANY },
        { TOK___enbt,        "ee",  NULL, 1, TP_OPR_ENBT, 0, TP_BUILTIN_ANY },
        { TOK___disc,        "eee", NULL,        0, TP_OPR_DISC, 0, TP_BUILTIN_ANY },
        { TOK___diss,        "ee",  NULL,        0, TP_OPR_DISS, 0, TP_BUILTIN_ANY },
        { TOK___dist,        "eee", NULL,        0, TP_OPR_DIST, 0, TP_BUILTIN_ANY },
        /* saveh/savel write two queue pointers; the st* forms set one. */
        { TOK___saveh,       "e",   NULL, 1, TP_OPR_SAVEH, 0, TP_BUILTIN_ANY },
        { TOK___savel,       "e",   NULL, 1, TP_OPR_SAVEL, 0, TP_BUILTIN_ANY },
        { TOK___sthf,        "e",   NULL, 1, TP_OPR_STHF, 0, TP_BUILTIN_ANY },
        { TOK___sthb,        "e",   NULL, 1, TP_OPR_STHB, 0, TP_BUILTIN_ANY },
        { TOK___stlf,        "e",   NULL, 1, TP_OPR_STLF, 0, TP_BUILTIN_ANY },
        { TOK___stlb,        "e",   NULL, 1, TP_OPR_STLB, 0, TP_BUILTIN_ANY },
        /* GAJW returns the old workspace; WCNT returns the word offset. */
        { TOK___gajw,        "e",   NULL,        0, TP_OPR_GAJW, 0, TP_BUILTIN_ANY },
        { TOK___wcnt,        "e",   NULL,        0, TP_OPR_WCNT, 0, TP_BUILTIN_ANY },
        { TOK___seterr, "", NULL, 1, TP_OPR_SETERR, 0, TP_BUILTIN_ANY },
        { TOK___testerr,     "",    NULL,     0, TP_OPR_TESTERR, 0, TP_BUILTIN_ANY },
        { TOK___sethalterr, "", NULL, 1, TP_OPR_SETHALTERR, 0, TP_BUILTIN_ANY },
        { TOK___clrhalterr, "", NULL, 1, TP_OPR_CLRHALTERR, 0, TP_BUILTIN_ANY },
        { TOK___testhalterr, "",    NULL, 0, TP_OPR_TESTHALTERR, 0, TP_BUILTIN_ANY },
        { TOK___stoperr, "", NULL, 1, TP_OPR_STOPERR, 0, TP_BUILTIN_ANY },
        { TOK___testpranal,  "",    NULL,  0, TP_OPR_TESTPRANAL, 0, TP_BUILTIN_ANY },
        { TOK___norm,        "e",   gen_builtin_norm,        0, 0, 0, TP_BUILTIN_ANY },
        { TOK___bitcnt,      "e",   gen_builtin_bitcnt,      0, 0, 0, TP_BUILTIN_ANY },
        { TOK_builtin_popcount,  "e", gen_builtin_bitcnt,    0, 0, 0, TP_BUILTIN_ANY },
        { TOK_builtin_popcountl, "e", gen_builtin_bitcnt,    0, 0, 0, TP_BUILTIN_ANY },
        { TOK___bitrevword,  "e",   gen_builtin_bitrevword,  0, 0, 0, TP_BUILTIN_T425 },
        { TOK___bitrevnbits, "ee",  gen_builtin_bitrevnbits, 0, 0, 0, TP_BUILTIN_T425 },
        { TOK___csub0,       "ee",  gen_builtin_csub0,       0, 0, 0, TP_BUILTIN_ANY },
        { TOK___fmul,        "ee",  gen_builtin_fmul,        0, 0, 0, TP_BUILTIN_ANY },
        { TOK___lddevid,     "",    gen_builtin_lddevid,     0, 0, 0, TP_BUILTIN_T805 },
        { TOK___fpchkerr,    "",    gen_builtin_fpchkerr,    1, 0, 0, TP_BUILTIN_FPU },
        { TOK___fptesterr,   "",    gen_builtin_fptesterr,   0, 0, 0, TP_BUILTIN_FPU },
        { TOK___fpseterr,    "",    gen_builtin_fpseterr,    1, 0, 0, TP_BUILTIN_FPU },
        { TOK___fpclrerr,    "",    gen_builtin_fpclrerr,    1, 0, 0, TP_BUILTIN_FPU },
        { TOK___fpint,       "e",   gen_builtin_fpint,       0, 0, 0, TP_BUILTIN_FPU },
        { TOK___fpintrp,     "e",   gen_builtin_fpintrp,     0, 0, 0, TP_BUILTIN_FPU },
        { TOK___fpintrm,     "e",   gen_builtin_fpintrm,     0, 0, 0, TP_BUILTIN_FPU },
        { TOK___fpintrz,     "e",   gen_builtin_fpintrz,     0, 0, 0, TP_BUILTIN_FPU },
        { TOK___fprem,       "ee",  gen_builtin_fprem,       0, 0, 0, TP_BUILTIN_FPU },
        { TOK___fpsqrt,      "e",   gen_builtin_fpsqrt,      0, 0, 0, TP_BUILTIN_ANY },
        { TOK___fpabs,       "e",   gen_builtin_fpabs,       0, 0, 0, TP_BUILTIN_ANY },
        /* TVX (MXFP4 / bfloat16) */
        { TOK___bf16tof32,   "e",   gen_builtin_bf16tof32,   0, 0, 0, TP_BUILTIN_ANY },
        { TOK___f32tobf16,   "e",   gen_builtin_f32tobf16,   0, 0, 0, TP_BUILTIN_ANY },
        { TOK___bf16pack,    "ee",  NULL, 0, 0, 0, TP_BUILTIN_TVX, TP_OPR_BF16PACK },
        { TOK___bf16unpkh,   "e",   NULL, 0, 0, 0, TP_BUILTIN_TVX, TP_OPR_BF16UNPKH },
        { TOK___mxfp4tof32,  "e",   NULL, 0, 0, 0, TP_BUILTIN_TVX, TP_OPR_MXFP4TOR32 },
        { TOK___f32tomxfp4,  "e",   NULL, 0, 0, 0, TP_BUILTIN_TVX, TP_OPR_R32TOMXFP4 },
        { TOK___mxe8m0tof32, "e",   NULL, 0, 0, 0, TP_BUILTIN_TVX, TP_OPR_MXE8M0TOR32 },
        { TOK___mxe8m0of,    "e",   NULL, 0, 0, 0, TP_BUILTIN_TVX, TP_OPR_MXE8M0OF },
        { TOK___mxfp4dot,    "eee", gen_builtin_mxfp4dot,    0, 0, 0, TP_BUILTIN_TVX },
        { TOK___bf16dot,     "eeee", NULL, 0, 0, 0, TP_BUILTIN_TVX, TP_OPR_BF16DOT },
        { TOK___mxfp4gemv,   "eeee", NULL, 0, 0, 0, TP_BUILTIN_TVX, TP_OPR_MXFP4GEMV },
        { TOK___mxfp4unpk8,  "ee",  gen_builtin_mxfp4unpk8,  1, 0, 0, TP_BUILTIN_TVX },
        { TOK___f32tomxfp4x8,"e",   NULL, 0, 0, 0, TP_BUILTIN_TVX, TP_OPR_R32TOMXFP4X8 },
        { TOK___bf16mul2,    "ee",  NULL, 0, 0, 0, TP_BUILTIN_TVX, TP_OPR_BF16MUL2 },
        { TOK___bf16add2,    "ee",  NULL, 0, 0, 0, TP_BUILTIN_TVX, TP_OPR_BF16ADD2 },
        { TOK___vsetlen,     "e",   gen_builtin_vsetlen,     1, 0, 0, TP_BUILTIN_TVX },
        { TOK___mxfp4gemvbf, "eeee", NULL, 0, 0, 0, TP_BUILTIN_TVX, TP_OPR_MXFP4GEMVBF },
        { TOK___vadd,        "eee", NULL, 1, 0, TP_OPR_VADDSN, TP_BUILTIN_TVX },
        { TOK___vmul,        "eee", NULL, 1, 0, TP_OPR_VMULSN, TP_BUILTIN_TVX },
        { TOK___vsilu,       "ee",  NULL, 1, 0, TP_OPR_VSILUSN, TP_BUILTIN_TVX },
        { TOK___vrmsnorm,    "eee", NULL, 1, 0, TP_OPR_VRMSNORM, TP_BUILTIN_TVX },
        { TOK___vsoftmax,    "ee",  NULL, 1, 0, TP_OPR_VSOFTMAXSN, TP_BUILTIN_TVX },
        { TOK___f16tof32,    "e",   gen_builtin_f16tof32,    0, 0, 0, TP_BUILTIN_ANY },
        { TOK___f32tof16,    "e",   gen_builtin_f32tof16,    0, 0, 0, TP_BUILTIN_ANY },
        { TOK___q8dot,       "eeee", NULL, 0, 0, 0, TP_BUILTIN_TVX, TP_OPR_Q8DOT },
        { TOK___q8dotbf,     "eeee", NULL, 0, 0, 0, TP_BUILTIN_TVX, TP_OPR_Q8DOTBF },
        { TOK___iq1mdot,     "eeee", NULL, 0, 0, 0, TP_BUILTIN_TVX, TP_OPR_IQ1MDOT },
        { TOK___iq2xxsdot,   "eeee", NULL, 0, 0, 0, TP_BUILTIN_TVX, TP_OPR_IQ2XXSDOT },
        { TOK___iq3xxsdot,   "eeee", NULL, 0, 0, 0, TP_BUILTIN_TVX, TP_OPR_IQ3XXSDOT },
        { TOK___qsetfmt,     "e",   gen_builtin_qsetfmt,     1, 0, 0, TP_BUILTIN_TVX },
        { TOK___qdot,        "eeee", NULL, 0, 0, 0, TP_BUILTIN_TVX, TP_OPR_QDOT },
        { TOK___vhad32,      "eee", NULL, 1, 0, TP_OPR_VHAD32, TP_BUILTIN_TVX },
        { TOK___mxqrot,      "eee", NULL, 1, 0, TP_OPR_MXQROT, TP_BUILTIN_TVX },
        { TOK___mxqrotbf,    "eee", NULL, 1, 0, TP_OPR_MXQROTBF, TP_BUILTIN_TVX },
        { TOK___ftsetvp,     "e",   gen_builtin_ftsetvp,     1, 0, 0, TP_BUILTIN_TVX },
        { TOK___ftransp,     "eee", gen_builtin_ftransp,     0, 0, 0, TP_BUILTIN_TVX },
        { TOK___ftranspb,    "eee", gen_builtin_ftranspb,    0, 0, 0, TP_BUILTIN_TVX },
        { TOK___fsincos,     "ee",  NULL, 1, 0, TP_OPR_FSINCOS, TP_BUILTIN_TVX },
        { TOK___frsqrtb,     "e",   NULL, 0, 0, 0, TP_BUILTIN_TVX, TP_OPR_FRSQRT },
        { TOK___frecipb,     "e",   NULL, 0, 0, 0, TP_BUILTIN_TVX, TP_OPR_FRECIP },
        { TOK___fsincosb,    "ee",  NULL, 1, 0, TP_OPR_FSINCOSB, TP_BUILTIN_TVX },
        { TOK___vsetstride,  "ee",  NULL, 1, 0, TP_OPR_VSETSTRIDE, TP_BUILTIN_TVX },
        { TOK___ftcull,      "eee", gen_builtin_ftcull,      0, 0, 0, TP_BUILTIN_TVX },
        { TOK___tskin,       "eee", gen_builtin_tskin,       0, 0, 0, TP_BUILTIN_TVX },
        { TOK___vsetfmt,     "e",   gen_builtin_vsetfmt,     1, 0, 0, TP_BUILTIN_TVX },
        { TOK___ftsetlit,    "e",   gen_builtin_ftsetlit,    1, 0, 0, TP_BUILTIN_TVX },
        { TOK___flitb,       "eee", gen_builtin_flitb,       0, 0, 0, TP_BUILTIN_TVX },
        { TOK___qgemm,       "eee", gen_builtin_qgemm,       0, 0, 0, TP_BUILTIN_TVX },
        { TOK___vblend8,     "eee", NULL, 1, 0, TP_OPR_VBLEND8, TP_BUILTIN_TVX },
        { TOK___vfill,       "eee", NULL, 1, 0, TP_OPR_VFILL, TP_BUILTIN_TVX },
        { TOK___vfill2d,     "eee", NULL, 1, 0, TP_OPR_VFILL2D, TP_BUILTIN_TVX },
        { TOK___csumb,       "eee", gen_builtin_csumb,       0, 0, 0, TP_BUILTIN_TVX },
        { TOK___vblend2d,    "eee", NULL, 1, 0, TP_OPR_VBLEND2D, TP_BUILTIN_TVX },
        { TOK___stvxstate,   "e",   gen_builtin_stvxstate,   1, 0, 0, TP_BUILTIN_TVX },
        { TOK___csumcpy,     "eee", gen_builtin_csumcpy,     0, 0, 0, TP_BUILTIN_TVX },
        { TOK___vq8k,        "ee",  NULL, 1, 0, TP_OPR_VQ8K, TP_BUILTIN_TVX },
        { TOK___vq80,        "ee",  NULL, 1, 0, TP_OPR_VQ80, TP_BUILTIN_TVX },
        { TOK___vcvt,        "eee", NULL, 1, 0, TP_OPR_VCVT, TP_BUILTIN_TVX },
        { TOK___vxfer,       "ee",  NULL, 1, 0, TP_OPR_VXFER, TP_BUILTIN_TVX },
        /* T9000 configuration space and VCP */
        { TOK___ldconf,      "e",   gen_builtin_ldconf,      0, 0, 0, TP_BUILTIN_T9000 },
        { TOK___stconf,        "ee",   NULL, 1, TP_OPR_STCONF, 0, TP_BUILTIN_T9000 },
        { TOK___initvlcb,      "ee",   NULL, 1, TP_OPR_INITVLCB, 0, TP_BUILTIN_T9000 },
        { TOK___sethdr,        "eee",  NULL, 1, TP_OPR_SETHDR, 0, TP_BUILTIN_T9000 },
        { TOK___writehdr,      "eee",  NULL, 1, TP_OPR_WRITEHDR, 0, TP_BUILTIN_T9000 },
        { TOK___readhdr,       "eee",  NULL, 1, TP_OPR_READHDR, 0, TP_BUILTIN_T9000 },
        { TOK___setchmode,     "ee",   NULL, 1, TP_OPR_SETCHMODE, 0, TP_BUILTIN_T9000 },
        { TOK___stopch,        "e",    NULL, 1, TP_OPR_STOPCH, 0, TP_BUILTIN_T9000 },
        { TOK___swapbfr,     "ee",  gen_builtin_swapbfr,     0, 0, 0, TP_BUILTIN_T9000 },
        { TOK___vin,         "eee", gen_builtin_vin,         0, 0, 0, TP_BUILTIN_T9000 },
        { TOK___vout,          "eee",  NULL, 1, TP_OPR_VOUT, 0, TP_BUILTIN_T9000 },
        { TOK___ldchstatus,  "e",   gen_builtin_ldchstatus,  0, 0, 0, TP_BUILTIN_T9000 },
        { TOK___chantype,    "e",   gen_builtin_chantype,    0, 0, 0, TP_BUILTIN_T9000 },
        /* T9000 semaphores and resource channels */
        { TOK___semwait,       "e",    NULL, 1, TP_OPR_WAIT, 0, TP_BUILTIN_T9000 },
        { TOK___semsignal,     "e",    NULL, 1, TP_OPR_SIGNAL, 0, TP_BUILTIN_T9000 },
        { TOK___mkrc,          "eee",  NULL, 1, TP_OPR_MKRC, 0, TP_BUILTIN_T9000 },
        { TOK___unmkrc,        "e",    NULL, 1, TP_OPR_UNMKRC, 0, TP_BUILTIN_T9000 },
        { TOK___grant,         "ee",   NULL, 1, TP_OPR_GRANT, 0, TP_BUILTIN_T9000 },
        { TOK___enbg,          "ee",   NULL, 1, TP_OPR_ENBG, 0, TP_BUILTIN_T9000 },
        { TOK___disg,        "eee", gen_builtin_disg,        0, 0, 0, TP_BUILTIN_T9000 },
        { TOK___irdsq,         "eee",  NULL, 1, TP_OPR_IRDSQ, 0, TP_BUILTIN_T9000 },
        { TOK___erdsq,       "ee",  gen_builtin_erdsq,       0, 0, 0, TP_BUILTIN_T9000 },
        { TOK___ldresptr,    "e",   gen_builtin_ldresptr,    0, 0, 0, TP_BUILTIN_T9000 },
        { TOK___stresptr,      "ee",   NULL, 1, TP_OPR_STRESPTR, 0, TP_BUILTIN_T9000 },
        /* T9 FPU Temps T0-T7 */
        { TOK___tld,         "e",   gen_builtin_tld,         0, 0, 0, TP_BUILTIN_T9000 },
        { TOK___tst,         "ee",  gen_builtin_tst,         1, 0, 0, TP_BUILTIN_T9000 },
        { TOK___tclr,        "e",   gen_builtin_tclr,        1, 0, 0, TP_BUILTIN_T9000 },
        { TOK___f2ldm,       "ee",  gen_builtin_f2ldm,       1, 0, 0, TP_BUILTIN_T9000 },
        { TOK___f4ldm,       "ee",  gen_builtin_f4ldm,       1, 0, 0, TP_BUILTIN_T9000 },
        { TOK___f2ldma,      "ee",  gen_builtin_f2ldma,      1, 0, 0, TP_BUILTIN_T9000 },
        { TOK___f2stm,       "ee",  gen_builtin_f2stm,       1, 0, 0, TP_BUILTIN_T9000 },
        { TOK___fmvmul4,     "ee",  gen_builtin_fmvmul4,     0, 0, 0, TP_BUILTIN_T9000 },
        { TOK___fmvmul4st,   "eee", gen_builtin_fmvmul4st,   1, 0, 0, TP_BUILTIN_T9000 },
        { TOK___fpoly3,      "ee",  gen_builtin_fpoly3,      0, 0, 0, TP_BUILTIN_T9000 },
        { 0,                 NULL,  NULL,                    0 }
    };
    const struct tp_builtin_desc *desc;

    for (desc = descs; desc->tok != 0; ++desc) {
        if (desc->tok == tok)
            return desc;
    }
    return NULL;
}

ST_FUNC int gen_transputer_builtin_call(int tok)
{
    const struct tp_builtin_desc *desc;

    desc = tp_find_builtin_desc(tok);
    if (!desc)
        return 0;
    /* FPU-only generators diagnose after parsing their operands. */
    if (desc->cpu == TP_BUILTIN_TVX && !tp_tvx()) {
        const struct tp_soft_tvx_desc *soft = tp_find_soft_tvx(tok);
        if (!soft)
            tcc_error("%s is a TVX instruction with no software "
                      "version, not available without -mt9000tvx",
                      get_tok_str(tok, NULL));
        parse_builtin_params(0, desc->args);
        save_reg(REG_IRET);
        tp_soft_tvx_call(soft);
        return 1;
    }
    if (desc->cpu == TP_BUILTIN_T9000 && !tp_isa_has(TP_EF_T9000))
        tcc_error("%s is a T9000 instruction, not available with -m%s",
                  get_tok_str(tok, NULL), tp_target_name(tcc_state));
    if ((desc->cpu == TP_BUILTIN_T425 || desc->cpu == TP_BUILTIN_T805) &&
        tp_t414())
        tcc_error("%s is a T425 instruction, not available with -mt414",
                  get_tok_str(tok, NULL));
    if (desc->cpu == TP_BUILTIN_T805 && tp_no_t805_ops())
        tcc_error("%s is a T805 instruction, not available with -mt800",
                  get_tok_str(tok, NULL));

    parse_builtin_params(0, desc->args);
    /* Integer results land in REG_IRET, which may still hold a pending
     * call result on the vstack (`icall() + __ldtimer()`); without a spill
     * both operands would name the builtin's result.  Spilling after the
     * arguments are parsed also covers an argument that is that result. */
    save_reg(REG_IRET);
    if (desc->scalar_op && strlen(desc->args) == 4)
        tp_gen_tvx_dot4(desc->scalar_op);
    else if (desc->scalar_op && !desc->args[1])
        tp_gen_tvx_unary(desc->scalar_op);
    else if (desc->scalar_op)
        tp_gen_tvx_binary(desc->scalar_op);
    else if (desc->vector_op && !desc->args[2])
        tp_gen_vec2(desc->vector_op);
    else if (desc->vector_op)
        tp_gen_vec3(desc->vector_op);
    else if (desc->simple_op && !desc->args[0] && !desc->returns_void)
        tp_gen_flag_result(desc->simple_op);
    else if (desc->simple_op && !desc->args[0])
        emit_instr(TP_OP_OPR, desc->simple_op);
    else if (desc->simple_op && !desc->args[1])
        tp_emit_1op(desc->simple_op);
    else if (desc->simple_op && !desc->args[2])
        tp_emit_2op(desc->simple_op, 1, 0);
    else if (desc->simple_op)
        tp_emit_3op(desc->simple_op, 2, 1, 0);
    else
        desc->gen();
    if (desc->simple_op && desc->args[0] && !desc->returns_void)
        tp_builtin_word_result();
    if (desc->returns_void)
        tp_builtin_return_void();
    return 1;
}

/*************************************************************/
/* Function Prologue/Epilogue */
/*************************************************************/

ST_FUNC void gfunc_prolog(Sym *func_sym) {
    Sym *sym;
    int n = 0;
    func_vc = 0;

    if (!func_sym) {
        fprintf(stderr, "gfunc_prolog: func_sym is NULL\n");
        return;
    }

    /* This output now holds code for the target (its e_flags). */
    tcc_state->tp_isa_used |= tcc_state->tp_isa;
    transputer_func_reset();
    func_start_ind = ind;
    func_start_sym = func_sym;
    func_start_nsym = symtab_section->data_offset / sizeof(ElfSym);
    func_start_nrel = cur_text_section->reloc
        ? cur_text_section->reloc->data_offset / sizeof(ElfW_Rel) : 0;

    if (tp_debug_check()) {
        fprintf(stderr, "[PROLOG_DEBUG] func=%s start_ind=0x%x inline=%d static=%d\n",
                funcname ? funcname : "<unknown>", func_start_ind,
                (func_sym->type.t & VT_INLINE) != 0, (func_sym->type.t & VT_STATIC) != 0);
    }

    loc = 0; /* Stack offset counter */

    /* 1. Placeholder for AJW -framesize
     * Reserve 5 bytes to accommodate frame sizes up to ~1M words (4MB).
     * AJW -N encoding: NFIX + PFIX + AJW opcode (3 bytes).
     * Supports frames up to 4095 words (16KB) — enough for all functions.
     * Reduced from 5-byte. PFIX 0 (0x20) padding for smaller frames.
     */
    func_prologue_ajw_offset = ind;
    g((TP_OP_NFIX << 4) | 0);  /* Placeholder NFIX */
    g((TP_OP_PFIX << 4) | 0);  /* Placeholder PFIX */
    g((TP_OP_AJW << 4) | 0);   /* Placeholder AJW */

     /* 2. Process Parameters
        CALL saves A/B/C at NEW WPtr[0..2], then automatically
        does AJW 4 to move WPtr down by 4 words.
        We must reload those saved values and spill them
        into locals so parameters have addresses.
     */
    if (!func_sym->type.ref) return;
    sym = func_sym->type.ref;

    /* If this function returns a struct via hidden pointer (sret),
     * capture that implicit first argument in a local slot so
     * gfunc_return() can copy into *func_vc. */
    if ((func_vt.t & VT_BTYPE) == VT_STRUCT) {
        CType ret_type;
        int ret_align, regsize;
        int ret_nregs = gfunc_sret(&func_vt, func_var, &ret_type,
                                   &ret_align, &regsize);
        if (ret_nregs == 0) {
            int src_off = 4; /* incoming ABI word 0 (saved A) */
            loc -= 4;
            func_vc = loc;
            emit_local_op(TP_OP_LDL, src_off);
            emit_local_op(TP_OP_STL, func_vc);
            n = 1; /* consume hidden pointer word */
        }
    }

    /* We iterate parameters.
        Param 0 (A), Param 1 (B), Param 2 (C):
          We allocate local slots for them and spill.
          Note: A is at top of stack.
     */

    if (func_var) {
        /* Variadic function: DON'T spill params to locals.
         * Point them directly at the CALL save area where they're
         * already contiguous ascending:
         *   tcc_off +4  = saved A (param 0)
         *   tcc_off +8  = saved B (param 1)
         *   tcc_off +12 = saved C (param 2)
         *   tcc_off +16 = stack arg 0 (param 3)
         *   tcc_off +20 = stack arg 1 (param 4) ...
         *
         * This makes va_start/va_arg (i386-style ascending) work for
         * ANY number of args, since the CALL save area and stack args
         * are naturally contiguous. No spilling needed. */

        sym = func_sym->type.ref;
        while ((sym = sym->next) != NULL) {
            int align = 4, sz;
            int bt = sym->type.t & VT_BTYPE;
            int param_off;
            if (bt == VT_STRUCT || bt == VT_UNION)
                sz = type_size(&sym->type, &align);
            else
                sz = get_primitive_type_size(bt);
            if ((bt == VT_STRUCT || bt == VT_UNION) && sz == 0) {
                /* Empty aggregate parameters consume no incoming ABI word. */
                gfunc_set_param(sym, 4 + 4 * n, 0);
                continue;
            }
            sz = (sz + 3) & ~3;
            if (sz <= 0) sz = 4;

            /* Point param directly at CALL save area (positive offset) */
            param_off = 4 + 4 * n;

            if (tp_debug_check()) {
                fprintf(stderr,
                        "[PROLOG_VARARG] func=%s param=%d tcc_off=%d\n",
                        funcname ? funcname : "<unknown>", n, param_off);
            }

            gfunc_set_param(sym, param_off, 0);
            n += sz / 4;
        }
    } else {
        /* Non-variadic: home parameters unless incoming storage is reused. */
        while ((sym = sym->next) != NULL) {
            int align = 4, size, words, w;
            int param_addr;
            int bt = sym->type.t & VT_BTYPE;

            if (bt == VT_STRUCT || bt == VT_UNION) {
                size = type_size(&sym->type, &align);
            } else {
                size = get_primitive_type_size(bt);
            }
            if ((bt == VT_STRUCT || bt == VT_UNION) && size == 0) {
                /* Keep caller and callee layouts aligned with the GCC ABI. */
                gfunc_set_param(sym, loc, 0);
                continue;
            }
            size = (size + 3) & ~3;
            if (size <= 0)
                size = 4;
            words = size / 4;

            /* CALL's saved A/B/C words and the stacked words form one
             * ascending, word-aligned argument area.  A double may straddle
             * its third/fourth words, but its halves are still contiguous.
             * As for named variadic parameters above, this storage belongs
             * to the callee's by-value parameter and survives nested calls.
             * Use it directly instead of homing two words only to load them
             * into the FPU again.  Keep volatile initialization unchanged. */
            if (size == 8 && (bt == VT_DOUBLE || bt == VT_LDOUBLE) &&
                !(sym->type.t & VT_VOLATILE)) {
                gfunc_set_param(sym, 4 + 4 * n, 0);
                n += words;
                continue;
            }

            /* Other parameters retain their callee-local homes. */
            loc -= size;
            param_addr = loc;

            for (w = 0; w < words; w++) {
                int incoming_word = n + w;
                int src_off;

                if (incoming_word < 3) {
                    /* Saved A/B/C words at +4/+8/+12 (after CALL). */
                    src_off = 4 + 4 * incoming_word;
                } else {
                    /* Stack words start at caller_wptr+0 => callee +16. */
                    src_off = 16 + 4 * (incoming_word - 3);
                }

                if (tp_debug_check()) {
                    fprintf(stderr,
                            "[PROLOG_PARAM_WORD] func=%s param_word=%d src_off=%d dst_loc=%d param_size=%d\n",
                            funcname ? funcname : "<unknown>", incoming_word, src_off, loc + (w * 4), size);
                }

                emit_local_op(TP_OP_LDL, src_off);
                emit_local_op(TP_OP_STL, loc + (w * 4));
            }

            /* Tell TCC where this parameter is stored */
            gfunc_set_param(sym, param_addr, 0);
            n += words;
        }
    }
}
#include "transputer-fnopt.h"

/*
 * Size of the reserved region below the locals, in words.
 *
 * Layout is  [0 .. reserved-1] pseudo-registers + backend scratch + outgoing
 * call staging,  [reserved .. ] locals and spilled parameters.
 *
 * The outgoing-call staging area is NOT stacked on top of the register/scratch
 * region: tp_stage_call_words() places stack argument words at Wptr[0..n-1]
 * and the three register-argument spills at Wptr[spill_base..spill_base+2]
 * with spill_base >= func_max_slot_used+1, so the highest slot it can touch is
 * func_max_outgoing_words-1.  Adding it to the reserved size (as the original
 * code did) simply wasted that many words and pushed every local that much
 * further out of the T9000's 32-word workspace cache.
 *
 * Policy is selectable for measurement via TCC_FRAME_POLICY:
 *   0  legacy   : 20 + outgoing
 *   1  max      : max(20, outgoing)
 *   2  dynamic  : max(func_max_slot_used+1, outgoing)   [default]
 */
static int tp_frame_policy_cached = -1;

static int tp_frame_policy(void)
{
    if (tp_frame_policy_cached < 0) {
        const char *e = tp_knob(TPK_TCC_FRAME_POLICY);
        tp_frame_policy_cached = (e && *e) ? atoi(e) : 2;
    }
    return tp_frame_policy_cached;
}

static int tp_frame_reserved_words(void)
{
    int r;

    switch (tp_frame_policy()) {
    case 0:
        return FRAME_SLOT_MIN_LOCALS + func_max_outgoing_words;
    case 1:
        r = FRAME_SLOT_MIN_LOCALS;
        break;
    default:
        if (func_uses_inline_asm) {
            r = FRAME_SLOT_MIN_LOCALS;
        } else {
            r = func_max_slot_used + 1;
            if (r < 0)
                r = 0;
        }
        break;
    }
    if (r < func_max_outgoing_words)
        r = func_max_outgoing_words;
    return r;
}

/* Tell the linker it may take this function apart and shrink its calls
 * (tp_link_relax): one word in the non-allocated section .note.tprelax,
 * relocated against the function, so the list survives `tcc -r`.  (A
 * .note name, because the kernel's modpost warns about any other
 * non-allocated section it does not know.)  Not for code the linker could
 * not decode safely - inline asm, a switch jump table addressed by
 * distance - nor with -g, whose line tables hold code offsets. */
static void tp_mark_relaxable(Sym *fsym)
{
    Section *s = NULL;
    int i;

    if (!fsym || !fsym->c || func_uses_inline_asm || func_has_switch_slots
        || tcc_state->do_debug)
        return;
    for (i = 1; i < tcc_state->nb_sections && !s; i++)
        if (!strcmp(tcc_state->sections[i]->name, ".note.tprelax"))
            s = tcc_state->sections[i];
    if (!s)
        s = new_section(tcc_state, ".note.tprelax", SHT_PROGBITS, 0);
    put_elf_reloc(symtab_section, s, s->data_offset, R_TRANSPUTER_32,
                  fsym->c);
    section_ptr_add(s, 4);
}

/* Write `ajw operand` into the 3-byte placeholder at `off`, which was
 * reserved before the frame size was known.  Up to 4095 words fit it; a
 * larger frame is left as `ajw 0` for tp_relax_function() to widen
 * (wide_log). */
static void tp_patch_ajw_slot(int off, int operand)
{
    uint8_t enc_buf[16];
    uint8_t *ajw_ptr;
    int enc_len, pad, i;
    const int placeholder_size = 3;

    if (off + placeholder_size > cur_text_section->data_offset)
        tcc_internal_error("ajw placeholder outside the section");
    ajw_ptr = cur_text_section->data + off;

    enc_len = tp_encode_instr(enc_buf, TP_OP_AJW, operand);
    if (enc_len > placeholder_size) {
        wide_log_add(off + placeholder_size - 1, TP_OP_AJW, operand);
        enc_len = tp_encode_instr(enc_buf, TP_OP_AJW, 0);
    }

    pad = placeholder_size - enc_len;
    if (pad < 0) pad = 0;
    for (i = 0; i < pad; i++)
        ajw_ptr[i] = 0x20;  /* PFIX 0 = safe NOP */
    for (i = 0; i < enc_len; i++)
        ajw_ptr[pad + i] = enc_buf[i];
}

/* A return with a value that is not the last statement of the function.
 * Other targets jump to the shared epilogue, but here that jump is a `j`, a
 * timeslice point: a low-priority process descheduled there resumes with
 * whatever the other processes left in AReg/BReg, and the return value
 * gfunc_return() loaded is gone (tests/test_return_timeslice.c).  So it
 * leaves through an epilogue of its own, `ajw +frame; ret`, which also saves
 * the jump.  A return without a value still jumps: nothing is live, and the
 * jump is the smaller code.  The frame size is not known yet: reserve the
 * prologue's 3-byte AJW placeholder for gfunc_epilog to patch. */
ST_FUNC void gfunc_leave(void)
{
    if (nocode_wanted)
        return;
    if (leave_ajw_count == leave_ajw_cap) {
        leave_ajw_cap = leave_ajw_cap ? leave_ajw_cap * 2 : 16;
        leave_ajw = tcc_realloc(leave_ajw, leave_ajw_cap * sizeof(*leave_ajw));
    }
    leave_ajw[leave_ajw_count++] = ind;
    g((TP_OP_PFIX << 4) | 0);
    g((TP_OP_PFIX << 4) | 0);
    g((TP_OP_AJW << 4) | 0);
    Tp_OPR(TP_OPR_RET);

    /* Nothing reaches the code after this but a jump to a label. */
    abcreg_stack_depth = 0;
    tp_hist_clear();
    tp_areg_invalidate();
}

ST_FUNC void gfunc_epilog(void)
{
    static int patch_counter = 0;
    uint8_t *ptr;
    int frame_size;
    int before_ret;
    struct local_patch *p;

    if (tp_debug_check()) {
        fprintf(stderr, "[EPILOG_ENTRY] func=%s ind=0x%x\n",
                funcname ? funcname : "<unknown>", (unsigned int)ind);
    }

    {
        loc = (loc - 3) & ~3;
        func_frame_reserved = tp_frame_reserved_words();
        frame_size = -loc + (func_frame_reserved * 4);
        func_frame_words = frame_size / 4;
        if (frame_size > 200 && tp_debug_check())
            fprintf(stderr, "[LARGE_FRAME] func=%s loc=%d frame_size=%d (%d words) outgoing=%d\n",
                    funcname ? funcname : "<unknown>", loc, frame_size,
                    frame_size/4, func_max_outgoing_words);
    }

    /*
     * CRITICAL: After CALL, WPtr[0] contains the return address.
     * We need to allocate space for:
     *   - Reserved slots 0..max_slot_used (dynamic, based on actual usage)
     *   - Any declared local variables
     *
     * After AJW -N, the return address moves
     * to WPtr[N], safely out of the way.
     */

    /* 1. Patch Prologue AJW */
    /* Note: func_prologue_ajw_offset >= 0 is valid (function can start at offset 0) */
    if (func_prologue_ajw_offset >= 0) {
        /*
         * Prologue has 3 bytes: NFIX, PFIX, AJW
         * AJW operand is negative: -(frame_size / 4) to allocate stack space
         * After AJW -N, return address moves from WPtr[0] to WPtr[N]
         */
        tp_patch_ajw_slot(func_prologue_ajw_offset, -(frame_size / 4));
    }

    /* ... and the AJW +N of every early return's own epilogue (gfunc_leave). */
    {
        int k;
        for (k = 0; k < leave_ajw_count; k++)
            tp_patch_ajw_slot(leave_ajw[k], frame_size / 4);
    }

    /* 2. Patch Local Accesses */
    /* Frame layout:
        [High]
        [Params > 3]
        [Ret Addr]
        [Saved A,B,C] <-- Old Wptr
        [Local N]
        ...
        [Local 0]     <-- New Wptr (Wptr = Old - FrameSize)
    */

    for (p = local_patches; p; p = p->next) {
        int real_offset;
        int tcc_off = p->tcc_offset;

        if (tcc_off > 0) {
            /* Stack Parameter (Args 4+) */
            /* Offset = FrameSize + tcc_off */
            real_offset = (frame_size + tcc_off) / 4;
        } else {
            /* Local Variable (includes spilled Reg Params) */
            /* TCC loc is negative (top down).
                Wptr is at bottom.
                Offset from Wptr = FrameSize + tcc_off.
                Ex: Frame=16. Loc=-4. Real=12.
            */
            real_offset = (frame_size + tcc_off) / 4;
        }

        if (0) fprintf(stderr, "[PATCH_LOCAL_%d] func=%s instr_off=0x%x tcc_off=%d frame=%d real_off=%d op=%s\n",
                patch_counter++,
                funcname ? funcname : "<unknown>",
                p->instr_offset, tcc_off, frame_size, real_offset,
                p->opcode == TP_OP_LDL ? "LDL" : (p->opcode == TP_OP_STL ? "STL" :
                 (p->opcode == TP_OP_LDLP ? "LDLP" : "UNKNOWN")));

        /* Safety check: ensure instr_offset + placeholder_size is within bounds */
        if (p->instr_offset + p->placeholder_size > cur_text_section->data_offset)
            tcc_internal_error("local access placeholder outside the section");

        /* Rewrite placeholder using tp_encode_instr.
         * Pad shorter encodings with PFIX 0 (0x20) at the front. */
        ptr = cur_text_section->data + p->instr_offset;
        {
            uint8_t enc_buf[8];
            int enc_len = tp_encode_instr(enc_buf, p->opcode, real_offset);
            int pad, i;
            int placeholder_size = p->placeholder_size;

            /* An offset the placeholder cannot reach: leave `op 0` for
               tp_relax_function() to widen (wide_log). */
            if (enc_len > placeholder_size) {
                wide_log_add(p->instr_offset + placeholder_size - 1,
                             p->opcode, real_offset);
                enc_len = tp_encode_instr(enc_buf, p->opcode, 0);
            }

            pad = placeholder_size - enc_len;
            if (pad < 0) pad = 0;
            for (i = 0; i < pad; i++)
                ptr[i] = 0x20;  /* PFIX 0 = safe NOP */
            for (i = 0; i < enc_len && i < placeholder_size; i++)
                ptr[pad + i] = enc_buf[i];
        }
    }

    /* 3. Bind return label and emit epilogue code */
    /* CRITICAL: Bind rsym label here so all return jumps point to the epilogue */
    if (0) fprintf(stderr, "[EPILOG_DEBUG] Calling gsym(rsym=0x%x) at ind=0x%x func=%s\n",
            rsym, ind, funcname ? funcname : "<unknown>");
    gsym(rsym);
    if (0) fprintf(stderr, "[EPILOG_DEBUG] gsym returned, epilogue at ind=0x%x\n", ind);

    /* Restore stack pointer before RET.
     * AJW is a direct instruction that takes operand from Oreg (via prefix bytes).
     * Simply emit AJW with a positive frame_size/4 to deallocate the frame.
     *
     * Wptr corruption check: Verify Wptr hasn't been corrupted before returning.
     * Corruption symptoms: Infinite loops, wrong return addresses, corrupted data.
     */
    if (0) fprintf(stderr, "[EPILOG_DEBUG] func=%s frame_size=%d ajw_operand=%d ind=0x%x\n",
            funcname ? funcname : "<unknown>", frame_size, frame_size / 4, ind);
    if (0) fprintf(stderr, "[WPTR_CHECK] frame_size=%d func=%s\n",
            frame_size, funcname ? funcname : "<unknown>");

    if (frame_size > 0) {
        int before_ajw = ind;
        emit_instr(TP_OP_AJW, frame_size / 4);
        if (0) fprintf(stderr, "[EPILOG_DEBUG] AJW emitted at 0x%x-%x\n", before_ajw, ind);
        if (0) fprintf(stderr, "[WPTR_DEBUG] func=%s before_AJW=0x%x after_AJW=0x%x (delta=%d)\n",
                funcname ? funcname : "<unknown>", (unsigned int)before_ajw, (unsigned int)ind,
                (int)(ind - before_ajw));
    }

    before_ret = ind;
    Tp_OPR(TP_OPR_RET);
    if (0) fprintf(stderr, "[EPILOG_DEBUG] RET emitted at 0x%x-%x\n", before_ret, ind);

    if (tp_trace_func_active() && func_start_ind >= 0) {
        tp_trace_emit_bytes("func_dump", func_start_ind, ind);
    }

    /* Branch relaxation: rewrite the finished function with minimal encodings,
     * reclaiming the PFIX 0 padding left by the local/jump/AJW placeholders. */
    {
        static int relax_off = -1;
        if (relax_off < 0)
            relax_off = tp_knob(TPK_TP_NO_RELAX) ? 1 : 0;
        if (!relax_off)
            tp_relax_function();
        else
            tp_relax_why = "TP_NO_RELAX";
    }

    /* A frame over 4095 words only becomes encodable once the relaxer has
     * widened the placeholders that could not hold their offsets. */
    if (wide_log_count)
        tcc_error("function '%s' needs a %d-word stack frame; frames over "
                  "4095 words are encoded by the relaxation pass, which "
                  "could not process this function (%s)",
                  funcname ? funcname : "?", func_frame_words,
                  tp_relax_why ? tp_relax_why : "?");

    tp_mark_relaxable(func_start_sym);
    transputer_func_reset();
}

#include "transputer-call.h"

#include "transputer-int.h"

#include "transputer-fp.h"

#endif /* !TARGET_DEFS_ONLY */
