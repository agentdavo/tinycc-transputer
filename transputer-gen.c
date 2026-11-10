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
        v = getenv("TP_NO_CALLOPT") ? 1 : 0;
    return v;
}
static int tp_case_cmp(uint64_t a, uint64_t b, int is_unsigned);
static uint64_t tp_switch_total_selectors(struct switch_t *sw, int is_unsigned);
static void tp_switch_emit_instr(int opcode, int operand);
static void tp_switch_emit_fixed_slot(int opcode, int operand, int slot_size);
static void tp_switch_patch_fixed_slot(int offset, int opcode, int operand, int slot_size);
static void switch_slot_add(int instr_offset, int slot_size, int opcode,
                            int target_addr, int base_addr);
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
static int func_start_ind;

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
        v = getenv("TP_NO_LDNLP") ? 1 : 0;
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

/* Tracks the STL that was emitted most recently, so a matching LDL of the same
 * workspace-slot register can cancel it instead of round-tripping the value
 * through memory. Valid only while tp_last_stl_end == ind, i.e. while nothing
 * whatsoever has been emitted since — which also makes raw g() byte writes
 * invalidate it for free. */
static int tp_last_stl_reg = -1;
static int tp_last_stl_off = -1;
static int tp_last_stl_end = -1;

/* The slot gen_opf last stored an FPU result to, and where that `ldlp slot;
 * fpstnl` is; see tp_fpu_take_resident(). */
static int tp_fpr_reg = -1;
static int tp_fpr_off = -1;
static int tp_fpr_end = -1;
static int tp_fpr_db;
static int tp_fpu_take_resident(SValue *sv, int is_db);

/* Same idea, but for the immediately preceding instruction of any kind. Used
 * to fold a constant address adjustment into the following non-local access. */
static int tp_last_opc = -1;
static int tp_last_val = 0;
static int tp_last_off = -1;
static int tp_last_end = -1;

/* One further back, so that cancelling a store can re-expose the instruction
 * that preceded it — typically the ADC whose offset we then want to fold into
 * the following LDNL/STNL. */
static int tp_prev_opc = -1;
static int tp_prev_val = 0;
static int tp_prev_off = -1;
static int tp_prev_end = -1;

/* A short trailing window of emitted instructions.
 *
 * The two trackers above only reach one and two instructions back, which is
 * enough to fold `ADC k` into the LDNL that immediately follows it. The store
 * path needs more: by the time store() runs, the address has been computed,
 * offset and spilled, and the whole RHS has been emitted on top of it, so the
 * ADC is four or more instructions behind the STNL. Keeping a small window of
 * what was emitted lets store() roll the tail back and rebuild it in the order
 * the T9000 wants.
 *
 * Only instructions emitted through emit_instr() and emit_local_op() are
 * recorded. Anything written with a raw g() — LDC_ABS relocation placeholders,
 * jump placeholders — leaves a gap that the contiguity check below detects, so
 * those invalidate the window for free, exactly as they do for tp_last_*. */
#define TP_HIST_N 24
#define TP_HIST_INSTR 0    /* emitted by emit_instr(): val is the operand   */
#define TP_HIST_LOCAL 1    /* emitted by emit_local_op(): val is a byte offset */

struct tp_hist_ent {
    int kind;
    int opc;
    int val;
    int off;
    int end;
};

/* hist[0] is the most recently emitted instruction. */
static struct tp_hist_ent tp_hist[TP_HIST_N];
static int tp_hist_n;

static void tp_hist_clear(void)
{
    tp_hist_n = 0;
}

/* Code from `off` onwards has been rolled back; forget it. */
static void tp_hist_rollback(int off)
{
    while (tp_hist_n > 0 && tp_hist[0].end > off) {
        int i;
        for (i = 0; i < tp_hist_n - 1; i++)
            tp_hist[i] = tp_hist[i + 1];
        tp_hist_n--;
    }
}

static void tp_hist_record(int kind, int opc, int val, int off, int end)
{
    int i;
    for (i = (tp_hist_n < TP_HIST_N ? tp_hist_n : TP_HIST_N - 1); i > 0; i--)
        tp_hist[i] = tp_hist[i - 1];
    tp_hist[0].kind = kind;
    tp_hist[0].opc = opc;
    tp_hist[0].val = val;
    tp_hist[0].off = off;
    tp_hist[0].end = end;
    if (tp_hist_n < TP_HIST_N)
        tp_hist_n++;
}

static void tp_stl_track_clear(void)
{
    tp_last_stl_reg = -1;
    tp_last_stl_off = -1;
    tp_last_stl_end = -1;
    tp_last_opc = -1;
    tp_last_off = -1;
    tp_last_end = -1;
    tp_prev_opc = -1;
    tp_prev_off = -1;
    tp_prev_end = -1;
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

    /* Never rewind under nocode_wanted: emission is suppressed, so `ind` is
     * frozen and the trackers may describe *live* pre-region code.  Folding
     * would delete that code while the replacement is itself suppressed. */
    if (nocode_wanted)
        return base_words;
    if ((tp_last_opc != TP_OP_ADC && tp_last_opc != TP_OP_LDNLP) ||
        tp_last_end != ind || tp_last_off < 0)
        return base_words;
    if (tp_last_off < func_start_ind)
        return base_words;
    if (tp_last_val == 0)
        return base_words;
    if (tp_last_opc == TP_OP_LDNLP) {
        /* LDNLP already carries a word displacement.  Folding it away is still
         * a win: the access resolves the displacement itself, so this removes
         * an instruction as well as a stage-2 slot. */
        words = base_words + tp_last_val;
    } else {
        if (tp_last_val & 3)
            return base_words;
        words = base_words + (tp_last_val >> 2);
    }
    /* Keep the folded form to encodings that are no longer than what we
     * removed; a huge displacement would just move the prefix bytes around. */
    if (words < 0 || words > 4095)
        return base_words;

    ind = tp_last_off;
    cur_text_section->data_offset = ind;
    tp_hist_rollback(ind);
    tp_stl_track_clear();
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
    if (!nocode_wanted &&
        tp_last_stl_reg == r && tp_last_stl_end == ind &&
        tp_last_stl_off >= 0 && tp_last_stl_off >= func_start_ind &&
        !tp_reg_live_on_vstack(r, except)) {
        int roll_to = tp_last_stl_off;
        int p_opc = tp_prev_opc, p_val = tp_prev_val;
        int p_off = tp_prev_off, p_end = tp_prev_end;

        ind = roll_to;
        cur_text_section->data_offset = ind;
        tp_hist_rollback(ind);
        tp_stl_track_clear();
        /* The instruction before the cancelled store is once again the last
         * thing emitted, so re-expose it — otherwise a preceding ADC can no
         * longer be folded into the LDNL/STNL that follows. */
        if (p_end == roll_to) {
            tp_last_opc = p_opc;
            tp_last_val = p_val;
            tp_last_off = p_off;
            tp_last_end = p_end;
        }
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

#define TP_SWITCH_JUMP_SLOT_SIZE 9
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
    int ldc_slot_off, table_anchor, table_off, miss_ind;
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

    targets = tcc_malloc(entries * sizeof(*targets));
    entry_offs = tcc_malloc(entries * sizeof(*entry_offs));
    for (i = 0; i < entries; ++i)
        targets[i] = -1;

    for (i = 0; i < sw->n; ++i) {
        struct case_t *p = sw->p[i];
        if (is_unsigned) {
            uint32_t v, v2 = (uint32_t)p->v2;
            for (v = (uint32_t)p->v1; ; ++v) {
                int idx = (int)((uint64_t)v - (uint64_t)min_u);
                if (idx < 0 || idx >= entries)
                    tcc_error("transputer switch index out of bounds");
                targets[idx] = p->ind;
                if (v == v2)
                    break;
            }
        } else {
            int64_t v, v2 = (int32_t)p->v2, min_s = (int32_t)min_u;
            for (v = (int32_t)p->v1; tp_case_cmp(v, v2, 0) <= 0; ++v) {
                int idx = (int)(v - min_s);
                if (idx < 0 || idx >= entries)
                    tcc_error("transputer switch index out of bounds");
                targets[idx] = p->ind;
            }
        }
    }

    d = *pdsym;

    {
        int ty = is_unsigned ? (VT_INT | VT_UNSIGNED) : VT_INT;

        vdup();
        vpush64(ty, min_u);
        gen_op(TOK_LT);
        d = gvtst(0, d);

        vdup();
        vpush64(ty, max_u);
        gen_op(TOK_GT);
        d = gvtst(0, d);

        vdup();
        if (min_u != 0) {
            vpush64(ty, min_u);
            gen_op('-');
        }
    }

    gv(RC_INT);
    idx_reg = vtop->r & VT_VALMASK;
    if (idx_reg < VT_CONST)
        tp_switch_emit_instr(TP_OP_LDL, idx_reg);
    else if (idx_reg == VT_CONST && !(vtop->r & VT_SYM))
        tp_switch_emit_instr(TP_OP_LDC, vtop->c.i);
    else
        tcc_error("transputer switch: unsupported idx value form");

    if (TP_SWITCH_JUMP_SLOT_SIZE != 1) {
        tp_switch_emit_instr(TP_OP_LDC, TP_SWITCH_JUMP_SLOT_SIZE);
        tp_switch_emit_instr(TP_OP_OPR, TP_OPR_PROD);
    }

    ldc_slot_off = ind;
    tp_switch_emit_fixed_slot(TP_OP_LDC, 0, TP_SWITCH_JUMP_SLOT_SIZE);
    tp_switch_emit_instr(TP_OP_OPR, TP_OPR_LDPI);

    table_anchor = ind;
    tp_switch_emit_instr(TP_OP_OPR, TP_OPR_BSUB);
    tp_switch_emit_instr(TP_OP_OPR, TP_OPR_GCALL);

    table_off = ind;
    for (i = 0; i < entries; ++i) {
        entry_offs[i] = ind;
        tp_switch_emit_fixed_slot(TP_OP_J, 0, TP_SWITCH_JUMP_SLOT_SIZE);
    }

    miss_ind = gind();
    d = gjmp(d);

    tp_switch_patch_fixed_slot(ldc_slot_off, TP_OP_LDC, table_off - table_anchor,
                               TP_SWITCH_JUMP_SLOT_SIZE);
    switch_slot_add(ldc_slot_off, TP_SWITCH_JUMP_SLOT_SIZE, TP_OP_LDC,
                    table_off, table_anchor);
    for (i = 0; i < entries; ++i) {
        int target = targets[i] >= 0 ? targets[i] : miss_ind;
        tp_switch_patch_fixed_slot(entry_offs[i], TP_OP_J,
                                   target - (entry_offs[i] + TP_SWITCH_JUMP_SLOT_SIZE),
                                   TP_SWITCH_JUMP_SLOT_SIZE);
        switch_slot_add(entry_offs[i], TP_SWITCH_JUMP_SLOT_SIZE, TP_OP_J,
                        target, -1);
    }

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
    env = getenv("T4_DEBUG");
    tp_debug_enabled = (env && *env) ? 1 : 0;
    env = getenv("T4_GENOPI_DEBUG");
    tp_genopi_debug = env && *env;
    env = getenv("T4_LOAD_DEBUG");
    tp_load_debug = env && *env;
    env = getenv("T4_INSTR_DEBUG");
    tp_instr_debug = env && *env;
    env = getenv("T4_EMIT_LOCAL_DEBUG");
    tp_emit_local_debug = env && *env ? atoi(env) : 0;
    env = getenv("T4_GEN_SYM");
    tp_gen_sym_filter = (env && *env) ? env : NULL;
    env = getenv("T4_STORE_DEBUG");
    tp_store_debug = env && *env;
    env = getenv("T4_STRUCTCOPY_DEBUG");
    tp_structcopy_debug = env && *env;
    env = getenv("T4_CALLARG_DEBUG");
    tp_callarg_debug = env && *env;
    env = getenv("T4_OPF_DEBUG");
    tp_opf_debug = env && *env;
    env = getenv("T4_VT_LOCAL_DEBUG");
    tp_vt_local_debug = env && *env;
    if (!tp_vt_local_debug) {
        env = getenv("TCC_TRANSPUTER_DEBUG_VT");
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
static int func_start_ind = -1;
static int func_start_nsym;   /* symtab size when the function began */
static int func_start_nrel;   /* its code section's relocation count then */
static Sym *func_start_sym;   /* the function itself */

static int tp_trace_func_enabled = -1;
static char tp_trace_func_name[64];

static int tp_trace_func_active(void) {
    const char *env;
    if (tp_trace_func_enabled == -1) {
        env = getenv("TCC_TRACE_FUNC");
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

struct switch_slot_meta {
    int instr_offset;                 /* fixed-width slot start */
    int slot_size;                    /* slot width used by switch table */
    int opcode;                       /* TP_OP_LDC or TP_OP_J */
    int target_addr;                  /* jump target or table offset target */
    int base_addr;                    /* base address for relative LDC, -1 for J */
    struct switch_slot_meta *next;
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
static struct switch_slot_meta *switch_slots = NULL;

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

static void switch_slot_add(int instr_offset, int slot_size, int opcode,
                            int target_addr, int base_addr)
{
    struct switch_slot_meta *p = tcc_malloc(sizeof(*p));
    p->instr_offset = instr_offset;
    p->slot_size = slot_size;
    p->opcode = opcode;
    p->target_addr = target_addr;
    p->base_addr = base_addr;
    p->next = switch_slots;
    switch_slots = p;
}

/* Reset per function */
static void transputer_func_reset(void) {
    struct local_patch *p = local_patches;
    struct switch_slot_meta *s = switch_slots;

    while(p) {
        struct local_patch *next = p->next;
        tcc_free(p);
        p = next;
    }
    while (s) {
        struct switch_slot_meta *next = s->next;
        tcc_free(s);
        s = next;
    }
    local_patches = NULL;
    local_patches_tail = NULL;
    jump_patch_count = 0;
    switch_slots = NULL;
    tp_hist_clear();
    tp_fpr_reg = -1;
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
/* NOTE: g(), gen_le16(), gen_le32() are defined in transputer-asm.c */

/* Emit Transputer instruction with nibble operand (handles Oreg properly) */
/* TCC_TRANSPUTER_FPLDNLI=0 keeps `wsub; fpldnlsn' apart, for an A/B run. */
static int tp_fpu_indexed_ok(void)
{
    static int ok = -1;

    if (ok < 0) {
        const char *e = getenv("TCC_TRANSPUTER_FPLDNLI");
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
        env = getenv("T4_NFIX_OPR_WARN");
        nfix_opr_warn = env && *env ? atoi(env) : 0;
        env = getenv("T4_CALL_EMIT_TRACE");
        call_log_limit = env && *env ? atoi(env) : 0;
        env = getenv("T4_CALL_EMIT_SHORT");
        call_log_short = env && *env ? atoi(env) : 0;
        call_log_filter = getenv("T4_CALL_EMIT_FILTER");
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
        tp_stl_track_clear();
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
         * with `tp_last_stl_end == ind`.  While emission is suppressed `ind`
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
        tp_stl_track_clear();
        tp_hist_clear();
        return;
    }

    /* Emit all bytes from the encoder */
    for (i = 0; i < len; i++) {
        g(buf[i]);
    }
    if (opc == TP_OP_STL && v >= 0 && v < NB_REGS) {
        tp_last_stl_reg = v;
        tp_last_stl_off = start_off;
        tp_last_stl_end = ind;
    } else {
        tp_stl_track_clear();
    }
    tp_prev_opc = tp_last_opc;
    tp_prev_val = tp_last_val;
    tp_prev_off = tp_last_off;
    tp_prev_end = tp_last_end;
    tp_last_opc = opc;
    tp_last_val = v;
    tp_last_off = start_off;
    tp_last_end = ind;
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
        const char *e = getenv("TP_T9K_MASK");
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
        const char *e = getenv("TP_POOL");
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
        v = getenv("TP_POOL_DEBUG") ? 1 : 0;
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
    tp_stl_track_clear();
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
        && tp_last_stl_reg == vr && tp_last_stl_end == ind
        && tp_last_stl_off >= 0 && tp_last_stl_off >= func_start_ind
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
            swap_off = getenv("TP_NO_STORE_SWAP") ? 1 : 0;
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
    tp_stl_track_clear();
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
/*************************************************************/
/* Function-level branch relaxation                          */
/*************************************************************/

/* Every local access is emitted as a fixed 3-byte placeholder (PFIX 0; PFIX 0;
 * OP 0) because the frame size is not known until the function has been fully
 * parsed. gfunc_epilog then patches the real operand in, left-padded with
 * PFIX 0 bytes — which decode as no-ops. Jump placeholders and the prologue AJW
 * are padded the same way. Measured over 112 real kernel translation units,
 * that padding is 11.3% of .text (a further 11% sits in CALL/LDC relocation
 * placeholders, which only a link-time pass could shrink).
 *
 * This pass rewrites the finished function with minimal encodings. It does not
 * trust any of the emit-time bookkeeping for instruction boundaries: prefix
 * encoding is self-describing, so the byte range is decoded linearly and every
 * instruction re-derived. That avoids the whole class of bugs where a stale
 * instr_offset/instr_len causes the re-encoder to overwrite an unrelated byte.
 *
 * An instruction is pinned at its current size when a relocation points into
 * it (the linker patches those in place and assumes the reserved width), and
 * the whole function is skipped when it contains anything else that records
 * absolute code offsets.
 */

#define TP_RELAX_MAX_INSNS 65536

struct tp_relax_insn {
    int old_off;      /* offset from func_start_ind, before relaxation */
    int new_off;      /* offset after relaxation */
    int old_len;
    int new_len;
    int opcode;       /* high nibble, or -1 for a raw/unknown byte run */
    int operand;
    int is_branch;    /* J, CJ or a near CALL: operand is a displacement */
    int target_old;   /* branch target, as an old offset */
    int ext;          /* target is before the function and does not move */
    int pinned;       /* relocation points into this instruction */
    int synth;        /* inserted by tp_gaddr_cache: no bytes in the old code;
                         old_off is that of the instruction it precedes */
};

/* A branch's target after relaxation, as a new offset. */
static int tp_relax_newt(const struct tp_relax_insn *ins, int i,
                         const int *tidx, int size, int outlen)
{
    int t = ins[i].target_old;

    if (ins[i].ext)
        return t;
    if (t >= 0 && t < size && tidx[t] >= 0)
        return ins[tidx[t]].new_off;
    return outlen;
}

/* LDLP s; [ADC k;] LB with 0 <= k <= 3 reads byte k of workspace word s
 * (tp_shift_mask_fold's byte form of (x >> 8K) & m): the address goes
 * nowhere, so it is a plain read of word s, as LDLP s; LDNL 0 is. */
static int tp_relax_byte_read(const struct tp_relax_insn *ins, int n, int i)
{
    int j = i + 1;

    if (ins[i].opcode != TP_ENC_LDLP)
        return 0;
    while (j < n && ins[j].opcode == -1)
        j++;
    if (j < n && ins[j].opcode == TP_ENC_ADC &&
        ins[j].operand >= 0 && ins[j].operand <= 3)
        for (j++; j < n && ins[j].opcode == -1; j++)
            ;
    return j < n && ins[j].opcode == TP_ENC_OPR && ins[j].operand == TP_OPR_LB;
}

/* Why the last function could not be relaxed, for the frame-size error. */
static const char *tp_relax_why;

/* Diagnostic for why a function could not be relaxed; set TP_RELAX_STATS=1. */
static int tp_relax_bail(const char *why)
{
    static int stats = -1;
    if (stats < 0)
        stats = getenv("TP_RELAX_STATS") ? 1 : 0;
    if (stats)
        fprintf(stderr, "[RELAXBAIL] %s\n", why);
    tp_relax_why = why;
    return 0;
}

/*************************************************************/
/* Evaluation-stack scheduling                               */
/*************************************************************/
/*
 * The register allocator maps every TCC pseudo-register onto a workspace
 * slot, so each intermediate value is written out with STL and read back
 * with LDL.  On a T9000 that is the most expensive habit in the emitted
 * code: stage 5 issues one write per cycle, so every STL closes its
 * instruction group, and a store also consumes one of the two stage-2
 * address-calculation slots.  Measured over 458 kernel objects, ldl+stl are
 * 58% of all instructions and 71% of instruction groups are closed by a
 * write.
 *
 * The hardware already has a three-deep evaluation stack (AReg/BReg/CReg),
 * and the left-to-right evaluation order the front end uses puts operands
 * into exactly the registers the ALU instructions expect.  This pass
 * recognises the round trips the always-STL strategy leaves behind and
 * deletes them, letting values stay where they already are.
 *
 * It runs on the finished function, after local slots and jump
 * displacements have been patched, so it sees the real instruction stream
 * rather than the emitter's intent.  That side-steps the reason the earlier
 * emit-time attempt (tp_pending_stl_slot, since removed)
 * was abandoned: no code path can touch a workspace slot behind this pass's
 * back, because there is no "back" - every instruction is in front of it.
 * The one reader outside the function, a callee taking its stacked arguments
 * from Wptr[0..n-1], is charged to the call instruction (tps_ann.uses).
 *
 * Patterns, hardware stack written bottom-to-top as [C, B, A]:
 *
 *   S1   STL a ; LDL a                  -> (nothing)
 *   S2   STL a ; STL b ; LDL b ; LDL a  -> (nothing)
 *   S3   STL a ; STL b ; LDL a ; LDL b  -> REV
 *   S4   STL a ; <e> ; LDL a            -> <e>          (<e> net 0)
 *   S5   STL a ; <e> ; LDL a            -> <e> ; REV    (<e> net +1)
 *   S6   LDL x / LDC k ; STL y          -> (nothing)    (y never read)
 *   S7   STL a ; STL b ; LDL a          -> REV ; STL b
 *   S9   CALL ; STL a                   -> CALL         (a never read)
 *   S10  LDL x ; STL a ; LDLP a ; <FP load>  -> LDLP x ; <FP load>
 *                                                        (a dead after)
 *   S11  LDL x ; [LDNLP k] ; STL a ; <e> ; LDL a ; <use>
 *                                   -> <e> ; LDL x ; <use, displacement +k>
 *   S12  LDNLP k ; LDNL/STNL/LDNLP m        -> LDNL/STNL/LDNLP k+m
 *
 * (S8 counts the REV deletions further down.)
 *
 * S1..S3, S6 and S7 are exact identities at any stack depth.  S4/S5 carry one
 * extra value underneath <e>, so they also require that
 *
 *   - <e> never reads deeper than it has pushed, otherwise it would consume
 *     the carried value instead of its own operand;
 *   - the deepest point of <e>, plus the carried value, still fits in the
 *     three-deep stack.
 *
 * Every pattern additionally requires the slots it stops writing to be dead
 * afterwards, decided by a backward liveness fixpoint over the function's
 * own control-flow graph.  The matcher iterates, because deleting one round
 * trip frequently exposes the next: S4 turns
 *     stl 9; ldl y; ldc 3; shl; stl 10; ldl 9; ldl 10; or
 * into
 *     ldl y; ldc 3; shl; stl 10; ldl 10; or
 * whose tail is then an S1.
 *
 * Functions the pass declines to touch: anything with a mid-function AJW or
 * a GAJW (the indirect-call and computed-goto trampolines build a temporary
 * frame, which renumbers every workspace slot, and a VLA moves Wptr), and
 * anything that takes the address of a pseudo-register slot for an access
 * whose target word the pass cannot determine.
 */

/* Upper bound on the pseudo-register slots; the actual count for a function is
 * min(func_frame_reserved, NB_REGS), computed as `nslot` in
 * tp_sched_function(), because the reserved region can now be smaller than the
 * register file and locals then live in low slot numbers. */
#define TPS_NSLOT   NB_REGS
#define TPS_MAXWIN  24            /* instructions scanned for an S4/S5 tail */
#define TPS_ROUNDS  6

struct tps_ann {
    int proven_pair;        /* original second index load + 1, or 0 */
    int depth;              /* eval-stack depth before this insn, -1 unknown */
    int delta;              /* stack effect, valid when known */
    int succ1;              /* fall-through/target successor, -1 = none */
    int succ2;              /* second successor (CJ), -1 = none */
    short use;              /* pseudo-register slot read, -1 = none */
    short def;              /* pseudo-register slot written, -1 = none */
    unsigned int uses;      /* more slots read: a call's stacked arguments */
    int reads;              /* stack cells consumed */
    unsigned int liveout;
    unsigned char known;    /* delta/reads are trustworthy */
    unsigned char head;     /* branch target: the emitter treats depth as 0 */
    unsigned char dead;     /* marked for deletion */
};

/*
 * Stack behaviour of a decoded instruction: how many cells it reads and how
 * the depth changes.  Anything not listed here is a barrier - the pass will
 * not reason about the evaluation stack across it.  Being absent from this
 * table is always safe; being present with the wrong numbers is not, so it
 * only lists opcodes this back end actually emits, taken from the emulator's
 * case labels in transputer/t4/p.c.
 */
static int tps_effect(int opc, int opd, int *delta, int *reads)
{
    switch (opc) {
    case -1:                                  /* PFIX 0 padding: a no-op */
    case TP_ENC_J:
        *reads = 0; *delta = 0;  return 1;
    case TP_ENC_AJW:                          /* moves Wptr, not the stack */
        *reads = 0; *delta = 0;  return 1;
    case TP_ENC_LDL: case TP_ENC_LDLP: case TP_ENC_LDC:
        *reads = 0; *delta = 1;  return 1;
    case TP_ENC_LDNL: case TP_ENC_LDNLP:
    case TP_ENC_ADC:  case TP_ENC_EQC:
        *reads = 1; *delta = 0;  return 1;
    case TP_ENC_STL:
        *reads = 1; *delta = -1; return 1;
    case TP_ENC_CJ:                           /* the not-taken edge pops */
        *reads = 1; *delta = -1; return 1;
    case TP_ENC_STNL:
        *reads = 2; *delta = -2; return 1;
    case TP_ENC_OPR:
        switch (opd) {
        /* Only the integer stack matters here. Indexed FP loads pop both
         * the base and the index, so a third word survives in A. */
        case TP_OPR_FPLDNLSNI: case TP_OPR_FPLDNLDBI:
            *reads = 2; *delta = -2; return 1;
        case TP_FPU_LDNLSN: case TP_FPU_LDNLDB:
        case TP_FPU_LDNLADDSN: case TP_FPU_LDNLADDDB:
        case TP_FPU_LDNLMULSN: case TP_FPU_LDNLMULDB:
        case TP_FPU_STNLSN: case TP_FPU_STNLDB:
            *reads = 1; *delta = -1; return 1;
        case TP_FPU_ADD: case TP_FPU_SUB: case TP_FPU_MUL: case TP_FPU_DIV:
            *reads = 0; *delta = 0; return 1;
        case TP_OPR_MINT:
            *reads = 0; *delta = 1;  return 1;
        case TP_OPR_DUP:
            *reads = 1; *delta = 1;  return 1;
        case TP_OPR_NOT: case TP_OPR_LB:
        /* T9000 unary narrow loads / sign extends (only reachable
           under -mt9000; harmless to describe unconditionally). */
        case TP_OPR_LS:  case TP_OPR_LSX: case TP_OPR_LBX:
        case TP_OPR_XSWORD: case TP_OPR_XBWORD:
            *reads = 1; *delta = 0;  return 1;
        case TP_OPR_REV:
            *reads = 2; *delta = 0;  return 1;
        case TP_OPR_POP:
            *reads = 1; *delta = -1; return 1;
        case TP_OPR_ADD:  case TP_OPR_SUB:   case TP_OPR_MUL:
        case TP_OPR_DIV:  case TP_OPR_REM:   case TP_OPR_SUM:
        case TP_OPR_DIFF: case TP_OPR_PROD:  case TP_OPR_AND:
        case TP_OPR_OR:   case TP_OPR_XOR:   case TP_OPR_SHL:
        case TP_OPR_SHR:  case TP_OPR_GT:    case TP_OPR_GTU:
        case TP_OPR_WSUB: case TP_OPR_BSUB:  case TP_OPR_WSUBDB:
        case TP_OPR_CSUB0: case TP_OPR_SSUB:
        case TP_OPR_XWORD:                    /* B sign-extended at mask A */
            *reads = 2; *delta = -1; return 1;
        case TP_OPR_SB: case TP_OPR_SS:
            *reads = 2; *delta = -2; return 1;
        }
        return 0;
    }
    return 0;
}

/* Per-pattern hit counts, printed with TP_SCHED_STATS=1. */
static int tps_stats[14];  /* S1..S12 hit counts (10, 11 unused) */
static int tps_stats_on = -1;

/* Bitmask of enabled patterns (bit 0 = S1 ... bit 6 = S7, bit 8 = S9,
 * bit 9 = S10, bit 10 = 5a' push hoisting, bit 11 = 5a'' CJ over J,
 * bit 12 = S11, bit 13 = S12), for bisecting a suspected miscompile:
 * TP_SCHED_MASK=0x3f3f disables S7, and so on.  S8 has no switch. */
static int tps_mask = -1;
static int tps_on(int which)
{
    if (tps_mask < 0) {
        const char *e = getenv("TP_SCHED_MASK");
        tps_mask = (e && *e) ? (int)strtol(e, NULL, 0) : 0x3f7f;
    }
    return (tps_mask >> which) & 1;
}

static void tps_note(int which)
{
    if (tps_stats_on < 0)
        tps_stats_on = getenv("TP_SCHED_STATS") ? 1 : 0;
    if (tps_stats_on)
        tps_stats[which]++;
}

static void tps_bail(const char *why)
{
    if (tps_stats_on < 0)
        tps_stats_on = getenv("TP_SCHED_STATS") ? 1 : 0;
    if (tps_stats_on)
        fprintf(stderr, "[SCHEDBAIL] %s %s\n", why, funcname ? funcname : "?");
}

static int tpg_ldlp_extent(const struct tp_relax_insn *c, int *lo);

/* Could a store through a pointer reach frame word x?  Only if the object
 * holding x had its address handed out (an LDLP that feeds anything but one
 * known access).  A local's object is the block of tp_loc_marks around it,
 * which no object straddles; a reserved word is its own, or the second word
 * of a double; above the locals (the frame-top words, the parameters) any
 * such LDLP counts.  `top` is the words tp_gaddr_cache put over the
 * locals. */
static int tps_word_escapes(struct tp_relax_insn *ins, int n, int x, int top)
{
    int L = func_frame_words - top, R = func_frame_reserved;
    int lo, hi, i, j, e;

    if (x >= L) {
        lo = L;
        hi = 0x7fffffff;
    } else if (x < R) {
        lo = x - 1;
        hi = x;
    } else {
        if (tp_loc_marks_bad)
            return 1;
        lo = R;
        hi = L;
        for (i = 0; i < tp_loc_marks_count; i++) {
            int m = tp_loc_marks[i], s;
            if (m > 0 || (m & 3))
                continue;
            s = L + m / 4;
            if (s <= x && s > lo)
                lo = s;
            if (s > x && s < hi)
                hi = s;
        }
    }
    for (i = 0; i < n; i++) {
        if (ins[i].opcode != TP_ENC_LDLP || ins[i].operand < lo ||
            ins[i].operand > hi)
            continue;
        for (j = i + 1; j < n && ins[j].opcode == -1; j++)
            ;
        if (j == n || tpg_ldlp_extent(&ins[j], &e) < 0)
            return 1;
    }
    return 0;
}

/* Is workspace word s dead after instruction m, on every path?  For a word
 * the liveness bitmask may not cover (one past the register file, such as
 * the scratch word 15, when a local's address escapes into an FP access).
 * A walk from m's successors: a path is settled by an STL of s and lost at
 * any read of it - LDL s, an LDLP access that reaches s, a call when s
 * holds a stacked argument - and at anything the walk cannot follow.  The
 * caller has checked that no LDLP hands out a reserved word's address in
 * a shape this cannot see.  The function's own end kills everything. */
static int tps_dead_after(struct tp_relax_insn *ins, struct tps_ann *an,
                          int n, int m, int s)
{
    int *stack = tcc_malloc(sizeof(int) * 2 * (n + 1));
    char *seen = tcc_mallocz(n);
    int sp = 0, k, dead = 1;

    stack[sp++] = an[m].succ1;
    stack[sp++] = an[m].succ2;
    while (sp && dead) {
        int opc, opd, j;
        k = stack[--sp];
        if (k < 0)
            continue;
        if (k >= n || seen[k])
            continue;
        seen[k] = 1;
        opc = ins[k].opcode;
        opd = ins[k].operand;
        if (an[k].dead || opc == -1) {
            /* deleted or padding: falls through */
        } else if (opc == TP_ENC_STL && opd == s) {
            continue;                       /* this path is settled */
        } else if (opc == TP_ENC_LDL && opd == s) {
            dead = 0;
        } else if (opc == TP_ENC_LDLP) {
            for (j = k + 1; j < n && ins[j].opcode == -1; j++)
                ;
            if (j < n && (ins[j].opcode == TP_ENC_LDNL ||
                          ins[j].opcode == TP_ENC_STNL)) {
                if (opd + ins[j].operand == s) {
                    if (ins[j].opcode == TP_ENC_STNL)
                        continue;
                    dead = 0;
                }
            } else if (opd == s || opd == s - 1) {
                dead = 0;                   /* an FP access, maybe double */
            }
        } else if ((opc == TP_ENC_CALL ||
                    (opc == TP_ENC_OPR && opd == TP_OPR_GCALL)) &&
                   s < func_max_stack_words) {
            dead = 0;
        } else if (opc == TP_ENC_OPR && opd == TP_OPR_RET) {
            continue;                       /* the frame is gone */
        }
        if (!dead)
            break;
        if (an[k].succ1 < 0 && an[k].succ2 < 0) {
            dead = 0;                       /* a jump this cannot follow */
            break;
        }
        stack[sp++] = an[k].succ1;
        stack[sp++] = an[k].succ2;
    }
    tcc_free(stack);
    tcc_free(seen);
    return dead;
}

/* For bisecting a miscompile across a build too big to read, the kernel:
 * TP_SCHED_BISECT=lo-hi keeps S11 to functions whose name hashes (FNV-1a,
 * mod 4096) into [lo, hi), the same functions in every compilation. */
static int tps_bisect_ok(void)
{
    static int lo = -2, hi;
    unsigned int h = 2166136261u;
    const char *p;

    if (lo == -2) {
        const char *e = getenv("TP_SCHED_BISECT");
        if (!e || sscanf(e, "%d-%d", &lo, &hi) != 2)
            lo = -1;
    }
    if (lo < 0)
        return 1;
    for (p = funcname ? funcname : ""; *p; p++)
        h = (h ^ (unsigned char)*p) * 16777619u;
    h %= 4096;
    return (int)h >= lo && (int)h < hi;
}

/* Next instruction that still exists, skipping padding and deletions. */
static int tps_next(struct tp_relax_insn *ins, struct tps_ann *an, int n, int i)
{
    for (++i; i < n; i++)
        if (ins[i].opcode != -1 && !an[i].dead)
            return i;
    return -1;
}

/* The same, backwards. */
static int tps_prev(struct tp_relax_insn *ins, struct tps_ann *an, int i)
{
    for (--i; i >= 0; i--)
        if (ins[i].opcode != -1 && !an[i].dead)
            return i;
    return -1;
}

/* Can [lo,hi] be rewritten?  Nothing pinned by a relocation, and nothing but
 * the first instruction may be a branch target.  Instructions already deleted
 * by an earlier round contribute no code and are skipped - that is what lets
 * a second round see through the hole an S4 left behind - but a deleted
 * branch target is still one: its branches land on whatever follows it. */
static int tps_free(struct tp_relax_insn *ins, struct tps_ann *an,
                    int lo, int hi)
{
    int k;
    for (k = lo; k <= hi; k++) {
        if (k > lo && an[k].head)
            return 0;
        if (an[k].dead)
            continue;
        if (ins[k].pinned)
            return 0;
    }
    return 1;
}

/* A local placeholder survives until relaxation, and is dropped on emitter
 * rollback.  Check both its final byte and its patched workspace operand so
 * a later rewrite cannot accidentally transfer the proof to a different load. */
static int tps_proven_nonvolatile_ldl(struct tp_relax_insn *ins, int i)
{
    struct local_patch *p;
    int end = func_start_ind + ins[i].old_off + ins[i].old_len;
    for (p = local_patches; p; p = p->next)
        if (p->proven_nonvolatile_read && p->opcode == TP_OP_LDL &&
            p->instr_offset + p->placeholder_size == end &&
            (p->tcc_offset & 3) == 0 &&
            ins[i].operand == func_frame_words + p->tcc_offset / 4)
            return 1;
    return 0;
}

/*
 * Mark the redundant workspace round trips in a decoded function.  `top` is
 * the number of words tp_gaddr_cache put over the locals.
 * Returns the number of instructions removed.
 */
static int tp_sched_function(struct tp_relax_insn *ins, int n, int size,
                             int top)
{
    struct tps_ann *an;
    int *idx;               /* byte offset -> instruction index */
    int i, pass, round, removed = 0;
    int resv_escape;        /* a reserved word's address escapes (S10) */
    static int sched_off = -1;
    /* Slots this pass is allowed to treat as pseudo-registers.  The frame's
     * reserved region can now be smaller than NB_REGS, in which case locals
     * occupy low slot numbers; an LDLP of one of *these* must disqualify the
     * whole function. */
    const int nreg = (func_frame_reserved < NB_REGS)
                     ? func_frame_reserved : NB_REGS;
    /* Slots the pass reasons about at all.  Every word of the function's own
     * frame qualifies, not just the register file:
     *
     *   ajw -12 ; ldl 13 ; stl 11 ; ldl 11 ; lb        (a one-parameter leaf)
     *
     * Slot 13 is the incoming argument the caller left above the frame, and
     * slot 11 is the home TCC gives that parameter.  The store and the reload
     * are a pure round trip - `ldl 13 ; lb` computes the same thing - but slot
     * 11 sits above the reserved register region, so the pass used to classify
     * it as untouchable memory and walk past.  Measured over a kernel corpus
     * that shape and its copy-chain relatives were the largest single group of
     * surviving stores.
     *
     * Two things make locals as safe to reason about as pseudo-registers:
     *
     *   - the frame is private.  The only instruction that can name a
     *     workspace word by address is LDLP, and the pre-scan below finds
     *     every one of them; if any escapes, the local range is abandoned and
     *     the pass falls back to exactly its previous behaviour.
     *   - incoming arguments live *above* the frame (slot 13 in the example),
     *     so limiting the range to the frame's own words keeps the caller's
     *     workspace out of it.
     *
     * Capped at 32 because `liveout` is a 32-bit mask - which happens to be
     * the T9000 workspace-cache size as well. */
    int nslot = func_frame_words;
    /* No LDLP hands out the address of a workspace word, and there is no
     * inline assembly, so nothing outside this function can write one. */
    int frame_private;

    if (sched_off < 0) {
        sched_off = getenv("TP_NO_SCHED") ? 1 : 0;
    }
    if (sched_off || n <= 0 || size <= 0)
        return 0;

    if (nslot > 32)
        nslot = 32;
    if (nslot < nreg)
        nslot = nreg;

    /* Pre-scan: does any LDLP hand out the address of a frame word in a shape
     * this pass cannot follow?  The recognised form is LDLP immediately
     * followed by STNL or LDNL, which is the back end staging an outgoing call
     * argument or reading one back - a plain access to a known slot.  Anything
     * else (`&local` passed to a callee, the FPU's `ldlp s ; fpldnlsn`) puts
     * the word beyond this pass's sight, so give up the widened range and
     * reason about the register file alone, as before.
     *
     * Inline assembly is opaque for the same reason and gets the same
     * treatment. */
    {
        int escapes_local = 0;

        frame_private = !func_uses_inline_asm;
        for (i = 0; i < n; i++) {
            int j, opd = ins[i].operand;
            if (ins[i].opcode != TP_ENC_LDLP)
                continue;
            for (j = i + 1; j < n; j++)
                if (ins[j].opcode != -1)
                    break;
            if (j < n && (ins[j].opcode == TP_ENC_STNL ||
                          ins[j].opcode == TP_ENC_LDNL))
                continue;                   /* recognised staging form */
            if (tp_relax_byte_read(ins, n, i))
                continue;                   /* a byte of the word, read */
            /* Nothing may hold the address of *any* workspace word for copy
             * propagation to re-materialise a value from a slot the def/use
             * annotations do not cover - an incoming argument, mainly. */
            frame_private = 0;
            if (opd >= 0 && opd < nslot)
                escapes_local = 1;          /* also disqualifies the local range */
        }
        if (escapes_local || func_uses_inline_asm)
            nslot = nreg;
    }

    an = tcc_mallocz(sizeof(*an) * n);
    idx = tcc_malloc(sizeof(int) * (size + 1));
    for (i = 0; i <= size; i++)
        idx[i] = -1;
    for (i = 0; i < n; i++)
        idx[ins[i].old_off] = i;
    idx[size] = -1;                       /* a branch past the last byte */

    /* ---- 1. annotate, and refuse anything this model cannot describe ---- */
    for (i = 0; i < n; i++) {
        int opc = ins[i].opcode, opd = ins[i].operand;
        int rd = 0, dl = 0;

        an[i].known = tps_effect(opc, opd, &dl, &rd);
        an[i].delta = dl;
        an[i].reads = rd;
        an[i].use = an[i].def = -1;
        an[i].succ1 = (i + 1 < n) ? i + 1 : -1;
        an[i].succ2 = -1;
        an[i].depth = -1;

        if (opc == TP_ENC_LDL && opd >= 0 && opd < nslot) {
            an[i].use = opd;
        } else if (opc == TP_ENC_STL && opd >= 0 && opd < nslot) {
            an[i].def = opd;
        } else if (opc == TP_ENC_LDLP) {
            /* Recognise consumers that access one known workspace word.
             * Other uses of a pseudo-register's address can alias a slot
             * beyond the liveness model. */
            int j, indirect = 0;
            for (j = i + 1; j < n; j++)
                if (ins[j].opcode != -1)
                    break;
            if (j < n && (ins[j].opcode == TP_ENC_STNL ||
                          ins[j].opcode == TP_ENC_LDNL)) {
                int t = opd + ins[j].operand;
                indirect = 1;
                /* Out of range means the access lands below Wptr or above the
                 * register file (staging an outgoing argument at Wptr[-1], for
                 * instance) and so cannot disturb a pseudo-register. */
                if (t >= 0 && t < nslot) {
                    if (ins[j].opcode == TP_ENC_STNL)
                        an[i].def = t;
                    else
                        an[i].use = t;
                }
            }
            /* A single-precision access consumes this exact word's address.
             * Treat even stores as uses: this keeps earlier writes live and
             * stops a carry across the access. Leave double accesses to the
             * existing escape check because they also touch the next word. */
            if (!func_uses_inline_asm && j < n && ins[j].opcode == TP_ENC_OPR &&
                (ins[j].operand == TP_FPU_LDNLSN ||
                 ins[j].operand == TP_FPU_LDNLADDSN ||
                 ins[j].operand == TP_FPU_LDNLMULSN ||
                 ins[j].operand == TP_FPU_STNLSN)) {
                indirect = 1;
                if (opd >= 0 && opd < nslot)
                    an[i].use = opd;
            }
            if (tp_relax_byte_read(ins, n, i)) {
                indirect = 1;
                if (opd >= 0 && opd < nslot)
                    an[i].use = opd;
            }
            /* Only a pseudo-register escaping is fatal.  A local escaping has
             * already been handled: the pre-scan shrank nslot back to nreg, so
             * the local range is not being reasoned about at all. */
            if (!indirect && opd >= 0 && opd < nreg) {
                tps_bail("ldlp-escape");
                tcc_free(idx); tcc_free(an);
                return 0;
            }
        } else if (opc == TP_ENC_AJW) {
            /* AJW moves Wptr, so a slot number means something different on
             * either side of it.  Only the prologue and epilogue AJWs are
             * expected here, an epilogue's directly before its RET (VLAs use
             * GAJW, handled below). */
            int j, first;
            for (first = 0; first < n; first++)
                if (ins[first].opcode != -1)
                    break;
            for (j = i + 1; j < n; j++)
                if (ins[j].opcode != -1)
                    break;
            if (i != first && !(j < n && ins[j].opcode == TP_ENC_OPR &&
                                ins[j].operand == TP_OPR_RET)) {
                tps_bail("ajw-midfunction");
                tcc_free(idx); tcc_free(an);
                return 0;
            }
        } else if (opc == TP_ENC_OPR && opd == TP_OPR_GAJW) {
            tps_bail("gajw");
            tcc_free(idx); tcc_free(an);
            return 0;
        } else if (opc == TP_ENC_CALL ||
                   (opc == TP_ENC_OPR && opd == TP_OPR_GCALL)) {
            /* The callee reads its stacked arguments from Wptr[0..n-1],
             * where they were staged with plain STLs.  Without this those
             * stores look dead.  Every call is charged with the largest n
             * in the function: a slot that really is dead at a call with
             * fewer stacked words only keeps a store it could have lost. */
            int w = func_max_stack_words < nslot ? func_max_stack_words : nslot;
            an[i].uses = w >= 32 ? ~0u : (1u << w) - 1;
        }

        if (opc == TP_ENC_J) {
            an[i].succ1 = (ins[i].target_old >= 0 && ins[i].target_old <= size)
                          ? idx[ins[i].target_old] : -1;
        } else if (opc == TP_ENC_CJ) {
            an[i].succ2 = (ins[i].target_old >= 0 && ins[i].target_old <= size)
                          ? idx[ins[i].target_old] : -1;
        } else if (opc == TP_ENC_OPR && opd == TP_OPR_RET) {
            /* Treating RET as a terminator - nothing is live past it - is
             * only sound for an epilogue.  The back end also uses RET as an
             * indirect-jump trampoline (ggoto builds a fake frame with
             * AJW -4 and RETs into it), and control does come back from
             * those.  So a RET before the end of the function must be an
             * early return's own epilogue (gfunc_leave), the one directly
             * after an AJW +frame; refuse the function for any other.  The
             * AJW guard above already covers today's trampolines; this is
             * the independent check that keeps the assumption honest if
             * another one is ever added. */
            int j, p;
            for (j = i + 1; j < n; j++)
                if (ins[j].opcode != -1)
                    break;
            for (p = i - 1; p >= 0; p--)
                if (ins[p].opcode != -1)
                    break;
            if (j < n && !(p >= 0 && ins[p].opcode == TP_ENC_AJW &&
                           ins[p].operand >= 0)) {
                tps_bail("ret-midfunction");
                tcc_free(idx); tcc_free(an);
                return 0;
            }
            an[i].succ1 = -1;
        }
    }

    /* ---- 2. branch targets.  The emitter zeroes its evaluation-stack
     * bookkeeping at every label, so a target is a point where the depth is
     * known to be zero. ---- */
    an[0].head = 1;
    for (i = 0; i < n; i++) {
        int k;
        if (!ins[i].is_branch)
            continue;
        if (ins[i].target_old < 0 || ins[i].target_old > size)
            continue;
        k = idx[ins[i].target_old];
        if (k >= 0)
            an[k].head = 1;
    }

    /* Record the original pair before S4 removes the address staging store.
     * Both reads must be proven nonvolatile, and the intervening store must
     * not change the index. Branch entries and relocations are barriers. */
    for (i = 0; i < n && !tp_t414() && !func_uses_inline_asm; i++) {
        int j[7], k;
        j[0] = i;
        for (k = 1; k < 7 && j[k-1] >= 0; k++) j[k] = tps_next(ins, an, n, j[k-1]);
        if (k != 7 || j[6] < 0 || ins[j[0]].opcode != TP_ENC_LDL ||
            ins[j[1]].opcode != TP_ENC_LDLP || ins[j[2]].opcode != TP_ENC_OPR ||
            ins[j[2]].operand != TP_OPR_WSUB || ins[j[3]].opcode != TP_ENC_STL ||
            ins[j[3]].operand == ins[j[0]].operand ||
            ins[j[4]].opcode != TP_ENC_LDL || ins[j[4]].operand != ins[j[0]].operand ||
            ins[j[5]].opcode != TP_ENC_LDLP || ins[j[5]].operand != ins[j[1]].operand ||
            ins[j[6]].opcode != TP_ENC_OPR || ins[j[6]].operand != TP_OPR_FPLDNLSNI ||
            !tps_proven_nonvolatile_ldl(ins, j[0]) || !tps_proven_nonvolatile_ldl(ins, j[4]) ||
            !tps_free(ins, an, j[0], j[6])) continue;
        an[j[0]].proven_pair = j[4] + 1;
    }

    /* ---- 3. evaluation-stack depth within each block ---- */
    for (i = 0; i < n; i++) {
        int prev_call = i > 0 && ((ins[i - 1].opcode == TP_ENC_CALL) ||
                                  (ins[i - 1].opcode == TP_ENC_OPR &&
                                   ins[i - 1].operand == TP_OPR_GCALL));
        if (an[i].head) {
            an[i].depth = 0;
        } else if (prev_call) {
            /* A call leaves the ABI result words in AReg (and BReg for a
             * 64-bit return); gfunc_call always captures both, so two is the
             * depth the emitter itself assumes here. Without this the whole
             * post-call sequence has an unknown depth and none of the
             * return-value round trips can be reasoned about. */
            an[i].depth = 2;
        } else if (i > 0 && an[i - 1].depth >= 0 && an[i - 1].known) {
            int d = an[i - 1].depth + an[i - 1].delta;
            an[i].depth = (d >= 0) ? d : -1;
        } else {
            an[i].depth = -1;
        }
    }

    /* ---- 4. backward liveness over the pseudo-register slots ---- */
    for (pass = 0; pass < 128; pass++) {
        int changed = 0;
        for (i = n - 1; i >= 0; i--) {
            unsigned int out = 0;
            int s;
            for (s = 0; s < 2; s++) {
                int t = s ? an[i].succ2 : an[i].succ1;
                unsigned int in;
                if (t < 0 || t >= n)
                    continue;
                in = an[t].liveout;
                if (an[t].def >= 0)
                    in &= ~(1u << an[t].def);
                if (an[t].use >= 0)
                    in |= 1u << an[t].use;
                in |= an[t].uses;
                out |= in;
            }
            if (out != an[i].liveout) {
                an[i].liveout = out;
                changed = 1;
            }
        }
        if (!changed)
            break;
    }
    if (pass == 128) {                    /* did not settle: do not risk it */
        tps_bail("liveness");
        tcc_free(idx); tcc_free(an);
        return 0;
    }

    /* ---- 4a. call results nobody reads (S9) ----
     *
     * gfunc_call() stores the result words of every non-void call whether or
     * not the caller uses them, and a call whose value is discarded - most of
     * them, in the kernel - leaves that store dead: 34k of the 62k `stl 7`
     * in vmlinux.  Dropping it leaves the word on the stack, underneath
     * everything pushed afterwards, where nothing reads it, because the
     * emitter treats the stack as empty once the results are captured.
     *
     * Two things keep that true.  Only a dead *suffix* of the capture run
     * may go, as an STL left after a deleted one would pop the wrong word.
     * And it runs here, on the emitter's own stream: once the rules below
     * start carrying values on the stack, a later instruction may read below
     * its own pushes, and the instruction after the run is required to read
     * nothing at all.  Removing a dead store changes no other slot's
     * liveness, so the annotations stay valid for the passes that follow. */
    if (tps_on(8)) {
        for (i = 0; i < n; i++) {
            int j, k, last = -1;

            if (ins[i].opcode != TP_ENC_CALL &&
                !(ins[i].opcode == TP_ENC_OPR && ins[i].operand == TP_OPR_GCALL))
                continue;
            for (j = tps_next(ins, an, n, i);
                 j >= 0 && ins[j].opcode == TP_ENC_STL &&
                 !an[j].head && !ins[j].pinned;
                 j = tps_next(ins, an, n, j))
                last = j;
            if (last < 0)
                continue;
            k = tps_next(ins, an, n, last);
            if (k >= 0 && !an[k].head && !(an[k].known && an[k].reads == 0))
                continue;
            for (j = last; j > i; j--) {
                int a = ins[j].operand;
                if (ins[j].opcode != TP_ENC_STL)
                    continue;               /* PFIX 0 padding */
                if (a < 0 || a >= nslot || (an[j].liveout & (1u << a)))
                    break;
                an[j].dead = 1;
                removed++;
                tps_note(8);
            }
        }
    }

    /* ---- 4b. copy propagation ----
     *
     * `LDL x ; STL y` copies one workspace word to another.  The peephole
     * rules above can only remove it when y turns out to be dead (S6) or when
     * the reload happens to sit within reach of the evaluation stack (S1, S4,
     * S5) - and even then S5 pays a REV for the privilege:
     *
     *     ldl 11 ; stl 9 ; ldc 0 ; ldl 9      ->   ldl 11 ; ldc 0 ; rev
     *
     * If instead every reader of y loads x directly, the copy simply goes
     * away, and nothing is added:
     *
     *                                         ->   ldc 0 ; ldl 11
     *
     * This is the shape TCC's prologue leaves behind for every parameter, and
     * over a kernel corpus `ldl ; stl` pairs were the largest class of stores
     * still standing after everything above.
     *
     * The rewrite is valid without any reaching-definitions machinery when two
     * things hold:
     *
     *   - y has exactly one definition in the whole function, this one, so
     *     every `LDL y` anywhere reads this copy (or reads y before it is
     *     ever written, which was already undefined and stays that way);
     *   - x is never written in the function at all, so `LDL x` yields the
     *     same value at every point.
     *
     * "Never written" has to account for what the pass cannot see directly:
     * a store through a pointer (excluded by frame_private) and a callee
     * writing back through its own stacked parameters, which land in this
     * function's outgoing-argument area - hence the func_max_outgoing_words
     * floor on x.
     *
     * A proper must-availability dataflow was written and measured as the
     * general replacement for those two tests, on the strength of a ceiling
     * measurement: of 17,374 `LDL x ; STL y` pairs in the kernel corpus these
     * rules take 49.8%, with 18.9% blocked only by x being written somewhere
     * (often after the last use of y) and 3.8% only by y having several
     * definitions.  It came out 2,003 instructions and 1,036 groups *worse*:
     * batching 32 copies into one fixpoint, and forbidding two committed
     * copies from sharing a slot, together lose more instances than the extra
     * generality wins - the simple rules here re-read the instruction stream
     * as they go, so a chain like `ldl 15; stl 13 ... ldl 13; stl 11` folds
     * end to end, which the batched version cannot see.  Revisit only with a
     * per-commit refresh, and measure.
     *
     * x is also required to stay inside the T9000 workspace cache and to
     * encode no wider than y, so that no use site gets slower or longer.
     *
     * Runs before the peephole rounds: where both could fire, this is strictly
     * the better outcome, and it exposes further round trips for them. */
    if (frame_private) {
        for (i = 0; i < n; i++) {
            int x, y, j, k, ok = 1;

            if (an[i].dead || ins[i].pinned || ins[i].opcode != TP_ENC_LDL)
                continue;
            /* `LDC k ; STL y` was tried as a second source form and measured:
             * 326 instructions and 76 groups over the whole kernel corpus,
             * 0.09%.  Almost no constant copy survives, and the ones that do
             * are already caught by S1/S4/S5/S6, so it was removed again. */
            x = ins[i].operand;
            if (x < 0 || x >= 32)           /* stays in the workspace cache */
                continue;
            if (x < func_max_outgoing_words)
                continue;                   /* a callee can write it back */
            j = tps_next(ins, an, n, i);
            if (j < 0 || !tps_free(ins, an, i, j))
                continue;
            if (ins[j].opcode != TP_ENC_STL)
                continue;
            y = ins[j].operand;
            if (y < 0 || y >= nslot || y == x)
                continue;
            /* A stacked argument has a reader that is not an LDL: the
             * callee.  Every other store to y is a second definition, so
             * this loses nothing the rule below would have taken. */
            if (y < func_max_stack_words)
                continue;
            if (tp_encode_len(TP_ENC_LDL, x) > tp_encode_len(TP_ENC_LDL, y))
                continue;

            /* Is anything other than this copy allowed to write x or y? */
            for (k = 0; k < n && ok; k++) {
                if (k == j || an[k].dead || ins[k].opcode == -1)
                    continue;
                if (ins[k].opcode == TP_ENC_STL &&
                    (ins[k].operand == x || ins[k].operand == y)) {
                    ok = 0;
                } else if (ins[k].opcode == TP_ENC_LDLP) {
                    int m, t;
                    for (m = k + 1; m < n; m++)
                        if (ins[m].opcode != -1)
                            break;
                    if (m < n && ins[m].opcode == TP_ENC_STNL) {
                        t = ins[k].operand + ins[m].operand;
                        if (t == x || t == y)
                            ok = 0;
                    }
                    /* Indirect word reads are not rewritten below. Keep the
                     * copy if LDLP s; LDNL d reads its destination word. */
                    if (m < n && ins[m].opcode == TP_ENC_LDNL &&
                        ((uint32_t)ins[k].operand +
                         (uint32_t)ins[m].operand - (uint32_t)y) * 4u == 0)
                        ok = 0;
                    /* A byte read of y would not be redirected to x below,
                     * and the copy that wrote y goes. */
                    if (ins[k].operand == y && tp_relax_byte_read(ins, n, k))
                        ok = 0;
                } else if (ins[k].pinned && ins[k].opcode == TP_ENC_LDL &&
                           ins[k].operand == y) {
                    ok = 0;                 /* a reader that cannot be rewritten */
                }
            }
            if (!ok)
                continue;

            /* Commit: every reader of y takes its value from x instead. */
            for (k = 0; k < n; k++) {
                if (k == i || k == j || an[k].dead || ins[k].pinned)
                    continue;
                if (ins[k].opcode != TP_ENC_LDL || ins[k].operand != y)
                    continue;
                ins[k].operand = x;
                an[k].use = (x < nslot) ? x : -1;
            }
            an[i].dead = an[j].dead = 1;
            removed += 2;
            i = j;
        }
    }

    /* For S10 beyond the liveness mask: is every LDLP of a reserved word
     * one access tps_dead_after can see? */
    resv_escape = 0;
    for (i = 0; i < n && !resv_escape; i++) {
        int j, lo, t = ins[i].operand;
        if (ins[i].opcode != TP_ENC_LDLP || t < 0 || t >= func_frame_reserved)
            continue;
        for (j = i + 1; j < n && ins[j].opcode == -1; j++)
            ;
        if (j == n || tpg_ldlp_extent(&ins[j], &lo) < 0)
            resv_escape = 1;
    }

    /* ---- 5. pattern matching, iterated ---- */
    for (round = 0; round < TPS_ROUNDS; round++) {
        int changed = 0;

        for (i = 0; i < n; i++) {
            int a, b, i1, i2, i3, j, r, maxr;

            if (an[i].dead || ins[i].pinned || ins[i].opcode == -1)
                continue;

            if (ins[i].opcode != TP_ENC_STL)
                goto try_copy;
            a = ins[i].operand;
            if (a < 0 || a >= nslot)
                continue;
            i1 = tps_next(ins, an, n, i);
            if (i1 < 0)
                continue;

            /* S1 */
            if (tps_on(0) &&
                ins[i1].opcode == TP_ENC_LDL && ins[i1].operand == a &&
                !(an[i1].liveout & (1u << a)) && tps_free(ins, an, i, i1)) {
                an[i].dead = an[i1].dead = 1;
                removed += 2; changed = 1; tps_note(0);
                i = i1;
                continue;
            }

            /* S2 / S3 */
            if (ins[i1].opcode == TP_ENC_STL &&
                ins[i1].operand >= 0 && ins[i1].operand < nslot &&
                ins[i1].operand != a) {
                b = ins[i1].operand;
                i2 = tps_next(ins, an, n, i1);
                i3 = i2 >= 0 ? tps_next(ins, an, n, i2) : -1;
                if (i3 >= 0 && ins[i2].opcode == TP_ENC_LDL &&
                    ins[i3].opcode == TP_ENC_LDL &&
                    !(an[i3].liveout & ((1u << a) | (1u << b))) &&
                    tps_free(ins, an, i, i3)) {
                    if (tps_on(1) &&
                        ins[i2].operand == b && ins[i3].operand == a) {
                        an[i].dead = an[i1].dead = 1;
                        an[i2].dead = an[i3].dead = 1;
                        removed += 4; changed = 1; tps_note(1);
                        i = i3;
                        continue;
                    }
                    if (tps_on(2) &&
                        ins[i2].operand == a && ins[i3].operand == b) {
                        /* The pair comes back swapped: that is exactly REV. */
                        ins[i].opcode = TP_ENC_OPR;
                        ins[i].operand = TP_OPR_REV;
                        ins[i].is_branch = 0;
                        an[i].known = tps_effect(TP_ENC_OPR, TP_OPR_REV,
                                                 &an[i].delta, &an[i].reads);
                        an[i].use = an[i].def = -1;
                        an[i1].dead = an[i2].dead = an[i3].dead = 1;
                        removed += 3; changed = 1; tps_note(2);
                        i = i3;
                        continue;
                    }
                }
                /* S7: STL a ; STL b ; LDL a  ->  REV ; STL b.
                 * REV brings the second value to the top so the surviving
                 * store can take it, leaving the first where LDL a would
                 * have put it back.  Trades a store for an ALU operation and
                 * drops an instruction; this is the shape gfunc_call leaves
                 * after every call that captures both ABI result words. */
                if (tps_on(6) && i2 >= 0 && ins[i2].opcode == TP_ENC_LDL &&
                    ins[i2].operand == a &&
                    !(an[i2].liveout & (1u << a)) &&
                    tps_free(ins, an, i, i2)) {
                    ins[i].opcode = TP_ENC_OPR;
                    ins[i].operand = TP_OPR_REV;
                    ins[i].is_branch = 0;
                    an[i].known = tps_effect(TP_ENC_OPR, TP_OPR_REV,
                                             &an[i].delta, &an[i].reads);
                    an[i].use = an[i].def = -1;
                    an[i2].dead = 1;
                    removed += 1; changed = 1; tps_note(6);
                    i = i2;
                    continue;
                }
            }

            /* S11: LDL x ; [LDNLP k ;] STL a ; <e> ; LDL a ; <use>  ->
             *      <e> ; LDL x ; <use>'
             * Recompute an address where it is used instead of parking it.
             * `p[k] = <e>` computes &p[k] before <e>, so the store found it
             * under the value and S5 paid a REV to bring it back:
             *     ldl 30; ldnlp 15; <e>; rev; stnl 0  ->  <e>; ldl 30; stnl 15
             * With an LDNLP, <use> must be an LDNL/STNL/LDNLP, which takes k
             * into its own displacement; without one, anything.  Valid when
             * the run is straight-line code (no branch target anywhere in
             * it, padding included: findRange's `if (c) a = x;` jumped over
             * the copy), nothing in it writes x, nothing else touches a, and
             * a is dead after.  The three removed instructions push and pop
             * one word between them, so the stack under them is untouched.
             * Where S4 would carry the value for nothing (the reload level
             * with the store, no LDNLP to fold), S4 is left to do it. */
            if (tps_on(12) && tps_bisect_ok()) {
                int p0, p1 = tps_prev(ins, an, i), k = 0, x = -1, use = -1;
                int jn = -1, ok = 1, r = 0, known = 1;
                if (p1 >= 0 && ins[p1].opcode == TP_ENC_LDNLP) {
                    k = ins[p1].operand;
                    p0 = tps_prev(ins, an, p1);
                } else {
                    p0 = p1;
                    p1 = -1;
                }
                /* A word whose address has escaped could be written through
                 * any pointer; a pseudo-register's never has (the pre-scan
                 * gives up on the function first).  Nor slot 0: outbyte,
                 * outword and the ALT instructions write Wptr[0] without
                 * naming it. */
                if (p0 < 0 || ins[p0].opcode != TP_ENC_LDL ||
                    (x = ins[p0].operand) <= 0 || x == a ||
                    !tps_free(ins, an, p0, i))
                    ok = 0;
                if (ok && !frame_private && x >= nreg &&
                    tps_word_escapes(ins, n, x, top))
                    ok = 0;
                for (j = i1; ok && j >= 0 && j - i < TPS_MAXWIN;
                     j = tps_next(ins, an, n, j)) {
                    int o = ins[j].opcode, d = ins[j].operand, m;
                    if (ins[j].pinned || ins[j].is_branch) {
                        ok = 0;
                    } else if (o == TP_ENC_LDL && d == a) {
                        use = j;
                        break;
                    } else if (an[j].use == a || an[j].def == a ||
                               (an[j].uses & (1u << a)) ||
                               (o == TP_ENC_STL && (d == x || d == a)) ||
                               (o == TP_ENC_LDLP && (d == x || d == x - 1 ||
                                                     d == a))) {
                        ok = 0;
                    } else if (o == TP_ENC_LDLP) {
                        m = tps_next(ins, an, n, j);
                        if (m >= 0 && ins[m].opcode == TP_ENC_STNL &&
                            d + ins[m].operand == x)
                            ok = 0;
                    } else if ((o == TP_ENC_CALL ||
                                (o == TP_ENC_OPR && d == TP_OPR_GCALL)) &&
                               x < func_max_outgoing_words) {
                        ok = 0;                 /* a callee could write x */
                    }
                    if (an[j].known)
                        r += an[j].delta;
                    else
                        known = 0;
                }
                if (use < 0 || !tps_free(ins, an, i, use) ||
                    (an[use].liveout & (1u << a)))
                    ok = 0;
                if (ok && p1 < 0 && known && r == 0)
                    ok = 0;                     /* S4's, for free */
                if (ok && p1 >= 0) {
                    jn = tps_next(ins, an, n, use);
                    if (!(jn >= 0 && tps_free(ins, an, use, jn) &&
                          (ins[jn].opcode == TP_ENC_LDNL ||
                           ins[jn].opcode == TP_ENC_STNL ||
                           ins[jn].opcode == TP_ENC_LDNLP)))
                        ok = 0;
                }
                if (ok) {
                    an[p0].dead = an[i].dead = 1;
                    if (p1 >= 0) {
                        an[p1].dead = 1;
                        ins[jn].operand += k;
                        if (tp_encode_len(ins[jn].opcode, ins[jn].operand) >
                            ins[jn].new_len)
                            ins[jn].new_len = tp_encode_len(ins[jn].opcode,
                                                            ins[jn].operand);
                    }
                    ins[use].operand = x;
                    if (tp_encode_len(TP_ENC_LDL, x) > ins[use].new_len)
                        ins[use].new_len = tp_encode_len(TP_ENC_LDL, x);
                    an[use].use = x < nslot ? x : -1;
                    /* x is now read at use: it is live from p0 on, which
                     * the liveness computed before the rewrite does not
                     * know.  Without this a later rule took x for dead and
                     * removed its store (c-testsuite 00182's `*p++`). */
                    if (x < 32) {
                        int q;
                        for (q = p0; q < use; q++)
                            an[q].liveout |= 1u << x;
                    }
                    removed += p1 >= 0 ? 3 : 2;
                    changed = 1; tps_note(12);
                    continue;
                }
            }

            /* S4 / S5: carry the value across a balanced run of code.
             * `r` is the stack depth relative to the point just after the
             * STL, so the carried value sits at r == -1 and <e> must never
             * try to read there. */
            if (an[i].depth < 1 || an[i].depth > 3 || !an[i].known)
                continue;
            r = 0;
            maxr = 0;
            for (j = i1; j >= 0 && j - i < TPS_MAXWIN;
                 j = tps_next(ins, an, n, j)) {
                if (ins[j].pinned || an[j].head || !an[j].known ||
                    ins[j].is_branch)
                    break;             /* a branch would strand the carry */
                if (ins[j].opcode == TP_ENC_LDL && ins[j].operand == a) {
                    if (r > 1 || (an[j].liveout & (1u << a)) != 0)
                        break;
                    if (!tps_free(ins, an, i, j))
                        break;
                    if (r == 0) {
                        if (!tps_on(3)) break;
                        an[i].dead = an[j].dead = 1;
                        removed += 2; tps_note(3);
                    } else {
                        if (!tps_on(4)) break;
                        /* One value of <e> is stacked above the carry, so
                         * they come back swapped: reuse the LDL slot as REV. */
                        ins[j].opcode = TP_ENC_OPR;
                        ins[j].operand = TP_OPR_REV;
                        ins[j].is_branch = 0;
                        an[j].known = tps_effect(TP_ENC_OPR, TP_OPR_REV,
                                                 &an[j].delta, &an[j].reads);
                        an[j].use = an[j].def = -1;
                        an[i].dead = 1;
                        removed += 1; tps_note(4);
                    }
                    /* Everything from here to the reload now runs one word
                     * deeper; a later round must see that. The reload itself
                     * is included because S5 keeps it (as REV). */
                    for (b = i + 1; b <= j; b++)
                        if (an[b].depth >= 0)
                            an[b].depth++;
                    changed = 1;
                    break;
                }
                if (an[j].use == a || an[j].def == a)
                    break;
                if (an[j].reads > r)
                    break;             /* would consume the carried value */
                r += an[j].delta;
                if (r < 0)
                    break;
                if (r > maxr)
                    maxr = r;
                if (an[i].depth + maxr > 3)
                    break;             /* would not fit the 3-deep stack */
            }
            if (an[i].dead) {
                i = (j > i) ? j : i;
                continue;
            }
            continue;

try_copy:
            /* S12: LDNLP k ; LDNL/STNL/LDNLP m  ->  LDNL/STNL/LDNLP k+m.
             * The displacement folds; what S11 leaves behind and what the
             * emitter could not fold across a parked register. */
            if (tps_on(13) && ins[i].opcode == TP_ENC_LDNLP &&
                (i1 = tps_next(ins, an, n, i)) >= 0 &&
                (ins[i1].opcode == TP_ENC_LDNL ||
                 ins[i1].opcode == TP_ENC_STNL ||
                 ins[i1].opcode == TP_ENC_LDNLP) &&
                tps_free(ins, an, i, i1)) {
                ins[i1].operand += ins[i].operand;
                if (tp_encode_len(ins[i1].opcode, ins[i1].operand) >
                    ins[i1].new_len)
                    ins[i1].new_len = tp_encode_len(ins[i1].opcode,
                                                    ins[i1].operand);
                an[i].dead = 1;
                removed += 1; changed = 1; tps_note(13);
                i = i1;
                continue;
            }

            /* S6: a push straight into a slot nobody reads.  Deleting the
             * pair is depth-neutral, and only ever leaves more of the stack
             * intact than the original did. */
            if (ins[i].opcode != TP_ENC_LDL && ins[i].opcode != TP_ENC_LDC)
                continue;
            i1 = tps_next(ins, an, n, i);
            if (i1 < 0 || ins[i1].opcode != TP_ENC_STL)
                continue;
            b = ins[i1].operand;
            if (b < 0)
                continue;

            /* S10: LDL x ; STL b ; LDLP b ; <FP load from A>  ->
             *      LDLP x ; <FP load>
             * A float copied to a scratch word only to give the FPU an
             * address can be read where it is, if the scratch word is dead
             * afterwards.  Nothing in between writes x.  That is how a float
             * constant kept in a frame word by tp_gaddr_cache is loaded. */
            if (tps_on(9) && ins[i].opcode == TP_ENC_LDL &&
                ins[i].operand >= 0 &&
                (i2 = tps_next(ins, an, n, i1)) >= 0 &&
                ins[i2].opcode == TP_ENC_LDLP && ins[i2].operand == b &&
                (i3 = tps_next(ins, an, n, i2)) >= 0 &&
                ins[i3].opcode == TP_ENC_OPR &&
                (ins[i3].operand == TP_FPU_LDNLSN ||
                 ins[i3].operand == TP_FPU_LDNLADDSN ||
                 ins[i3].operand == TP_FPU_LDNLMULSN) &&
                tps_free(ins, an, i, i3) &&
                (b < nslot ? !(an[i3].liveout & (1u << b))
                           : b >= func_max_outgoing_words &&
                             b < func_frame_reserved && !resv_escape &&
                             tps_dead_after(ins, an, n, i3, b))) {
                int x = ins[i].operand;
                ins[i2].operand = x;
                if (tp_encode_len(TP_ENC_LDLP, x) > ins[i2].new_len)
                    ins[i2].new_len = tp_encode_len(TP_ENC_LDLP, x);
                an[i2].use = x < nslot ? x : -1;
                if (x < 32) {           /* now read at i2: live until it */
                    an[i].liveout |= 1u << x;
                    an[i1].liveout |= 1u << x;
                }
                an[i].dead = an[i1].dead = 1;
                removed += 2; changed = 1; tps_note(9);
                i = i3;
                continue;
            }
            if (b >= nslot || !tps_on(5))
                continue;
            if (an[i1].liveout & (1u << b))
                continue;
            if (!tps_free(ins, an, i, i1))
                continue;
            an[i].dead = an[i1].dead = 1;
            removed += 2; changed = 1; tps_note(5);
            i = i1;
        }

        if (!changed)
            break;
    }

    /* S4 may expose:
     *   LDL i; LDLP a; WSUB; LDL i; LDLP a; FPLDNLSNI
     * The already-computed address can instead be duplicated:
     *   LDL i; LDLP a; WSUB; DUP; FPLDNLSN
     * The floating-point access remains in place and occurs exactly once. */
    for (i = 0; i < n; i++) {
        int j[6], k;
        if (!an[i].proven_pair || an[i].dead) continue;
        j[0] = i;
        for (k = 1; k < 6 && j[k-1] >= 0; k++) j[k] = tps_next(ins, an, n, j[k-1]);
        /* The original indexed load overwrites the deeper integer-stack
         * cells; the duplicated form preserves different garbage there. */
        if (k != 6 || j[5] < 0 || an[i].proven_pair != j[3] + 1 ||
            an[j[2]].depth != 2 ||
            ins[j[0]].opcode != TP_ENC_LDL || ins[j[1]].opcode != TP_ENC_LDLP ||
            ins[j[2]].opcode != TP_ENC_OPR || ins[j[2]].operand != TP_OPR_WSUB ||
            ins[j[3]].opcode != TP_ENC_LDL || ins[j[3]].operand != ins[j[0]].operand ||
            ins[j[4]].opcode != TP_ENC_LDLP || ins[j[4]].operand != ins[j[1]].operand ||
            ins[j[5]].opcode != TP_ENC_OPR || ins[j[5]].operand != TP_OPR_FPLDNLSNI ||
            !tps_proven_nonvolatile_ldl(ins, j[0]) || !tps_proven_nonvolatile_ldl(ins, j[3]) ||
            !tps_free(ins, an, j[0], j[5])) continue;
        ins[j[3]].opcode = TP_ENC_OPR; ins[j[3]].operand = TP_OPR_DUP;
        ins[j[3]].new_len = tp_encode_len(TP_ENC_OPR, TP_OPR_DUP);
        an[j[3]].use = -1;
        an[j[3]].known = tps_effect(ins[j[3]].opcode, ins[j[3]].operand, &an[j[3]].delta, &an[j[3]].reads);
        ins[j[4]].opcode = -1; an[j[4]].dead = 1;
        an[j[4]].known = 1; an[j[4]].delta = an[j[4]].reads = 0;
        ins[j[5]].operand = TP_FPU_LDNLSN;
        an[j[5]].known = tps_effect(ins[j[5]].opcode, ins[j[5]].operand, &an[j[5]].delta, &an[j[5]].reads);
        removed++;
    }

    /* ---- 5a. delete a REV by swapping the two pure pushes that feed it ----
     *
     * `LDL a ; LDL b ; REV` and `LDL b ; LDL a` leave exactly the same stack:
     * LDL and LDC are pure pushes that read no stack and have no side effect,
     * so the REV only undoes the order they were emitted in. Emitting them the
     * other way round removes the instruction outright — strictly better than
     * the commutative-consumer rule below, which only applies when the
     * consumer tolerates either order.
     *
     * This is the dominant shape: of the 17,808 `REV; STNL` sites measured
     * across 462 kernel translation units, 84.6% are preceded by exactly two
     * LDLs. STNL wants AReg=address, BReg=value, so those REVs are purely the
     * scheduler having produced the pair the wrong way round.
     *
     * Both pushes must be reached together: if the second one is a branch
     * target, control can arrive without having executed the first. */
    for (i = 0; i < n; i++) {
        int j0, j1, k;
        if (an[i].dead || ins[i].pinned || an[i].head)
            continue;
        if (ins[i].opcode != TP_ENC_OPR || ins[i].operand != TP_OPR_REV)
            continue;

        j1 = -1;
        for (k = i - 1; k >= 0; k--)
            if (!an[k].dead) { j1 = k; break; }
        if (j1 < 0 || ins[j1].pinned || an[j1].head)
            continue;
        j0 = -1;
        for (k = j1 - 1; k >= 0; k--)
            if (!an[k].dead) { j0 = k; break; }
        if (j0 < 0 || ins[j0].pinned)
            continue;

        if ((ins[j0].opcode != TP_ENC_LDL && ins[j0].opcode != TP_ENC_LDC) ||
            (ins[j1].opcode != TP_ENC_LDL && ins[j1].opcode != TP_ENC_LDC))
            continue;
        if (ins[j0].is_branch || ins[j1].is_branch)
            continue;
        if (!tps_free(ins, an, j0, i))
            continue;               /* a deleted branch target in between */

        {
            int t_opc = ins[j0].opcode, t_opd = ins[j0].operand;
            int t_old = ins[j0].old_len, t_new = ins[j0].new_len;
            int t_use = an[j0].use;
            ins[j0].opcode = ins[j1].opcode;
            ins[j0].operand = ins[j1].operand;
            /* The encoded widths must travel with the operands. The relaxation
             * fixpoint only ever SHRINKS from new_len, so an instruction left
             * holding a slot narrower than its new operand needs makes the
             * encoder disagree at emit time and the whole function is
             * abandoned — silently forfeiting the relaxation win. Swapping the
             * pair keeps every (opcode, operand, length) triple consistent and
             * leaves the total width unchanged. */
            ins[j0].old_len = ins[j1].old_len;
            ins[j0].new_len = ins[j1].new_len;
            an[j0].use = an[j1].use;
            ins[j1].opcode = t_opc;
            ins[j1].operand = t_opd;
            ins[j1].old_len = t_old;
            ins[j1].new_len = t_new;
            an[j1].use = t_use;
        }
        an[i].dead = 1;
        removed += 1;
        tps_note(7);
    }

    /* ---- 5a'. hoist a pure push over the run that computed the value
     * under it ----
     *
     * S5 keeps a value on the stack under <e> and pays a REV, as the two come
     * back swapped.  Where <e> is a single pure push, the code that computed
     * the value is a straight run, and the push depends on nothing the run
     * does, the push can simply go first:
     *
     *     ldl i ; ldl p ; wsub ; ldc 0 ; rev ; stnl 0
     *  -> ldc 0 ; ldl i ; ldl p ; wsub ; stnl 0
     *
     * which is every word-fill loop's body.  The run is the shortest that ends
     * just before the push, leaves exactly one word, and never reads below
     * where it started; it then runs one word deeper, which must still fit
     * the stack.  It holds no store, call, branch, FPU operation or pinned or
     * synthesised instruction, so an LDL push reads the same word before it
     * as after, and only its first instruction may be a branch target - entry
     * there now executes the push and then the run, as before it executed the
     * run and then the push.  Like 5a this moves contents, not positions:
     * old_off, the CFG and branch targets stay where they are. */
    for (i = 0; i < n && tps_on(10); i++) {
        int jp, j0, m, k, cand, best = -1, maxr = 0;
        if (an[i].dead || ins[i].pinned || an[i].head)
            continue;
        if (ins[i].opcode != TP_ENC_OPR || ins[i].operand != TP_OPR_REV)
            continue;
        jp = -1;
        for (k = i - 1; k >= 0; k--)
            if (!an[k].dead && ins[k].opcode != -1) { jp = k; break; }
        if (jp < 0 || ins[jp].pinned || an[jp].head || ins[jp].synth ||
            ins[jp].is_branch || !an[jp].known)
            continue;
        /* Not LDLP: separated from its consumer, an address push would look
         * like an escape to the slot analyses that follow. */
        if (ins[jp].opcode != TP_ENC_LDL && ins[jp].opcode != TP_ENC_LDC)
            continue;
        j0 = -1;
        for (k = jp - 1; k >= 0; k--)
            if (!an[k].dead && ins[k].opcode != -1) { j0 = k; break; }
        if (j0 < 0)
            continue;

        /* Find the run's start: walk back, re-checking the run forward. */
        for (cand = j0; cand >= 0 && j0 - cand < TPS_MAXWIN; cand--) {
            int r = 0, mr = 0, ok = 1, q;
            if (an[cand].dead || ins[cand].opcode == -1)
                continue;
            for (q = cand; q <= j0 && ok; q++) {
                int opc = ins[q].opcode, opd = ins[q].operand;
                if (an[q].dead || opc == -1)
                    continue;
                if (ins[q].pinned || ins[q].synth || ins[q].is_branch ||
                    !an[q].known || (q > cand && an[q].head))
                    ok = 0;
                else if (opc == TP_ENC_OPR) {
                    switch (opd) {
                    case TP_OPR_ADD: case TP_OPR_SUB: case TP_OPR_MUL:
                    case TP_OPR_SUM: case TP_OPR_DIFF: case TP_OPR_PROD:
                    case TP_OPR_AND: case TP_OPR_OR: case TP_OPR_XOR:
                    case TP_OPR_SHL: case TP_OPR_SHR: case TP_OPR_GT:
                    case TP_OPR_GTU: case TP_OPR_WSUB: case TP_OPR_BSUB:
                    case TP_OPR_WSUBDB: case TP_OPR_SSUB: case TP_OPR_NOT:
                    case TP_OPR_LB: case TP_OPR_LS: case TP_OPR_LSX:
                    case TP_OPR_LBX: case TP_OPR_XSWORD: case TP_OPR_XBWORD:
                    case TP_OPR_XWORD: case TP_OPR_MINT: case TP_OPR_DUP:
                    case TP_OPR_REV:
                        break;
                    default:
                        ok = 0;
                    }
                } else if (opc != TP_ENC_LDL && opc != TP_ENC_LDLP &&
                           opc != TP_ENC_LDC && opc != TP_ENC_LDNL &&
                           opc != TP_ENC_LDNLP && opc != TP_ENC_ADC &&
                           opc != TP_ENC_EQC)
                    ok = 0;
                if (!ok)
                    break;
                if (an[q].reads > r)
                    break;              /* reads below the run's start */
                r += an[q].delta;
                if (r > mr)
                    mr = r;
            }
            if (!ok)
                break;                  /* no longer run can be clean either */
            if (q > j0 && r == 1) {
                best = cand;
                maxr = mr;
                break;
            }
        }
        if (best < 0 || an[best].depth < 0 || an[best].depth + 1 + maxr > 3)
            continue;
        /* A target on padding or a deleted instruction is still a way in. */
        for (k = best + 1; k <= i; k++)
            if (an[k].head)
                break;
        if (k <= i)
            continue;

        /* Rotate contents: the push to `best`, the run one place on. */
        {
            struct tp_relax_insn p = ins[jp];
            struct tps_ann pa = an[jp];
            for (m = jp; m > best; m--) {
                ins[m].opcode = ins[m - 1].opcode;
                ins[m].operand = ins[m - 1].operand;
                ins[m].old_len = ins[m - 1].old_len;
                ins[m].new_len = ins[m - 1].new_len;
                an[m].use = an[m - 1].use;
                an[m].def = an[m - 1].def;
                an[m].uses = an[m - 1].uses;
                an[m].reads = an[m - 1].reads;
                an[m].delta = an[m - 1].delta;
                an[m].known = an[m - 1].known;
                an[m].dead = an[m - 1].dead;
                an[m].liveout = an[m - 1].liveout;
                if (an[m - 1].depth >= 0)
                    an[m].depth = an[m - 1].depth + 1;
            }
            ins[best].opcode = p.opcode;
            ins[best].operand = p.operand;
            ins[best].old_len = p.old_len;
            ins[best].new_len = p.new_len;
            an[best].use = pa.use;
            an[best].def = pa.def;
            an[best].uses = pa.uses;
            an[best].reads = pa.reads;
            an[best].delta = pa.delta;
            an[best].known = pa.known;
            an[best].dead = 0;
            an[best].liveout = an[best].liveout | pa.liveout;
        }
        an[i].dead = 1;
        removed += 1;
        tps_note(7);
    }

    /* A three-word copy loop often computes the indexed destination before
     * loading the indexed local source, then uses REV for STNL. Both address
     * calculations only read workspace words. Load the source first, then
     * compute the destination, so STNL receives them in its natural order.
     * Restrict this to an empty stack and the exact indexed-local sequence. */
    for (i = 0; i < n; i++) {
        int q[9], j, k, order[7] = { 3, 4, 5, 6, 0, 1, 2 };
        struct tp_relax_insn saved[7];
        struct tps_ann saved_ann[7];

        if (func_uses_inline_asm || an[i].dead || an[i].depth != 0 ||
            ins[i].opcode != TP_ENC_LDL)
            continue;
        q[0] = i;
        for (j = 1; j < 9; j++) {
            q[j] = tps_next(ins, an, n, q[j - 1]);
            if (q[j] < 0)
                break;
        }
        if (j != 9 || !tps_free(ins, an, i, q[8]))
            continue;
        if (ins[q[1]].opcode != TP_ENC_LDL ||
            ins[q[2]].opcode != TP_ENC_OPR || ins[q[2]].operand != TP_OPR_WSUB ||
            ins[q[3]].opcode != TP_ENC_LDL || ins[q[3]].operand != ins[i].operand ||
            ins[q[4]].opcode != TP_ENC_LDLP ||
            ins[q[5]].opcode != TP_ENC_OPR || ins[q[5]].operand != TP_OPR_WSUB ||
            ins[q[6]].opcode != TP_ENC_LDNL || ins[q[6]].operand != 0 ||
            ins[q[7]].opcode != TP_ENC_OPR || ins[q[7]].operand != TP_OPR_REV ||
            ins[q[8]].opcode != TP_ENC_STNL || ins[q[8]].operand != 0)
            continue;
        for (j = 0; j < 9; j++)
            if (ins[q[j]].synth)
                break;
        if (j != 9)
            continue;
        for (j = 0; j < 7; j++) {
            saved[j] = ins[q[j]];
            saved_ann[j] = an[q[j]];
        }
        for (j = 0; j < 7; j++) {
            k = order[j];
            ins[q[j]].opcode = saved[k].opcode;
            ins[q[j]].operand = saved[k].operand;
            ins[q[j]].old_len = saved[k].old_len;
            ins[q[j]].new_len = saved[k].new_len;
            an[q[j]].use = saved_ann[k].use;
            an[q[j]].def = saved_ann[k].def;
            an[q[j]].reads = saved_ann[k].reads;
            an[q[j]].delta = saved_ann[k].delta;
            an[q[j]].known = saved_ann[k].known;
        }
        an[q[7]].dead = 1;
        removed++;
        tps_note(7);
    }

    /* ---- 5a''. a CJ that only skips a forward J ----
     *
     *     cj L1 ; j L2 ; L1:      ->      eqc 0 ; cj L2 ; L1:
     *
     * TCC's `c ? a : b` leaves this whenever the first arm needs no code of
     * its own (a constant or a variable): the CJ to the second arm jumps over
     * nothing but the J to where the first arm's value is loaded.  EQC 0
     * inverts the test, so the CJ goes where the J went and the fall-through
     * is the old target.  Only the stack junk at the two targets differs
     * (a taken CJ leaves its zero in AReg), and nothing is live at a label.
     * The J must not be a branch target itself, and must go forwards: a
     * backward J may be a loop's only timeslice point. */
    for (i = 0; i < n && tps_on(11); i++) {
        int jj, t1, k, nx;
        if (an[i].dead || ins[i].pinned || ins[i].opcode != TP_ENC_CJ ||
            !ins[i].is_branch || ins[i].ext)
            continue;
        jj = tps_next(ins, an, n, i);
        if (jj < 0 || ins[jj].opcode != TP_ENC_J || !ins[jj].is_branch ||
            ins[jj].pinned || ins[jj].ext ||
            ins[jj].target_old <= ins[jj].old_off)
            continue;
        for (k = i + 1; k <= jj; k++)
            if (an[k].head)
                break;
        if (k <= jj)
            continue;
        t1 = ins[i].target_old;
        if (t1 < 0 || t1 >= size || idx[t1] < 0)
            continue;
        for (k = idx[t1]; k < n && (ins[k].opcode == -1 || an[k].dead); k++)
            ;
        nx = tps_next(ins, an, n, jj);
        if (nx < 0 || k != nx)
            continue;
        ins[i].opcode = TP_ENC_EQC;
        ins[i].operand = 0;
        ins[i].is_branch = 0;
        an[i].known = tps_effect(TP_ENC_EQC, 0, &an[i].delta, &an[i].reads);
        an[i].succ2 = -1;
        ins[jj].opcode = TP_ENC_CJ;
        an[jj].known = tps_effect(TP_ENC_CJ, 0, &an[jj].delta, &an[jj].reads);
        an[jj].succ2 = an[jj].succ1;
        an[jj].succ1 = jj + 1 < n ? jj + 1 : -1;
    }

    /* ---- 5b. drop REVs whose consumer does not care about operand order ----
     *
     * Rules S2, S4/S5 and S7 all pay for removing a store by inserting a REV,
     * because the two values come back off the stack swapped. But REV is a
     * stage-4 ALU operation, and a group gets only one of those — so a REV
     * closes a group exactly as a store did, and the win is only the byte
     * saved, not the pipeline slot.
     *
     * When the very next thing to execute is a commutative binary operation,
     * the swap is unobservable and the REV is pure waste. Measured after this
     * scheduler landed: rev went 24,871 -> 120,942 while ALU group closures
     * rose 19.9% -> 26.7%, which is most of the group-width regression.
     *
     * Only exact 2-in/1-out commutative OPRs qualify. DIFF, SUB, GT, the
     * shifts and WSUB are all order-sensitive and must keep their REV. */
    for (i = 0; i < n; i++) {
        int j;
        if (an[i].dead || ins[i].pinned)
            continue;
        if (ins[i].opcode != TP_ENC_OPR || ins[i].operand != TP_OPR_REV)
            continue;
        j = tps_next(ins, an, n, i);
        if (j < 0 || !tps_free(ins, an, i, j))
            continue;               /* a branch target may be reached with
                                     * the operands the other way round */
        if (ins[j].opcode != TP_ENC_OPR)
            continue;
        switch (ins[j].operand) {
        case TP_OPR_SUM: case TP_OPR_ADD:
        case TP_OPR_PROD: case TP_OPR_MUL:
        case TP_OPR_AND: case TP_OPR_OR: case TP_OPR_XOR:
        /* bsub is plain commutative addition (AReg + BReg, t4/p.c:4703), so a
         * preceding REV is dead for it too.  wsub/ssub/wsubdb are NOT
         * commutative - they scale BReg and use AReg as the base - and must
         * keep their REV. */
        case TP_OPR_BSUB:
            if (an[j].reads != 2)
                break;
            an[i].dead = 1;
            removed += 1;
            tps_note(7);
            break;
        default:
            break;
        }
    }

    /* ---- 5b. rev ; rev  ==  nothing ----
     * REV exchanges AReg and BReg, so two in a row restore the original
     * stack.  Pairs arise when gen_store_addr already leaves AReg=address /
     * BReg=value and store()'s narrow path adds its own REV to guarantee that
     * order: every initialised byte of `char buf[16] = "hello"` emitted one.
     * The second REV must not be a branch target, or control could arrive
     * having executed only one of the pair. */
    for (i = 0; i < n; i++) {
        int j;
        if (an[i].dead || ins[i].pinned)
            continue;
        if (ins[i].opcode != TP_ENC_OPR || ins[i].operand != TP_OPR_REV)
            continue;
        j = tps_next(ins, an, n, i);
        if (j < 0 || !tps_free(ins, an, i, j))
            continue;
        if (ins[j].opcode != TP_ENC_OPR || ins[j].operand != TP_OPR_REV)
            continue;
        an[i].dead = 1;
        an[j].dead = 1;
        removed += 2;
        tps_note(7);
    }

    /* A table index can shift right and then scale back up by the same
     * power of two (e.g. table[bits >> 4] with 16-byte rows). Clear the
     * low bits directly. Match the decoded sequence so spills removed by
     * scheduling do not hide it, and preserve every interior entry point. */
    for (i = 0; i < n; i++) {
        int right, count, left, shift, mask, width;
        if (an[i].dead || ins[i].opcode != TP_ENC_LDC)
            continue;
        shift = ins[i].operand;
        if (shift < 1 || shift > 31)
            continue;
        right = tps_next(ins, an, n, i);
        if (right < 0 || ins[right].opcode != TP_ENC_OPR ||
            ins[right].operand != TP_OPR_SHR)
            continue;
        count = tps_next(ins, an, n, right);
        if (count < 0 || ins[count].opcode != TP_ENC_LDC ||
            ins[count].operand != shift)
            continue;
        left = tps_next(ins, an, n, count);
        if (left < 0 || ins[left].opcode != TP_ENC_OPR ||
            ins[left].operand != TP_OPR_SHL || !tps_free(ins, an, i, left))
            continue;

        mask = (int)(~0u << shift);
        width = tp_encode_len(TP_ENC_LDC, mask);
        if (width > ins[i].new_len + ins[right].new_len + ins[count].new_len)
            continue;
        ins[i].operand = mask;
        ins[i].new_len = width;
        ins[left].operand = TP_OPR_AND;   /* same encoded width as SHL */
        ins[right].new_len = ins[count].new_len = 0;
        an[right].dead = an[count].dead = 1;
        removed += 2;
    }

    /* ---- 6. commit ---- */
    for (i = 0; i < n; i++) {
        if (an[i].dead) {
            ins[i].opcode = -1;
            ins[i].operand = 0;
            ins[i].is_branch = 0;
        }
    }
    if (tps_stats_on > 0)
        fprintf(stderr, "[SCHED] cumulative S1=%d S2=%d S3=%d S4=%d S5=%d "
                        "S6=%d S7=%d S8=%d S9=%d S10=%d S11=%d S12=%d "
                        "(last %s -%d)\n",
                tps_stats[0], tps_stats[1], tps_stats[2], tps_stats[3],
                tps_stats[4], tps_stats[5], tps_stats[6], tps_stats[7],
                tps_stats[8], tps_stats[9], tps_stats[12], tps_stats[13],
                funcname ? funcname : "?", removed);
    tcc_free(idx);
    tcc_free(an);
    return removed;
}

/*
 * Workspace slot layout.
 *
 * Only slots 0..15 are addressable in one byte, and the emitter numbers the
 * frame by convention rather than by use: pseudo-registers, the float and
 * return slots and backend scratch take the low numbers whether or not the
 * function touches them, and locals follow in allocation order.  In vmlinux
 * over half of all LDL/STL/LDLP needed a prefix byte while slots 3-6 and 8
 * sat almost idle.
 *
 * This renumbers the frame by use.  The frame is cut into units that must
 * keep their internal layout: each slot of the reserved region (a
 * pseudo-register or scratch word) and each block of locals between two
 * recorded values of `loc` (tp_loc_marks) - a struct, say, or an array whose
 * address is taken.  Units are laid out from the bottom of the frame in order
 * of accesses per word, every LDL/STL/LDLP naming a moved slot is rewritten,
 * and the result is kept only when the function gets smaller.
 *
 * What stays put:
 *   - slot 0, which outbyte/outword and the ALT instructions use implicitly,
 *     and the actual outgoing stack-argument words, which the callee reads
 *     in place. Temporary call-staging words are accessed explicitly and
 *     can move with the other reserved slots;
 *   - everything at and above the frame top: return address and parameters,
 *     and the `top` words tp_gaddr_cache put just under them, which this
 *     pass takes to be above the frame;
 *   - the shape around an LDLP naming the first slot of a unit, which may be
 *     `&arr[N]` for the unit below: the two are merged.  One naming the frame
 *     top pins the unit beneath it.
 * Functions the pass cannot see into are left alone, as tp_sched_function
 * leaves them: inline assembly, GAJW, a mid-function AJW, and an LDLP of a
 * pseudo-register in any shape but the LDNL/STNL pairs the back end emits.
 *
 * Runs after the scheduler, which reasons about the emitter's numbering.
 * Returns 1 if it rewrote anything; new_len then holds each rewritten
 * instruction's width, which may have grown.
 */
static int tpg_ldlp_extent(const struct tp_relax_insn *c, int *lo);

static int tp_slot_remap(struct tp_relax_insn *ins, int n, int top)
{
    static int off = -1, stats = -1;
    int F = func_frame_words - top, R = func_frame_reserved;
    int P = func_max_stack_words > 1 ? func_max_stack_words : 1;
    int pin_top = 0, nu = 0, lo, hi, first, i, j, s, pos;
    int old_bytes = 0, new_bytes = 0;
    char *cut;
    int *unit, *ustart, *usize, *uweight, *uorder, *unew;

    if (off < 0) {
        off = getenv("TP_NO_SLOTMAP") ? 1 : 0;
        stats = getenv("TP_SLOTMAP_STATS") ? 1 : 0;
    }
    if (off || func_uses_inline_asm || tp_loc_marks_bad ||
        F <= P + 1 || F > 4096 || R > F)
        return 0;

    for (first = 0; first < n && ins[first].opcode == -1; first++)
        ;
    for (i = 0; i < n; i++) {
        if (ins[i].opcode == TP_ENC_OPR && ins[i].operand == TP_OPR_GAJW)
            return 0;
        if (ins[i].opcode == TP_ENC_AJW && i != first) {
            for (j = i + 1; j < n && ins[j].opcode == -1; j++)
                ;
            if (!(j < n && ins[j].opcode == TP_ENC_OPR &&
                  ins[j].operand == TP_OPR_RET))
                return 0;
        }
    }

    /* cut[s]: a unit starts at slot s.  Every reserved slot is its own unit;
     * locals are cut at each recorded `loc` that falls on a word boundary. */
    cut = tcc_mallocz(F + 1);
    for (s = 0; s <= R; s++)
        cut[s] = 1;
    cut[F] = 1;
    for (i = 0; i < tp_loc_marks_count; i++) {
        int m = tp_loc_marks[i];
        if (m <= 0 && !(m & 3) && F + m / 4 > R && F + m / 4 < F)
            cut[F + m / 4] = 1;
    }

    /* Merge what LDLP ties together. */
    for (i = 0; i < n; i++) {
        int t, last, recognised, bias = 0, extent = -1;
        if (ins[i].opcode != TP_ENC_LDLP)
            continue;
        if (tp_relax_byte_read(ins, n, i))
            continue;                       /* reads its own word in place */
        s = ins[i].operand;
        for (j = i + 1; j < n && ins[j].opcode == -1; j++)
            ;
        recognised = j < n && (ins[j].opcode == TP_ENC_LDNL ||
                               ins[j].opcode == TP_ENC_STNL);
        if (j < n)
            extent = tpg_ldlp_extent(&ins[j], &bias);
        /* A known double access ties both words together, including when
         * its first word is pinned outgoing storage.  Unknown pseudo-
         * register escapes retain the conservative path below. */
        if (extent == 2)
            recognised = 1;
        if (recognised) {
            int64_t begin = (int64_t)s + bias;
            int64_t end = begin + extent - 1;
            if (begin < INT32_MIN || end > INT32_MAX)
                goto bail;
            t = begin;
            last = end;
            if ((s < 0 || s >= F) != (t < 0 || t >= F) ||
                (s < 0 || s >= F) != (last < 0 || last >= F)) {
                if (s < P && t < P && last < P)
                    continue;               /* staging, which stays put */
                goto bail;                  /* reaches across the frame edge */
            }
            if (s >= 0 && s < F && (t != s || last != s)) {
                int a = s < t ? s : t, b = s > last ? s : last, k;
                for (k = a + 1; k <= b; k++)
                    cut[k] = 0;
            }
        } else if (s >= 0 && s < R) {
            if (s >= P)
                goto bail;                  /* a pseudo-register escapes */
        } else if (s > R && s < F) {
            cut[s] = 0;                     /* maybe one past the unit below */
        } else if (s == F) {
            pin_top = 1;
        }
    }

    /* Units, their weights, and the pinned ends: [0, lo) holds slot 0 and the
     * staging words, [hi, F) the unit an LDLP of the frame top points past. */
    unit = tcc_malloc(F * sizeof(int));
    ustart = tcc_malloc((F + 1) * sizeof(int));
    usize = tcc_malloc((F + 1) * sizeof(int));
    uweight = tcc_mallocz((F + 1) * sizeof(int));
    uorder = tcc_malloc((F + 1) * sizeof(int));
    unew = tcc_malloc((F + 1) * sizeof(int));
    for (s = 0; s < F; s++) {
        if (cut[s]) {
            ustart[nu] = s;
            usize[nu++] = 0;
        }
        unit[s] = nu - 1;
        usize[nu - 1]++;
    }
    lo = ustart[unit[P - 1]] + usize[unit[P - 1]];
    hi = pin_top ? ustart[unit[F - 1]] : F;
    for (i = 0; i < n; i++) {
        int opc = ins[i].opcode;
        if ((opc == TP_ENC_LDL || opc == TP_ENC_STL || opc == TP_ENC_LDLP) &&
            ins[i].operand >= 0 && ins[i].operand < F)
            uweight[unit[ins[i].operand]]++;
    }

    /* Lay the movable units out from lo, densest first; insertion sort keeps
     * equal densities in frame order. */
    {
        int m = 0, u, k;
        for (u = unit[lo < F ? lo : F - 1]; u < nu && ustart[u] < hi; u++) {
            if (ustart[u] < lo)
                continue;
            for (k = m; k > 0; k--) {
                int v = uorder[k - 1];
                if ((long long)uweight[v] * usize[u] >=
                    (long long)uweight[u] * usize[v])
                    break;
                uorder[k] = v;
            }
            uorder[k] = u;
            m++;
        }
        for (pos = lo, k = 0; k < m; k++) {
            unew[uorder[k]] = pos;
            pos += usize[uorder[k]];
        }
    }

    for (i = 0; i < n; i++) {
        int opc = ins[i].opcode, u;
        if (opc != TP_ENC_LDL && opc != TP_ENC_STL && opc != TP_ENC_LDLP)
            continue;
        s = ins[i].operand;
        if (s < lo || s >= hi)
            continue;
        u = unit[s];
        old_bytes += tp_encode_len(opc, s);
        new_bytes += tp_encode_len(opc, s - ustart[u] + unew[u]);
    }
    if (stats && old_bytes)
        fprintf(stderr, "[SLOTMAP] %s: %d -> %d bytes%s\n",
                funcname ? funcname : "?", old_bytes, new_bytes,
                new_bytes < old_bytes ? "" : " (kept)");
    if (new_bytes >= old_bytes)
        goto bail;

    for (i = 0; i < n; i++) {
        int opc = ins[i].opcode, u;
        if (opc != TP_ENC_LDL && opc != TP_ENC_STL && opc != TP_ENC_LDLP)
            continue;
        s = ins[i].operand;
        if (s < lo || s >= hi)
            continue;
        u = unit[s];
        ins[i].operand = s - ustart[u] + unew[u];
        ins[i].new_len = tp_encode_len(opc, ins[i].operand);
    }
    tcc_free(unit); tcc_free(ustart); tcc_free(usize);
    tcc_free(uweight); tcc_free(uorder); tcc_free(unew);
    tcc_free(cut);
    return 1;

bail:
    if (nu) {
        tcc_free(unit); tcc_free(ustart); tcc_free(usize);
        tcc_free(uweight); tcc_free(uorder); tcc_free(unew);
    }
    tcc_free(cut);
    return 0;
}

/*
 * Global addresses kept in the frame.
 *
 * A global's address is the 8-byte `ldc` placeholder the linker fills in
 * (tp_push_sym_addr), and RAM starts at MinInt, so an address such as
 * #800469B4 needs all eight nibbles: 8 cycles a use, where occam reaches
 * its globals through the static link for 2 or 3.  In the ray tracer that
 * was 133 M T800 cycles more than occam, most of it in loops: sceneSect
 * reads worldModel three times for every object in the scene.
 *
 * When a function uses one symbol's address often enough, load it once,
 * right after the prologue, into a frame word of its own and make every
 * use an `ldl` of that word.  The address never changes, so the load can
 * sit at the entry whatever the control flow.  It costs 10 cycles even on
 * a path that never uses it and saves 5 or 6 a use, so a symbol needs a
 * weight of three, a use inside a loop (under a backward branch) weighing
 * four.  A single use qualifies only in a loop and only for a named
 * symbol: an anonymous one (L.n) in a loop is usually a message string on
 * the loop's error path, as in sceneSect.
 *
 * Constants of six bytes or more get the same treatment: a float's bits
 * (coneSect loads 1.0 five times and 0.001 four, `ldc #3F800000; stl 15;
 * ldlp 15; fpldnlsn`) or a wide mask.  The word then holds the constant
 * itself, so the scheduler's S10 can load a float straight from it
 * (`ldlp word; fpldnlsn`).
 *
 * The words are new, on top of the frame: the locals keep their slots and
 * only what lies above them - the return address and the parameters, each
 * LDL/STL/LDLP from slot F up - moves, and the AJWs grow.  Inserting them
 * lower would push the locals up: Doom's R_DrawColumn had its loop's locals
 * pushed past slot 15, a prefix byte and a cycle more on every access.  An
 * LDLP of slot F is one past the topmost local (an array's end) and stays,
 * unless what it feeds reads above it.  The slot remap leaves the new words
 * where they are, for the same reason.  Only those instructions may name a
 * frame word, so functions with inline assembly, GAJW, an AJW other than
 * the prologue's and the epilogues' `ajw N; ret`, or an access through an
 * LDLP that crosses the frame's top are left alone.  So are those with
 * startp, endp or runp: a process can enter the body there on another
 * workspace, skipping the loads (test_process_builtins).
 *
 * The relocations change only when the rewrite is committed
 * (tp_gaddr_commit): the first use's moves to the new load, whose bytes
 * repeat the placeholder, and the others are deleted.
 * TCC_TRANSPUTER_GADDR=0 turns it off; TP_GADDR_STATS=1 lists what it
 * keeps.
 */
#define TPG_MAX 8

static struct {
    int n;                  /* symbols kept; 0 means nothing to commit */
    int move_rel[TPG_MAX];  /* relocation that moves to the entry load */
    int load_rec[TPG_MAX];  /* the entry load */
    int *drop;              /* relocations of the uses that became LDLs */
    int ndrop, cap;
} tpg;

/* The frame words an LDLP's consumer touches, from `*lo` words above the
 * LDLP's slot; -1 when it is not an access this pass can follow. */
static int tpg_ldlp_extent(const struct tp_relax_insn *c, int *lo)
{
    *lo = 0;
    if (c->opcode == TP_ENC_LDNL || c->opcode == TP_ENC_STNL) {
        *lo = c->operand;
        return 1;
    }
    if (c->opcode != TP_ENC_OPR)
        return -1;
    switch (c->operand) {
    case TP_FPU_LDNLSN: case TP_FPU_STNLSN:
    case TP_FPU_LDNLADDSN: case TP_FPU_LDNLMULSN:
    case TP_OPR_FPI32TOR32: case TP_OPR_FPI32TOR64:
    case TP_OPR_FPB32TOR64: case TP_OPR_FPSTNLI32:
        return 1;
    case TP_FPU_LDNLDB: case TP_FPU_STNLDB:
    case TP_FPU_LDNLADDDB: case TP_FPU_LDNLMULDB:
        return 2;
    }
    return -1;
}

/* Does LDLP i name a word above a frame of F words, which moves when the
 * frame grows?  -1 when the access it feeds crosses the frame's top. */
static int tpg_ldlp_above(const struct tp_relax_insn *ins, int n, int i,
                          int F)
{
    int j, lo, ext, s = ins[i].operand;

    for (j = i + 1; j < n && ins[j].opcode == -1; j++)
        ;
    ext = j < n ? tpg_ldlp_extent(&ins[j], &lo) : -1;
    if (ext < 0)
        return s > F;           /* an address handed on: F is a local's end */
    if ((s + lo >= F) != (s + lo + ext - 1 >= F))
        return -1;
    if (s + lo >= F)
        return s >= F ? 1 : -1;
    return s <= F ? 0 : -1;
}

/* Uses as {kind, key, record, relocation, weight}: kind 0 a symbol's
 * address (key its symbol index), kind 1 a constant (key its value).
 * Sorted by kind, key, then code order. */
#define TPG_U 5
static int tpg_use_cmp(const void *a, const void *b)
{
    const int *x = a, *y = b;
    int k;
    for (k = 0; k < 3; k++)
        if (x[k] != y[k])
            return x[k] < y[k] ? -1 : 1;
    return 0;
}

/* Returns 1 if it rewrote anything; *pins may then have moved and *pn
 * grown, and tidx is rebuilt. */
static int tp_gaddr_cache(struct tp_relax_insn **pins, int *pn, int *tidx,
                          int size)
{
    static int off = -1, stats = -1;
    Section *sec = cur_text_section;
    struct tp_relax_insn *ins = *pins;
    int n = *pn, F = func_frame_words;
    int base = func_start_ind;
    int first, i, j, k, w = 0, nu = 0, nb = 0, nrel, at;
    int *u, *lp;
    int pick[TPG_MAX], plen[TPG_MAX], key[TPG_MAX];
    ElfW_Rel *rel;

    tpg.n = 0;
    if (off < 0) {
        const char *e = getenv("TCC_TRANSPUTER_GADDR");
        off = (e && *e == '0') ? 1 : 0;
        stats = getenv("TP_GADDR_STATS") ? 1 : 0;
    }
    if (off || func_uses_inline_asm || !sec->reloc || F <= 0)
        return 0;

    /* The frame's shape: `ajw -F` first, `ajw F; ret` to leave, and every
     * other access to a frame word an LDL, STL or LDLP. */
    for (first = 0; first < n && ins[first].opcode == -1; first++)
        ;
    if (first + 1 >= n || ins[first].opcode != TP_ENC_AJW ||
        ins[first].operand != -F)
        return 0;
    for (i = 0; i < n; i++) {
        int opc = ins[i].opcode;
        /* A process started here (startp at the next instruction, the
         * `__startp(ws, 0)` idiom), resumed here by endp, or made runnable
         * by runp may run this body on another workspace, without the
         * prologue that fills the new words. */
        if (opc == TP_ENC_OPR && (ins[i].operand == TP_OPR_GAJW ||
                                  ins[i].operand == TP_OPR_STARTP ||
                                  ins[i].operand == TP_OPR_ENDP ||
                                  ins[i].operand == TP_OPR_RUNP))
            return 0;
        if (ins[i].pinned && (opc == TP_ENC_LDL || opc == TP_ENC_STL ||
                              opc == TP_ENC_LDLP || opc == TP_ENC_AJW))
            return 0;
        if (opc == TP_ENC_AJW && i != first) {
            for (j = i + 1; j < n && ins[j].opcode == -1; j++)
                ;
            if (ins[i].operand != F || j == n || ins[j].opcode != TP_ENC_OPR
                || ins[j].operand != TP_OPR_RET)
                return 0;
        }
        if (opc == TP_ENC_LDLP && tpg_ldlp_above(ins, n, i, F) < 0)
            return 0;
    }

    /* The uses.  A symbol's: an LDC_ABS relocation on an untouched
     * placeholder, seven PFIX 0 and `ldc 0`, which the pinning step decoded
     * as eight records.  A constant's: an `ldc` of six bytes or more, a
     * float's bits or a wide mask, but not an `ldc; ldpi` offset, which
     * moves with the code. */
    rel = (ElfW_Rel *)sec->reloc->data;
    nrel = sec->reloc->data_offset / sizeof(*rel);
    if (nrel < func_start_nrel)
        return 0;
    u = tcc_malloc(sizeof(int) * TPG_U * (nrel - func_start_nrel + n));
    for (i = func_start_nrel; i < nrel; i++) {
        int ro = (int)rel[i].r_offset - base;
        if (ELFW(R_TYPE)(rel[i].r_info) != R_TRANSPUTER_LDC_ABS ||
            ro < 0 || ro + 8 > size || (k = tidx[ro]) < 0 || k + 8 > n)
            continue;
        for (j = 0; j < 7; j++)
            if (ins[k + j].opcode != -1 || ins[k + j].old_len != 1 ||
                ins[k + j].old_off != ro + j)
                break;
        if (j < 7 || ins[k + 7].opcode != TP_ENC_LDC ||
            ins[k + 7].operand != 0 || ins[k + 7].old_len != 1)
            continue;
        u[TPG_U * nu] = 0;
        u[TPG_U * nu + 1] = ELFW(R_SYM)(rel[i].r_info);
        u[TPG_U * nu + 2] = k;
        u[TPG_U * nu + 3] = i;
        u[TPG_U * nu + 4] = 1;
        nu++;
    }
    for (i = 0; i < n; i++) {
        if (ins[i].opcode != TP_ENC_LDC || ins[i].pinned ||
            tp_encode_len(TP_ENC_LDC, ins[i].operand) < 6)
            continue;
        for (j = i + 1; j < n && ins[j].opcode == -1; j++)
            ;
        if (j < n && ins[j].opcode == TP_ENC_OPR &&
            ins[j].operand == TP_OPR_LDPI)
            continue;
        u[TPG_U * nu] = 1;
        u[TPG_U * nu + 1] = ins[i].operand;
        u[TPG_U * nu + 2] = i;
        u[TPG_U * nu + 3] = -1;
        u[TPG_U * nu + 4] = 1;
        nu++;
    }
    if (nu < 1) {
        tcc_free(u);
        return 0;
    }

    /* A use under a backward branch is in a loop. */
    lp = tcc_malloc(sizeof(int) * 2 * n);
    for (i = 0; i < n; i++)
        if (ins[i].is_branch && ins[i].target_old <= ins[i].old_off) {
            lp[2 * nb] = ins[i].target_old;
            lp[2 * nb + 1] = ins[i].old_off;
            nb++;
        }
    for (i = 0; i < nu; i++) {
        int o = ins[u[TPG_U * i + 2]].old_off;
        for (j = 0; j < nb; j++)
            if (lp[2 * j] <= o && o <= lp[2 * j + 1]) {
                u[TPG_U * i + 4] = 4;
                break;
            }
    }
    tcc_free(lp);

    /* The ones worth a word, heaviest first. */
    qsort(u, nu, sizeof(int) * TPG_U, tpg_use_cmp);
    for (;;) {
        int best = -1, bestlen = 0, bestwt = 2;
        for (i = 0; i < nu; i += k) {
            int wt = 0;
            for (k = 0; i + k < nu && u[TPG_U * (i + k)] == u[TPG_U * i] &&
                        u[TPG_U * (i + k) + 1] == u[TPG_U * i + 1]; k++)
                wt += u[TPG_U * (i + k) + 4];
            for (j = 0; j < w && pick[j] != i; j++)
                ;
            if (k == 1 && u[TPG_U * i] == 0) {
                ElfW(Sym) *es = (ElfW(Sym) *)symtab_section->data +
                                u[TPG_U * i + 1];
                const char *nm = (char *)symtab_section->link->data +
                                 es->st_name;
                if (nm[0] == 'L' && nm[1] == '.')
                    continue;
            }
            if (j == w && wt > bestwt) {
                best = i;
                bestlen = k;
                bestwt = wt;
            }
        }
        if (best < 0 || w == TPG_MAX)
            break;
        pick[w] = best;
        plen[w++] = bestlen;
    }
    if (!w) {
        tcc_free(u);
        return 0;
    }

    /* Make room: the new words are F .. F+w-1. */
    for (i = 0; i < n; i++) {
        int opc = ins[i].opcode;
        if ((opc == TP_ENC_LDL || opc == TP_ENC_STL) && ins[i].operand >= F)
            ins[i].operand += w;
        else if (opc == TP_ENC_LDLP && tpg_ldlp_above(ins, n, i, F) > 0)
            ins[i].operand += w;
        else if (opc == TP_ENC_AJW)
            ins[i].operand += ins[i].operand < 0 ? -w : w;
        else
            continue;
        if (tp_encode_len(opc, ins[i].operand) > ins[i].new_len)
            ins[i].new_len = tp_encode_len(opc, ins[i].operand);
    }

    /* Every use becomes `ldl word`; a placeholder's PFIX 0 go. */
    tpg.ndrop = 0;
    for (j = 0; j < w; j++) {
        int sym = u[TPG_U * pick[j]] == 0;
        key[j] = u[TPG_U * pick[j] + 1];
        tpg.move_rel[j] = -1;
        for (i = pick[j]; i < pick[j] + plen[j]; i++) {
            int r = u[TPG_U * i + 2];
            if (sym) {
                for (k = r; k < r + 7; k++)
                    ins[k].pinned = 0;
                r += 7;
            }
            ins[r].pinned = 0;
            ins[r].opcode = TP_ENC_LDL;
            ins[r].operand = F + j;
            ins[r].new_len = tp_encode_len(TP_ENC_LDL, F + j);
            if (!sym)
                continue;
            if (i == pick[j]) {
                tpg.move_rel[j] = u[TPG_U * i + 3];
                continue;
            }
            if (tpg.ndrop == tpg.cap) {
                tpg.cap = tpg.cap ? 2 * tpg.cap : 64;
                tpg.drop = tcc_realloc(tpg.drop, sizeof(int) * tpg.cap);
            }
            tpg.drop[tpg.ndrop++] = u[TPG_U * i + 3];
        }
        if (stats) {
            int wt = 0;
            for (i = pick[j]; i < pick[j] + plen[j]; i++)
                wt += u[TPG_U * i + 4];
            if (sym) {
                ElfW(Sym) *es = (ElfW(Sym) *)symtab_section->data + key[j];
                fprintf(stderr, "[GADDR] %s: %s, %d uses, weight %d\n",
                        funcname ? funcname : "?",
                        (char *)symtab_section->link->data + es->st_name,
                        plen[j], wt);
            } else {
                fprintf(stderr, "[GADDR] %s: #%08X, %d uses, weight %d\n",
                        funcname ? funcname : "?", (unsigned)key[j],
                        plen[j], wt);
            }
        }
    }
    tcc_free(u);

    /* After the prologue, `ldc; stl word` for each: a symbol's a
     * placeholder the linker fills as it did the first use's, a constant's
     * the constant.  They come before the instruction they are inserted in
     * front of and share its old offset, so a branch to that instruction
     * still lands on it. */
    ins = tcc_realloc(ins, sizeof(*ins) * (n + 2 * w + 1));
    memmove(ins + first + 1 + 2 * w, ins + first + 1,
            sizeof(*ins) * (n - first - 1));
    at = ins[first + 1 + 2 * w].old_off;
    for (j = 0; j < w; j++) {
        struct tp_relax_insn *a = &ins[first + 1 + 2 * j], *b = a + 1;
        memset(a, 0, 2 * sizeof(*a));
        a->old_off = b->old_off = at;
        a->opcode = TP_ENC_LDC;
        if (tpg.move_rel[j] >= 0) {
            a->new_len = 8;
            a->pinned = 1;
        } else {
            a->operand = key[j];
            a->new_len = tp_encode_len(TP_ENC_LDC, key[j]);
        }
        a->synth = 1;
        b->opcode = TP_ENC_STL;
        b->operand = F + j;
        b->new_len = tp_encode_len(TP_ENC_STL, F + j);
        b->synth = 1;
        tpg.load_rec[j] = first + 1 + 2 * j;
    }
    n += 2 * w;
    for (i = 0; i <= size; i++)
        tidx[i] = -1;
    for (i = 0; i < n; i++)
        if (!ins[i].synth)
            tidx[ins[i].old_off] = i;

    func_frame_words = F + w;
    tpg.n = w;
    *pins = ins;
    *pn = n;
    return 1;
}

/* Once the rewrite is in place: each kept symbol's relocation moves to its
 * entry load, and the other uses' relocations are deleted. */
static void tp_gaddr_commit(Section *sec, const struct tp_relax_insn *ins)
{
    ElfW_Rel *rel = (ElfW_Rel *)sec->reloc->data;
    int nrel = sec->reloc->data_offset / sizeof(*rel);
    int i, j, k;
    char *gone;

    for (j = 0; j < tpg.n; j++)
        if (tpg.move_rel[j] >= 0)           /* not a constant */
            rel[tpg.move_rel[j]].r_offset = func_start_ind +
                                            ins[tpg.load_rec[j]].new_off;
    gone = tcc_mallocz(nrel - func_start_nrel);
    for (i = 0; i < tpg.ndrop; i++)
        gone[tpg.drop[i] - func_start_nrel] = 1;
    for (i = k = func_start_nrel; i < nrel; i++)
        if (!gone[i - func_start_nrel])
            rel[k++] = rel[i];
    sec->reloc->data_offset = k * sizeof(*rel);
    tcc_free(gone);
    tpg.n = 0;
}

/* Returns 1 if the function was rewritten. */
static int tp_relax_function(void)
{
    Section *sec = cur_text_section;
    uint8_t *code;
    struct tp_relax_insn *ins;
    int n = 0, i, pass;
    int base = func_start_ind;
    int size = ind - func_start_ind;
    int p, saved;
    int *map;           /* old offset -> new offset */
    int *tidx;          /* old offset -> instruction index */
    uint8_t *out;
    int outlen;
    int grown = 0;      /* an instruction may now need more bytes */

    if (base < 0 || size <= 0 || size > TP_RELAX_MAX_INSNS)
        return tp_relax_bail("size");
    /* Switch jump tables embed absolute code addresses that are patched later
     * from recorded slot offsets; relaxing under them would invalidate both. */
    if (func_has_switch_slots)
        return tp_relax_bail("switch");
    /* Debug info records code offsets per line. */
    if (tcc_state->do_debug)
        return tp_relax_bail("debug");

    /* An assembler addend can name an interior byte by its distance from a
     * label. Preserve that layout, including for backward references. */
    if (sec->reloc) {
        ElfW_Rel *rel = (ElfW_Rel *)sec->reloc->data;
        int end = sec->reloc->data_offset / sizeof(*rel);
        for (i = func_start_nrel; i < end; i++) {
            int type = ELFW(R_TYPE)(rel[i].r_info);
            if (type == R_TRANSPUTER_INSN || tp_data_layout_reloc(type))
                return tp_relax_bail("asm-addend");
        }
    }

    code = sec->data + base;
    ins = tcc_malloc(sizeof(*ins) * (size + 1));

    /* 1. Linear decode. */
    for (p = 0; p < size; ) {
        int opc = -1, opd = 0;
        int len;

        /* At an instruction boundary Oreg is 0, so a PFIX 0 byte contributes
         * nothing: it is pure placeholder padding. Record it as a standalone
         * no-op rather than letting it merge into the instruction that
         * follows. Merging would be semantically harmless but would erase the
         * boundary between them, and jump targets land on exactly those
         * boundaries — which is why most functions previously bailed out. */
        if (code[p] == ((TP_ENC_PFIX << 4) | 0)) {
            ins[n].old_off = p;
            ins[n].old_len = 1;
            ins[n].new_len = 1;
            ins[n].opcode = -1;
            ins[n].operand = 0;
            ins[n].pinned = 0;
            ins[n].is_branch = 0;
            ins[n].target_old = 0;
            ins[n].ext = 0;
            ins[n].synth = 0;
            n++;
            p++;
            continue;
        }

        len = tp_decode_instr_at(code, size, p, &opc, &opd);
        if (len <= 0) {           /* malformed tail — bail out entirely */
            tcc_free(ins);
            return tp_relax_bail("decode");
        }
        ins[n].old_off = p;
        ins[n].old_len = len;
        ins[n].new_len = len;
        ins[n].opcode = opc;
        ins[n].operand = opd;
        ins[n].pinned = 0;
        ins[n].is_branch = (opc == TP_ENC_J || opc == TP_ENC_CJ);
        ins[n].target_old = ins[n].is_branch ? (p + len + opd) : 0;
        ins[n].ext = 0;
        ins[n].synth = 0;
        n++;
        p += len;
    }

    /* An offset -> index map. Branch retargeting is done three times over
     * (shrink fixpoint, emit, validation) and a linear search there made the
     * whole pass quadratic in the size of the function. */
    tidx = tcc_malloc(sizeof(int) * (size + 1));
    for (i = 0; i <= size; i++)
        tidx[i] = -1;
    for (i = 0; i < n; i++)
        tidx[ins[i].old_off] = i;

    /* 2. Pin anything a relocation points into.  This function's
     * relocations are the ones added since its prologue. */
    if (sec->reloc) {
        ElfW_Rel *rel = (ElfW_Rel *)sec->reloc->data;
        int nrel = sec->reloc->data_offset / sizeof(*rel);
        for (i = func_start_nrel; i < nrel; i++) {
            int ro = (int)rel[i].r_offset - base;
            int k, m;
            if (ro < 0 || ro >= size)
                continue;
            /* the instruction holding ro starts at or before it */
            for (k = ro; k > 0 && tidx[k] < 0; k--)
                ;
            k = tidx[k];
            if (k < 0 || ro >= ins[k].old_off + ins[k].old_len) {
                tcc_free(tidx); tcc_free(ins);
                return tp_relax_bail("reloc-outside");
            }
            /* A relocated placeholder such as the 8-byte LDC_ABS chain
             * is now decoded as a run of PFIX 0 no-ops followed by the
             * real opcode. The linker rewrites the whole reserved
             * width, so pin the entire run, not just the first byte. */
            for (m = k; m < n; m++) {
                ins[m].pinned = 1;
                if (ins[m].opcode != -1)
                    break;
            }
        }
    }

    /* A branch must land exactly on an instruction boundary, otherwise our
     * decode disagrees with what the emitter intended and we must not touch
     * the function. */
    for (i = 0; i < n; i++) {
        int t, ok = 0;
        if (!ins[i].is_branch)
            continue;
        t = ins[i].target_old;
        if (t == size)
            ok = 1;                       /* branch to the very end */
        else if (t >= 0 && t < size && tidx[t] >= 0)
            ok = 1;
        if (!ok) {
            tcc_free(tidx); tcc_free(ins);
            return tp_relax_bail("branch-target");
        }
    }

    /* 2a. Give the placeholders gfunc_epilog could not fit (wide_log) their
     * real operand and the width it needs.  This must come before the
     * scheduler, which would otherwise take them for accesses to slot 0. */
    for (i = 0; i < wide_log_count; i++) {
        int o = wide_log[i].offset - base;
        int k = (o >= 0 && o < size) ? tidx[o] : -1;
        if (k < 0 || ins[k].pinned || ins[k].opcode != wide_log[i].opcode) {
            tcc_free(tidx); tcc_free(ins);
            return tp_relax_bail("wide");
        }
        ins[k].operand = wide_log[i].operand;
        ins[k].new_len = tp_encode_len(ins[k].opcode, ins[k].operand);
        grown = 1;
    }

    /* 2a'. Keep the addresses of busy globals in frame words of their own.
     * This renumbers the frame, so it comes before the passes that reason
     * about frame words. */
    if (tp_gaddr_cache(&ins, &n, tidx, size))
        grown = 1;

    /* 2b. Evaluation-stack scheduling: delete the workspace round trips the
     * always-STL register model leaves behind. Deleted instructions become
     * zero-length no-ops, which the shrink pass below already knows how to
     * drop; a rewritten one keeps its slot and is re-encoded from its new
     * opcode/operand. */
    tp_sched_function(ins, n, size, tpg.n);

    /* 2b'. Renumber the frame by use.  Moving a cold unit up can widen an
     * access, which the growth pass below then makes room for. */
    if (tp_slot_remap(ins, n, tpg.n))
        grown = 1;

    /* 2b''. Narrowed direct calls (near_call_log) stayed pinned through the
     * scheduler.  Now make each a branch to its callee: that sits before this
     * function and does not move, so the call re-encodes like a J - shorter
     * as the code in front of it shrinks, longer if widening pushed it away.
     * Its pfix 0 front padding becomes deletable no-ops; the relocation,
     * on the first of them, then slides onto the call itself.  Only a call
     * still encoded with the logged callee's displacement is the logged one;
     * anything else there stays pinned, as re-targeting it would send it to
     * the wrong function. */
    for (i = 0; i < near_call_count; i++) {
        int o = near_call_log[i].offset - base;
        int k = (o >= 0 && o < size) ? tidx[o] : -1;
        int m, t = near_call_log[i].target - base;

        if (k < 0)
            continue;
        for (m = k; m < n && ins[m].opcode == -1; m++)
            ;
        if (m == n || ins[m].opcode != TP_ENC_CALL
            || ins[m].old_off + ins[m].old_len + ins[m].operand != t)
            continue;
        for (; k < m; k++)
            ins[k].pinned = 0;
        ins[m].pinned = 0;
        ins[m].is_branch = 1;
        ins[m].ext = 1;
        ins[m].target_old = t;
    }

    /* 2c. Widening pushes code apart, so a branch across a widened
     * instruction may no longer reach with the width it had. Grow those
     * first: sizes only ever increase here, so this settles, and the shrink
     * pass below then starts from widths that all fit. */
    for (pass = 0; grown && pass < 16; pass++) {
        int changed = 0;
        int off = 0;

        for (i = 0; i < n; i++) {
            ins[i].new_off = off;
            off += ins[i].new_len;
        }
        outlen = off;

        for (i = 0; i < n; i++) {
            int newt, need;
            if (!ins[i].is_branch || ins[i].pinned)
                continue;
            newt = tp_relax_newt(ins, i, tidx, size, outlen);
            need = tp_encode_len(ins[i].opcode,
                                 newt - (ins[i].new_off + ins[i].new_len));
            if (need > ins[i].new_len) {
                ins[i].new_len = need;
                changed = 1;
            }
        }
        if (!changed)
            break;
    }

    /* 3. Shrink to fixpoint. Sizes only ever decrease, so this terminates. */
    for (pass = 0; pass < 16; pass++) {
        int changed = 0;
        int off = 0;

        for (i = 0; i < n; i++) {
            ins[i].new_off = off;
            off += ins[i].new_len;
        }
        outlen = off;

        for (i = 0; i < n; i++) {
            int want;
            if (ins[i].pinned)
                continue;
            if (ins[i].is_branch) {
                /* Displacement is relative to the end of this instruction, so
                 * it depends on its own length. Solve by trying lengths from
                 * the current one downwards and keeping the first that fits. */
                int newt = tp_relax_newt(ins, i, tidx, size, outlen);
                want = ins[i].new_len;
                while (want > 1) {
                    int disp = newt - (ins[i].new_off + want - 1);
                    /* Accept the shorter slot whenever the displacement fits
                     * in it. Insisting on an exact match left branches one
                     * byte too wide, and - worse - the emit step below then
                     * found the encoder disagreeing with the reserved width
                     * and abandoned the whole function, padding and all. */
                    if (tp_encode_len(ins[i].opcode, disp) > want - 1)
                        break;
                    want--;
                }
            } else if (ins[i].opcode == -1) {
                want = 0;               /* redundant PFIX 0 — delete it */
            } else {
                want = tp_encode_len(ins[i].opcode, ins[i].operand);
            }
            if (want < ins[i].new_len) {
                ins[i].new_len = want;
                changed = 1;
            }
        }
        if (!changed)
            break;
    }

    /* Recompute final offsets after the last shrink. */
    {
        int off = 0;
        for (i = 0; i < n; i++) {
            ins[i].new_off = off;
            off += ins[i].new_len;
        }
        outlen = off;
    }
    if (outlen == size && !grown) {
        tcc_free(tidx); tcc_free(ins);
        return tp_relax_bail("nogain");
    }

    /* 4. Emit the relaxed stream. */
    out = tcc_malloc(outlen > size ? outlen : size);
    map = tcc_malloc(sizeof(int) * (size + 1));
    for (i = 0; i <= size; i++)
        map[i] = -1;

    for (i = 0; i < n; i++) {
        uint8_t buf[16];
        int len;
        if (ins[i].synth && ins[i].pinned) {
            /* tp_gaddr_cache's entry load: a fresh LDC_ABS placeholder */
            memset(out + ins[i].new_off, (TP_ENC_PFIX << 4), 7);
            out[ins[i].new_off + 7] = TP_ENC_LDC << 4;
            continue;
        }
        if (!ins[i].synth)
            map[ins[i].old_off] = ins[i].new_off;
        if (ins[i].pinned) {
            int k;
            /* A pinned instruction keeps its exact width, so every byte inside
             * it has a well-defined new position. Relocations do not always
             * point at the first byte of the reserved placeholder, and leaving
             * an interior offset unmapped would strand the relocation on stale
             * code — which showed up as LDC_ABS_PATCH_FAIL at link time. */
            for (k = 0; k < ins[i].old_len; k++)
                map[ins[i].old_off + k] = ins[i].new_off + k;
            memcpy(out + ins[i].new_off, code + ins[i].old_off, ins[i].old_len);
            continue;
        }
        if (ins[i].opcode == -1) {
            continue;                   /* deleted no-op */
        }
        if (ins[i].is_branch) {
            int newt = tp_relax_newt(ins, i, tidx, size, outlen);
            len = tp_encode_instr(buf, ins[i].opcode,
                                  newt - (ins[i].new_off + ins[i].new_len));
        } else {
            len = tp_encode_instr(buf, ins[i].opcode, ins[i].operand);
        }
        if (len > ins[i].new_len) {      /* encoder wanted more — abandon */
            tcc_free(out); tcc_free(map); tcc_free(tidx); tcc_free(ins);
            return tp_relax_bail("encoder");
        }
        /* A branch displacement is measured from the end of the instruction,
         * so shortening the instruction lengthens the displacement: there are
         * targets for which no width is an exact fixed point (a two-byte slot
         * whose displacement then encodes in one). Left-pad with PFIX 0,
         * which contributes nothing to Oreg at an instruction boundary and so
         * works for NFIX-prefixed negative displacements too. */
        if (len < ins[i].new_len) {
            int q, pad = ins[i].new_len - len;
            for (q = 0; q < pad; q++)
                out[ins[i].new_off + q] = (TP_ENC_PFIX << 4);
            memcpy(out + ins[i].new_off + pad, buf, len);
            continue;
        }
        memcpy(out + ins[i].new_off, buf, len);
    }
    map[size] = outlen;

    /* Every relocation must land on a mapped byte, otherwise the rewrite would
     * strand it. Verify before committing anything. */
    if (sec->reloc) {
        ElfW_Rel *rel = (ElfW_Rel *)sec->reloc->data;
        int nrel = sec->reloc->data_offset / sizeof(*rel);
        for (i = func_start_nrel; i < nrel; i++) {
            int ro = (int)rel[i].r_offset - base;
            if (ro < 0 || ro > size)
                continue;
            if (map[ro] < 0) {
                tcc_free(out); tcc_free(map); tcc_free(tidx); tcc_free(ins);
                return tp_relax_bail("reloc-unmapped");
            }
        }
    }
    /* The same for labels, which step 6 moves. */
    {
        Sym *s;
        for (s = global_label_stack; s; s = s->prev) {
            int o = s->jnext - base;
            if (s->r == LABEL_DEFINED && o >= 0 && o <= size && map[o] < 0) {
                tcc_free(out); tcc_free(map); tcc_free(tidx); tcc_free(ins);
                return tp_relax_bail("label-unmapped");
            }
        }
    }

    if (base + outlen > sec->data_allocated) {
        section_realloc(sec, base + outlen);
        code = sec->data + base;
    }
    memcpy(code, out, outlen);
    saved = size - outlen;
    ind -= saved;
    sec->data_offset = ind;
    wide_log_count = 0;

    /* 5. Slide relocations that point into this function. */
    if (sec->reloc) {
        ElfW_Rel *rel = (ElfW_Rel *)sec->reloc->data;
        int nrel = sec->reloc->data_offset / sizeof(*rel);
        for (i = func_start_nrel; i < nrel; i++) {
            int ro = (int)rel[i].r_offset - base;
            if (ro < 0 || ro > size)
                continue;
            rel[i].r_offset = base + map[ro];
        }
        if (tpg.n)
            tp_gaddr_commit(sec, ins);
    }

    /* 6. Move the symbols that name code in this function.  A label whose
     * address is taken (&&l) is defined by label_pop() from s->jnext only
     * after gfunc_epilog returns, so remap jnext.  Anything already defined
     * by now - a __label__ local popped at the end of its block, an asm
     * label - is an ELF symbol at its old offset; such symbols were all
     * created since the prologue. */
    {
        Sym *s;
        ElfSym *es, *es_end;
        for (s = global_label_stack; s; s = s->prev) {
            int o = s->jnext - base;
            if (s->r == LABEL_DEFINED && o >= 0 && o <= size)
                s->jnext = base + map[o];
        }
        es = (ElfSym *)symtab_section->data + func_start_nsym;
        es_end = (ElfSym *)(symtab_section->data + symtab_section->data_offset);
        for (; es < es_end; es++) {
            int o = (int)es->st_value - base;
            if (es->st_shndx == sec->sh_num && o >= 0 && o <= size
                && map[o] >= 0)
                es->st_value = base + map[o];
        }
    }

    tcc_free(out);
    tcc_free(map);
    tcc_free(tidx);
    tcc_free(ins);
    return saved;
}

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
        const char *e = getenv("TCC_FRAME_POLICY");
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
    tp_stl_track_clear();
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
            relax_off = getenv("TP_NO_RELAX") ? 1 : 0;
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

static void tp_call_words_reserve(struct tp_call_words *cw, int extra_words)
{
    int need, new_cap;

    need = cw->count + extra_words;
    if (need <= cw->cap)
        return;

    new_cap = cw->cap ? cw->cap : 16;
    while (new_cap < need) {
        if (new_cap > (1 << 20))
            tcc_error("too many call argument words");
        new_cap <<= 1;
    }
    cw->words = tcc_realloc(cw->words, new_cap * sizeof(*cw->words));
    cw->cap = new_cap;
}

/* A volatile member of an aggregate is an observable source access too. */
static int tp_call_type_volatile(CType *type)
{
    Sym *field;
    if (type->t & (VT_VOLATILE | VT_VLA))
        return 1;
    if (type->t & VT_ARRAY)
        return tp_call_type_volatile(pointed_type(type));
    if ((type->t & VT_BTYPE) == VT_STRUCT)
        for (field = type->ref->next; field; field = field->next)
            if (tp_call_type_volatile(&field->type))
                return 1;
    return 0;
}

static void tp_expand_struct_call_words(struct tp_call_words *cw, SValue *arg, int arg_index, int callarg_debug)
{
    int align, size, words, w;

    size = type_size(&arg->type, &align);
    words = (size + 3) >> 2;

    /*
     * GCC's ABI gives an empty struct no argument words.  Linux uses this
     * deliberately for feature-dependent token parameters (for example,
     * kmalloc_token_t when partitioned kmalloc caches are disabled).  The
     * callee layout already consumes zero words for such a parameter, so
     * manufacturing one here shifts every following argument and can turn
     * kzalloc(size, flags) into kzalloc(token, flags).
     */
    if (size == 0) {
        if (callarg_debug) {
            fprintf(stderr,
                    "[CALLARG_EMPTY_STRUCT] func=%s arg_index=%d skipped\n",
                    funcname ? funcname : "<unknown>", arg_index);
        }
        return;
    }

    if (arg->r & VT_LVAL) {
        /* Reserve complete ABI words, including the padded final word.
         * Rounding only (loc - size) can leave its final word overlapping
         * a narrow local when loc is not word-aligned. */
        int spill_base = loc - words * 4;
        SValue src_addr = *arg;
        SValue dst_addr;

        spill_base &= ~3;
        loc = spill_base;
        src_addr.r &= ~VT_LVAL;

        dst_addr.type = int_type;
        dst_addr.r = VT_LOCAL;
        dst_addr.sym = NULL;
        if (tp_call_type_volatile(&arg->type)) {
            /* Preserve the original per-word source accesses for volatile
             * objects, including aggregates with volatile subobjects. */
            for (w = 0; w < words; w++) {
                int off = w * 4;
                int n = size - off < 4 ? size - off : 4;
                Tp_LDC(0);
                emit_local_op(TP_OP_STL, spill_base + off);
                abcreg_stack_depth = 0;
                dst_addr.c.i = spill_base + off;
                tp_push_sv(&src_addr);
                if (off)
                    Tp_ADC(off);
                tp_push_sv(&dst_addr);
                Tp_LDC(n);
                Tp_OPR(TP_OPR_MOVE);
                abcreg_pop(3);
                abcreg_stack_depth = 0;
            }
        } else {
            /* Only the last partial ABI word needs zero padding. */
            if (size & 3) {
                Tp_LDC(0);
                emit_local_op(TP_OP_STL, spill_base + (words - 1) * 4);
                abcreg_stack_depth = 0;
            }
            dst_addr.c.i = spill_base;
            tp_push_sv(&src_addr);
            tp_push_sv(&dst_addr);
            Tp_LDC(size);
            Tp_OPR(TP_OPR_MOVE);
            abcreg_pop(3);
            abcreg_stack_depth = 0;
        }
    } else {
        /* Keep current fallback semantics for struct rvalues during refactor. */
        words = 1;
    }

    for (w = 0; w < words; w++) {
        tp_call_words_reserve(cw, 1);
        cw->words[cw->count] = *arg;
        cw->words[cw->count].type.t =
            (cw->words[cw->count].type.t & ~(VT_BTYPE | VT_UNSIGNED | VT_VOLATILE)) | VT_INT;

        if (cw->words[cw->count].r & VT_LVAL) {
            cw->words[cw->count].r = VT_LOCAL | VT_LVAL;
            cw->words[cw->count].c.i = loc + (w * 4);
        }

        if (callarg_debug) {
            fprintf(stderr,
                    "[CALLARG_WORD] func=%s arg_index=%d word=%d abi_word=%d r=0x%x v=0x%x bt=0x%x c.i=%ld lval=%d\n",
                    funcname ? funcname : "<unknown>", arg_index, w, cw->count,
                    cw->words[cw->count].r,
                    cw->words[cw->count].r & VT_VALMASK,
                    cw->words[cw->count].type.t & VT_BTYPE,
                    (long)cw->words[cw->count].c.i,
                    (cw->words[cw->count].r & VT_LVAL) != 0);
        }
        cw->count++;
    }
}

static void tp_expand_scalar_call_words(struct tp_call_words *cw, SValue *arg, int arg_index, int callarg_debug)
{
    int bt, size;

    bt = arg->type.t & VT_BTYPE;
    size = get_primitive_type_size(bt);
    if (size == 8) {
        SValue lo = *arg;
        SValue hi = *arg;
        int v = arg->r & VT_VALMASK;

        lo.type.t = (lo.type.t & ~(VT_BTYPE | VT_UNSIGNED)) | VT_INT;
        hi.type.t = (hi.type.t & ~(VT_BTYPE | VT_UNSIGNED)) | VT_INT;

        if ((arg->r & VT_LVAL) &&
            ((v < VT_CONST && v != VT_LOCAL) || v == VT_LLOCAL)) {
            int spill_lo, spill_hi;
            /* STL addresses whole workspace words, while later VT_LOCAL
             * loads honor byte offsets.  Narrow locals can leave loc off a
             * word boundary, so align the pair before emitting either. */
            loc = (loc - 8) & -4;
            spill_lo = loc + 4;
            spill_hi = loc;
            if (v == VT_LLOCAL)
                emit_local_op(TP_OP_LDL, arg->c.i);
            else
                Tp_LDL(v);
            if (tp_t414()) {
                /* no dup: load the pointer again for the high word */
                Tp_LDNL(0);
                emit_local_op(TP_OP_STL, spill_lo);
                if (v == VT_LLOCAL)
                    emit_local_op(TP_OP_LDL, arg->c.i);
                else
                    Tp_LDL(v);
            } else {
                Tp_DUP();
                Tp_LDNL(0);
                emit_local_op(TP_OP_STL, spill_lo);
            }
            Tp_LDNL(1);
            emit_local_op(TP_OP_STL, spill_hi);
            lo.r = VT_LOCAL | VT_LVAL;
            lo.c.i = spill_lo;
            hi.r = VT_LOCAL | VT_LVAL;
            hi.c.i = spill_hi;
        } else if (arg->r & VT_LVAL) {
            hi.c.i += 4;
        } else if (v == VT_CONST && !(arg->r & VT_SYM)) {
            uint64_t bits;
            if (bt == VT_DOUBLE || bt == VT_LDOUBLE) {
                union { double d; uint64_t u; } u;
                u.d = arg->c.d;
                bits = u.u;
            } else {
                bits = arg->c.i;
            }
            lo.r = VT_CONST;
            lo.c.i = (uint32_t)bits;
            lo.sym = NULL;
            hi.r = VT_CONST;
            hi.c.i = (uint32_t)(bits >> 32);
            hi.sym = NULL;
        } else if (v == VT_CONST && (arg->r & VT_SYM)) {
            lo.r |= VT_LVAL;
            hi.r |= VT_LVAL;
            hi.c.i += 4;
        } else if (v < VT_CONST) {
            /* The high word is in r2, as in load(): after lbuild() the pair
             * can be any two registers (e.g. r=11, r2=9).  Assuming v+1 read
             * a slot nothing had written - (off + bs) >> bits passed garbage
             * as __ashrdi3's high word.  v+1 stays the fallback when r2 names
             * no register. */
            int v2 = arg->r2 & VT_VALMASK;
            hi.r = (hi.r & ~VT_VALMASK) | (v2 < VT_CONST ? v2 : v + 1);
        }

        tp_call_words_reserve(cw, 2);
        cw->words[cw->count++] = lo;
        cw->words[cw->count++] = hi;

        if (callarg_debug) {
            fprintf(stderr,
                    "[CALLARG_WORD64] func=%s arg_index=%d lo{r=0x%x v=0x%x c.i=%ld} hi{r=0x%x v=0x%x c.i=%ld}\n",
                    funcname ? funcname : "<unknown>", arg_index,
                    lo.r, lo.r & VT_VALMASK, (long)lo.c.i,
                    hi.r, hi.r & VT_VALMASK, (long)hi.c.i);
        }
    } else {
        tp_call_words_reserve(cw, 1);
        cw->words[cw->count] = *arg;
        if (callarg_debug) {
            fprintf(stderr,
                    "[CALLARG_WORD] func=%s arg_index=%d word=0 abi_word=%d r=0x%x v=0x%x bt=0x%x c.i=%ld lval=%d\n",
                    funcname ? funcname : "<unknown>", arg_index, cw->count,
                    cw->words[cw->count].r,
                    cw->words[cw->count].r & VT_VALMASK,
                    cw->words[cw->count].type.t & VT_BTYPE,
                    (long)cw->words[cw->count].c.i,
                    (cw->words[cw->count].r & VT_LVAL) != 0);
        }
        cw->count++;
    }
}

static void tp_expand_call_words(struct tp_call_words *cw, int nb_args, int callarg_debug)
{
    int i;

    for (i = 0; i < nb_args; i++) {
        SValue *arg = &vtop[-(nb_args - 1 - i)];
        int bt = arg->type.t & VT_BTYPE;

        if (bt == VT_STRUCT)
            tp_expand_struct_call_words(cw, arg, i, callarg_debug);
        else
            tp_expand_scalar_call_words(cw, arg, i, callarg_debug);
    }
}

/* Does tp_push_sv() materialise this value as a *pure push* - one that reads
 * nothing already on the evaluation stack, writes no memory and touches no
 * backend scratch slot?
 *
 * Only such values can be pushed straight into A/B/C in reverse ABI order
 * (see tp_stage_call_words below).  This is a deliberate whitelist: anything
 * not named here - narrow loads, which rebuild a halfword through
 * FRAME_SLOT_SCRATCH, and the VT_CMP / VT_JMP forms, which are not values at
 * all yet - falls back to the spill path.  Being absent is always safe.
 *
 * Word-size lvalues qualify even though they take two instructions
 * (`ldl p; ldnl 0`): the second consumes only what the first pushed, so the
 * pair is still net +1 and reads nothing underneath. */
static int tp_push_sv_is_pure(const SValue *sv)
{
    int fr = sv->r;
    int v  = fr & VT_VALMASK;
    int bt = sv->type.t & VT_BTYPE;
    int size = get_primitive_type_size(bt);

    if (bt == VT_STRUCT)
        return 0;
    if (fr & VT_LVAL) {
        /* Narrow and unaligned word loads need extra stack slots and
           temporaries. A wider value pushes its low word here. */
        if (size < 4 || tp_word_unaligned(sv))
            return 0;
        return v == VT_CONST || v == VT_LOCAL || v == VT_LLOCAL ||
               (v >= 0 && v < VT_CONST);
    }
    return v == VT_CONST || v == VT_LOCAL || v == VT_LLOCAL ||
           (v >= 0 && v < VT_CONST);
}

/* Whether materialising `sv` reads or writes a workspace slot in [lo, hi).
 * A pseudo-register r is slot r; an impure load may go through the scratch
 * slots [TP_SCRATCH_BASE, FRAME_SLOT_MIN_LOCALS) (halfword loads use
 * SCRATCH/+1 or DBL_TMP/+1). */
static int tp_push_sv_touches(const SValue *sv, int lo, int hi)
{
    int v = sv->r & VT_VALMASK;

    if (v >= 0 && v < VT_CONST && v >= lo && v < hi)
        return 1;
    return !tp_push_sv_is_pure(sv) &&
           lo < FRAME_SLOT_MIN_LOCALS && hi > TP_SCRATCH_BASE;
}

/* Push one call word while others already sit in workspace slots: the spills
 * and parked words [spill_lo, spill_hi), the stacked words stored so far
 * [area_lo, area_hi) and an indirect target in fn_slot (-1: none).  The
 * staging rules below - fresh spill slots, parking, late_impure - exist so
 * that the push touches none of them; emit_instr checks it, so a push that
 * breaks them (a new path through the scratch slots, say) stops the compile
 * instead of passing a clobbered argument, as c-testsuite 09003 once did. */
static void tp_stage_push(SValue *sv, int spill_lo, int spill_hi,
                          int area_lo, int area_hi, int fn_slot)
{
    tp_staged_lo[0] = spill_lo;
    tp_staged_hi[0] = spill_hi;
    tp_staged_lo[1] = area_lo;
    tp_staged_hi[1] = area_hi;
    tp_staged_lo[2] = fn_slot;
    tp_staged_hi[2] = fn_slot < 0 ? fn_slot : fn_slot + 1;
    tp_staging_push = 1;
    tp_push_sv(sv);
    tp_staging_push = 0;
}

/* Materialise the call words.  Returns the slot holding an indirect target
 * (-1 for a direct call).  *abc_slot is -1 when the register words are left
 * in A/B/C, else the first of the three slots holding them. */
static int tp_stage_call_words(struct tp_call_words *cw, int nb_args,
                               int callarg_debug, int *abc_slot)
{
    int i, arg_index;
    int reg_words = cw->count > 3 ? 3 : cw->count;
    int stack_words = cw->count > 3 ? (cw->count - 3) : 0;
    int spill_base = stack_words;
    int call_setup_words;
    int indirect_fn_slot = -1;
    SValue *fn_sv;
    int is_direct;
    int *parked = NULL;
    int park_base, nparked = 0, park_fn = 0, late_impure = 0;

    *abc_slot = -1;

    /* Fast path: the first three ABI words go to A/B/C, and the hardware
     * evaluation stack is exactly three deep, so when every one of them is a
     * pure push they can simply be pushed in reverse ABI order and left where
     * they land.  No spill, no reload.
     *
     * The slow path below exists because evaluating argument n generally needs
     * the whole evaluation stack and would destroy arguments 0..n-1, so each
     * one is written out to a workspace word (`stl s`) and read back
     * (`ldl s`) once they are all in place.  That is two extra instructions
     * and one extra stage-5 write per argument - and over a kernel corpus most
     * arguments are a local, a constant or a value already sitting in a
     * workspace slot, none of which need the stack at all.
     *
     * Reordering the pushes is sound precisely because they are pure: no push
     * can observe or disturb another, so left-to-right and right-to-left
     * produce the same three values.  (The arguments were fully evaluated by
     * the front end long before this point; all that happens here is
     * materialisation.)
     *
     * Restricted to calls with no stacked arguments: with stack_words > 0 the
     * outgoing area is written before the register words are materialised, and
     * keeping that order avoids reasoning about whether a stacked argument's
     * store can alias a later argument's load. */
    if (stack_words == 0 && reg_words > 0 && !tp_no_callopt()) {
        int all_pure = 1;
        for (i = 0; i < reg_words; i++) {
            if (!tp_push_sv_is_pure(&cw->words[i])) {
                all_pure = 0;
                break;
            }
        }
        if (all_pure) {
            SValue *fn_sv = &vtop[-nb_args];
            int is_direct = (fn_sv->r & VT_SYM) && !(fn_sv->r & VT_LVAL);

            /* An indirect target has to be parked before the arguments go up,
             * because materialising it needs the stack they would occupy. */
            if (!is_direct) {
                indirect_fn_slot = FRAME_SLOT_DBL_TMP;
                tp_push_sv(fn_sv);
                Tp_STL(indirect_fn_slot);
            }
            /* The DUP shortcut in tp_push_sv() compares against whatever was
             * last left in AReg, which here is either nothing or the indirect
             * target - neither of which is an argument. */
            tp_areg_invalidate();
            for (i = reg_words - 1; i >= 0; i--) {
                if (callarg_debug) {
                    fprintf(stderr,
                            "[CALLARG_DIRECT] func=%s abi_word=%d r=0x%x bt=0x%x\n",
                            funcname ? funcname : "<unknown>", i,
                            cw->words[i].r, cw->words[i].type.t & VT_BTYPE);
                }
                tp_stage_push(&cw->words[i], 0, 0, 0, 0, indirect_fn_slot);
            }
            return indirect_fn_slot;
        }
    }

    fn_sv = &vtop[-nb_args];
    is_direct = (fn_sv->r & VT_SYM) && !(fn_sv->r & VT_LVAL);

    if (stack_words > 0 && spill_base < func_max_slot_used + 1)
        spill_base = func_max_slot_used + 1;

    /* Stacked word a is stored to Wptr[a-3], top down, so no word pushed
     * after it may touch Wptr[a-2 .. stack_words-1]: a pseudo-register
     * holding a later word's value or address, or the scratch slots an
     * impure load goes through, can lie inside the staging area once it
     * is large enough.  Such words are parked above the spills before the
     * first store, and so is an indirect target that would read the area,
     * or be written into FRAME_SLOT_DBL_TMP inside it or over a spill. */
    if (stack_words > 0) {
        parked = tcc_malloc(cw->count * sizeof(int));
        for (arg_index = 3; arg_index < cw->count; arg_index++) {
            parked[arg_index] = -1;
            if (tp_push_sv_touches(&cw->words[arg_index], arg_index - 2,
                                   stack_words))
                parked[arg_index] = nparked++;
        }
        park_fn = !is_direct &&
                  (tp_push_sv_touches(fn_sv, 0, stack_words) ||
                   FRAME_SLOT_DBL_TMP < stack_words ||
                   (FRAME_SLOT_DBL_TMP >= spill_base &&
                    FRAME_SLOT_DBL_TMP < spill_base + reg_words));
        if (park_fn)
            nparked++;

        /* Spills and parked words must also survive every push after the
         * first spill, and an impure one may use any scratch slot.
         * spill_base came from func_max_slot_used *so far*, which says
         * nothing about the scratch a push still to come will use: when a
         * two-sb 16-bit store (slot 15 only) was the function's one earlier
         * scratch use, c-testsuite 09003's printf spills landed on
         * FRAME_SLOT_DBL_TMP and the -mt800 halfword loads of the next
         * arguments overwrote them.  Keep them out of the scratch region. */
        for (i = 1; i < cw->count; i++)
            if (!tp_push_sv_is_pure(&cw->words[i]))
                late_impure = 1;
        if (!is_direct && !tp_push_sv_is_pure(fn_sv))
            late_impure = 1;
        if (late_impure && spill_base < FRAME_SLOT_MIN_LOCALS &&
            spill_base + reg_words + nparked > TP_SCRATCH_BASE)
            spill_base = FRAME_SLOT_MIN_LOCALS;
    }
    park_base = spill_base + reg_words;

    call_setup_words = park_base + nparked;
    if (call_setup_words < stack_words)
        call_setup_words = stack_words;
    if (call_setup_words > func_max_outgoing_words)
        func_max_outgoing_words = call_setup_words;
    if (stack_words > func_max_stack_words)
        func_max_stack_words = stack_words;

    for (i = 0; i < reg_words; i++) {
        int spill_slot = spill_base + i;
        if (callarg_debug) {
            fprintf(stderr,
                    "[CALLARG_REG_MAT] func=%s abi_word=%d spill=%d r=0x%x v=0x%x bt=0x%x c.i=%ld lval=%d\n",
                    funcname ? funcname : "<unknown>", i, spill_slot,
                    cw->words[i].r, cw->words[i].r & VT_VALMASK,
                    cw->words[i].type.t & VT_BTYPE, (long)cw->words[i].c.i,
                    (cw->words[i].r & VT_LVAL) != 0);
        }
        tp_stage_push(&cw->words[i], spill_base, spill_slot, 0, 0, -1);
        Tp_STL(spill_slot);
    }

    if (parked) {
        int k = 0;

        for (arg_index = 3; arg_index < cw->count; arg_index++) {
            if (parked[arg_index] < 0)
                continue;
            parked[arg_index] = park_base + k;
            tp_stage_push(&cw->words[arg_index], spill_base, park_base + k,
                          0, 0, -1);
            Tp_STL(parked[arg_index]);
            k++;
        }
        if (park_fn) {
            indirect_fn_slot = park_base + k;
            tp_stage_push(fn_sv, spill_base, indirect_fn_slot, 0, 0, -1);
            Tp_STL(indirect_fn_slot);
        }
    }

    for (arg_index = cw->count - 1; arg_index >= 3; arg_index--) {
        int slot_off = (arg_index - 3) * 4;
        if (callarg_debug) {
            fprintf(stderr,
                    "[CALLARG_STACK] func=%s abi_word=%d slot_off=%d r=0x%x v=0x%x bt=0x%x c.i=%ld lval=%d\n",
                    funcname ? funcname : "<unknown>", arg_index, slot_off,
                    cw->words[arg_index].r, cw->words[arg_index].r & VT_VALMASK,
                    cw->words[arg_index].type.t & VT_BTYPE,
                    (long)cw->words[arg_index].c.i,
                    (cw->words[arg_index].r & VT_LVAL) != 0);
        }
        if (parked && parked[arg_index] >= 0)
            Tp_LDL(parked[arg_index]);
        else
            tp_stage_push(&cw->words[arg_index], spill_base,
                          park_base + nparked, arg_index - 2, stack_words, -1);
        Tp_STL(slot_off / 4);
    }
    tcc_free(parked);

    if (!is_direct && !park_fn) {
        indirect_fn_slot = FRAME_SLOT_DBL_TMP;
        tp_stage_push(fn_sv, spill_base, park_base + nparked, 0, stack_words,
                      -1);
        Tp_STL(indirect_fn_slot);
    }

    /* An indirect call stores the register words below Wptr from slots;
     * with stacked words, hand it the spills, which lie clear of the
     * staging area, rather than reloading them for it to park again at
     * FRAME_SLOT_SPILL_A..C, which the area can cover. */
    if (!is_direct && stack_words > 0) {
        *abc_slot = spill_base;
        goto ready;
    }

    for (i = reg_words - 1; i >= 0; i--) {
        int spill_slot = spill_base + i;
        if (callarg_debug) {
            fprintf(stderr,
                    "[CALLARG_REG_LDL] func=%s abi_word=%d spill=%d\n",
                    funcname ? funcname : "<unknown>", i, spill_slot);
        }
        Tp_LDL(spill_slot);
    }

ready:
    if (callarg_debug) {
        fprintf(stderr,
                "[CALLARG_READY] func=%s nb_args=%d call_words=%d reg_words=%d stack_words=%d vtop_r=0x%x\n",
                funcname ? funcname : "<unknown>", nb_args, cw->count,
                reg_words, stack_words, vtop ? vtop->r : -1);
        fflush(stderr);
    }

    return indirect_fn_slot;
}

/* Bytes reserved for a direct call's prefixes and opcode. */
#define TP_CALL_SLOT 6

/* Width of the slot for a direct call to `sym`, storing the callee's section
 * offset in *target when it is narrower than TP_CALL_SLOT.
 *
 * A callee this object already holds earlier in the same section gets only
 * the width its displacement needs, and nothing afterwards can lengthen that
 * displacement: tp_relax_function() treats the call as a branch to the fixed
 * target (near_call_log), the linker keeps an input section's bytes in
 * order, and ICF never folds a function a narrowed call reaches
 * (tp_icf_fold).  Left out are inline functions, whose per-object outlines
 * are what ICF folds into one copy: a narrowed call would keep every outline
 * alive.  (An earlier attempt, before the ICF guard, narrowed those too and
 * the kernel link failed with 32 IPREL_PATCH_FAIL, e.g. current_thread_info.)
 * Also left out are weak definitions, which a strong one elsewhere replaces.
 * Forward and external callees keep TP_CALL_SLOT.
 *
 * Nothing is emitted under nocode_wanted (typeof, sizeof, dead code), so
 * nothing may be logged either: an entry at the frozen `ind` would describe
 * whatever is emitted there next - in the kernel, a full-width call to
 * another function, which the relaxer then re-targeted. */
static int tp_call_slot_width(Sym *sym, int *target)
{
    ElfSym *es;
    int w;

    if (nocode_wanted || !sym->c || (sym->type.t & VT_INLINE) || sym->a.weak)
        return TP_CALL_SLOT;
    es = elfsym(sym);
    if (!es || es->st_shndx != cur_text_section->sh_num
        || ELFW(ST_BIND)(es->st_info) == STB_WEAK
        || (int)es->st_value > ind)
        return TP_CALL_SLOT;
    for (w = 1; w < TP_CALL_SLOT; w++)
        if (tp_encode_len(TP_OP_CALL, (int)es->st_value - (ind + w)) <= w)
            break;
    *target = (int)es->st_value;
    return w;
}

static void tp_emit_call_target(int call_word_count, int indirect_fn_slot,
                                int abc_slot, int callarg_debug)
{
    if ((vtop->r & VT_SYM) && !(vtop->r & VT_LVAL)) {
        int i, target = 0;
        int w = tp_call_slot_width(vtop->sym, &target);
        greloc(cur_text_section, vtop->sym, ind, R_TRANSPUTER_IPREL32);
        if (tp_debug_check()) {
            fprintf(stderr, "[CALL_DEBUG] func=%s call_to=%s at ind=0x%x\n",
                    funcname ? funcname : "<unknown>",
                    get_tok_str(vtop->sym->v, NULL), ind);
        }
        if (tp_gen_sym_filter) {
            const char *name = get_tok_str(vtop->sym->v, NULL);
            if (name && strstr(name, tp_gen_sym_filter)) {
                int bt = vtop->type.t & VT_BTYPE;
                fprintf(stderr,
                        "GEN_RELOC: kind=call sym=%s type=%d bt=0x%x file=%s line=%d\n",
                        name, R_TRANSPUTER_IPREL32, bt,
                        file ? file->filename : "<unknown>",
                        file ? file->line_num : -1);
            }
        }
        /* Six bytes carry a 24-bit displacement, +/-16 MB, which reaches any
         * call within one image; a full 32-bit operand would take eight, and
         * calls are a quarter of all kernel code, so the two spare bytes were
         * 6% of it.  The linker re-encodes the slot and fails the link for a
         * target out of reach, except an unresolved one at address 0, which
         * it handles itself (tp_patch_iprel).  A callee already behind us in
         * this section gets a narrower slot, holding the real displacement
         * (front-padded with pfix 0 where the shortest encoding is shorter
         * still); see tp_call_slot_width. */
        if (w < TP_CALL_SLOT) {
            uint8_t enc[16];
            int len = tp_encode_instr(enc, TP_OP_CALL, target - (ind + w));
            near_call_add(ind, target);
            for (i = len; i < w; i++)
                g((TP_OP_PFIX << 4) | 0);
            for (i = 0; i < len; i++)
                g(enc[i]);
        } else {
            for (i = 0; i < TP_CALL_SLOT - 1; i++)
                g((TP_OP_PFIX << 4) | 0);
            g((TP_OP_CALL << 4) | 0);
        }
        if (callarg_debug) {
            fprintf(stderr, "[CALLARG_CALL_DIRECT] func=%s sym=%s ind=0x%x\n",
                    funcname ? funcname : "<unknown>",
                    vtop->sym ? get_tok_str(vtop->sym->v, NULL) : "<null>",
                    ind);
            fflush(stderr);
        }
    } else {
        int num_reg_args = call_word_count > 3 ? 3 : call_word_count;

        if (abc_slot < 0) {
            abc_slot = FRAME_SLOT_SPILL_A;
            if (num_reg_args >= 1)
                Tp_STL(FRAME_SLOT_SPILL_A);
            if (num_reg_args >= 2)
                Tp_STL(FRAME_SLOT_SPILL_B);
            if (num_reg_args >= 3)
                Tp_STL(FRAME_SLOT_SPILL_C);
        }

        if (num_reg_args >= 3) {
            Tp_LDL(abc_slot + 2);
            Tp_LDLP(0);
            Tp_STNL(-1);
        }
        if (num_reg_args >= 2) {
            Tp_LDL(abc_slot + 1);
            Tp_LDLP(0);
            Tp_STNL(-2);
        }
        if (num_reg_args >= 1) {
            Tp_LDL(abc_slot);
            Tp_LDLP(0);
            Tp_STNL(-3);
        }

        emit_instr(TP_OP_AJW, -4);

        {
            uint8_t enc_buf[8];
            int fn_slot_shifted = indirect_fn_slot + 4;
            int ldl_len = tp_encode_instr(enc_buf, TP_OP_LDL, fn_slot_shifted);
            int skip = 1 + ldl_len + 1;
            emit_instr(TP_OP_LDC, skip);
            Tp_OPR(TP_OPR_LDPI);
            Tp_STL(0);
        }

        Tp_LDL(indirect_fn_slot + 4);
        Tp_OPR(TP_OPR_GCALL);

        if (callarg_debug) {
            fprintf(stderr, "[CALLARG_CALL_INDIRECT] func=%s fn_slot=%d ind=0x%x\n",
                    funcname ? funcname : "<unknown>", indirect_fn_slot, ind);
            fflush(stderr);
        }
    }
}

ST_FUNC void gfunc_call(int nb_args) {
    /* Function call - TCWG calling convention:
     * - First 3 args already in Areg, Breg, Creg
     * - Additional args on workspace (not implemented yet)
     * - CALL instruction:
     *   1. Stores Iptr, Areg, Breg, Creg at new Wptr
     *   2. Adjusts Wptr down by 4 words
     *   3. Jumps to target address (in Areg before CALL)
     * - Function address should be in vtop (TOS)
     *
     * vstack management:
     * - TCC's frontend pushes all arguments including any hidden struct return pointer
     * - nb_args includes the hidden pointer if present
     * - We pop ALL arguments (nb_args entries)
     * - We pop the function address (1 entry)
     * - TCC's frontend will push the return value after gfunc_call returns
     *   (see tccgen.c lines 6275-6289: vsetc() after gfunc_call)
     */

    int i;
    int callarg_debug = tp_callarg_debug;
    struct tp_call_words call_words;
    int indirect_fn_slot, abc_slot;
    /* The callee's return type, read before the argument pops move vtop.
     * VT_INT is the conservative fallback: it captures AReg only, which is
     * what R_RET() names for every non-float, non-64-bit type. */
    int ret_type_t = VT_INT;
    /* An old-style callee's declared type says nothing about what the caller
     * reads back: gen_opl() and the conversion helpers call __divdi3 & co.
     * through func_old_type ("int f()") and then read REG_IRET/REG_IRE2 or
     * the float slots themselves.  Capture every result word for those. */
    int capture_all = 1;

    {
        SValue *fsv = vtop - nb_args;
        if (fsv >= vstack && (fsv->type.t & VT_BTYPE) == VT_FUNC &&
            fsv->type.ref) {
            ret_type_t = fsv->type.ref->type.t;
            capture_all = fsv->type.ref->f.func_type == FUNC_OLD;
        }
    }

    call_words.words = NULL;
    call_words.count = 0;
    call_words.cap = 0;

    if (callarg_debug) {
        fprintf(stderr, "[CALLARG_ENTER] func=%s nb_args=%d vtop_r=0x%x\n",
                funcname ? funcname : "<unknown>", nb_args, vtop ? vtop->r : -1);
        fflush(stderr);
    }
    if (tp_try_inline_memcpy(nb_args)) {
        if (callarg_debug) {
            fprintf(stderr, "[CALLARG_MEMCPY_INLINE] func=%s ind=0x%x nb_args=%d\n",
                    funcname ? funcname : "<unknown>", ind, nb_args);
            fflush(stderr);
        }
        return;
    }
    if (tp_try_inline_local_zero_memset(nb_args)) {
        if (callarg_debug) {
            fprintf(stderr, "[CALLARG_MEMSET_INLINE] func=%s ind=0x%x nb_args=%d\n",
                    funcname ? funcname : "<unknown>", ind, nb_args);
            fflush(stderr);
        }
        return;
    }

    /* Save any register-held values to the stack before the call.
     * This matches x86 (save_regs(nb_args+1)) and ARM behavior.
     * Without this, values cached in registers would be clobbered by CALL. */
    save_regs(nb_args + 1);

    tp_expand_call_words(&call_words, nb_args, callarg_debug);
    indirect_fn_slot = tp_stage_call_words(&call_words, nb_args, callarg_debug,
                                           &abc_slot);

    /* Pop all arguments from vstack */
    for (i = 0; i < nb_args; i++) {
        vtop--;
    }

    /* Now vtop points to the function address.
     * For direct calls with a symbol, emit a relocation so the linker can patch the offset.
     * For indirect calls (function pointers), we would need GCALL instead.
     */
    tp_emit_call_target(call_words.count, indirect_fn_slot, abc_slot,
                        callarg_debug);

    /* Pop function address from vstack */
    vtop--;

    /* Capture the ABI result words from A/B into workspace slots.
     *
     * Only the slots the front end will actually read are worth writing.
     * tccgen labels the call's result with PUT_R_RET(), i.e. R_RET(t) and
     * R2_RET(t) of the callee's *return type*, so those two macros decide
     * exactly which of REG_IRET / REG_IRE2 / REG_FRET / REG_FRET+1 can ever
     * be read afterwards:
     *
     *   float            -> FRET
     *   double           -> FRET, FRET+1
     *   long long        -> IRET, IRE2
     *   anything else    -> IRET
     *
     * This used to store all four unconditionally - six instructions, four of
     * them writes, after every single call - and a write is the T9000's
     * scarcest pipeline resource: stage 5 issues one per cycle, so each one
     * closes an instruction group, and an STL burns a stage-2 address
     * calculation on top.  Over a kernel corpus that was ~10% of the whole
     * instruction stream spent copying words nothing reads.
     *
     * Anything left in BReg afterwards is dead: save_regs() above has already
     * spilled every vstack entry that is not an argument to this call, so no
     * live value names a register here, and the value the front end pushes
     * next names only the slots written below.
     *
     * The eval-stack scheduler models the depth after a call as 2 and it stays
     * correct: dropping a store leaves the corresponding word in place, which
     * is exactly the depth the model already assumes. */
    if (tp_no_callopt() || capture_all) {
        /* Old-style callee (see capture_all), or the bisection escape hatch:
         * the unconditional four-slot capture. */
        emit_instr(TP_OP_STL, REG_IRET);
        emit_instr(TP_OP_STL, REG_IRE2);
        Tp_LDL(REG_IRET);
        emit_instr(TP_OP_STL, REG_FRET);
        Tp_LDL(REG_IRE2);
        emit_instr(TP_OP_STL, REG_FRET + 1);
    } else {
        int bt = ret_type_t & VT_BTYPE;

        if (is_float(ret_type_t)) {
            emit_instr(TP_OP_STL, REG_FRET);
            if (bt == VT_DOUBLE || bt == VT_LDOUBLE)
                emit_instr(TP_OP_STL, REG_FRET + 1);
        } else {
            emit_instr(TP_OP_STL, REG_IRET);
            if (bt == VT_LLONG)
                emit_instr(TP_OP_STL, REG_IRE2);
        }
    }
    abcreg_stack_depth = 0;

    /* A call clobbers the hardware A/B/C evaluation stack.  The return-word
     * capture above reconstructs only the ABI result, not any value that was
     * cached before the call.  Leaving this tag live can make the next
     * tp_push_sv() emit DUP instead of reloading its local; Linux rwsem then
     * forwards a post-call workspace value as the semaphore pointer. */
    tp_areg_invalidate();

    if (callarg_debug) {
        fprintf(stderr, "[CALLARG_EXIT] func=%s ind=0x%x\n",
                funcname ? funcname : "<unknown>", ind);
        fflush(stderr);
    }

    tcc_free(call_words.words);
}

/* Leave a canonical double's bits in ABI A=low, B=high without a copy
 * through IRET/IRE2.  Decline before emitting anything if gv() still has
 * work to do (bounds/casts/unaligned accesses in particular). */
static int tp_push_double_return(SValue *sv, int bt)
{
    int fr, v;

    if ((bt != VT_DOUBLE && bt != VT_LDOUBLE) ||
        (sv->type.t & VT_BTYPE) != bt)
        return 0;
    fr = sv->r;
    v = fr & VT_VALMASK;
    if ((fr == VT_CONST || fr == (VT_CONST | VT_NONCONST)) && !sv->sym) {
        union { double d; uint64_t u; } bits;
        /* gv() would first put this already-converted literal in rodata,
         * then load and park its two words.  Return the identical bits
         * directly; do not evaluate it with the target FPU (not even zero,
         * whose sign must survive).  Symbol/flagged operands still use gv. */
        bits.d = sv->c.d;
        tp_areg_invalidate();
        abcreg_stack_depth = 0;
        Tp_LDC((uint32_t)(bits.u >> 32));
        Tp_LDC((uint32_t)bits.u);
        return 1;
    }
    if (fr >= TREG_FA && fr < NB_REGS &&
        sv->r2 >= TREG_FA && sv->r2 < NB_REGS && fr != sv->r2) {
        /* Use the actual high slot, including nonadjacent rvalue pairs. */
        tp_areg_invalidate();
        abcreg_stack_depth = 0;
        Tp_LDL(sv->r2);
        Tp_LDL(fr);
        return 1;
    }
    if (!(fr & VT_LVAL) ||
        (fr & ~(VT_VALMASK | VT_LVAL | VT_SYM)) ||
        (sv->type.t & (VT_VOLATILE | VT_BITFIELD)) ||
        tp_word_unaligned(sv) ||
        ((fr & VT_SYM) && v != VT_CONST) ||
        (v == VT_CONST && sv->sym && !(fr & VT_SYM)) ||
        (v != VT_LOCAL && v != VT_LLOCAL && v != VT_CONST && v >= NB_REGS) ||
        (v != VT_LOCAL && tp_t414()))
        return 0;
    if (v == VT_LOCAL) {
        Sym *s;
        /* gen_cast() strips qualifiers.  As for tp_push_simple(), require
         * the original unescaped local symbol before reversing its reads:
         * the scheduler can fold two LDLs followed by REV into swapped
         * LDLs, so emitting low/high/REV alone is not a volatile barrier. */
        for (s = local_stack; s; s = s->prev)
            if (s == sv->sym &&
                (s->r & (VT_VALMASK | VT_LVAL)) == (VT_LOCAL | VT_LVAL) &&
                s->c == sv->c.i && !(s->type.t & (VT_VOLATILE | VT_BITFIELD)) &&
                (s->type.t & VT_BTYPE) == bt && !s->a.addrtaken)
                break;
        if (!s)
            return 0;
    }

    tp_areg_invalidate();
    abcreg_stack_depth = 0;
    if (v == VT_LOCAL) {
        /* Proven nonvolatile storage needs no address or parked pair. */
        emit_local_op(TP_OP_LDL, sv->c.i + 4);
        abcreg_push(1);
        emit_local_op(TP_OP_LDL, sv->c.i);
        abcreg_push(1);
    } else {
        /* Share the canonical/spilled/symbol address handling with FP
         * operands.  Alignment was checked above, so no byte-copy scratch
         * is needed.  Read low then high, retaining both on the stack.
         * T414 has no DUP and retains load()'s existing staging path. */
        tp_emit_float_addr(sv);
        Tp_DUP();
        Tp_LDNL(0);
        Tp_REV();
        Tp_LDNL(1);
        Tp_REV();
    }
    return 1;
}

ST_FUNC void gfunc_return(CType *func_type) {
    /* Handle struct returns - structs may occupy multiple vstack entries */
    if ((func_type->t & VT_BTYPE) == VT_STRUCT) {
        CType type, ret_type;
        int ret_align, ret_nregs, regsize;

        /* Query how many registers are needed for this struct */
        ret_nregs = gfunc_sret(func_type, func_var, &ret_type,
                               &ret_align, &regsize);

        if (ret_nregs < 0) {
            /* Struct returned via hidden pointer (already handled) */
            /* vstore() already consumed vstack, just pop final entry */
        } else if (ret_nregs == 0) {
            /* Copy to implicit first pointer arg location.
             * vstore() calls gen_struct_copy() which now properly manages vstack. */
            type = *func_type;
            mk_pointer(&type);
            vset(&type, VT_LOCAL | VT_LVAL, func_vc);
            indir();
            vswap();
            vstore();
            /* vstore+gen_struct_copy consumed both source and dest, leaving dest_copy.
             * The final vtop-- at the end of this function will pop it. */
        } else {
            /* For Transputer, return values are typically in AReg */
            gv(RC_INT);
        }
    } else {
        /* Simple return: load into return register
         *
         * CRITICAL FIX (2026-01-17): gv(r) stores the value into
         * workspace slot r (slot 7 for int), but RET expects the return
         * value in AReg (hardware A register). We must explicitly load
         * from the workspace slot into AReg before returning.
         */
        int bt = func_type->t & VT_BTYPE;
        if (bt != VT_VOID && !tp_push_double_return(vtop, bt)) {
            /* Cleanups / prior calls may have left a stale AReg tag or
             * clobbered slot 7.  Force a full rematerialize into IRET and
             * then into hardware A so RET sees the true return value. */
            tp_areg_invalidate();
            gv(RC_IRET);  /* Load return value into REG_IRET (workspace slot 7) */

            /* Load from slot back to Hardware Stack */
            if (bt == VT_LLONG || bt == VT_DOUBLE || bt == VT_LDOUBLE) {
                /* 64-bit return: Needs 2 stack entries.
                   Order depends on calling convention (A=Low vs A=High).
                   Assuming A=Low, B=High:
                */
                Tp_LDL(REG_IRE2); /* Load High (Slot 8) -> A */
                Tp_LDL(REG_IRET); /* Load Low  (Slot 7) -> A, pushes High to B */
            } else {
                /* The caller takes the result from AReg, and this entry
                 * is popped below, so nothing reads slot 7 again: the
                 * `stl 7` gv() just parked it with cancels, and `return
                 * x;` is `ldl x` rather than `ldl x; stl 7; ldl 7`. */
                tp_push_reg_or_cancel_stl(REG_IRET, vtop);
            }
        }
    }

    /* Pop final vstack entry - do this once at the end like ARM64 */
    vtop--;
}

/* Push a vstack entry's value onto the hardware eval stack.
 * Unlike load(REG_IRET, ...), this ALWAYS generates code to push
 * the value, even if it's conceptually "already in REG_IRET". */
static void tp_push_sv(SValue *sv) {
    int fr = sv->r;
    int v = fr & VT_VALMASK;
    int bt = sv->type.t & VT_BTYPE;
    int is_unsigned = (sv->type.t & VT_UNSIGNED) != 0 || bt == VT_BF16 || bt == VT_F16
                      || bt == VT_FLOAT16;
    int size = get_primitive_type_size(bt);

    if (size > 4)
        size = 4;   /* one word: the low half of a two-word value */

    /* DUP optimization: if AReg already holds the value we want, DUP it
     * instead of reloading from workspace. Only for word-size local lvals
     * and rvalues in workspace slots (the common case). */
    if (tp_areg_tag != TP_AREG_TAG_NONE && !tp_t414()) {
        int want_tag = TP_AREG_TAG_NONE;
        if ((fr & VT_LVAL) && v == VT_LOCAL && size == 4) {
            want_tag = TP_AREG_TAG_LOCAL_BASE + (int)sv->c.i;
        } else if (!(fr & VT_LVAL) && v < VT_CONST && v >= 0) {
            want_tag = TP_AREG_TAG_REG_BASE + v;
        }
        if (want_tag != TP_AREG_TAG_NONE && want_tag == tp_areg_tag) {
            Tp_DUP();
            tp_areg_tag = want_tag;  /* restore tag — DUP doesn't change AReg */
            return;
        }
    }
    tp_areg_invalidate();

    if (fr & VT_LVAL) {
        /* Lvalue: load from memory */
        if (v == VT_LOCAL) {
            if (size == 1) {
                /* Byte load: use byte-addressed LDLP + LB.
                 * LDL truncates non-word-aligned offsets, so sbuf[1]
                 * at byte offset -3 would read from the same word as
                 * sbuf[0] at offset -4.  LDLP + ADC + LB is correct
                 * for any byte position within a word. */
                emit_local_addr(sv->c.i);
                abcreg_push(1);
                if (!is_unsigned && tp_have_t9000_feat(TP_T9K_LOAD8)) {
                    Tp_LBX();
                } else {
                    Tp_LB();                    /* zero-extends */
                    if (!is_unsigned)
                        tp_sign_extend(8, 1);
                }
            } else if (size == 2 && tp_t9000_half_ok((int)sv->c.i, sv->r)) {
                emit_local_addr(sv->c.i);
                abcreg_push(1);
                if (is_unsigned) Tp_LS(); else Tp_LSX();
            } else if (size == 2) {
                tp_load_local_half((int)sv->c.i, is_unsigned);
                abcreg_push(1);         /* as the byte path above counts */
            } else if (sv->c.i & 3) {
                /* a word of a packed local (tp_word_unaligned) */
                int t = tp_park_new(4);
                emit_local_addr(sv->c.i);
                tp_park_op(TP_OP_STL, -1, t);
                tp_bytes_load(-1, t, 0);
            } else {
                /* Word or larger: LDL is correct (word-aligned). */
                emit_local_op(TP_OP_LDL, sv->c.i);
                abcreg_push(1);
            }
        } else {
            int addr_in_areg = 0;

            if (v == VT_CONST) {
                if (fr & VT_SYM) {
                    tp_emit_sym_reloc(sv, NULL, 0);
                    abcreg_push(1);
                    if (sv->c.i) Tp_ADC(sv->c.i);
                } else {
                    Tp_LDC(sv->c.i);
                }
                addr_in_areg = 1;
            } else if (v == VT_LLOCAL) {
                emit_local_op(TP_OP_LDL, sv->c.i);
                abcreg_push(1);
                addr_in_areg = 1;
            } else if (v < VT_CONST) {
                /* Pointer in workspace slot */
                Tp_LDL(v);
                addr_in_areg = 1;
            }

            if (addr_in_areg) {
                if (size == 1) {
                    if (!is_unsigned && tp_have_t9000_feat(TP_T9K_LOAD8)) {
                        Tp_LBX();
                    } else {
                        Tp_LB();                    /* zero-extends */
                        if (!is_unsigned)
                            tp_sign_extend(8, 1);
                    }
                } else if (size == 2 && tp_t9000_half_ptr_ok(sv->r,
                               (v == VT_CONST) ? (long long)sv->c.i : 0)) {
                    if (is_unsigned) Tp_LS(); else Tp_LSX();
                } else if (size == 2) {
                    /* LDNL word-aligns its address — the emulator masks with
                     * MemWordMask — so LDNL 0 on a halfword sitting at byte
                     * offset 2 mod 4 silently reads the *containing word*, and
                     * the 0xFFFF mask below then yields the neighbouring
                     * field. `struct { u16 a, b; }` passed as p->b returned
                     * p->a, with no diagnostic. Rebuild from two byte loads,
                     * exactly as the VT_LOCAL path above already does for the
                     * same reason. */
                    /* NOT FRAME_SLOT_SCRATCH/+1: those alias
                     * FRAME_SLOT_SPILL_A/SPILL_B, which hold the spilled
                     * A/B registers while a call's arguments are being
                     * staged — and staging arguments is exactly when this
                     * path runs. Use the dedicated 64-bit temp pair. */
                    tp_load_half_bytes(FRAME_SLOT_DBL_TMP);

                    /* two zero-extended bytes: already 0..0xFFFF */
                    if (!is_unsigned)
                        tp_sign_extend(16, 1);
                } else if (fr & VT_UNALIGNED) {
                    /* a word of a packed member: from its bytes */
                    int t = tp_park_new(4);
                    tp_park_op(TP_OP_STL, -1, t);
                    tp_bytes_load(-1, t, 0);
                } else {
                    Tp_LDNL(0);
                }
            }
        }
    } else {
        /* Rvalue */
        if (v == VT_CONST) {
            if (fr & VT_SYM) {
                tp_emit_sym_reloc(sv, NULL, 0);
                abcreg_push(1);
                if (sv->c.i) Tp_ADC(sv->c.i);
            } else {
                tp_ldc_pooled(sv->c.i);
            }
        } else if (v == VT_LOCAL) {
            /* VT_LOCAL rvalue is an address of a local object. */
            emit_local_addr(sv->c.i);
            abcreg_push(1);
        } else if (v == VT_LLOCAL) {
            /* Spilled address value in a local slot. */
            emit_local_op(TP_OP_LDL, sv->c.i);
            abcreg_push(1);
        } else if (v < VT_CONST) {
            /* Value in workspace slot */
            Tp_LDL(v);
        }
    }

    /* Set DUP tag for trackable cases */
    if ((fr & VT_LVAL) && v == VT_LOCAL && size == 4) {
        tp_areg_tag = TP_AREG_TAG_LOCAL_BASE + (int)sv->c.i;
    } else if (!(fr & VT_LVAL) && v < VT_CONST && v >= 0) {
        tp_areg_tag = TP_AREG_TAG_REG_BASE + v;
    } else {
        tp_areg_tag = TP_AREG_TAG_NONE;
    }
}

static int gen_ptradd_wsub_gv2(void);

/* A pointer-arithmetic base must be a genuine runtime value. A literal base
 * (VT_CONST with no symbol) is the offsetof idiom `((T *)0)->m`, which appears
 * inside constant expressions — emitting an instruction sequence for it makes
 * the expression non-constant and the parser rejects it. Let those fall through
 * to the generic lowering, which constant-folds. */
static int tp_operand_is_simple(const SValue *sv);
static void tp_push_simple(const SValue *sv);

static int tp_ptradd_base_ok(const SValue *sv)
{
    /* A symbol address is a real runtime base and is fine; a bare literal is
     * the offsetof idiom and must be left to constant folding. */
    if ((sv->r & VT_VALMASK) == VT_CONST)
        return (sv->r & VT_SYM) != 0;
    return 1;
}

/* Emit the absolute address of a symbol: a relocation plus the 8-nibble
 * placeholder the linker rewrites, matching load()/gen_store_addr. */
static void tp_push_sym_addr(const SValue *sv)
{
    tp_emit_sym_reloc(sv, NULL, 0);
    abcreg_push(1);
    if (sv->c.i)
        Tp_ADC((int)sv->c.i);
}

/* A local array's address can be pushed after the index with LDLP, just
 * as a global array's address can be pushed with a symbol relocation. */
static int tp_ptradd_base_is_simple(const SValue *sv)
{
    if (tp_operand_is_simple(sv))
        return 1;
    if (sv->r & (VT_LVAL | VT_MUSTCAST | VT_MUSTBOUND | VT_BOUNDED))
        return 0;
    if ((sv->r & (VT_VALMASK | VT_SYM)) != VT_LOCAL &&
        (sv->r & (VT_VALMASK | VT_SYM)) != (VT_CONST | VT_SYM))
        return 0;
    return get_primitive_type_size(sv->type.t & VT_BTYPE) == 4;
}

static void tp_push_base(const SValue *sv)
{
    if ((sv->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_LOCAL)
        emit_local_addr(sv->c.i);
    else if ((sv->r & VT_VALMASK) == VT_CONST && (sv->r & VT_SYM))
        tp_push_sym_addr(sv);
    else
        tp_push_simple(sv);
}

/* Can this SValue be pushed onto the eval stack with a single instruction? */
static int tp_operand_is_simple(const SValue *sv)
{
    int v = sv->r & VT_VALMASK;

    if (sv->r & (VT_SYM | VT_MUSTCAST | VT_MUSTBOUND | VT_BOUNDED))
        return 0;
    if (get_primitive_type_size(sv->type.t & VT_BTYPE) != 4)
        return 0;
    if (sv->r & VT_LVAL)
        return v == VT_LOCAL && !(sv->c.i & 3);    /* not a packed member */
    return v == VT_CONST || (v >= TREG_W0 && v < NB_REGS);
}

/* A cast can erase the lvalue's volatile qualifier.  Require the original
 * active local symbol too, and reject address-taken locals: a volatile access
 * could otherwise have been made through a cast pointer to that object. */
static int tp_simple_nonvolatile_local(const SValue *sv)
{
    Sym *s;
    if (sv->type.t & VT_VOLATILE)
        return 0;
    for (s = local_stack; s; s = s->prev)
        if (s == sv->sym && (s->r & (VT_VALMASK | VT_LVAL)) == (VT_LOCAL | VT_LVAL) &&
            s->c == sv->c.i && !(s->type.t & (VT_VOLATILE | VT_BITFIELD)) &&
            (s->type.t & VT_BTYPE) == VT_INT && !s->a.addrtaken)
            return 1;
    return 0;
}

static void tp_push_simple(const SValue *sv)
{
    int v = sv->r & VT_VALMASK;

    if (sv->r & VT_LVAL) {
        emit_local_op(TP_OP_LDL, sv->c.i);
        if (!nocode_wanted && local_patches && tp_simple_nonvolatile_local(sv))
            local_patches->proven_nonvolatile_read = 1;
        abcreg_push(1);
    } else if (v == VT_CONST) {
        tp_ldc_pooled((int)sv->c.i);
    } else {
        Tp_LDL(v);
    }
}

/* Reserve the result before pushing either operand: get_reg can emit a
 * spill. Reuse a consumed operand's slot only if no other value needs it. */
static int tp_ptradd_result_reg(void)
{
    SValue *sv;
    for (sv = vtop - 1; sv <= vtop; sv++) {
        int r = sv->r & VT_VALMASK;
        if (!(sv->r & VT_LVAL) && r >= TREG_W0 && r < NB_REGS &&
            !tp_reg_live_on_vstack(r, sv))
            return r;
    }
    return get_reg(RC_INT);
}

/* `base + constant` costs no code at all when the base is the address of a
 * local (a frame offset) or of a symbol (a link-time constant): the generic
 * lowering scales the index, and gen_opic folds it into the offset, so the
 * element is then one ldl/ldlp, or one ldnl off the symbol.  The indexed fast
 * paths would instead park the base in a register and add to it -
 * `float p[3]; p[0]` cost `ldlp 15; stl 9; ldc 0; stl 10; ldl 10; ldl 9;
 * wsub; stl 9` before the access.  The conditions are gen_opic's own. */
static int tp_ptradd_const_folds(void)
{
    int br = vtop[-1].r;

    if ((vtop[0].r & (VT_VALMASK | VT_LVAL | VT_SYM)) != VT_CONST)
        return 0;
    return (br & (VT_VALMASK | VT_LVAL)) == VT_LOCAL ||
           (br & (VT_VALMASK | VT_LVAL | VT_SYM)) == (VT_CONST | VT_SYM);
}

/* T800 fast path for pointer arithmetic on 4-byte (word) elements. */
static int gen_ptradd_wsub(void) {
    if (tp_ptradd_const_folds())
        return 0;
    /* Push index then base straight onto the eval stack, so `a[i]` becomes
     * LDL i; LDL a; WSUB; STL r. Routing through gv2 first spills both
     * operands to fresh slots and reloads them, doubling the instruction
     * count for the most common addressing form in the kernel and in
     * CoreMark's list and matrix loops. */
    if (!nocode_wanted && tp_ptradd_base_ok(&vtop[-1]) &&
        tp_ptradd_base_is_simple(&vtop[-1]) && tp_operand_is_simple(&vtop[0])) {
        SValue base = vtop[-1];
        SValue index = vtop[0];
        int iv = index.r & VT_VALMASK;
        int index_const = !(index.r & VT_LVAL) && iv == VT_CONST;
        int dest = tp_ptradd_result_reg();

        abcreg_stack_depth = 0;
        if (index_const) {
            /* Compiler writer's guide 5.4.2: the address of component e of a
             * word-addressed structure Wa is either `e; Wa; wsub` or, for a
             * literal subscript, simply `Wa; ldnlp e`. The second form drops
             * the LDC entirely.  Index 0 is just the base (no ldnlp 0). */
            tp_push_base(&base);
            if ((int)index.c.i != 0)
                emit_instr(TP_OP_LDNLP, (int)index.c.i);
        } else {
            /* Variable index: push index then base → `ldl i; ldl a; wsub`
             * so the T9000 grouper packs L2+A1 (textbook a[i] form). */
            tp_push_simple(&index);
            tp_push_base(&base);
            Tp_WSUB();
        }

        vtop--;
        vtop->r = dest;
        Tp_STL(dest);
        return 1;
    }
    return gen_ptradd_wsub_gv2();
}

static int gen_ptradd_wsub_gv2(void) {
    SValue sv_base, sv_index;
    int result_v;

    gv2(RC_INT, RC_INT);
    sv_base = vtop[-1];
    sv_index = vtop[0];

    abcreg_stack_depth = 0;
    tp_push_sv(&sv_index);  /* push index first so it lands in BReg */
    tp_push_sv(&sv_base);   /* AReg = base, BReg = index */

    Tp_WSUB();              /* result = base + index * 4 */
    vtop--;

    result_v = vtop->r & VT_VALMASK;
    if (result_v < VT_CONST) {
        Tp_STL(result_v);
    }
    return 1;
}

/* Pointer arithmetic on 1-byte elements.
 *
 * `bsub` computes AReg + BReg and is numerically identical to `sum`, so this
 * changes nothing about the result.  What it changes is *which pipeline stage
 * pays for it*: the T9000 grouper treats `bsub` as a stage-2 address
 * calculation (2 per cycle) and `sum` as a stage-4 ALU operation (1 per
 * cycle, the narrowest resource in the machine).  Measurement shows stage 2
 * has spare capacity - relaxing its limit from 2 to 4 changes nothing - so
 * moving byte-pointer arithmetic there is close to free, and it releases the
 * stage-4 slot for real work.
 *
 * Unlike WSUB there is no constant-index form: `ldnlp` scales by a word, so
 * it cannot express a byte displacement.  Constant indices therefore fall
 * through to the generic path, which folds them into a single `adc`.
 *
 * `bsub` is commutative, so unlike WSUB the operand order does not matter. */
static int gen_ptradd_bsub(void) {
    SValue sv_base, sv_index;
    int result_v;

    if (tp_ptradd_const_folds())
        return 0;
    if (!nocode_wanted && tp_ptradd_base_ok(&vtop[-1]) &&
        tp_ptradd_base_is_simple(&vtop[-1]) && tp_operand_is_simple(&vtop[0])) {
        SValue base = vtop[-1];
        SValue index = vtop[0];
        int iv = index.r & VT_VALMASK;
        int index_const = !(index.r & VT_LVAL) && iv == VT_CONST;
        int dest;

        if (index_const)
            return 0;           /* `adc n` is shorter; let the generic path run */

        dest = tp_ptradd_result_reg();
        abcreg_stack_depth = 0;
        tp_push_simple(&index);
        tp_push_base(&base);
        Tp_BSUB();

        vtop--;
        vtop->r = dest;
        Tp_STL(dest);
        return 1;
    }

    /* No gv2 fallback.  Unlike WSUB, BSUB buys nothing the generic path
     * cannot already do - `sum` is the same arithmetic - so the only reason
     * to emit it is the free stage-2 slot.  Forcing gv2 here to get it would
     * spill and reload both operands and lose the zero-STL local-to-local
     * lowering that gen_opi already applies, which measured as *more*
     * dynamic instructions than it saved.  Return 0 and let gen_op handle
     * it. */
    (void)sv_base; (void)sv_index; (void)result_v;
    return 0;
}

/* T800 fast path for pointer arithmetic on 8-byte elements.
 * Expects vstack: vtop[-1] = base pointer, vtop[0] = unscaled index.
 * TCWG wsubdb semantics: AReg = base, BReg = index → result = AReg + BReg * 8.
 * Returns 1 when emitted. */
static int gen_ptradd_wsubdb(void);

/* T9000 fast path for pointer arithmetic on 2-byte (16-bit) elements.
 * ssub: AReg := AReg + 2*BReg, i.e. the same operand order as WSUB.
 * Returns 0 without emitting anything when -mt9000 is off or when the
 * operands are not in the simple form, so the caller's generic
 * multiply-then-add lowering still runs. */
static int gen_ptradd_ssub(void) {
    if (!tp_have_t9000_feat(TP_T9K_SSUB))
        return 0;
    if (tp_ptradd_const_folds())
        return 0;
    if (!nocode_wanted && tp_ptradd_base_ok(&vtop[-1]) &&
        tp_ptradd_base_is_simple(&vtop[-1]) && tp_operand_is_simple(&vtop[0])) {
        SValue base = vtop[-1];
        SValue index = vtop[0];
        int iv = index.r & VT_VALMASK;
        int index_const = !(index.r & VT_LVAL) && iv == VT_CONST;
        int dest = tp_ptradd_result_reg();

        abcreg_stack_depth = 0;
        if (index_const) {
            /* Literal subscript: fold 2*k into the address as an ADC.
             * (LDNLP is word-scaled, so it cannot serve here.) */
            tp_push_base(&base);
            if (index.c.i)
                Tp_ADC((int)(2 * index.c.i));
        } else {
            tp_push_simple(&index);
            tp_push_base(&base);
            Tp_SSUB();
        }

        vtop--;
        vtop->r = dest;
        Tp_STL(dest);
        return 1;
    }
    return 0;
}

static int gen_ptradd_wsubdb(void) {
    SValue sv_base, sv_index;
    int result_v;

    if (tp_t414())
        return 0;               /* no wsubdb: the generic multiply */
    if (tp_ptradd_const_folds())
        return 0;

    /* Same direct form as gen_ptradd_wsub. */
    if (!nocode_wanted && tp_ptradd_base_ok(&vtop[-1]) &&
        tp_ptradd_base_is_simple(&vtop[-1]) && tp_operand_is_simple(&vtop[0])) {
        SValue base = vtop[-1];
        SValue index = vtop[0];
        int dest = tp_ptradd_result_reg();

        abcreg_stack_depth = 0;
        tp_push_simple(&index);
        tp_push_base(&base);
        Tp_WSUBDB();

        vtop--;
        vtop->r = dest;
        Tp_STL(dest);
        return 1;
    }

    gv2(RC_INT, RC_INT);
    sv_base = vtop[-1];
    sv_index = vtop[0];

    abcreg_stack_depth = 0;
    tp_push_sv(&sv_index);  /* push index first so it lands in BReg */
    tp_push_sv(&sv_base);   /* AReg = base, BReg = index */

    Tp_WSUBDB();            /* result = base + index * 8 */
    vtop--;

    /* Store result to workspace — matches gen_opi's post-switch STL.
     * Without this, subsequent load()/tp_push_sv() reads stale data
     * from the workspace slot assigned by gv2. */
    result_v = vtop->r & VT_VALMASK;
    if (result_v < VT_CONST) {
        Tp_STL(result_v);
    }
    return 1;
}

/* TCC_TRANSPUTER_WSUB_OPT=0 (bsub=0) or TCC_TRANSPUTER_BSUB_OPT=0 (bsub=1)
   turns an indexed pointer-add lowering off, for A/B measurement.  Read
   once: this is asked on every pointer addition. */
static int tp_ptradd_opt_off(int bsub)
{
    static int off[2] = { -1, -1 };
    if (off[bsub] < 0) {
        const char *e = getenv(bsub ? "TCC_TRANSPUTER_BSUB_OPT"
                                    : "TCC_TRANSPUTER_WSUB_OPT");
        off[bsub] = e && !strcmp(e, "0");
    }
    return off[bsub];
}

/* base + index*elem_size in one indexed instruction - the whole point of
 * the transputer's indexed addressing.  vtop[-1] is the base, vtop[0] the
 * unscaled index.  Returns 0, with nothing emitted, where the element size
 * has no such instruction on this part or the generic multiply-then-add
 * does better. */
ST_FUNC int gen_ptradd_indexed(int elem_size)
{
    if (tp_ptradd_opt_off(0))
        return 0;
    switch (elem_size) {
    case 4:
        return gen_ptradd_wsub();
    case 8:
        return gen_ptradd_wsubdb();
    case 1:
        /* separately gateable so the A/B can be measured */
        return tp_ptradd_opt_off(1) ? 0 : gen_ptradd_bsub();
    case 2:
        /* T9000 only; a distinct element size from bsub's */
        return gen_ptradd_ssub();
    }
    return 0;
}

/* The add tccgen.c does between tp_ptradd_addr(1) and tp_ptradd_addr(0)
 * yields an address, so it can go on the T9000's spare stage-2 address
 * slot instead of the contended stage-4 ALU (tp_addr_disp_words). */
ST_FUNC void tp_ptradd_addr(int on)
{
    tp_ptr_add_is_addr = on && !tp_ptradd_opt_off(1);
}

static int tp_mul_const_strength(long c, int *out)
{
    if (c == -1 || c == 0 || c == 1 ||
        c == 2 || c == 3 || c == 4 || c == 8) {
        *out = (int)c;
        return 1;
    }
    if (c > 0 && (c & (c - 1)) == 0) {
        int shift = 0;
        long t = c;
        while (t > 1) {
            shift++;
            t >>= 1;
        }
        if (shift >= 4 && shift <= 31) {
            *out = 0x100 + shift;
            return 1;
        }
    }
    return 0;
}

/* ---- strict comparisons at a loop back-edge -----------------------------
 *
 * CJ branches when AReg == 0.  A loop back-edge wants to branch when the
 * condition is TRUE, so gvtst asks gjmp_cond to invert and gjmp_cond emits
 * `EQC 0` - one contended stage-4 slot on every iteration.  Worse, the EQC is
 * itself a stage-4 op, so it cannot share the compare's group and the CJ that
 * follows starts a third: the tail of a counted loop costs two groups where
 * one would do.
 *
 * The non-strict forms already avoid this.  TOK_GE/TOK_LE/TOK_UGE/TOK_ULE
 * compute the *complement* with a plain GT/GTU and tag the result
 * VT_CMP(TOK_EQ) - "zero is true" - which gjmp_cond consumes with no EQC at
 * all.  TOK_GT/TOK_LT/TOK_UGT/TOK_ULT did not, which is why `i <= 9999`
 * measured 25% faster than `i < 10000` for the identical loop.
 *
 * Against a literal the complement is free, because a strict comparison with
 * the bound moved by one is the negation of the same comparison reversed:
 *
 *     A >  K   ===  !(K+1 >  A)        K >  A   ===  !(A >  K-1)
 *
 * Swapping the two vstack entries and moving the literal by one costs no
 * instructions - both operands are already evaluated, exactly as the
 * TOK_LT/TOK_GE swap at the top of gen_opi relies on - and lets the result be
 * tagged "zero is true" like the non-strict forms.  For TOK_ULT it also drops
 * the REV, since the reversed form needs no operand exchange.
 *
 * Getting the polarity wrong is not a correctness bug: VT_CMP carries the
 * convention and both consumers (gjmp_cond and load()) handle either.  A
 * mispredicted consumer only moves the EQC, so the hint below can be
 * approximate. */

/* Set while a loop's controlling expression is generated. */
static int tp_cmp_branch_true;

ST_FUNC void tp_cmp_hint_branch_true(int on)
{
    tp_cmp_branch_true = on;
}

static int tp_cmp_is_word_literal(const SValue *sv)
{
    if ((sv->r & (VT_VALMASK | VT_LVAL | VT_SYM | VT_MUSTCAST |
                  VT_MUSTBOUND | VT_BOUNDED)) != VT_CONST)
        return 0;
    return get_primitive_type_size(sv->type.t & VT_BTYPE) == 4;
}

static void tp_cmp_swap_operands(void)
{
    SValue tmp = vtop[-1];
    vtop[-1] = vtop[0];
    vtop[0] = tmp;
}

/* Rewrite a strict comparison against a literal into its complement.  Returns
 * 1 when the rewrite happened, in which case the result must be tagged
 * VT_CMP(TOK_EQ) rather than VT_CMP(TOK_NE). */
/* TP_NO_CMPFLIP=1 restores the pre-fix lowering, so the change can be A/B
 * measured without rebuilding a different tree - same handle as TP_NO_CALLOPT
 * and TP_POOL. */
/* TP_NO_EQC0=1 keeps the `x == 0` eager inversion.  Separate from
 * TP_NO_CMPFLIP so the two compare changes can be attributed independently -
 * they are different mechanisms and, as it turned out, very different wins. */
static int tp_no_eqc0(void)
{
    static int v = -1;
    if (v < 0)
        v = getenv("TP_NO_EQC0") ? 1 : 0;
    return v;
}

static int tp_no_cmpflip(void)
{
    static int v = -1;
    if (v < 0)
        v = getenv("TP_NO_CMPFLIP") ? 1 : 0;
    return v;
}

static int tp_cmp_complement_vs_literal(int *op)
{
    if (!tp_cmp_branch_true || nocode_wanted || tp_no_cmpflip())
        return 0;

    switch (*op) {
    case TOK_GT:
        /* A > K  ===  !(K+1 > A) */
        if (tp_cmp_is_word_literal(&vtop[0])) {
            int k = (int)vtop[0].c.i;
            if (k == 0x7FFFFFFF)
                return 0;
            vtop[0].c.i = k + 1;
            tp_cmp_swap_operands();
            return 1;
        }
        /* K > A  ===  !(A > K-1) */
        if (tp_cmp_is_word_literal(&vtop[-1])) {
            int k = (int)vtop[-1].c.i;
            if (k == (int)0x80000000)
                return 0;
            vtop[-1].c.i = k - 1;
            tp_cmp_swap_operands();
            return 1;
        }
        return 0;

    case TOK_UGT:
        /* A >u K  ===  !(K+1 >u A) */
        if (tp_cmp_is_word_literal(&vtop[0])) {
            uint32_t k = (uint32_t)vtop[0].c.i;
            if (k == 0xFFFFFFFFu)
                return 0;
            vtop[0].c.i = (int)(k + 1);
            tp_cmp_swap_operands();
            return 1;
        }
        return 0;

    case TOK_ULT:
        /* A <u K  ===  !(A >u K-1), and the reversed form needs no REV. */
        if (tp_cmp_is_word_literal(&vtop[0])) {
            uint32_t k = (uint32_t)vtop[0].c.i;
            if (k == 0)
                return 0;
            vtop[0].c.i = (int)(k - 1);
            *op = TOK_UGT;
            return 1;
        }
        return 0;

    default:
        return 0;
    }
}

/* Take back the code from `off` (the start of an entry still in the
 * history window) so it can be emitted again differently.  The caller
 * adjusts the eval-stack model for what it removed. */
static void tp_roll_back_to(int off)
{
    ind = off;
    cur_text_section->data_offset = ind;
    tp_drop_local_patches_from(ind);
    tp_hist_rollback(ind);
    tp_stl_track_clear();
    tp_areg_invalidate();
    tp_last_opc = tp_prev_opc = -1;
    tp_last_off = tp_last_end = tp_prev_off = tp_prev_end = -1;
}

/* gtu (opr 0x5F) arrived with the T9000: the T414/T425/T800/T805
 * instruction sets have none (t4 accepts it in T800 mode regardless), so
 * for -mt800 and -mt425 flip the sign bit of both operands and let the
 * signed gt order them as unsigned values.  Entry AReg = R, BReg = L;
 * leaves AReg = (L >u R), or (R >u L) - what `rev; gtu` gives - when
 * `swapped`.  The extra mint pushes a third word: CReg is dead here, as the
 * operands are the only values gen_opi keeps on the stack.
 *
 * Better, when both operands were pushed by short runs the history window
 * can replay (a load and up to two ldnl/ldnlp/adc on it, tp_hist_value_run):
 * ldiff subtracts with a borrow, so with 0 pushed under them `ldc 0; X; Y;
 * ldiff` leaves the borrow of X - Y, X <u Y, in B, and `rev` brings it up.
 * 5 T800 cycles beside the pushes, where the sign flips cost 11, and every
 * part from the T414 has ldiff.  (Flipping a literal's sign at compile time
 * does not pay: a small K becomes an eight-byte constant.)
 * TCC_TRANSPUTER_LDIFF_CMP=0 keeps the sign flips. */
static int tp_hist_value_run(int i)
{
    int j;

    for (j = i; j < tp_hist_n && j < i + 3; j++) {
        if (j > i && tp_hist[j].end != tp_hist[j - 1].off)
            return -1;
        if (tp_hist_is_pure_push(&tp_hist[j]))
            return j;
        if (tp_hist[j].kind != TP_HIST_INSTR ||
            (tp_hist[j].opc != TP_OP_LDNL && tp_hist[j].opc != TP_OP_LDNLP &&
             tp_hist[j].opc != TP_OP_ADC))
            return -1;
    }
    return -1;
}

static void tp_emit_gtu(int swapped)
{
    static int ldiff_cmp = -1;
    int r_end, l_end;

    if (tp_have_t9000_feat(TP_T9K_GTU)) {
        if (swapped)
            Tp_REV();
        Tp_OPR(TP_OPR_GTU);
        return;
    }
    if (ldiff_cmp < 0) {
        const char *e = getenv("TCC_TRANSPUTER_LDIFF_CMP");
        ldiff_cmp = !(e && *e == '0');
    }
    /* R's run is hist[0..r_end], L's the one under it, ending at l_end. */
    if (ldiff_cmp && !nocode_wanted && tp_hist_n >= 2 && tp_hist[0].end == ind &&
        (r_end = tp_hist_value_run(0)) >= 0 && r_end + 1 < tp_hist_n &&
        tp_hist[r_end + 1].end == tp_hist[r_end].off &&
        (l_end = tp_hist_value_run(r_end + 1)) >= 0) {
        struct tp_hist_ent rr[3], ll[3];
        int nr = r_end + 1, nl = l_end - r_end, k;

        for (k = 0; k < nr; k++)
            rr[k] = tp_hist[r_end - k];         /* oldest first */
        for (k = 0; k < nl; k++)
            ll[k] = tp_hist[l_end - k];
        tp_roll_back_to(tp_hist[l_end].off);
        abcreg_pop(2);
        Tp_LDC(0);                              /* no borrow in */
        /* (L >u R) is R <u L: X = R, Y = L; swapped wants L <u R */
        for (k = 0; k < (swapped ? nl : nr); k++)
            tp_hist_replay(swapped ? &ll[k] : &rr[k]);
        for (k = 0; k < (swapped ? nr : nl); k++)
            tp_hist_replay(swapped ? &rr[k] : &ll[k]);
        Tp_OPR(TP_OPR_LDIFF);                   /* A = X - Y, B = X <u Y */
        Tp_REV();
        abcreg_pop(2);                          /* the 0 and one operand */
        return;
    }
    Tp_LDC(-0x7fffffff - 1);                    /* mint */
    Tp_XOR();                                   /* A = R^M, B = L */
    Tp_REV();
    Tp_LDC(-0x7fffffff - 1);
    Tp_XOR();                                   /* A = L^M, B = R^M */
    if (!swapped)
        Tp_REV();
    Tp_GT();
}

/* (x >> K) & m with a mask that keeps only bits the shift brings down from
 * x itself (m < 2^(32 - K)) is the same for an arithmetic and a logical
 * shift.  A signed >> K is xdble; ldc K; lshr (there is no sign-filling
 * shift), so for these masks it becomes ldc K; shr: one instruction and a
 * long shift fewer.  Doom's texture lookups, (frac >> 16) & 127, do this
 * every pixel.
 *
 * Better still on the T800, T805, T425 and T414, whose shifts take a cycle
 * a place (shr 16 is 18 cycles): when K is 8, 16 or 24, m fits in a byte
 * and x came straight from a workspace word, the answer is byte K / 8 of
 * that word, so ldlp x; adc K/8; lb (7 cycles) replaces ldl x ... shr (21),
 * and the and goes too when m is 255.  The T9000 shifts in a cycle and
 * keeps the shift.
 *
 * Called with `ldc m` just emitted for the `and` that follows.  Returns 0
 * to leave the code alone, 1 with the code rewritten and the and still
 * wanted, 2 when the and is not.  TCC_TRANSPUTER_SHIFT_MASK=0 turns it
 * off for an A/B run. */
static int tp_shift_mask_fold(void)
{
    static int enabled = -1;
    struct tp_hist_ent x;
    unsigned int m, k;
    int i, sar, nwin, roll;

    if (enabled < 0) {
        const char *e = getenv("TCC_TRANSPUTER_SHIFT_MASK");
        enabled = !(e && *e == '0');
    }
    if (!enabled || nocode_wanted || tp_hist_n < 3)
        return 0;
    if (tp_hist[0].kind != TP_HIST_INSTR || tp_hist[0].opc != TP_OP_LDC ||
        tp_hist[0].end != ind)
        return 0;
    m = (unsigned int)tp_hist[0].val;
    if (tp_hist[1].kind == TP_HIST_INSTR && tp_hist[1].opc == TP_OP_OPR &&
        tp_hist[1].val == TP_OPR_LSHR && tp_hist_n >= 4 &&
        tp_hist[3].kind == TP_HIST_INSTR && tp_hist[3].opc == TP_OP_OPR &&
        tp_hist[3].val == TP_OPR_XDBLE)
        sar = 1;
    else if (tp_hist[1].kind == TP_HIST_INSTR && tp_hist[1].opc == TP_OP_OPR &&
             tp_hist[1].val == TP_OPR_SHR)
        sar = 0;
    else
        return 0;
    if (tp_hist[2].kind != TP_HIST_INSTR || tp_hist[2].opc != TP_OP_LDC)
        return 0;
    k = (unsigned int)tp_hist[2].val;
    if (k < 1 || k > 31 || (m >> (32 - k)) != 0)
        return 0;
    nwin = sar ? 4 : 3;                 /* ldc m back to the shift's start */
    for (i = 0; i + 1 < nwin; i++)
        if (tp_hist[i].off != tp_hist[i + 1].end)
            return 0;

    /* The byte form needs x's own load just before the shift. */
    if (!tp_isa_has(TP_EF_T9000) &&
        (k == 8 || k == 16 || k == 24) && m <= 0xff &&
        tp_hist_n > nwin && tp_hist[nwin].end == tp_hist[nwin - 1].off &&
        tp_hist[nwin].opc == TP_OP_LDL &&
        (tp_hist[nwin].kind == TP_HIST_LOCAL ||
         tp_hist[nwin].kind == TP_HIST_INSTR)) {
        x = tp_hist[nwin];
        roll = x.off;
        abcreg_pop(2);                  /* x and m are rolled back */
    } else if (sar) {
        x.kind = -1;
        roll = tp_hist[3].off;
        abcreg_pop(1);                  /* m is; the value x stays in A */
    } else
        return 0;

    tp_roll_back_to(roll);

    if (x.kind >= 0) {
        x.opc = TP_OP_LDLP;
        tp_hist_replay(&x);
        Tp_ADC(k / 8);
        Tp_LB();
        tp_areg_invalidate();
        if (m == 0xff)
            return 2;
    } else {
        Tp_LDC(k);
        Tp_SHR();
    }
    Tp_LDC(m);
    return 1;
}

/* tp_hist[a] is `and` and tp_hist[a + 1] the `ldc k`, 0 <= k <= 31, it
 * masks with, back to back. */
static int tp_hist_masks_31(int a)
{
    return a + 1 < tp_hist_n &&
           tp_hist[a].kind == TP_HIST_INSTR && tp_hist[a].opc == TP_OP_OPR &&
           tp_hist[a].val == TP_OPR_AND &&
           tp_hist[a + 1].kind == TP_HIST_INSTR &&
           tp_hist[a + 1].opc == TP_OP_LDC &&
           tp_hist[a + 1].val >= 0 && tp_hist[a + 1].val <= 31 &&
           tp_hist[a + 1].end == tp_hist[a].off;
}

/* Is the shift count now in AReg already in 0..31?  The count masked in the
 * source, as rol32/ror32 do (`shift & 31`), makes the runtime clamp a no-op.
 * AReg comes from the last instruction emitted, or from the one before a
 * push and `rev`; that is `ldc k; and`, or `ldl r` where the latest write to
 * pseudo-register r was `ldc k; and; stl r` and no `ldlp r` since could have
 * changed it.  The history only holds contiguous code with no label inside
 * it, so those instructions are what produced AReg. */
static int tp_shift_count_masked(void)
{
    int a = 0, s, r;

    if (nocode_wanted || tp_hist_n < 2 || tp_hist[0].end != ind)
        return 0;
    if (tp_hist[0].kind == TP_HIST_INSTR && tp_hist[0].opc == TP_OP_OPR &&
        tp_hist[0].val == TP_OPR_REV) {
        if (tp_hist_n < 4 || !tp_hist_is_pure_push(&tp_hist[1]) ||
            tp_hist[1].end != tp_hist[0].off ||
            tp_hist[2].end != tp_hist[1].off)
            return 0;
        a = 2;
    }
    if (tp_hist_masks_31(a))
        return 1;
    if (tp_hist[a].kind != TP_HIST_INSTR || tp_hist[a].opc != TP_OP_LDL)
        return 0;
    r = tp_hist[a].val;
    if (r < 0 || r >= NB_REGS)
        return 0;
    for (s = a + 1; s < tp_hist_n; s++) {
        if (tp_hist[s].end != tp_hist[s - 1].off)
            return 0;
        if (tp_hist[s].kind != TP_HIST_INSTR || tp_hist[s].val != r)
            continue;
        if (tp_hist[s].opc == TP_OP_LDLP)
            return 0;
        if (tp_hist[s].opc == TP_OP_STL)
            return tp_hist_masks_31(s + 1) &&
                   tp_hist[s + 1].end == tp_hist[s].off;
    }
    return 0;
}

ST_FUNC void gen_opi(int op) {
    int cmp_zero_true;
    int shl_add_opt = 0;
    int mul_const_opt = 0;
    int mul_const_val = 0;
    int eqc_const_opt = 0;
    int eqc_const_val = 0;
    int adc_const_opt = 0;
    int adc_const_val = 0;
    /* x >> K with a literal K: the shift count can be pushed *after* xdble
     * instead of being parked in a workspace slot across it.  See TOK_SAR. */
    int sar_const_opt = 0;
    int sar_const_val = 0;
    int mod_pow2_opt = 0;   /* unsigned modulo power-of-2: x % N -> x & (N-1) */
    int mod_pow2_mask = 0;
    /* Shift count is a literal already in 0..31, so the runtime
     * "LDC 31; AND" clamp is provably a no-op and can be dropped. */
    int shift_const_ok = 0;

    /* UMULL leaves its high word in REG_IRE2, which may still hold the high
     * half of a pending long long call result (`get_u64() + x * y`). */
    if (op == TOK_UMULL)
        save_reg(REG_IRE2);

    /* `a < b` and `a >= b` used to emit REV before GT, because GT computes
     * (BReg > AReg) and the operands are pushed first-then-second.  Swapping
     * the two vstack entries and flipping the operator gives the same result
     * with no REV at all:
     *
     *     a <  b   ===   b >  a
     *     a >= b   ===   b <= a
     *
     * Both operands are already evaluated by the time gen_opi runs, so
     * exchanging them cannot reorder side effects - this is the same
     * reasoning behind the existing TOK_EQ/TOK_NE commutation swap further
     * down.  Doing it here rather than at the push sites means every operand
     * path (local/local, local/const, register, and the gv2 fallback) gets it,
     * and the classification below sees the final order.
     *
     * The peephole rule that was supposed to catch this pattern reports zero
     * hits on every function, so it never fired; removing the REV at the
     * source is the fix. */
    if (op == TOK_LT || op == TOK_GE) {
        SValue tmp = vtop[-1];
        vtop[-1] = vtop[0];
        vtop[0] = tmp;
        op = (op == TOK_LT) ? TOK_GT : TOK_LE;
    }

    /* Strict compare against a literal at a loop back-edge: produce the
     * complement so the result can be tagged "zero is true" and gjmp_cond
     * needs no EQC 0. */
    cmp_zero_true = tp_cmp_complement_vs_literal(&op);

    /* The unsigned relations, likewise.  With GTU, a <u b is b >u a with no
     * REV.  Without it the mint/xor sequence is the other way round - REV
     * comes free with a <u b and costs one for a >u b - so there the swap
     * goes towards TOK_ULT/TOK_UGE.  A literal compare the complement has
     * already rewritten is left as it is. */
    if (!cmp_zero_true) {
        int to = 0;
        if (tp_have_t9000_feat(TP_T9K_GTU)) {
            if (op == TOK_ULT) to = TOK_UGT;
            else if (op == TOK_UGE) to = TOK_ULE;
        } else {
            if (op == TOK_UGT) to = TOK_ULT;
            else if (op == TOK_ULE) to = TOK_UGE;
        }
        if (to) {
            tp_cmp_swap_operands();
            op = to;
        }
    }

    /* Transputer: comparison operations need explicit operand loading.
     *
     * For arithmetic ops (+, -, etc.), the operands are typically already
     * on the eval stack from preceding load/computation instructions.
     * These work "by construction" — TCC's generic layer generates loads
     * that happen to leave values on the Transputer eval stack.
     *
     * For comparison ops (==, !=, >, <, etc.), this doesn't work after
     * function calls because the eval stack is corrupted by the CALL
     * instruction's return value. We must explicitly load operands.
     *
     * We use tp_push_sv() which always generates code to push a value
     * onto the eval stack, handling all SValue types correctly. */
    /* Always explicitly push both operands onto the hardware eval stack.
     * TCC's generic layer may leave values in workspace-backed "registers"
     * (workspace slots) rather than on the eval stack. On the Transputer,
     * all binary operations (arithmetic, logic, comparisons) operate on
     * AReg and BReg, so we must ensure both values are pushed. */
    /* Transputer-specific gv2: for two word-size local lvalues, emit
     * LDL directly from local offsets and STL to allocated registers.
     * This produces LDL a; STL r1; LDL b; STL r2 (4 insns) which is
     * the same as normal gv2. BUT: tp_push_sv then does LDL r1; LDL r2
     * (2 more insns). With the DUP/areg_tag tracker, the second load
     * might be optimized. The real win comes when we later skip the
     * tp_push_sv reload by keeping values on the eval stack.
     *
     * For now, this function is identical to gv2 for the common case
     * but lays the groundwork for direct eval-stack loading. */
    {
        int v1 = vtop[-1].r & VT_VALMASK;
        int v2 = vtop[0].r & VT_VALMASK;

        /* Classify operands */
        /* (a word of a packed local at a misaligned offset is not one LDL:
         * tp_word_unaligned) */
        int first_local = (vtop[-1].r & VT_LVAL) && v1 == VT_LOCAL &&
                          !(vtop[-1].r & VT_SYM) && !(vtop[-1].c.i & 3) &&
                          get_primitive_type_size(vtop[-1].type.t & VT_BTYPE) == 4;
        int second_local = (vtop[0].r & VT_LVAL) && v2 == VT_LOCAL &&
                           !(vtop[0].r & VT_SYM) && !(vtop[0].c.i & 3) &&
                           get_primitive_type_size(vtop[0].type.t & VT_BTYPE) == 4;
        int first_const = (v1 == VT_CONST) && !(vtop[-1].r & VT_SYM);
        int second_const = (v2 == VT_CONST) && !(vtop[0].r & VT_SYM);

        /* A value already materialised in a general workspace-slot register
         * costs exactly one LDL r to push — the same as a local. Without this,
         * (register op X) forms fell through to gv2 and emitted the
         * five-instruction spill dance `STL a; LDC k; STL b; LDL a; LDL b`
         * ahead of the real operation. That is the single most common waste in
         * kernel code: ~184k sites, ~738 KB of .text.
         *
         * Restricted to TREG_W0..NB_REGS-1: slots 0-2 are the hardware A/B/C
         * eval-stack registers and 3-5 are the FPU slots, none of which follow
         * the plain "value lives in workspace slot r" rule. VT_MUSTCAST must be
         * excluded too — bypassing gv2 would skip a required narrowing cast. */
        int first_reg = !(vtop[-1].r & (VT_LVAL | VT_SYM | VT_MUSTCAST |
                                        VT_MUSTBOUND | VT_BOUNDED)) &&
                        v1 >= TREG_W0 && v1 < NB_REGS &&
                        get_primitive_type_size(vtop[-1].type.t & VT_BTYPE) == 4;
        int second_reg = !(vtop[0].r & (VT_LVAL | VT_SYM | VT_MUSTCAST |
                                        VT_MUSTBOUND | VT_BOUNDED)) &&
                         v2 >= TREG_W0 && v2 < NB_REGS &&
                         get_primitive_type_size(vtop[0].type.t & VT_BTYPE) == 4;


        /* Exclude 3-operand ops and special cases from direct path */
        int exclude = op == TOK_UMULL || op == TOK_ADDC1 || op == TOK_SUBC1 ||
                      op == TOK_ADDC2 || op == TOK_SUBC2 ||
                      op == TOK_UDIV || op == TOK_UMOD ||
                      op == TOK_A_SHL || op == TOK_A_SAR || op == TOK_NEG;

        /* Plain shifts are NOT excluded: SHL/SHR/SAR all want AReg=count,
         * BReg=value, which is exactly the operand order CASE 1/2/3 produce.
         * Excluding them forced every shift down the gv2 spill path, which
         * cost 8 extra bytes per constant shift (the dominant kernel form:
         * `x >> LITERAL` in field extracts and bitmap indexing). */
        int is_shift = (op == TOK_SHL || op == TOK_SHR || op == TOK_SAR);
        int second_folded;

        /* A literal count already in range makes the runtime clamp dead. */
        if ((is_shift || op == TOK_A_SHL || op == TOK_A_SAR) && second_const) {
            unsigned long cnt = (unsigned long)vtop[0].c.i;
            if (cnt <= 31)
                shift_const_ok = 1;
        }

        /* Constant-RHS folds, decided once for every direct case below.
         * `direct_ready` jumps past the block that used to compute these, so
         * without this the fast paths emitted `LDC k; SUM` where the slow path
         * would have emitted a single `ADC k`. That matters enormously: every
         * struct field access is lowered by tccgen as `pointer + constant`
         * (see the TOK_ARROW case in tccgen.c), so ADC folding applies to
         * essentially all field addressing in the kernel.
         *
         * When a fold fires, the second operand must NOT be pushed — the
         * folded instruction carries the literal itself. */
        if (!exclude && second_const) {
            long c = (long)vtop[0].c.i;
            switch (op) {
            case '+': case TOK_A_ADD:
                if (!tp_adc_would_trap()) {
                    adc_const_opt = 1; adc_const_val = (int)c;
                }
                break;
            case '-': case TOK_A_SUB:
                if (!tp_adc_would_trap()) {
                    adc_const_opt = 1; adc_const_val = -(int)c;
                }
                break;
            case TOK_EQ: case TOK_NE:
                eqc_const_opt = 1; eqc_const_val = (int)c;
                break;
            case '*': case TOK_A_MUL:
                if (tp_mul_const_strength(c, &mul_const_val))
                    mul_const_opt = 1;
                break;
            case TOK_SHL:
                if (c == 1 || c == 2)
                    shl_add_opt = (int)c;
                break;
            case TOK_SAR: case TOK_A_SAR:
                /* Only for counts the runtime clamp would have passed
                   through unchanged.  Negative counts, and counts above 31,
                   keep the generic path and its LDC 31; AND clamp, so
                   behaviour there is exactly as before. */
                if (c >= 0 && c <= 31) {
                    sar_const_opt = 1; sar_const_val = (int)c;
                }
                break;
            default:
                break;
            }
        }

        /* True when the constant RHS is folded into the operation and so must
         * not be materialised on the eval stack. */
        second_folded = adc_const_opt || eqc_const_opt ||
                        mul_const_opt || shl_add_opt || sar_const_opt;

        if (!exclude && first_local && second_local) {
            /* CASE 1: local op local → LDL a; LDL b */
            int off1 = vtop[-1].c.i;
            int off2 = vtop[0].c.i;
            abcreg_stack_depth = 0;
            if (off1 == off2 && !tp_t414()) {
                emit_local_op(TP_OP_LDL, off1);
                Tp_DUP();
            } else {
                emit_local_op(TP_OP_LDL, off1);
                emit_local_op(TP_OP_LDL, off2);
            }
            vtop[-1].r = get_reg(RC_INT);
            goto direct_ready;
        }

        if (!exclude && first_local && second_const) {
            /* CASE 2: local op const → LDL a; LDC const */
            abcreg_stack_depth = 0;
            emit_local_op(TP_OP_LDL, vtop[-1].c.i);
            if (!second_folded)
                tp_ldc_pooled((int)vtop[0].c.i);
            vtop[-1].r = get_reg(RC_INT);
            goto direct_ready;
        }

        if (!exclude && first_const && second_local) {
            /* CASE 3: const op local → LDC const; LDL b */
            abcreg_stack_depth = 0;
            if ((op == '*' || op == TOK_A_MUL) &&
                tp_mul_const_strength((long)vtop[-1].c.i, &mul_const_val)) {
                emit_local_op(TP_OP_LDL, vtop[0].c.i);
                mul_const_opt = 1;
            } else {
                tp_ldc_pooled((int)vtop[-1].c.i);
                emit_local_op(TP_OP_LDL, vtop[0].c.i);
            }
            vtop[-1].r = get_reg(RC_INT);
            goto direct_ready;
        }

        /* CASES 4-8: one or both operands already live in a workspace-slot
         * register. Push directly with LDL instead of round-tripping through
         * gv2's freshly allocated spill slots. The first operand's register is
         * reused as the destination — it is consumed by this operation anyway,
         * and reusing it avoids a get_reg() call that could spill a live value
         * after we have already pushed onto the eval stack. */
        if (!exclude && (first_reg || second_reg) &&
            (first_reg || first_local || first_const) &&
            (second_reg || second_local || second_const)) {
            int dest;
            abcreg_stack_depth = 0;

            /* Push first operand. The common shape is that the value was just
             * computed and stored into v1 by the previous operation, so try to
             * cancel that store instead of reloading it. */
            if (first_reg)
                tp_push_reg_or_cancel_stl(v1, &vtop[-1]);
            else if (first_local)
                emit_local_op(TP_OP_LDL, vtop[-1].c.i);
            else
                tp_ldc_pooled((int)vtop[-1].c.i);

            /* Push second operand, unless it was folded into the operation. */
            if (second_folded) {
                /* nothing to push — the literal rides in the instruction */
            } else if (second_reg) {
                if (first_reg && v1 == v2 && !tp_t414())
                    Tp_DUP();
                else
                    Tp_LDL(v2);
            } else if (second_local) {
                emit_local_op(TP_OP_LDL, vtop[0].c.i);
            } else {
                tp_ldc_pooled((int)vtop[0].c.i);
            }

            /* Plain assignment, as CASES 1-3 do: the result is a fresh value
             * in a register, so every other r-flag (VT_LVAL, VT_SYM,
             * VT_MUSTCAST, ...) must be cleared, not carried over. */
            dest = first_reg ? v1 : (second_reg ? v2 : get_reg(RC_INT));
            vtop[-1].r = dest;
            goto direct_ready;
        }

        /* Standard path: gv2 */
        gv2(RC_INT, RC_INT);
    }
    {
        SValue sv_first = vtop[-1];
        SValue sv_second = vtop[0];
        int first_is_imm_const = ((sv_first.r & VT_VALMASK) == VT_CONST) && !(sv_first.r & VT_SYM);
        int second_is_imm_const = ((sv_second.r & VT_VALMASK) == VT_CONST) && !(sv_second.r & VT_SYM);

        /* Spill/reload desync guard:
         * For arithmetic/comparison, a scalar local can transiently appear as
         * VT_LOCAL without VT_LVAL. Treat it as a value load in this context.
         * Keep pointer/array/function operands untouched (address semantics). */
        if (!(sv_first.r & VT_LVAL) && (sv_first.r & VT_VALMASK) == VT_LOCAL) {
            int bt1 = sv_first.type.t & VT_BTYPE;
            if (!(sv_first.type.t & (VT_ARRAY | VT_VLA)) && bt1 != VT_PTR && bt1 != VT_FUNC) {
                tp_debug_log_vt_local_normalize(&sv_first, "lhs");
                sv_first.r |= VT_LVAL;
            }
        }
        if (!(sv_second.r & VT_LVAL) && (sv_second.r & VT_VALMASK) == VT_LOCAL) {
            int bt2 = sv_second.type.t & VT_BTYPE;
            if (!(sv_second.type.t & (VT_ARRAY | VT_VLA)) && bt2 != VT_PTR && bt2 != VT_FUNC) {
                tp_debug_log_vt_local_normalize(&sv_second, "rhs");
                sv_second.r |= VT_LVAL;
            }
        }

        if (op == '*' || op == TOK_A_MUL) {
            /* PROD latency depends on AReg; keep immediate constant in AReg. */
            if (first_is_imm_const && !second_is_imm_const) {
                SValue tmp = sv_first;
                sv_first = sv_second;
                sv_second = tmp;
                first_is_imm_const = 0;
                second_is_imm_const = 1;
            }
            /* Strength reduction for multiply by constants.
             * DUP+SUM = 2 cycles vs PROD's 30+ cycles.
             * x*0 → 0
             * x*1 → x
             * x*(-1) → -x
             * x*2 → DUP;SUM
             * x*3 → DUP;SUM;(orig);SUM (save x, x*2, add x)
             * x*4 → DUP;SUM;DUP;SUM
             * x*8 → DUP;SUM;DUP;SUM;DUP;SUM
             * x*(1<<N), N>=4 → LDC N; SHL */
            if (second_is_imm_const) {
                long c = sv_second.c.i;
                if (tp_mul_const_strength(c, &mul_const_val)) {
                    mul_const_opt = 1;
                }
            }
        }

        /* Modulo power-of-2: x % N -> x & (N-1).
         * Only valid for unsigned modulo. Signed C remainder must keep the
         * dividend sign, e.g. -9 % 8 == -1, while -9 & 7 == 7. */
        if (op == TOK_UMOD && second_is_imm_const) {
            long c = sv_second.c.i;
            if (c > 0 && (c & (c - 1)) == 0) {  /* power of 2 check */
                mod_pow2_opt = 1;
                mod_pow2_mask = (int)(c - 1);
            }
        }

        if ((op == TOK_A_SHL || op == TOK_SHL) && second_is_imm_const) {
            if (sv_second.c.i == 1) {
                shl_add_opt = 1;
            } else if (sv_second.c.i == 2) {
                shl_add_opt = 2;
            }
        }

        if (op == TOK_EQ || op == TOK_NE) {
            /* Commutative: if left is constant, swap so right is constant */
            if (first_is_imm_const && !second_is_imm_const) {
                SValue tmp = sv_first;
                sv_first = sv_second;
                sv_second = tmp;
                first_is_imm_const = 0;
                second_is_imm_const = 1;
            }
            if (second_is_imm_const) {
                eqc_const_opt = 1;
                eqc_const_val = sv_second.c.i;
            }
        }

        if (op == '+' || op == TOK_A_ADD || op == '-' || op == TOK_A_SUB) {
            /* ADC optimization: a + const → load a; ADC const (1 instruction)
             * Also works for subtraction: a - const → ADC(-const).
             * Commutative for +: if left is constant, swap. */
            if ((op == '+' || op == TOK_A_ADD) && first_is_imm_const && !second_is_imm_const) {
                SValue tmp = sv_first;
                sv_first = sv_second;
                sv_second = tmp;
                first_is_imm_const = 0;
                second_is_imm_const = 1;
            }
            if (second_is_imm_const && !tp_adc_would_trap()) {
                int cval = (op == '-' || op == TOK_A_SUB) ? -(int)sv_second.c.i : (int)sv_second.c.i;
                adc_const_opt = 1;
                adc_const_val = cval;
            }
        }

        if (tp_genopi_debug) {
            fprintf(stderr, "[GEN_OPI] func=%s op=0x%x('%c') ind=0x%x "
                    "first: r=0x%x v=0x%x c.i=%ld lval=%d  "
                    "second: r=0x%x v=0x%x c.i=%ld lval=%d\n",
                    funcname ? funcname : "?", op, (op > 0x20 && op < 0x7f) ? op : '?',
                    ind,
                    sv_first.r, sv_first.r & VT_VALMASK, (long)sv_first.c.i,
                    (sv_first.r & VT_LVAL) != 0,
                    sv_second.r, sv_second.r & VT_VALMASK, (long)sv_second.c.i,
                    (sv_second.r & VT_LVAL) != 0);
        }
        abcreg_stack_depth = 0;
        /* Prime CReg for 3-operand instructions (LMUL, LSUM, LDIFF, LDIV).
         * Push the CReg value before operands so it naturally falls to CReg
         * after two operand pushes. Eliminates STL/REV/LDL workspace thrash. */
        if (op == TOK_UMULL || op == TOK_ADDC1 || op == TOK_SUBC1
            || op == TOK_UDIV || op == TOK_UMOD) {
            Tp_LDC(0);
            abcreg_push(1);
        } else if (op == TOK_ADDC2 || op == TOK_SUBC2) {
            Tp_LDL(FRAME_SLOT_CARRY);
            abcreg_push(1);
        }
        /* Direct LDL chaining: DISABLED — gv2() already emits load
         * instructions and updating the vstack. Adding extra LDLs on top
         * corrupts the eval stack state. Proper fix requires either:
         * a) Preventing gv2() from emitting loads for the direct path
         * b) Replacing gv2() entirely with a transputer-specific loader
         * Both are major TCC refactors. See STL-LDL-PLAN.md. */
        tp_push_sv(&sv_first);
        if (!shl_add_opt && !mul_const_opt && !eqc_const_opt && !adc_const_opt
            && !mod_pow2_opt && !sar_const_opt) {
            int same_operand = 0;
            if (sv_first.r == sv_second.r && sv_first.c.i == sv_second.c.i)
                same_operand = 1;
            if (same_operand && !tp_t414()) {
                Tp_DUP();
            } else {
                tp_push_sv(&sv_second);
            }
        }
    }

direct_ready:
    switch (op) {
    case TOK_A_ADD:  /* += compound assignment */
    case '+':
        if (adc_const_opt) {
            int w = tp_addr_disp_words(adc_const_val);
            if (w)
                emit_instr(TP_OP_LDNLP, w);   /* stage 2, not stage 4 */
            else
                emit_instr(TP_OP_ADC, adc_const_val);
        } else {
            Tp_SUM();
        }
        vtop--; break;
    case TOK_A_SUB:  /* -= compound assignment */
    case '-':
        if (adc_const_opt) {
            int w = tp_addr_disp_words(adc_const_val);
            if (w)
                emit_instr(TP_OP_LDNLP, w);   /* already negated; stage 2 */
            else
                emit_instr(TP_OP_ADC, adc_const_val);
        } else {
            Tp_DIFF();
        }
        vtop--; break;
    case TOK_A_MUL:  /* *= compound assignment */
    case '*':
        if (mul_const_opt && mul_const_val == 0) {
            if (tp_no_t805_ops()) {
                Tp_LDC(0);              /* no pop: x & 0 */
                Tp_AND();
            } else {
                Tp_POP();
                Tp_LDC(0);
            }
            vtop--; break;
        }
        if (mul_const_opt && tp_t414() && (mul_const_val == 2
            || mul_const_val == 4 || mul_const_val == 8)) {
            /* the dup; sum chains below need dup */
            Tp_LDC(mul_const_val == 2 ? 1 : mul_const_val == 4 ? 2 : 3);
            Tp_SHL();
            vtop--; break;
        }
        if (mul_const_opt && tp_t414() && mul_const_val == 3) {
            Tp_LDC(3);
            Tp_OPR_BINARY(TP_OPR_PROD);
            vtop--; break;
        }
        if (mul_const_opt && mul_const_val == 1) {
            vtop--; break;
        }
        if (mul_const_opt && mul_const_val == -1) {
            /* ~x + 1, but not with adc, the error-checking add: negating
             * MinInt overflows.  That is undefined for an int, but this is
             * also how a 64-bit multiply's cross terms take a low word of
             * all ones (`x * -1LL`, `(u64)(i32)-1 * y`), where the words
             * are signed or unsigned ints with the 64-bit type's sign and
             * any value is defined (c-testsuite 09117). */
            Tp_OPR(TP_OPR_NOT);
            Tp_LDC(1);
            Tp_SUM();
            vtop--; break;
        }
        if (mul_const_opt && mul_const_val == 2) {
            /* x * 2 → DUP; SUM */
            Tp_DUP(); Tp_SUM();
            vtop--; break;
        }
        if (mul_const_opt && mul_const_val == 3) {
            /* x * 3 → DUP; SUM; x; SUM (need original x)
             * But x is on eval stack — we can't easily get it back.
             * Alternative: DUP; DUP; SUM; SUM = x + x + x = 3x */
            Tp_DUP(); Tp_DUP();  /* AReg=x, BReg=x, CReg=x */
            Tp_SUM();            /* AReg=2x, BReg=x */
            Tp_SUM();            /* AReg=3x */
            vtop--; break;
        }
        if (mul_const_opt && mul_const_val == 4) {
            /* x * 4 → DUP; SUM; DUP; SUM */
            Tp_DUP(); Tp_SUM(); Tp_DUP(); Tp_SUM();
            vtop--; break;
        }
        if (mul_const_opt && mul_const_val == 8) {
            /* x * 8 → DUP; SUM; DUP; SUM; DUP; SUM */
            Tp_DUP(); Tp_SUM(); Tp_DUP(); Tp_SUM(); Tp_DUP(); Tp_SUM();
            vtop--; break;
        }
        if (mul_const_opt && (mul_const_val & 0x100) != 0) {
            /* x * (1 << N) → x << N.  Entry has AReg=x; push N as
             * shift count so SHL sees AReg=count, BReg=value. */
            Tp_LDC(mul_const_val & 0xff);
            Tp_SHL();
            vtop--; break;
        }
        Tp_OPR(TP_OPR_PROD); abcreg_pop(1); vtop--; break;
    case TOK_A_DIV:  /* /= compound assignment */
    case '/':
    case TOK_PDIV:   /* pointer difference / element size: signed */
        Tp_OPR(TP_OPR_DIV); abcreg_pop(1); vtop--; break;
    case TOK_UDIV:   /* unsigned division */
        /* T4 DIV is signed-only. Use LDIV's CReg==0 fast path for unsigned
         * 32-bit division. CReg already primed with 0 in preamble.
         * Entry: AReg=divisor, BReg=dividend, CReg=0. */
        Tp_OPR(TP_OPR_LDIV);           /* AReg=quotient, BReg=remainder */
        abcreg_pop(1); vtop--; break;
    case TOK_A_MOD:  /* %= compound assignment */
    case '%':
        if (mod_pow2_opt) {
            Tp_LDC(mod_pow2_mask);
            Tp_AND();
            vtop--; break;
        }
        Tp_OPR(TP_OPR_REM); abcreg_pop(1); vtop--; break;
    case TOK_UMOD:   /* unsigned modulo */
        if (mod_pow2_opt) {
            Tp_LDC(mod_pow2_mask);
            Tp_AND();
            vtop--; break;
        }
        /* CReg already primed with 0 in preamble.
         * Entry: AReg=divisor, BReg=dividend, CReg=0. */
        Tp_OPR(TP_OPR_LDIV);           /* AReg=quotient, BReg=remainder */
        Tp_REV();                      /* AReg=remainder */
        abcreg_pop(1); vtop--; break;
    case TOK_A_AND:  /* &= compound assignment */
    case '&':
        if (tp_shift_mask_fold() == 2) {
            vtop--;
            break;
        }
        Tp_AND(); vtop--; break;
    case TOK_A_OR:   /* |= compound assignment */
    case '|': Tp_OR(); vtop--; break;
    case TOK_A_XOR:  /* ^= compound assignment */
    case '^': Tp_XOR(); vtop--; break;
    case TOK_A_SHL:  /* <<= compound assignment */
    case TOK_SHL:
        if (shl_add_opt && tp_t414()) {
            Tp_LDC(shl_add_opt);        /* no dup for x + x */
            Tp_SHL();
            vtop--;
            break;
        }
        if (shl_add_opt == 1) {
            /* x << 1 == x + x */
            Tp_DUP();
            Tp_SUM();
            vtop--;
            break;
        } else if (shl_add_opt == 2) {
            /* x << 2 == ((x + x) + (x + x)) */
            Tp_DUP();
            Tp_SUM();
            Tp_DUP();
            Tp_SUM();
            vtop--;
            break;
        }
        /* Clamp shift count to 0-31: TCWG warns count >= 32 can lock
         * the processor for minutes.  AReg=count, BReg=value.
         * A literal count already in range makes the clamp dead code. */
        if (!shift_const_ok && !tp_shift_count_masked()) {
            Tp_LDC(31); Tp_AND();
        }
        Tp_SHL(); vtop--; break;
    case TOK_SHR:    /* >> logical shift right */
        /* Clamp shift count to 0-31 (same safety as SHL) */
        if (!shift_const_ok && !tp_shift_count_masked()) {
            Tp_LDC(31); Tp_AND();
        }
        Tp_SHR(); vtop--; break;
    case TOK_A_SAR:  /* >>= compound assignment (arithmetic shift) */
    case TOK_SAR:    /* arithmetic shift right using XDBLE+LSHR (Compiler Guide p28) */
        if (sar_const_opt) {
            /* Literal count: the count was NOT pushed (second_folded), so the
             * value alone is in AReg and the workspace round trip that the
             * general case needs disappears.
             *
             * ISM 21 FD (xdble):  Areg unchanged, Breg' = sign word, Creg' =
             * Breg'.  ISM 23 F5 (lshr): shifts the double word Creg:Breg (most
             * significant word in Creg) right by the count *in Areg*.
             *
             *   value in AReg
             *   xdble   -> AReg=value, BReg=sign, CReg=sign
             *   ldc K   -> AReg=K,     BReg=value, CReg=sign   (ldc pushes)
             *   lshr    -> AReg = (sign:value) >> K            (arithmetic)
             *
             * which is register-for-register identical to what
             * `ldc K; stl s; xdble; ldl s; lshr` produced, minus the stl/ldl.
             * K == 0 is the identity, so emit nothing at all. */
            if (sar_const_val != 0) {
                Tp_OPR(TP_OPR_XDBLE);
                Tp_LDC(sar_const_val);   /* abcreg_push(1) */
                Tp_OPR(TP_OPR_LSHR);
                /* Net the LDC back out so the eval-stack depth on exit is 1,
                 * exactly what the general path's trailing abcreg_pop(1)
                 * leaves.  Tp_OPR does not touch the AReg tag cache and,
                 * unlike the general path, there is no Tp_STL/Tp_LDL here to
                 * do it, so invalidate explicitly. */
                abcreg_pop(1);
                tp_areg_invalidate();
            }
            vtop--; break;
        }
        /* Entry: AReg=shift, BReg=value.
         * XDBLE sign-extends AReg into BReg:AReg (64-bit).
         * LSHR shifts CReg:BReg right by AReg bits, result in AReg. */
        if (!shift_const_ok && !tp_shift_count_masked()) {
            Tp_LDC(31); Tp_AND();                /* clamp shift 0-31 */
        }
        Tp_STL(FRAME_SLOT_SCRATCH2);   /* save shift; AReg=value */
        Tp_OPR(TP_OPR_XDBLE);         /* BReg=sign(-1/0), AReg=value */
        Tp_LDL(FRAME_SLOT_SCRATCH2);  /* AReg=shift, BReg=value, CReg=sign */
        Tp_OPR(TP_OPR_LSHR);          /* CReg:BReg >> AReg → AReg=result */
        abcreg_pop(1); vtop--; break;

    /* Comparisons: Result 1(True) or 0(False) in AReg.
     *
     * All comparisons produce "non-zero = true" in AReg.  gvtst() calls
     * gjmp_cond(op ^ inv, t).  gjmp_cond adds EQC 0 when (op & 1) == 1.
     * CJ jumps when AReg == 0.
     *
     * With TOK_NE (0x95, odd):
     *   inv=0 → gjmp_cond(odd) → EQC 0 + CJ → jumps when was non-zero (true)
     *   inv=1 → gjmp_cond(even) → CJ → jumps when zero (false)
     *
     * So vset_VT_CMP(TOK_NE) is correct for "non-zero = true" results. */
    case TOK_EQ:
        if (eqc_const_opt && eqc_const_val == 0 && !tp_no_eqc0()) {
            /* `x == 0` is exactly what VT_CMP(TOK_EQ) already means - "zero is
             * true" - so the value can be left raw and no instruction emitted
             * at all.  This is the mirror of the `x != 0` case below, which
             * was already deferred; the equality side was not, and it is the
             * one that pays more.
             *
             * Emitting `EQC 0` and tagging TOK_NE ("nonzero is true") costs an
             * inversion that every consumer then has to undo:
             *
             *   branch when true   EQC 0 ; EQC 0 ; CJ   ->  CJ
             *   branch when false  EQC 0 ; CJ           ->  EQC 0 ; CJ
             *   used as a value    EQC 0 ; EQC 0 ; EQC 0 ->  EQC 0
             *
             * so deferring is better or equal in every context, never worse -
             * no consumer hint is needed.  `return !x` was three EQC 0 and is
             * now one; the second operand of `a == 0 && b == 0` was two and is
             * now none.  Each EQC is a stage-4 ALU op that cannot share the
             * group of the compare feeding it, so a removed pair also lets the
             * CJ group with its operand. */
            vtop--;
            vset_VT_CMP(TOK_EQ);
        } else if (eqc_const_opt) {
            /* EQC K: single instruction, AReg = (AReg == K) ? 1 : 0.
             * Replaces LDC K + DIFF + EQC 0 (3 instrs) with 1 instr. */
            emit_instr(TP_OP_EQC, eqc_const_val);
            /* EQC produces 1 if equal → non-zero = true */
            vtop--;
            vset_VT_CMP(TOK_NE);
        } else {
            /* DIFF produces 0 when equal, nonzero otherwise.
             * vset_VT_CMP(TOK_EQ) defers normalization: gjmp_cond and
             * load() know "zero = true" and add EQC 0 only when needed. */
            Tp_DIFF();
            vtop--;
            vset_VT_CMP(TOK_EQ);
        }
        break;
    case TOK_NE:
        if (eqc_const_opt) {
            if (eqc_const_val == 0) {
                /* `x != 0` is exactly what VT_CMP(TOK_NE) already means -
                 * "nonzero is true" - so the value can be left raw.  The
                 * previous `EQC 0; EQC 0` normalised it to 1/0 and then
                 * inverted twice, costing 2 instructions and 2 stage-4 slots
                 * for nothing.  Both consumers cope: gjmp_cond keys only on
                 * op & 1, and load()'s VT_CMP handler re-normalises when a
                 * 0/1 value is actually needed.  Fires on every `if (p)`,
                 * `while (n)`, `if (x != 0)`. */
            } else {
                /* EQC K gives 1 if equal: "zero is true" for x != K.  As
                 * for `x == 0` above, leaving it raw and tagging TOK_EQ is
                 * better or equal for every consumer than inverting now:
                 *   branch when false  EQC K ; EQC 0 ; CJ  (unchanged)
                 *   branch when true   EQC K ; EQC 0 ; EQC 0 ; CJ  ->  EQC K ; CJ
                 *   used as a value    EQC K ; EQC 0 ; EQC 0 ; EQC 0  ->  EQC K ; EQC 0 */
                emit_instr(TP_OP_EQC, eqc_const_val);
                vtop--;
                vset_VT_CMP(TOK_EQ);
                break;
            }
            vtop--;
            vset_VT_CMP(TOK_NE);
        } else {
            /* DIFF produces 0 when equal, nonzero when not equal.
             * vset_VT_CMP(TOK_NE) defers normalization to 0/1 until
             * materialization in load()'s VT_CMP handler. For branching,
             * gvtst/gjmp_cond only tests zero vs nonzero. */
            Tp_DIFF();
            vtop--;
            vset_VT_CMP(TOK_NE);
        }
        break;
    case TOK_GT:
        Tp_GT();                   /* 1 if a > b, 0 otherwise */
        vtop--;
        vset_VT_CMP(cmp_zero_true ? TOK_EQ : TOK_NE);
        break;
    case TOK_LT:
        Tp_REV();
        Tp_GT();                   /* 1 if a < b (swapped), 0 otherwise */
        vtop--;
        vset_VT_CMP(TOK_NE);
        break;
    case TOK_GE:
        /* a >= b  ===  !(a < b)  ===  !(b > a)
         * REV so GT tests b > a (= a < b).  The result is then inverted for
         * free by deferring with VT_CMP(TOK_EQ), i.e. "zero is true" - the
         * same mechanism TOK_EQ's DIFF path above uses.  The explicit
         * `EQC 0` that used to do the inversion is pure waste. */
        Tp_REV();
        Tp_GT();                   /* 1 when a < b, so 0 means a >= b */
        vtop--;
        vset_VT_CMP(TOK_EQ);
        break;
    case TOK_LE:
        /* a <= b  ===  !(a > b).  Same deferred inversion as TOK_GE. */
        Tp_GT();                   /* 1 when a > b, so 0 means a <= b */
        vtop--;
        vset_VT_CMP(TOK_EQ);
        break;



    case TOK_NEG: Tp_OPR(TP_OPR_NOT); emit_instr(TP_OP_ADC, 1); break;  /* ~x + 1 = -x (two's complement) */
    case '~': Tp_OPR(TP_OPR_NOT); break;  /* Unary - no vtop-- */

    /* Multi-precision arithmetic (64-bit operations decomposed by gen_opl)
     *
     * Entry state after gv2 + tp_push_sv: AReg=R (vtop[0]), BReg=L (vtop[-1])
     *
     * LMUL semantics: result = BReg * AReg + CReg → AReg=low32, BReg=high32
     * LSUM semantics: AReg = BReg + AReg + (CReg & 1), BReg = carry_out
     * LDIFF semantics: AReg = BReg - AReg - (CReg & 1), BReg = borrow_out
     *
     * Key insight: CReg must be explicitly set up because gv2/load between
     * ADDC1 and ADDC2 clobbers the entire eval stack. We save carry/borrow
     * to FRAME_SLOT_CARRY and restore it in the C2 operation.
     */
    case TOK_UMULL:
        /* Unsigned 32x32→64 multiply. CReg already primed with 0 in preamble.
         * Entry: AReg=R, BReg=L, CReg=0. LMUL: result = BReg*AReg+CReg. */
        Tp_OPR(TP_OPR_LMUL);         /* AReg=low(L*R), BReg=high(L*R) */
        Tp_REV();                     /* AReg=high, BReg=low */
        Tp_STL(REG_IRE2);            /* save high word; AReg←low */
        abcreg_pop(1);
        vtop--;
        vtop->r2 = REG_IRE2;         /* tell TCC where high word is */
        break;

    case TOK_ADDC1:
        /* Add low halves, generate carry. CReg already primed with 0.
         * Entry: AReg=R_lo, BReg=L_lo, CReg=0. */
        Tp_OPR(TP_OPR_LSUM);         /* AReg=sum_lo, BReg=carry_out */
        Tp_REV();                     /* AReg=carry, BReg=sum_lo */
        Tp_STL(FRAME_SLOT_CARRY);    /* save carry; AReg←sum_lo */
        abcreg_pop(1);
        vtop--;
        break;

    case TOK_ADDC2:
        /* Add high halves with saved carry. CReg primed with carry in preamble.
         * Entry: AReg=R_hi, BReg=L_hi, CReg=carry. */
        Tp_OPR(TP_OPR_LSUM);         /* AReg=sum_hi, BReg=carry_out (discarded) */
        abcreg_pop(1);
        vtop--;
        break;

    case TOK_SUBC1:
        /* Subtract low halves, generate borrow. CReg already primed with 0.
         * Entry: AReg=R_lo, BReg=L_lo, CReg=0. */
        Tp_OPR(TP_OPR_LDIFF);        /* AReg=diff_lo, BReg=borrow_out */
        Tp_REV();                     /* AReg=borrow, BReg=diff_lo */
        Tp_STL(FRAME_SLOT_CARRY);    /* save borrow; AReg←diff_lo */
        abcreg_pop(1);
        vtop--;
        break;

    case TOK_SUBC2:
        /* Subtract high halves with saved borrow. CReg primed with borrow in preamble.
         * Entry: AReg=R_hi, BReg=L_hi, CReg=borrow. */
        Tp_OPR(TP_OPR_LDIFF);        /* AReg=diff_hi, BReg=borrow_out (discarded) */
        abcreg_pop(1);
        vtop--;
        break;

    /* Unsigned comparisons - XOR both operands with MinInt (0x80000000)
     * to convert unsigned ordering to signed ordering, then use GT.
     *
     * Entry state: AReg = right (vtop[0]), BReg = left (vtop[-1])
     * GT semantics: AReg = (BReg > AReg) ? 1 : 0  (signed comparison)
     *
     * MINT (OPR 0x42) loads 0x80000000 in a single byte — much more
     * efficient than LDC 0x80000000 which needs 8 prefix bytes.
     *
     * Core XOR-both-operands sequence (from entry AReg=R, BReg=L):
     *   MINT    → AReg=MINT, BReg=R, CReg=L
     *   XOR     → AReg=R^MINT, BReg=L        (XOR pops CReg→BReg)
     *   REV     → AReg=L, BReg=R^MINT
     *   MINT    → AReg=MINT, BReg=L, CReg=R^MINT
     *   XOR     → AReg=L^MINT, BReg=R^MINT   (XOR pops CReg→BReg)
     *   (no REV)
     *   GT      → (R^MINT > L^MINT) = unsigned(R > L) = unsigned(L < R)
     *
     * With final REV before GT:
     *   REV     → AReg=R^MINT, BReg=L^MINT
     *   GT      → (L^MINT > R^MINT) = unsigned(L > R)
     */
    case TOK_ULT:
        /* unsigned left < right: REV + GTU → (right >u left) */
        tp_emit_gtu(1);
        vtop--;
        vset_VT_CMP(TOK_NE);
        break;

    case TOK_UGE:
        /* unsigned left >= right = !(left < right) = !(right >u left).
         * REV + GTU yields 1 when left < right; defer the invert via
         * VT_CMP(TOK_EQ) ("zero is true"), matching signed TOK_GE.
         * Eager EQC 0 here + load()'s odd-op double-EQC 0 used to emit
         * three EQC 0 bytes for `return a + b <= c` (seq_buf_can_fit). */
        tp_emit_gtu(1);
        vtop--;
        vset_VT_CMP(TOK_EQ);
        break;

    case TOK_UGT:
        /* unsigned left > right: GTU → (BReg >u AReg) = (left >u right) */
        tp_emit_gtu(0);
        vtop--;
        vset_VT_CMP(cmp_zero_true ? TOK_EQ : TOK_NE);
        break;

    case TOK_ULE:
        /* unsigned left <= right = !(left >u right). Same deferred invert
         * as TOK_LE / TOK_UGE: GTU is 1 when left > right, so 0 means <=.
         * ISM App B: gtu = secondary 0x5F (25 FF). Never emit non-IMS OPR. */
        tp_emit_gtu(0);
        vtop--;
        vset_VT_CMP(TOK_EQ);
        break;

    default: tcc_error("unsupported integer op 0x%x", op);
    }

    /* After gen_opi, the result is in AReg. For non-comparison operations,
     * store it back to the target workspace slot so later LDL reads the
     * correct value. Comparisons use vset_VT_CMP which sets vtop->r to
     * VT_CMP (>= VT_CONST), so the check below naturally skips them. */
    {
        int result_v = vtop->r & VT_VALMASK;
        if (tp_genopi_debug) {
            fprintf(stderr, "[GEN_OPI_RESULT] func=%s vtop->r=0x%x result_v=0x%x %s ind=0x%x\n",
                    funcname ? funcname : "?", vtop->r, result_v,
                    result_v < VT_CONST ? "→ STL" : "→ skip(VT_CMP)", ind);
        }
        if (result_v < VT_CONST) {
            Tp_STL(result_v);
        }
    }
}

/* 64-bit shift using native LSHL/LSHR instructions.
 *
 * Called from tccgen.c gen_opl() for variable-count 64-bit shifts.
 * Entry vstack: [..., 64-bit_value (VT_LLONG), shift_count (VT_INT)]
 *
 * Transputer LSHL/LSHR semantics:
 *   Input:  AReg = shift count, BReg = lo word, CReg = hi word
 *   Output: AReg = result lo word, BReg = result hi word
 *
 * For SAR (arithmetic right shift), LSHR is logical so we call the
 * __ashrdi3 runtime helper (SAR on signed long long is uncommon in
 * kernel code; most 64-bit values are unsigned).
 */
ST_FUNC void gen_opl_shift(int op)
{
    int t, shift_slot, lo_slot, hi_slot;

    t = vtop[-1].type.t;  /* type of the 64-bit value */

    if (op == TOK_SAR) {
        /* Arithmetic right shift: LSHR is logical only.
         * Fall back to __ashrdi3 runtime helper. */
        int func = TOK___ashrdi3;
        vpush_helper_func(func);
        vrott(3);
        gfunc_call(2);
        vpushi(0);
        vtop->r = REG_IRET;
        vtop->r2 = REG_IRE2;
        return;
    }

    /* Step 1: Swap so 64-bit value is on top, shift below */
    vswap();
    /* vstack: [..., shift_count, 64-bit_value] */

    /* Step 2: Expand 64-bit value to lo, hi */
    lexpand();
    /* vstack: [..., shift_count, lo (VT_INT), hi (VT_INT)] */

    /* Step 3: Rotate shift to top: shift lo hi → lo hi shift */
    vrotb(3);
    /* vstack: [..., lo, hi, shift_count] */

    /* Materialize shift count into a workspace slot, save to scratch */
    gv(RC_INT);
    shift_slot = vtop->r & VT_VALMASK;

    /* Clamp shift count to 0-63: TCWG warns that shift > 2*wordlength
     * can lock the processor for a significant time. */
    abcreg_stack_depth = 0;
    Tp_LDL(shift_slot);
    Tp_LDC(63);
    Tp_AND();
    Tp_STL(FRAME_SLOT_SCRATCH2);
    shift_slot = FRAME_SLOT_SCRATCH2;
    vpop();

    /* vstack: [..., lo, hi] */

    /* Materialize both halves into workspace slots */
    gv2(RC_INT, RC_INT);
    hi_slot = vtop[0].r & VT_VALMASK;
    lo_slot = vtop[-1].r & VT_VALMASK;

    /* Reset eval stack tracking — we'll manually manage it */
    abcreg_stack_depth = 0;

    /* SHL or SHR: use native LSHL / LSHR
     *
     * Push order onto eval stack (last push = AReg):
     *   1. LDL hi_slot   → goes to CReg after subsequent pushes
     *   2. LDL lo_slot   → goes to BReg after subsequent push
     *   3. LDL shift_slot → stays in AReg
     *
     * LSHL/LSHR: CReg=hi, BReg=lo, AReg=shift
     * Result: AReg=result_lo, BReg=result_hi
     */
    Tp_LDL(hi_slot);
    Tp_LDL(lo_slot);
    Tp_LDL(shift_slot);

    if (op == TOK_SHL)
        Tp_OPR_BINARY(TP_OPR_LSHL);  /* 3 in → 2 out, net -1 like binary */
    else
        Tp_OPR_BINARY(TP_OPR_LSHR);

    /* After LSHL/LSHR: AReg=result_lo, BReg=result_hi
     * LSHL/LSHR consume all 3 eval stack entries, produce 2.
     * Store results back to workspace slots. */
    Tp_STL(lo_slot);   /* STL stores AReg=result_lo; AReg←BReg=result_hi */
    Tp_STL(hi_slot);   /* STL stores AReg=result_hi (was BReg) */

    /* The vstack still has [..., lo, hi] from lexpand + gv2.
     * lo_slot and hi_slot now contain the shifted values.
     * Rebuild the 64-bit result (inline lbuild). */
    gv2(RC_INT, RC_INT);
    vtop[-1].r2 = vtop[0].r;
    vtop[-1].type.t = t;
    vpop();
}

/* Compare FBreg (the first operand) with FAreg (the second), leaving the C
 * truth value in AReg; with a NaN operand every relation but != is false
 * (C99 F.3).
 *
 * T9000: fpeq, fpgt and fpge are the IEEE relations and are false for a
 * NaN (ISM 11.9.1), so a <= b is b >= a, not !(a > b).
 * T800: fpgt and fpeq compare on a total ordering in which a NaN is just a
 * large magnitude (ACWG B.2.7), so each relation is masked with fpordered,
 * which pushes "neither is a NaN" and leaves the FPU stack in place. */
static void tp_emit_fp_compare(int op)
{
    int t800 = !tp_have_t9000_feat(TP_T9K_FPU);
    int invert = 0;

    if (t800)
        emit_instr(TP_OP_OPR, TP_OPR_FPORDERED);
    switch (op) {
    case TOK_NE:                                /* !(ordered && a == b) */
        invert = 1;
        /* fall through */
    case TOK_EQ:
        emit_instr(TP_OP_OPR, TP_FPU_EQ);
        break;
    case TOK_GT:
        emit_instr(TP_OP_OPR, TP_FPU_GT);
        break;
    case TOK_LT:                                /* b > a */
        emit_instr(TP_OP_OPR, TP_FPU_REV);
        emit_instr(TP_OP_OPR, TP_FPU_GT);
        break;
    case TOK_GE:
    case TOK_LE:
        if (!t800) {
            if (op == TOK_LE)                   /* b >= a */
                emit_instr(TP_OP_OPR, TP_FPU_REV);
            emit_instr(TP_OP_OPR, TP_FPU_GE);
        } else {
            if (op == TOK_GE)                   /* !(b > a) */
                emit_instr(TP_OP_OPR, TP_FPU_REV);
            emit_instr(TP_OP_OPR, TP_FPU_GT);   /* !(a > b) */
            emit_instr(TP_OP_EQC, 0);
        }
        break;
    }
    if (t800)
        emit_instr(TP_OP_OPR, TP_OPR_AND);
    if (invert)
        emit_instr(TP_OP_EQC, 0);
}

/* ------------------------------------------------------------------------
 * Keeping an FPU result in FA
 *
 * gen_opf stores every result to its slot, and the next operation often
 * loads it straight back: `(a[1]*b[1]) + (a[2]*b[2])` went
 * `... fpldnlmulsn; ldlp 5; fpstnlsn; ldlp 4; fpldnlsn; ldlp 5; fpldnladdsn`.
 * The FPU holds three values, and the occam compiler keeps whole expressions
 * on it.  This catches the common case of that: an operand of an FP operation
 * that is the value the last one stored, with only integer instructions -
 * address arithmetic for the other operand - emitted since.  The store is
 * taken back, those instructions are emitted again, and the value is used
 * where it still is, in FA.
 *
 * Sound because: nothing in between touches the FPU or the slot (checked
 * instruction by instruction in the history window); the operand is the only
 * vstack entry naming the slot, and the operation consumes it, so nothing
 * reads the slot later; and the window holds no branch target or raw bytes
 * (gsym_addr clears the history, and jumps and relocations break its
 * contiguity, which is checked). */

/* Does a direct workspace instruction touch [base, base + words)?
 * LDLP may feed a double-word access, so conservatively count two slots.
 * At int limits the original callers' +/- 1 tests could overflow: refuse
 * to move the instruction instead of relying on signed wraparound. */
static int tp_hist_workspace_overlap(const struct tp_hist_ent *h,
                                     int base, int words)
{
    int64_t start = h->val, first = base;
    if (first <= INT32_MIN || first >= INT32_MAX)
        return 1;
    return start <= first + words - 1 &&
           start + (h->opc == TP_OP_LDLP) >= first;
}

/* May h stand between the store and its consumer?  An integer instruction
 * that neither reads nor writes slot r (or r + 1, a double's pair). */
static int tp_hist_fpu_neutral(const struct tp_hist_ent *h, int r)
{
    if (h->kind == TP_HIST_LOCAL)
        return h->opc == TP_OP_LDL || h->opc == TP_OP_LDLP ||
               h->opc == TP_OP_STL;
    switch (h->opc) {
    case TP_OP_LDL:
    case TP_OP_STL:
    case TP_OP_LDLP:
        return !tp_hist_workspace_overlap(h, r, 2);
    case TP_OP_LDC:
    case TP_OP_ADC:
    case TP_OP_LDNL:
    case TP_OP_LDNLP:
        return 1;
    case TP_OP_OPR:
        switch (h->val) {
        case TP_OPR_REV: case TP_OPR_LB: case TP_OPR_BSUB: case TP_OPR_WSUB:
        case TP_OPR_WSUBDB: case TP_OPR_SUM: case TP_OPR_DIFF:
        case TP_OPR_PROD: case TP_OPR_AND: case TP_OPR_OR: case TP_OPR_XOR:
        case TP_OPR_NOT: case TP_OPR_SHL: case TP_OPR_SHR: case TP_OPR_MINT:
        case TP_OPR_DUP:
            return 1;
        }
        return 0;
    }
    return 0;
}

/* sv is about to be consumed by an FP operation of precision is_db.  If it
 * is the value the last FP operation stored and the store can be taken back,
 * take it back and return 1: the value is in FA, under nothing. */
static int tp_fpu_take_resident(SValue *sv, int is_db)
{
    struct tp_hist_ent after[TP_HIST_N];
    int r = sv->r & VT_VALMASK;
    int k, i, n;

    if (nocode_wanted || tp_fpr_reg < 0 || r != tp_fpr_reg ||
        (sv->r & (VT_LVAL | VT_SYM)) || tp_fpr_db != is_db ||
        tp_fpr_off < func_start_ind || tp_hist_n == 0 ||
        tp_hist[0].end != ind)
        return 0;
    if (tp_reg_live_on_vstack(r, sv) ||
        (is_db && tp_reg_live_on_vstack(r + 1, sv)))
        return 0;
    for (k = 1; k < tp_hist_n; k++)
        if (tp_hist[k].off == tp_fpr_off)
            break;
    if (k >= tp_hist_n ||
        tp_hist[k].kind != TP_HIST_INSTR || tp_hist[k].opc != TP_OP_LDLP ||
        tp_hist[k].val != r ||
        tp_hist[k - 1].kind != TP_HIST_INSTR ||
        tp_hist[k - 1].opc != TP_OP_OPR ||
        tp_hist[k - 1].val != (is_db ? TP_FPU_STNLDB : TP_FPU_STNLSN) ||
        tp_hist[k - 1].end != tp_fpr_end)
        return 0;
    for (i = k; i > 0; i--)
        if (tp_hist[i].end != tp_hist[i - 1].off)
            return 0;
    for (i = k - 2; i >= 0; i--)
        if (!tp_hist_fpu_neutral(&tp_hist[i], r))
            return 0;

    n = k - 1;
    for (i = 0; i < n; i++)
        after[i] = tp_hist[k - 2 - i];          /* oldest first */
    ind = tp_fpr_off;
    cur_text_section->data_offset = ind;
    tp_drop_local_patches_from(ind);
    tp_hist_rollback(ind);
    tp_stl_track_clear();
    tp_areg_invalidate();
    tp_fpr_reg = -1;
    for (i = 0; i < n; i++) {
        if (after[i].kind == TP_HIST_LOCAL)
            emit_local_op(after[i].opc, after[i].val);
        else
            emit_instr(after[i].opc, after[i].val);
    }
    return 1;
}

/* ------------------------------------------------------------------------
 * Dead parking, cleaned up at the end of a statement
 *
 * A value TinyCC computes is parked in a workspace slot (always-STL) so the
 * vstack can name it, and most are loaded straight back.  When vpop()
 * discards one - an expression statement's value, typically an
 * assignment's - the slot has no reader left, and the code since the last
 * label is still in the history window.  At the end of an expression
 * statement, once that value has been popped, the window is cleaned of
 * what is now dead:
 *
 *   stl x; ldl x          -> (nothing)        the value rides in AReg
 *   <push>; stl x         -> (nothing)        x is never read: `i++`'s copy
 *   ldlp x; fpstnlsn; ldl x; stl y      -> ldlp y; fpstnlsn
 *   ldlp x; fpstnlsn; ldl x; <addr>; stnl k -> <addr>; [ldnlp k;] fpstnlsn
 *
 * each when the value parked in x has no other reader: nothing in the
 * window reads x before x is written again, and either it is written again
 * or no vstack entry names x.  Every rule leaves A/B/C (and the FPU stack)
 * exactly as it found them, so the code after it is replayed unchanged, and
 * removing one pair can expose another (`i = 0;` is `ldc 0; stl 9; ldl 9;
 * stl i`, `i++;` `ldl i; stl 9; ldl 9; stl 10; ldl 9; adc 1; stl i`), so
 * the rules run until nothing changes.  TinyCC keeps no register value
 * across a label, and the window starts after the last one (gsym_addr
 * clears it), so no later code can read a slot no vstack entry names.
 */

/* What may stand in the window: instructions whose position does not
 * matter to them (no jump, call, ldpi or lend) and whose slot traffic is
 * all visible (no other kinds of local access). */
static int tp_ent_movable(const struct tp_hist_ent *h)
{
    if (h->kind == TP_HIST_LOCAL)
        return h->opc == TP_OP_LDL || h->opc == TP_OP_LDLP ||
               h->opc == TP_OP_STL;
    switch (h->opc) {
    case TP_OP_LDL: case TP_OP_STL: case TP_OP_LDLP: case TP_OP_LDC:
    case TP_OP_ADC: case TP_OP_LDNL: case TP_OP_LDNLP: case TP_OP_STNL:
    case TP_OP_EQC:
        return 1;
    case TP_OP_OPR:
        switch (h->val) {
        case TP_OPR_GCALL: case TP_OPR_LDPI: case TP_OPR_RET:
        case TP_OPR_LEND:
            return 0;
        }
        return 1;
    }
    return 0;
}

/* `ldlp y` may be followed by a double access, which reads or writes y and
 * y + 1: count it as touching both. */
static int tp_ent_reads(const struct tp_hist_ent *h, int x)
{
    if (h->kind != TP_HIST_INSTR)
        return 0;
    if (h->opc == TP_OP_LDL)
        return h->val == x;
    return h->opc == TP_OP_LDLP && (h->val == x || h->val == x - 1);
}

static int tp_ent_writes(const struct tp_hist_ent *h, int x)
{
    return h->kind == TP_HIST_INSTR && h->opc == TP_OP_STL && h->val == x;
}

/* A push with no other effect: removing it with the store it feeds leaves
 * the stacks as they were. */
static int tp_ent_pure_push(const struct tp_hist_ent *h)
{
    if (h->kind == TP_HIST_LOCAL)
        return h->opc == TP_OP_LDL || h->opc == TP_OP_LDLP;
    if (h->opc == TP_OP_LDL || h->opc == TP_OP_LDLP || h->opc == TP_OP_LDC)
        return 1;
    return h->opc == TP_OP_OPR && h->val == TP_OPR_MINT;
}

static int tp_ent_is_fpu(const struct tp_hist_ent *h)
{
    return h->kind == TP_HIST_INSTR && h->opc == TP_OP_OPR &&
           !tp_hist_fpu_neutral(h, -2) && h->val != TP_OPR_SB &&
           h->val != TP_OPR_SS && h->val != TP_OPR_GT &&
           h->val != TP_OPR_ADD && h->val != TP_OPR_SUB;
}

#define TP_HIST_FPSTORE 2   /* (local list only) `[ldnlp val;] fpstnlsn` */

/* Is the value the entry at w[i] parks in slot x dead?  `reload` is the
 * one read allowed (the index of a reload being taken back), or -1. */
static int tp_park_dead(struct tp_hist_ent *w, const int *keep, int n,
                        int i, int x, int reload)
{
    int j;

    if (x < 0 || x >= NB_REGS ||
        !(reg_classes[x] & (RC_INT | RC_FLOAT | RC_IRET)))
        return 0;
    for (j = i + 1; j < n; j++) {
        if (!keep[j] || j == reload)
            continue;
        if (tp_ent_reads(&w[j], x))
            return 0;
        if (tp_ent_writes(&w[j], x))
            return 1;
    }
    /* Not written again in the window: dead if no vstack entry names it.
     * Only at the end of a statement does the vstack say that much -
     * mid-expression tccgen and the builtins hold slots outside it (gv()
     * loads a double's high word through a temporary it pops before naming
     * the slot; a builtin pops its parked arguments, then loads them). */
    return !tp_reg_live_on_vstack(x, NULL);
}

static int tp_next_kept(const int *keep, int n, int i)
{
    for (i++; i < n && !keep[i]; i++)
        ;
    return i;
}

/* The integer stack effect of an entry of an address computation: how many
 * values it needs on the stack and how many it leaves (net).  0 for anything
 * not modelled, which is then not part of an address. */
static int tp_addr_effect(const struct tp_hist_ent *h, int *need, int *delta)
{
    *need = 0;
    *delta = 1;
    if (h->kind == TP_HIST_LOCAL)
        return h->opc == TP_OP_LDL || h->opc == TP_OP_LDLP;
    if (h->kind != TP_HIST_INSTR)
        return 0;
    switch (h->opc) {
    case TP_OP_LDL: case TP_OP_LDLP: case TP_OP_LDC:
        return 1;
    case TP_OP_ADC: case TP_OP_LDNL: case TP_OP_LDNLP: case TP_OP_EQC:
        *need = 1; *delta = 0;
        return 1;
    case TP_OP_OPR:
        switch (h->val) {
        case TP_OPR_MINT:
            return 1;
        case TP_OPR_NOT: case TP_OPR_LB:
            *need = 1; *delta = 0;
            return 1;
        case TP_OPR_SUM: case TP_OPR_DIFF: case TP_OPR_PROD: case TP_OPR_AND:
        case TP_OPR_OR: case TP_OPR_XOR: case TP_OPR_BSUB: case TP_OPR_WSUB:
        case TP_OPR_WSUBDB: case TP_OPR_SHL: case TP_OPR_SHR:
            *need = 2; *delta = -1;
            return 1;
        }
        return 0;
    }
    return 0;
}

/* A statement-ending double assignment can write FA straight to its local.
 * Accept only the exact low/high copy, optionally preceded by gv() moving
 * the high word to another float slot.  No address evaluation or other
 * operation may intervene, and no vstack value may still need either copy. */
static int tp_stmt_double_store(struct tp_hist_ent *w, int *keep, int *mod,
                                int n, int a, int b)
{
    int q[6], count = 0, i, x, hi, first, dest;
    Sym *s;

    if (w[a].kind != TP_HIST_INSTR || w[a].opc != TP_OP_LDLP ||
        w[b].kind != TP_HIST_INSTR || w[b].opc != TP_OP_OPR ||
        w[b].val != TP_FPU_STNLDB)
        return 0;
    x = w[a].val;
    if (x < TREG_FA || x > TREG_FC ||
        tp_reg_live_on_vstack(x, NULL) || tp_reg_live_on_vstack(x + 1, NULL))
        return 0;
    for (i = tp_next_kept(keep, n, b); i < n; i = tp_next_kept(keep, n, i)) {
        if (count == 6)
            return 0;
        q[count++] = i;
    }
    if (count != 4 && count != 6)
        return 0;
    first = count - 4;
    hi = x + 1;
    if (first) {
        if (w[q[0]].kind != TP_HIST_INSTR || w[q[0]].opc != TP_OP_LDL ||
            w[q[0]].val != hi || w[q[1]].kind != TP_HIST_INSTR ||
            w[q[1]].opc != TP_OP_STL)
            return 0;
        hi = w[q[1]].val;
        if (hi < TREG_FA || hi > TREG_FC || hi == x || hi == x + 1 ||
            tp_reg_live_on_vstack(hi, NULL))
            return 0;
    }
    if (w[q[first]].kind != TP_HIST_INSTR ||
        w[q[first]].opc != TP_OP_LDL || w[q[first]].val != x ||
        w[q[first + 1]].kind != TP_HIST_LOCAL ||
        w[q[first + 1]].opc != TP_OP_STL ||
        w[q[first + 2]].kind != TP_HIST_INSTR ||
        w[q[first + 2]].opc != TP_OP_LDL || w[q[first + 2]].val != hi ||
        w[q[first + 3]].kind != TP_HIST_LOCAL ||
        w[q[first + 3]].opc != TP_OP_STL)
        return 0;
    dest = w[q[first + 1]].val;
    if ((dest & 3) || (int64_t)dest + 4 != w[q[first + 3]].val)
        return 0;
    /* The original symbol, rather than a cast's type, proves these writes
     * nonvolatile.  Exclude escaped locals and anonymous spill temporaries. */
    for (s = local_stack; s; s = s->prev) {
        int bt = s->type.t & VT_BTYPE;
        if ((s->r & (VT_VALMASK | VT_LVAL)) == (VT_LOCAL | VT_LVAL) &&
            s->c == dest && (bt == VT_DOUBLE || bt == VT_LDOUBLE) &&
            !(s->type.t & (VT_VOLATILE | VT_BITFIELD)) && !s->a.addrtaken)
            break;
    }
    if (!s)
        return 0;
    w[a].kind = TP_HIST_LOCAL;
    w[a].val = dest;
    mod[a] = 1;
    for (i = 0; i < count; i++)
        keep[q[i]] = 0;
    return 1;
}

/* The current pc is a label that a jump emitted later may reach (a loop
 * head, a `continue' target, a case label: tccgen's gind()).  gsym_addr()
 * clears the window when it resolves a forward jump; a backward target is
 * resolved before the jump exists, so it says so here.  Without this a
 * statement-end cleanup could roll back across the label and move code
 * from before it to after it, or the reverse, and the stl tracking could
 * cancel an `stl' that the path arriving by the jump needs. */
ST_FUNC void tp_label_here(void)
{
    tp_hist_clear();
    tp_stl_track_clear();
    tp_areg_invalidate();
    tp_fpr_reg = -1;
}

/* An expression statement, or a for loop's init or step, is complete:
 * clean its parking out of the window. */
ST_FUNC void tp_stmt_end(void)
{
    struct tp_hist_ent w[TP_HIST_N];   /* the window, oldest first */
    int keep[TP_HIST_N], mod[TP_HIST_N];
    int n, i, j, a, b, c, d, first, changed, any = 0;

    static int off = -1;

    if (off < 0) {
        const char *e = getenv("TCC_TRANSPUTER_VPOP_DEAD");
        off = e && *e == '0';           /* =0 for an A/B run */
    }
    if (off || nocode_wanted || func_uses_inline_asm || tp_hist_n == 0 ||
        tp_hist[0].end != ind)
        return;
    {
        /* TCC_TRANSPUTER_VPOP_ONLY=name: only in functions whose name
         * contains it - for bisecting a miscompile */
        const char *only = getenv("TCC_TRANSPUTER_VPOP_ONLY");

        if (only && *only && (!funcname || !strstr(funcname, only)))
            return;
    }
    /* the window: the newest contiguous run of movable instructions */
    for (n = 0; n < tp_hist_n; n++) {
        if (!tp_ent_movable(&tp_hist[n]) || tp_hist[n].off < func_start_ind)
            break;
        if (n > 0 && tp_hist[n].end != tp_hist[n - 1].off)
            break;
    }
    if (n < 2)
        return;
    for (i = 0; i < n; i++) {
        w[i] = tp_hist[n - 1 - i];
        keep[i] = 1;
        mod[i] = 0;
    }
    do {
        changed = 0;
        for (a = 0; a < n; a = tp_next_kept(keep, n, a)) {
            if (!keep[a])
                continue;
            b = tp_next_kept(keep, n, a);
            if (b >= n)
                break;
            /* stl x; ldl x */
            if (w[a].kind == TP_HIST_INSTR && w[a].opc == TP_OP_STL &&
                w[b].kind == TP_HIST_INSTR && w[b].opc == TP_OP_LDL &&
                w[b].val == w[a].val &&
                tp_park_dead(w, keep, n, a, w[a].val, b)) {
                keep[a] = keep[b] = 0;
                changed = 1;
                continue;
            }
            /* <push>; stl x, x never read */
            if (tp_ent_pure_push(&w[a]) &&
                w[b].kind == TP_HIST_INSTR && w[b].opc == TP_OP_STL &&
                tp_park_dead(w, keep, n, b, w[b].val, -1)) {
                keep[a] = keep[b] = 0;
                changed = 1;
                continue;
            }
            if (tp_stmt_double_store(w, keep, mod, n, a, b)) {
                changed = 1;
                continue;
            }
            /* ldlp x; fpstnlsn; ldl x; <store of A> */
            if (w[a].kind == TP_HIST_INSTR && w[a].opc == TP_OP_LDLP &&
                w[b].kind == TP_HIST_INSTR && w[b].opc == TP_OP_OPR &&
                w[b].val == TP_FPU_STNLSN) {
                int x = w[a].val;

                c = tp_next_kept(keep, n, b);
                if (c >= n || !(w[c].kind == TP_HIST_INSTR &&
                                w[c].opc == TP_OP_LDL && w[c].val == x))
                    continue;
                /* the store: a local's stl, or an address and stnl.  The
                 * address is computed from what it pushes itself above the
                 * loaded x - never consuming x, which stnl must find under
                 * it - so nothing depends on the stack's shape below, and
                 * not on the FPU.  `x = -(a + b)` parks the sum and flips
                 * its sign in A with `ldl x; mint; xor`: that pops x, and
                 * taking it for address code lost the negation. */
                {
                    int depth = 0, need, delta;

                    for (d = tp_next_kept(keep, n, c); d < n;
                         d = tp_next_kept(keep, n, d)) {
                        if (w[d].kind == TP_HIST_LOCAL &&
                            w[d].opc == TP_OP_STL)
                            break;
                        if (w[d].kind == TP_HIST_INSTR &&
                            w[d].opc == TP_OP_STNL) {
                            if (depth != 1)
                                d = n;
                            break;
                        }
                        if (tp_ent_reads(&w[d], x) || tp_ent_is_fpu(&w[d]) ||
                            !tp_addr_effect(&w[d], &need, &delta) ||
                            need > depth) {
                            d = n;
                            break;
                        }
                        depth += delta;
                    }
                }
                if (d >= n ||
                    (w[d].kind == TP_HIST_LOCAL &&
                     d != tp_next_kept(keep, n, c)) ||
                    !tp_park_dead(w, keep, n, b, x, c))
                    continue;
                keep[c] = 0;
                if (w[d].kind == TP_HIST_LOCAL) {
                    /* ldlp y; fpstnlsn */
                    w[a].kind = TP_HIST_LOCAL;
                    w[a].opc = TP_OP_LDLP;
                    w[a].val = w[d].val;
                    mod[a] = 1;
                    keep[d] = 0;
                } else {
                    keep[a] = keep[b] = 0;
                    w[d].kind = TP_HIST_FPSTORE;
                    mod[d] = 1;
                }
                changed = 1;
            }
        }
        any |= changed;
    } while (changed);
    if (!any)
        return;

    for (first = 0; first < n && keep[first] && !mod[first]; first++)
        ;
    if (getenv("TCC_TRANSPUTER_VPOP_DEAD") &&
        *getenv("TCC_TRANSPUTER_VPOP_DEAD") == '2') {
        SValue *p;

        fprintf(stderr, "[VPOP_DEAD] %s @%x vstack:", funcname, w[first].off);
        for (p = vstack; p <= vtop; p++)
            fprintf(stderr, " %x/%x", p->r, p->r2);
        fprintf(stderr, "\n");
        for (j = first; j < n; j++)
            fprintf(stderr, "    %s %s opc=%x val=%d\n",
                    keep[j] ? (mod[j] ? "MOD" : "   ") : "DEL",
                    w[j].kind == TP_HIST_LOCAL ? "local" : "instr",
                    w[j].opc, w[j].val);
    }
    ind = w[first].off;
    cur_text_section->data_offset = ind;
    tp_drop_local_patches_from(ind);
    tp_hist_rollback(ind);
    tp_stl_track_clear();
    tp_areg_invalidate();
    tp_fpr_reg = -1;
    for (j = first; j < n; j++) {
        if (!keep[j])
            continue;
        if (w[j].kind == TP_HIST_LOCAL)
            emit_local_op(w[j].opc, w[j].val);
        else if (w[j].kind == TP_HIST_FPSTORE) {
            if (w[j].val)
                emit_instr(TP_OP_LDNLP, w[j].val);
            emit_instr(TP_OP_OPR, TP_FPU_STNLSN);
        } else
            emit_instr(w[j].opc, w[j].val);
    }
}

/* The FPU stack effect of an FP instruction in the history window: what it
 * does to the depth above a value parked below everything it touches; the
 * depth it needs above that value to touch only values above it; and the
 * most it allows, as the fused loads leave FC undefined (TCWG chapter 11:
 * `FCreg' = undefined`), so a value parked in FC does not survive one.
 * Pushes, pops, stores, compares and fprev carry FC along, and the unary
 * operations touch FA alone.  Returns 0 for anything not modelled (the
 * square-root and remainder sequences use the whole stack). */
static int tp_fpu_effect(const struct tp_hist_ent *w, int i, int n,
                         int *delta, int *need, int *most)
{
    int v = w[i].val;

    *delta = 0;
    *need = 0;
    *most = 2;
    switch (v) {
    case TP_FPU_LDNLSN: case TP_FPU_LDNLDB:
    case TP_OPR_FPLDNLSNI: case TP_OPR_FPLDNLDBI:
    case TP_OPR_FPLDZEROSN: case TP_OPR_FPLDZERODB:
    case TP_FPU_I32TOR32: case TP_FPU_I32TOR64: case TP_OPR_FPB32TOR64:
        *delta = 1;
        return 1;
    case TP_FPU_DUP:
        *delta = 1; *need = 1;
        return 1;
    case TP_FPU_LDNLADDSN: case TP_FPU_LDNLADDDB:
    case TP_FPU_LDNLMULSN: case TP_FPU_LDNLMULDB:
        *need = 1; *most = 1;
        return 1;
    case TP_OPR_FPINT: case TP_FPU_RTOI32:
    case TP_FPU_NAN: case TP_FPU_NOTFINITE:
        *need = 1;
        return 1;
    case TP_FPU_STNLSN: case TP_FPU_STNLDB: case TP_FPU_STNLI32:
        *delta = -1; *need = 1;
        return 1;
    case TP_FPU_ADD: case TP_FPU_SUB: case TP_FPU_MUL: case TP_FPU_DIV:
        *delta = -1; *need = 2;
        return 1;
    case TP_FPU_GT: case TP_FPU_EQ: case TP_FPU_GE:
        *delta = -2; *need = 2;
        return 1;
    case TP_FPU_REV: case TP_FPU_ORDERED:
        *need = 2;
        return 1;
    case TP_FPU_CHKERR: case TP_FPU_TESTERR:
        return 1;
    case TP_OPR_FPENTRY:
        /* the selector is the `ldc` before it */
        if (i == 0 || w[i - 1].kind != TP_HIST_INSTR ||
            w[i - 1].opc != TP_OP_LDC)
            return 0;
        switch (w[i - 1].val) {
        case TP_FPENTRY_SQRTFIRST: case TP_FPENTRY_SQRTSTEP:
        case TP_FPENTRY_SQRTLAST:
            return 0;
        case TP_FPENTRY_RP: case TP_FPENTRY_RM: case TP_FPENTRY_RZ:
        case TP_FPENTRY_URN:
        case TP_FPU_OP_SETERR & 0xFF: case TP_FPU_OP_CLRERR & 0xFF:
            return 1;
        }
        *need = 1;              /* a unary operation on FA */
        return 1;
    }
    if (tp_have_t9000_feat(TP_T9K_FPU)) {
        switch (v) {
        case TP_OPR_FPABS: case TP_OPR_FPSQRT:
        case TP_OPR_FPMULBY2: case TP_OPR_FPDIVBY2:
        case TP_OPR_FPR32TOR64: case TP_OPR_FPR64TOR32:
        case TP_OPR_FPEXPINC32: case TP_OPR_FPEXPDEC32:
            *need = 1;
            return 1;
        case TP_OPR_FPRN: case TP_OPR_FPRZ: case TP_OPR_FPRP: case TP_OPR_FPRM:
            return 1;
        }
    }
    (void)n;
    return 0;
}

/* Let L, the left operand of an FP operation whose right operand has just
 * been taken back into FA, stay on the FPU stack too: take back its store
 * if every instruction since keeps it there untouched - pushing above it
 * first, never consuming it, never more than two values above it (the
 * stack is three deep), and leaving exactly one, the right operand, above
 * it.  Then L is in FB and R in FA, and the operation needs no memory
 * operand: `a*b + c*d` is `fpldnlsn; fpldnlmulsn; fpldnlsn; fpldnlmulsn;
 * fpadd`, as occam compiles it, where it used to store the first product
 * and add it back with fpldnladdsn.  The window holds no label (a label
 * empties it), call or jump (raw bytes break its contiguity), so nothing
 * can come between the two paths.  TCC_TRANSPUTER_FPSTACK=0 turns it off. */
static int tp_fpu_take_under(SValue *sv, int is_db)
{
    struct tp_hist_ent w[TP_HIST_N];    /* the window, oldest first */
    int r = sv->r & VT_VALMASK;
    int k, i, n, d, delta, need, most;
    static int off = -1;

    if (off < 0) {
        const char *e = getenv("TCC_TRANSPUTER_FPSTACK");
        off = e && *e == '0';
    }
    if (off || nocode_wanted || (sv->r & (VT_LVAL | VT_SYM)) ||
        r >= VT_CONST || !(reg_classes[r] & RC_FLOAT) ||
        tp_hist_n == 0 || tp_hist[0].end != ind)
        return 0;
    {
        /* TCC_TRANSPUTER_FPSTACK_ONLY=name: only in functions whose name
         * contains it - for bisecting a miscompile */
        const char *only = getenv("TCC_TRANSPUTER_FPSTACK_ONLY");

        if (only && *only && (!funcname || !strstr(funcname, only)))
            return 0;
    }
    if (tp_reg_live_on_vstack(r, sv) ||
        (is_db && tp_reg_live_on_vstack(r + 1, sv)))
        return 0;
    /* L's store, the newest write of its slot: `ldlp r; fpstnl` */
    for (k = 1; k < tp_hist_n; k++) {
        if (tp_hist[k].end != tp_hist[k - 1].off ||
            tp_hist[k].off < func_start_ind)
            return 0;
        if (tp_hist[k].kind == TP_HIST_INSTR &&
            tp_hist[k].opc == TP_OP_LDLP && tp_hist[k].val == r &&
            tp_hist[k - 1].kind == TP_HIST_INSTR &&
            tp_hist[k - 1].opc == TP_OP_OPR &&
            tp_hist[k - 1].val == (is_db ? TP_FPU_STNLDB : TP_FPU_STNLSN))
            break;
    }
    if (k >= tp_hist_n || k < 2)
        return 0;
    n = k - 1;
    for (i = 0; i < n; i++)
        w[i] = tp_hist[k - 2 - i];
    d = 0;
    for (i = 0; i < n; i++) {
        if (w[i].kind == TP_HIST_LOCAL)
            continue;
        if (w[i].opc == TP_OP_OPR &&
            tp_fpu_effect(w, i, n, &delta, &need, &most)) {
            if (d < need || d > most || d + delta < 0 || d + delta > 2)
                return 0;
            d += delta;
            continue;
        }
        if (!tp_hist_fpu_neutral(&w[i], r))
            return 0;
    }
    if (d != 1)
        return 0;
    if (getenv("TCC_TRANSPUTER_FPSTACK") &&
        *getenv("TCC_TRANSPUTER_FPSTACK") == '2') {
        fprintf(stderr, "[FPSTACK] %s r=%d window:", funcname, r);
        for (i = 0; i < n; i++)
            fprintf(stderr, " %s%x:%d", w[i].kind == TP_HIST_LOCAL ? "L" : "",
                    w[i].opc, w[i].val);
        fprintf(stderr, "\n");
    }

    ind = tp_hist[k].off;
    cur_text_section->data_offset = ind;
    tp_drop_local_patches_from(ind);
    tp_hist_rollback(ind);
    tp_stl_track_clear();
    tp_areg_invalidate();
    tp_fpr_reg = -1;
    for (i = 0; i < n; i++) {
        if (w[i].kind == TP_HIST_LOCAL)
            emit_local_op(w[i].opc, w[i].val);
        else
            emit_instr(w[i].opc, w[i].val);
    }
    return 1;
}

/* May h stand between an address's park in register v (one word, not an
 * FP pair) and its use?  An integer instruction that leaves v alone. */
static int tp_hist_int_neutral(const struct tp_hist_ent *h, int v)
{
    if (h->kind == TP_HIST_LOCAL)
        return h->opc == TP_OP_LDL || h->opc == TP_OP_LDLP ||
               h->opc == TP_OP_STL;
    switch (h->opc) {
    case TP_OP_LDL:
    case TP_OP_STL:
    case TP_OP_LDLP:
        return !tp_hist_workspace_overlap(h, v, 1);
    case TP_OP_OPR:
        return tp_hist_fpu_neutral(h, -2);   /* its integer operations */
    }
    return tp_hist_fpu_neutral(h, -2);
}

/* The integer stack effect of a pure entry of an FP operand's code: what
 * it pushes, and what it needs there first.  0 for anything that stores
 * or is not modelled. */
static int tp_fpu_pure(const struct tp_hist_ent *h, int *idelta, int *ineed,
                       int *fdelta)
{
    *idelta = *ineed = *fdelta = 0;
    if (h->kind == TP_HIST_LOCAL) {
        if (h->opc != TP_OP_LDL && h->opc != TP_OP_LDLP)
            return 0;
        *idelta = 1;
        return 1;
    }
    switch (h->opc) {
    case TP_OP_LDL: case TP_OP_LDLP: case TP_OP_LDC:
        *idelta = 1;
        return 1;
    case TP_OP_LDNLP: case TP_OP_ADC: case TP_OP_LDNL:
        *ineed = 1;
        return 1;
    case TP_OP_OPR:
        switch (h->val) {
        case TP_OPR_WSUB: case TP_OPR_BSUB: case TP_OPR_WSUBDB:
        case TP_OPR_SUM: case TP_OPR_PROD:
            *idelta = -1; *ineed = 2;
            return 1;
        case TP_FPU_LDNLSN: case TP_FPU_LDNLDB:
            *idelta = -1; *ineed = 1; *fdelta = 1;
            return 1;
        case TP_OPR_FPLDNLSNI: case TP_OPR_FPLDNLDBI:
            *idelta = -2; *ineed = 2; *fdelta = 1;
            return 1;
        case TP_FPU_LDNLADDSN: case TP_FPU_LDNLADDDB:
        case TP_FPU_LDNLMULSN: case TP_FPU_LDNLMULDB:
            *idelta = -1; *ineed = 1;
            return 1;
        case TP_OPR_FPLDZEROSN: case TP_OPR_FPLDZERODB:
        case TP_FPU_DUP:        /* a square's; tp_fpu_effect checks its need */
            *fdelta = 1;
            return 1;
        case TP_FPU_ADD: case TP_FPU_MUL: case TP_FPU_SUB: case TP_FPU_DIV:
            *fdelta = -1;
            return 1;
        }
        return 0;
    }
    return 0;
}

/* The left operand of a commutative FP operation (+ or *) whose right
 * operand is in FA cannot stay under it - it would be in FC under a fused
 * load, which loses FC - but its own code can move: if the code that
 * computed it starts from empty stacks, only loads (locals, registers,
 * memory through them) and does arithmetic, and the right operand's code
 * after it stores nothing it reads and nothing to memory, emit the right
 * operand's code first and the left's on top of it.  Then the right is in
 * FB and the left in FA, and FB op FA is the same IEEE result: occam's
 * order for `a*b + (c*d + e*f)`, the deeper side first.  Returns 1 with
 * both on the stack. */
static int tp_fpu_take_moved(SValue *sv, int is_db)
{
    struct tp_hist_ent rseg[TP_HIST_N], lseg[TP_HIST_N];
    int r = sv->r & VT_VALMASK;
    int k, j, s, nr, nl, i, d, delta, need, most, fd, id, idelta, ineed, fdelta;
    int rd_reg[TP_HIST_N], rd_loc[TP_HIST_N], nrd = 0, nloc = 0;
    static int off = -1;

    if (off < 0) {
        const char *e = getenv("TCC_TRANSPUTER_FPSTACK");
        off = e && *e == '0';
    }
    if (off || nocode_wanted || (sv->r & (VT_LVAL | VT_SYM)) ||
        r >= VT_CONST || !(reg_classes[r] & RC_FLOAT) ||
        tp_hist_n == 0 || tp_hist[0].end != ind ||
        tp_reg_live_on_vstack(r, sv) ||
        (is_db && tp_reg_live_on_vstack(r + 1, sv)))
        return 0;
    {
        const char *only = getenv("TCC_TRANSPUTER_FPSTACK_ONLY");

        if (only && *only && (!funcname || !strstr(funcname, only)))
            return 0;
    }
    /* the left operand's store, `ldlp r; fpstnl`, at k and k - 1 */
    for (k = 1; k < tp_hist_n; k++) {
        if (tp_hist[k].end != tp_hist[k - 1].off ||
            tp_hist[k].off < func_start_ind)
            return 0;
        if (tp_hist[k].kind == TP_HIST_INSTR &&
            tp_hist[k].opc == TP_OP_LDLP && tp_hist[k].val == r &&
            tp_hist[k - 1].kind == TP_HIST_INSTR &&
            tp_hist[k - 1].opc == TP_OP_OPR &&
            tp_hist[k - 1].val == (is_db ? TP_FPU_STNLDB : TP_FPU_STNLSN))
            break;
    }
    if (k >= tp_hist_n || k < 2)
        return 0;
    /* its code: back from the store to where both stacks were empty */
    fd = 1;
    id = 0;
    for (s = k + 1; s < tp_hist_n; s++) {
        if (tp_hist[s].end != tp_hist[s - 1].off ||
            tp_hist[s].off < func_start_ind ||
            !tp_fpu_pure(&tp_hist[s], &idelta, &ineed, &fdelta))
            return 0;
        fd -= fdelta;
        id -= idelta;
        if (fd < 0 || id < 0 || id < ineed)
            return 0;
        if (fd == 0 && id == 0)
            break;
    }
    if (s >= tp_hist_n)
        return 0;
    nl = s - k;
    for (i = 0; i < nl; i++) {
        lseg[i] = tp_hist[s - i];                   /* oldest first */
        if (lseg[i].kind == TP_HIST_LOCAL)
            rd_loc[nloc++] = lseg[i].val;
        else if (lseg[i].opc == TP_OP_LDL || lseg[i].opc == TP_OP_LDLP)
            rd_reg[nrd++] = lseg[i].val;
    }
    /* the right operand's code: no store into what the left reads, none
     * to memory except its own parking in register slots */
    nr = k - 1;
    for (i = 0; i < nr; i++) {
        const struct tp_hist_ent *e = &tp_hist[k - 2 - i];
        int x;

        rseg[i] = *e;
        /* Moving L's producer after this segment also removes L's store.
         * An integer copy of either result word (assignment expressions
         * in particular) must still see that store before it executes. */
        if (e->kind == TP_HIST_INSTR && tp_is_direct_workspace_op(e->opc) &&
            tp_hist_workspace_overlap(e, r, is_db ? 2 : 1))
            return 0;
        if (e->kind == TP_HIST_LOCAL) {
            if (e->opc == TP_OP_STL)
                for (x = 0; x < nloc; x++)
                    if (rd_loc[x] == e->val)
                        return 0;
            continue;
        }
        if (e->opc == TP_OP_STL) {
            for (x = 0; x < nrd; x++)
                if (rd_reg[x] == e->val || rd_reg[x] + 1 == e->val)
                    return 0;
            continue;
        }
        if (e->opc == TP_OP_STNL)
            return 0;
        if (e->opc == TP_OP_OPR &&
            (e->val == TP_FPU_STNLSN || e->val == TP_FPU_STNLDB ||
             e->val == TP_FPU_STNLI32)) {
            /* only to a register slot the left does not read */
            const struct tp_hist_ent *a = i > 0 ? &rseg[i - 1] : NULL;
            if (!a || a->kind != TP_HIST_INSTR || a->opc != TP_OP_LDLP)
                return 0;
            for (x = 0; x < nrd; x++)
                if (rd_reg[x] == a->val || rd_reg[x] == a->val + 1)
                    return 0;
            continue;
        }
        if (e->opc == TP_OP_OPR &&
            (e->val == TP_OPR_SB || e->val == TP_OPR_SS ||
             e->val == TP_OPR_MOVE))
            return 0;
    }
    /* the right operand's code leaves the integer stack as it found it,
     * so the left's pushes land where they did */
    id = 0;
    for (i = 0; i < nr; i++) {
        const struct tp_hist_ent *e = &rseg[i];

        if (tp_fpu_pure(e, &idelta, &ineed, &fdelta)) {
            if (id < ineed)
                return 0;
            id += idelta;
        } else if (e->opc == TP_OP_STL ||
                   (e->kind == TP_HIST_INSTR && e->opc == TP_OP_OPR &&
                    (e->val == TP_FPU_STNLSN || e->val == TP_FPU_STNLDB ||
                     e->val == TP_FPU_STNLI32))) {
            if (id < 1)
                return 0;
            id--;
        } else
            return 0;
    }
    if (id != 0)
        return 0;
    /* the left's code on top of the right operand: the stack rules */
    d = 0;
    for (i = 0; i < nl; i++) {
        if (lseg[i].kind == TP_HIST_LOCAL || lseg[i].opc != TP_OP_OPR)
            continue;
        if (!tp_fpu_effect(lseg, i, nl, &delta, &need, &most))
            continue;           /* integer operations */
        if (d < need || d > most || d + delta < 0 || d + delta > 2)
            return 0;
        d += delta;
    }
    if (d != 1)
        return 0;
    if (getenv("TCC_TRANSPUTER_FPSTACK") &&
        *getenv("TCC_TRANSPUTER_FPSTACK") == '2')
        fprintf(stderr, "[FPSTACK] %s moved %d entries above %d\n",
                funcname, nl, nr);

    ind = tp_hist[s].off;
    cur_text_section->data_offset = ind;
    tp_drop_local_patches_from(ind);
    tp_hist_rollback(ind);
    tp_stl_track_clear();
    tp_areg_invalidate();
    tp_fpr_reg = -1;
    for (j = 0; j < 2; j++) {
        struct tp_hist_ent *w = j ? lseg : rseg;
        int n = j ? nl : nr;

        for (i = 0; i < n; i++) {
            if (w[i].kind == TP_HIST_LOCAL)
                emit_local_op(w[i].opc, w[i].val);
            else
                emit_instr(w[i].opc, w[i].val);
        }
    }
    return 1;
}

/* sv, about to be loaded onto an empty FPU stack, is a float at the address
 * in register v - an address computed and parked (`ldl a; ldnlp 1; stl 9`)
 * before the other operand's.  If only integer instructions that leave v
 * and the FPU alone came since, load through the address where it was
 * computed instead of parking it: `ldl a; ldnlp 1; fpldnlsn; ldl b; ldnlp
 * 1; fpldnlmulsn` as occam does, where it was `...; stl 9; ldl b; ldnlp 1;
 * fpldnlsn; ldl 9; fpldnlmulsn`.  Returns 1 with the value in FA. */
static int tp_fpu_load_early(SValue *sv, int is_db)
{
    struct tp_hist_ent w[TP_HIST_N];
    int v = sv->r & VT_VALMASK;
    int k, i, n;
    static int off = -1;

    if (off < 0) {
        const char *e = getenv("TCC_TRANSPUTER_FPSTACK");
        off = e && *e == '0';
    }
    /* A register lvalue is loaded from its register whatever its c.i
     * (tp_emit_float_addr), and that register is what was parked. */
    if (off || nocode_wanted || !(sv->r & VT_LVAL) || (sv->r & VT_SYM) ||
        v >= VT_CONST || tp_hist_n == 0 || tp_hist[0].end != ind ||
        tp_reg_live_on_vstack(v, sv))
        return 0;
    for (k = 0; k < tp_hist_n; k++) {
        if ((k > 0 && tp_hist[k].end != tp_hist[k - 1].off) ||
            tp_hist[k].off < func_start_ind)
            return 0;
        if (tp_hist[k].kind == TP_HIST_INSTR &&
            tp_hist[k].opc == TP_OP_STL && tp_hist[k].val == v)
            break;
        if (!tp_hist_int_neutral(&tp_hist[k], v))
            return 0;
    }
    if (k >= tp_hist_n || k == 0)
        return 0;               /* none, or the last: the plain load cancels it */
    n = k;
    for (i = 0; i < n; i++)
        w[i] = tp_hist[k - 1 - i];

    ind = tp_hist[k].off;
    cur_text_section->data_offset = ind;
    tp_drop_local_patches_from(ind);
    tp_hist_rollback(ind);
    tp_stl_track_clear();
    tp_areg_invalidate();
    tp_fpr_reg = -1;
    emit_instr(TP_OP_OPR, is_db ? TP_FPU_LDNLDB : TP_FPU_LDNLSN);
    for (i = 0; i < n; i++) {
        if (w[i].kind == TP_HIST_LOCAL)
            emit_local_op(w[i].opc, w[i].val);
        else
            emit_instr(w[i].opc, w[i].val);
    }
    return 1;
}

ST_DATA int tp_opf_volatile;

/* Does an object of type t hold, at byte off, a scalar of basic type bt that
 * is not volatile?  Through arrays (off modulo the element size) and the
 * members of structs and unions, so `p[1]`, `n.v[2]` and `q.a.x` of a
 * local or global are found as well as plain variables. */
static int tp_type_holds(CType *t, long off, int bt)
{
    CType *e;
    Sym *f;
    int size, align;

    if (off < 0 || (t->t & (VT_VOLATILE | VT_VLA)))
        return 0;
    if (t->t & VT_ARRAY) {
        e = pointed_type(t);
        size = type_size(e, &align);
        return size > 0 && tp_type_holds(e, off % size, bt);
    }
    if ((t->t & VT_BTYPE) == VT_STRUCT) {
        for (f = t->ref->next; f; f = f->next) {
            size = type_size(&f->type, &align);
            if (off >= f->c && off < f->c + size &&
                tp_type_holds(&f->type, off - f->c, bt))
                return 1;
        }
        return 0;
    }
    return off == 0 && (t->t & VT_BTYPE) == bt;
}

/* Are a and b one non-volatile variable, or one element or member of one:
 * then x * x needs one load.  The vstack types are no help with volatile -
 * tccgen's arithmetic conversion gives both operands the unqualified common
 * type before gen_opf sees them, so gen_op sets tp_opf_volatile - and an
 * identifier's vstack entry keeps its symbol, whose declaration says too:
 * a local or global read at the symbol's own place, or an element or member
 * found in its type (tp_type_holds).  Nothing reached through a pointer,
 * which has no symbol to ask. */
static int tp_fpu_same_operand(const SValue *a, const SValue *b)
{
    int v = a->r & VT_VALMASK;
    Sym *s = a->sym;
    long off;

    if (tp_opf_volatile || a->r != b->r || a->sym != b->sym ||
        a->c.i != b->c.i || !s ||
        !(a->r & VT_LVAL) || (a->r & (VT_MUSTCAST | VT_MUSTBOUND)) ||
        (s->type.t & VT_VOLATILE))
        return 0;
    if (v == VT_LOCAL)
        off = (long)a->c.i - s->c;
    else if (v == VT_CONST && (a->r & VT_SYM))
        off = (long)a->c.i;
    else
        return 0;
    return tp_type_holds(&s->type, off, a->type.t & VT_BTYPE);
}

/* The right operand of x * x through a pointer, `p->x * p->x`: its address
 * was just computed and parked (`ldl p; ldnlp 12; stl 10`), and the same
 * computation, entry for entry, sits just before the left operand's load
 * (`ldl p; ldnlp 12; fpldnlsn`), with nothing between them but that load.
 * Both compute one address from the same unchanged locals, so drop the
 * second: the caller follows with fpdup; fpmul.  Returns 1 with the left
 * operand still alone in FA. */
static int tp_fpu_square_twin(SValue *sv, int is_db)
{
    int v = sv->r & VT_VALMASK;
    int m, i, k, id, idelta, ineed, fdelta;

    if (tp_opf_volatile ||
        nocode_wanted || !(sv->r & VT_LVAL) || (sv->r & VT_SYM) ||
        v >= VT_CONST || tp_hist_n < 4 || tp_hist[0].end != ind ||
        tp_reg_live_on_vstack(v, sv) ||
        tp_hist[0].kind != TP_HIST_INSTR || tp_hist[0].opc != TP_OP_STL ||
        tp_hist[0].val != v)
        return 0;
    id = 1;                     /* the address, before its `stl` */
    for (m = 1; m < tp_hist_n; m++) {
        if (tp_hist[m].end != tp_hist[m - 1].off ||
            tp_hist[m].off < func_start_ind ||
            !tp_fpu_pure(&tp_hist[m], &idelta, &ineed, &fdelta) || fdelta ||
            (tp_hist[m].kind == TP_HIST_INSTR && tp_hist[m].opc == TP_OP_OPR))
            return 0;
        id -= idelta;
        if (id < 0 || id < ineed)
            return 0;
        if (id == 0)
            break;
    }
    i = m + 1;                  /* the left operand's load */
    if (m >= tp_hist_n || i + m >= tp_hist_n ||
        tp_hist[i].end != tp_hist[m].off ||
        tp_hist[i].kind != TP_HIST_INSTR || tp_hist[i].opc != TP_OP_OPR ||
        tp_hist[i].val != (is_db ? TP_FPU_LDNLDB : TP_FPU_LDNLSN))
        return 0;
    for (k = 1; k <= m; k++)
        if (tp_hist[i + k].end != tp_hist[i + k - 1].off ||
            tp_hist[i + k].kind != tp_hist[k].kind ||
            tp_hist[i + k].opc != tp_hist[k].opc ||
            tp_hist[i + k].val != tp_hist[k].val)
            return 0;
    ind = tp_hist[m].off;
    cur_text_section->data_offset = ind;
    tp_drop_local_patches_from(ind);
    tp_hist_rollback(ind);
    tp_stl_track_clear();
    tp_areg_invalidate();
    tp_fpr_reg = -1;
    return 1;
}

/* Can the result of an FP operation go to sv's slot?  When sv is a float
 * value the operation consumes and no other vstack entry names: then
 * get_reg need not spill a live value (`ldl 3; stl 12` and a reload) to find
 * a free one.  A double must donate its contiguous pair, with neither
 * word named by any value outside the two operands being consumed. */
static int tp_fpu_dst_reusable(SValue *sv, int is_db)
{
    int r = sv->r & VT_VALMASK;
    SValue *p;

    if ((sv->r & (VT_LVAL | VT_SYM)) || r >= VT_CONST ||
        !(reg_classes[r] & RC_FLOAT))
        return 0;
    if (is_db && (sv->r != r || sv->r2 != r + 1 || r + 1 >= NB_REGS))
        return 0;
    for (p = vstack; p < vtop - 1; p++)
        if ((p->r & VT_VALMASK) == r || (p->r2 & VT_VALMASK) == r ||
            (is_db && ((p->r & VT_VALMASK) == r + 1 ||
                       (p->r2 & VT_VALMASK) == r + 1)))
            return 0;
    return 1;
}

/* Is sv an address that was parked in its register by the very last
 * instruction?  Then loading through it first cancels that `stl`. */
static int tp_fpu_addr_just_parked(SValue *sv)
{
    int v = sv->r & VT_VALMASK;

    return (sv->r & VT_LVAL) && !(sv->r & VT_SYM) && v < VT_CONST &&
           sv->c.i >= 0 && tp_last_stl_reg == v && tp_last_stl_end == ind;
}

ST_FUNC void gen_opf(int op) {
    SValue orig_first, orig_second;
    SValue sv_first, sv_second;
    int bt;
    int is_db;
    int opf_debug = tp_opf_debug;

    /* Unary minus: flip the sign bit.  0.0 - x is not negation: it gives
     * +0.0 for x = +0.0.  Float values live in workspace slots, low word
     * first, so the sign is the top bit of the word in the last slot. */
    if (op == TOK_NEG) {
        int neg_bt = vtop->type.t & VT_BTYPE;
        int neg_db = (neg_bt == VT_DOUBLE || neg_bt == VT_LDOUBLE);
        int hi;

        gv(RC_FLOAT);
        hi = (vtop->r & VT_VALMASK) + (neg_db ? 1 : 0);
        abcreg_stack_depth = 0;
        fpureg_stack_depth = 0;
        Tp_LDL(hi);
        Tp_LDC(-0x7fffffff - 1);             /* mint */
        Tp_XOR();
        Tp_STL(hi);
        abcreg_stack_depth = 0;
        return;
    }

    if (tp_soft_float()) {
        tp_softfp_opf(op);
        return;
    }

    orig_first = vtop[-1];
    orig_second = vtop[0];
    bt = orig_first.type.t & VT_BTYPE;
    is_db = (bt == VT_DOUBLE || bt == VT_LDOUBLE);

    /* Direct-addressable operands: skip gv2() materialization entirely and
     * let the FPU load each operand straight from its home (local slot,
     * pointer register, global, or constant pool/scratch).  gv2 would copy
     * each value through a workspace slot with integer ldnl/stl first —
     * three memory operations per operand where one fpldnl does the job.
     * Covers float32 and float64 uniformly. */
    {
        int v1 = orig_first.r & VT_VALMASK;
        int v2 = orig_second.r & VT_VALMASK;
        int p1 = (v1 < VT_CONST || v1 == VT_CONST || v1 == VT_LOCAL ||
                  v1 == VT_LLOCAL) && !(orig_first.r & VT_MUSTBOUND);
        int p2 = (v2 < VT_CONST || v2 == VT_CONST || v2 == VT_LOCAL ||
                  v2 == VT_LLOCAL) && !(orig_second.r & VT_MUSTBOUND);

        if (p1 && p2 &&
            (op == '+' || op == '-' || op == '*' || op == '/')) {
            /* Allocate the result register first: get_reg may spill, and a
             * spill rewrites vstack entries, so re-read the operands from
             * the vstack afterwards (a spilled operand becomes a LOCAL /
             * LLOCAL lvalue, which stays directly addressable).  Doubles
             * are two-word values living in rdst/rdst+1 (gv()'s transputer
             * convention): evict whatever holds the pair's high slot —
             * an evicted operand spills to a local and stays pushable. */
            int comm = (op == '+' || op == '*');
            int rdst;
            int in_fa = 0;      /* 1: vtop[-1] is already in FA, 2: vtop[0],
                                   3: vtop[-1] in FB and vtop[0] in FA */

            if (tp_fpu_dst_reusable(&vtop[0], is_db))
                rdst = vtop[0].r & VT_VALMASK;
            else if (tp_fpu_dst_reusable(&vtop[-1], is_db))
                rdst = vtop[-1].r & VT_VALMASK;
            else {
                rdst = get_reg(RC_FLOAT);
                if (is_db)
                    save_reg_upstack(rdst + 1, 0);
            }
            /* x * x: one load, then fpdup; fpmul (12 cycles) as occam, where
             * a second load fused into the multiply costs 13 and its
             * address.  TCC_TRANSPUTER_FPSTACK=0 keeps two loads. */
            if (op == '*' && tp_fpu_same_operand(&vtop[-1], &vtop[0]) &&
                !(getenv("TCC_TRANSPUTER_FPSTACK") &&
                  *getenv("TCC_TRANSPUTER_FPSTACK") == '0')) {
                abcreg_stack_depth = 0;
                fpureg_stack_depth = 0;
                tp_push_float_sv(&vtop[0]);
                emit_instr(TP_OP_OPR, TP_FPU_DUP);
                emit_instr(TP_OP_OPR, TP_FPU_MUL);
                goto fp_arith_done;
            }
            /* After get_reg, whose spills rewrite the vstack. */
            if (tp_fpu_take_resident(&vtop[0], is_db)) {
                in_fa = 2;
                if (tp_fpu_take_under(&vtop[-1], is_db) ||
                    (comm && tp_fpu_take_moved(&vtop[-1], is_db)))
                    in_fa = 3;
            } else if (tp_fpu_take_resident(&vtop[-1], is_db) ||
                       tp_fpu_load_early(&vtop[-1], is_db))
                in_fa = 1;
            abcreg_stack_depth = 0;
            fpureg_stack_depth = in_fa == 3 ? 2 : in_fa ? 1 : 0;
            if (op == '*' && in_fa == 1 &&
                !(getenv("TCC_TRANSPUTER_FPSTACK") &&
                  *getenv("TCC_TRANSPUTER_FPSTACK") == '0') &&
                tp_fpu_square_twin(&vtop[0], is_db)) {
                emit_instr(TP_OP_OPR, TP_FPU_DUP);
                emit_instr(TP_OP_OPR, TP_FPU_MUL);
                goto fp_arith_done;
            }

            /* T9000 monadic idioms (ISM 11.7.2): x*2.0, 2.0*x, x/2.0,
             * x*0.5, x/0.5 and x+x all scale by an exact power of two, so
             * fpmulby2/fpdivby2 replace a constant materialization plus a
             * dyadic op with a single exponent-update instruction.
             * (Multiplying by 0.5 and dividing by 2 are the same IEEE
             * operation: scaling by a power of two is always exact apart
             * from the one rounding at the extreme of the range.)
             * The T800 has them too, as fpumulby2/fpudivby2 through
             * fpentry - what occam emits for x * 2.0: `ldc #12; fpentry`
             * is 10 cycles where `ldc #40000000; stl; ldlp; fpldnlmulsn`
             * is 23.  Not x + x there: `fpldnladdsn` (8) beats fpentry. */
            {
                int t9k = tp_have_t9000_feat(TP_T9K_FPU);
                SValue *other = NULL;
                int by2op = 0;
                int c1 = ((vtop[-1].r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST);
                int c2 = ((vtop[0].r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST);
                double k1 = c1 ? (is_db ? vtop[-1].c.d : (double)vtop[-1].c.f) : 0.0;
                double k2 = c2 ? (is_db ? vtop[0].c.d : (double)vtop[0].c.f) : 0.0;

                if (op == '*' && c2 && (k2 == 2.0 || k2 == 0.5)) {
                    other = &vtop[-1];
                    by2op = (k2 == 2.0) ? TP_OPR_FPMULBY2 : TP_OPR_FPDIVBY2;
                } else if (op == '*' && c1 && (k1 == 2.0 || k1 == 0.5)) {
                    other = &vtop[0];
                    by2op = (k1 == 2.0) ? TP_OPR_FPMULBY2 : TP_OPR_FPDIVBY2;
                } else if (op == '/' && c2 && (k2 == 2.0 || k2 == 0.5)) {
                    other = &vtop[-1];
                    by2op = (k2 == 2.0) ? TP_OPR_FPDIVBY2 : TP_OPR_FPMULBY2;
                } else if (t9k && op == '+' &&
                           tp_fpu_same_operand(&vtop[-1], &vtop[0])) {
                    /* x + x == 2*x exactly; one load instead of two */
                    other = &vtop[0];
                    by2op = TP_OPR_FPMULBY2;
                }
                if (by2op && in_fa != 3) {
                    /* `other` is the only non-constant operand, so if
                     * either is in FA it is this one */
                    if (!in_fa)
                        tp_push_float_sv(other);
                    if (t9k)
                        emit_instr(TP_OP_OPR, by2op);
                    else
                        tp_fpentry(by2op == TP_OPR_FPMULBY2
                                   ? TP_FPU_OP_MULBY2 & 0xFF
                                   : TP_FPU_OP_DIVBY2 & 0xFF);
                    goto fp_arith_done;
                }
            }

            {
                /* `first` goes on the FPU stack, `second` is fused into the
                 * operation or pushed above it; the IEEE + and * commute, so
                 * for those either operand may be `first`. */
                SValue *first = &vtop[-1], *second = &vtop[0];
                int reversed = 0;

                if (in_fa == 3) {
                    /* both on the stack, left in FB: FB op FA */
                    emit_instr(TP_OP_OPR, op == '+' ? TP_FPU_ADD :
                                          op == '-' ? TP_FPU_SUB :
                                          op == '*' ? TP_FPU_MUL : TP_FPU_DIV);
                    fpureg_pop(1);
                    goto fp_arith_done;
                }
                if (in_fa == 2 || (!in_fa && comm &&
                                   tp_fpu_addr_just_parked(&vtop[0]) &&
                                   !tp_fpu_addr_just_parked(&vtop[-1]))) {
                    /* vtop[0] is in FA, or its address was parked by the
                     * last instruction and loading it first cancels that */
                    first = &vtop[0];
                    second = &vtop[-1];
                    reversed = 1;
                }
                if (!in_fa)
                    tp_push_float_sv(first);
                if (comm && tp_emit_float_addr(second)) {
                    /* Fused: load second operand AND operate in one instr */
                    if (op == '+')
                        emit_instr(TP_OP_OPR, is_db ? TP_FPU_LDNLADDDB : TP_FPU_LDNLADDSN);
                    else
                        emit_instr(TP_OP_OPR, is_db ? TP_FPU_LDNLMULDB : TP_FPU_LDNLMULSN);
                    abcreg_pop(1);
                } else if (comm) {
                    /* Second operand is const 0.0 (no address): fpldzero + op */
                    emit_instr(TP_OP_OPR, is_db ? TP_OPR_FPLDZERODB : TP_OPR_FPLDZEROSN);
                    fpureg_push(1);
                    emit_instr(TP_OP_OPR, op == '+' ? TP_FPU_ADD : TP_FPU_MUL);
                    fpureg_pop(1);
                } else {
                    /* fpsub/fpdiv compute FBreg op FAreg: the left operand
                     * must be in FB */
                    tp_push_float_sv(second);
                    if (reversed)
                        emit_instr(TP_OP_OPR, TP_FPU_REV);
                    emit_instr(TP_OP_OPR, op == '-' ? TP_FPU_SUB : TP_FPU_DIV);
                    fpureg_pop(1);
                }
            }

        fp_arith_done:
            vtop--;
            vtop->r = rdst;
            vtop->r2 = is_db ? rdst + 1 : VT_CONST;
            vtop->sym = NULL;
            {
                int store_off = ind;

                tp_store_fpu_result_to_slot(rdst, bt);
                tp_fpr_reg = rdst;
                tp_fpr_off = store_off;
                tp_fpr_end = ind;
                tp_fpr_db = is_db;
            }
            abcreg_stack_depth = 0;
            fpureg_stack_depth = 0;
            return;
        }

        if (p1 && p2 &&
            (op == TOK_EQ || op == TOK_NE || op == TOK_GT ||
             op == TOK_LT || op == TOK_GE || op == TOK_LE)) {
            int in_fa = 0;

            if (tp_fpu_take_resident(&vtop[0], is_db)) {
                in_fa = 2;
                if (tp_fpu_take_under(&vtop[-1], is_db))
                    in_fa = 3;
            } else if (tp_fpu_take_resident(&vtop[-1], is_db) ||
                       tp_fpu_load_early(&vtop[-1], is_db))
                in_fa = 1;
            abcreg_stack_depth = 0;
            fpureg_stack_depth = in_fa == 3 ? 2 : in_fa ? 1 : 0;
            /* Comparisons need no result register: the FPU compare leaves
             * an int truth value in AReg (VT_CMP).  Push order: first
             * operand ends in FBreg, second in FAreg, so fpgt/fpeq test
             * "first OP second" directly. */
            if (in_fa == 3) {
                /* both already there */
            } else if (in_fa == 2) {
                tp_push_float_sv(&vtop[-1]);
                emit_instr(TP_OP_OPR, TP_FPU_REV);
            } else {
                if (!in_fa)
                    tp_push_float_sv(&vtop[-1]);
                tp_push_float_sv(&vtop[0]);
            }
            tp_emit_fp_compare(op);
            fpureg_stack_depth = 0;
            abcreg_stack_depth = 1;
            vtop--;
            vset_VT_CMP(TOK_NE);
            return;
        }
    }

    if (is_db) {
        /* Direct FPU path for doubles: same as float32 but with db variants */
        if (op == '+' || op == '*' || op == '-' || op == '/') {
            int fv1 = orig_first.r & VT_VALMASK;
            int fv2 = orig_second.r & VT_VALMASK;
            int f1_local = (orig_first.r & VT_LVAL) && fv1 == VT_LOCAL &&
                           !(orig_first.r & VT_SYM);
            int f2_local = (orig_second.r & VT_LVAL) && fv2 == VT_LOCAL &&
                           !(orig_second.r & VT_SYM);

            if (f1_local && f2_local) {
                gv2(RC_FLOAT, RC_FLOAT);
                abcreg_stack_depth = 0;
                fpureg_stack_depth = 0;

                emit_local_addr(orig_first.c.i);
                emit_instr(TP_OP_OPR, TP_FPU_LDNLDB);
                abcreg_pop(1);
                fpureg_push(1);

                if (op == '+' || op == '*') {
                    emit_local_addr(orig_second.c.i);
                    emit_instr(TP_OP_OPR, op == '+' ? TP_FPU_LDNLADDDB : TP_FPU_LDNLMULDB);
                    abcreg_pop(1);
                } else {
                    emit_local_addr(orig_second.c.i);
                    emit_instr(TP_OP_OPR, TP_FPU_LDNLDB);
                    abcreg_pop(1);
                    fpureg_push(1);
                    emit_instr(TP_OP_OPR, op == '-' ? TP_FPU_SUB : TP_FPU_DIV);
                    fpureg_pop(1);
                }

                vtop--;
                tp_store_fpu_result_to_slot(vtop->r & VT_VALMASK, bt);
                abcreg_stack_depth = 0;
                fpureg_stack_depth = 0;
                return;
            }
        }

        /* Doubles occupy two workspace words.
         * gv2() may emit overlapping RC_FLOAT reloads (e.g. 3/4 and 4/5),
         * which can clobber a live intermediate before we consume it.
         * Preserve both operands into non-overlapping safe pairs first. */
        sv_first = orig_first;
        sv_second = orig_second;
        load(FRAME_SLOT_SCRATCH, &sv_first);      /* words 0..1 */
        load(FRAME_SLOT_DBL_TMP, &sv_second);     /* words 6..7 */

        /* Keep generic allocator/stack state in sync with backend expectations. */
        gv2(RC_FLOAT, RC_FLOAT);

        if (opf_debug) {
            fprintf(stderr,
                    "[OPF_DB] op=0x%x func=%s first{type=0x%x r=0x%x v=0x%x} "
                    "second_orig{type=0x%x r=0x%x v=0x%x} second_gv2{type=0x%x r=0x%x v=0x%x} "
                    "materialized={0..1,6..7}\n",
                    op, funcname ? funcname : "<unknown>",
                    sv_first.type.t, sv_first.r, sv_first.r & VT_VALMASK,
                    orig_second.type.t, orig_second.r, orig_second.r & VT_VALMASK,
                    vtop[0].type.t, vtop[0].r, vtop[0].r & VT_VALMASK);
        }
        sv_first.r = FRAME_SLOT_SCRATCH;
        sv_second.r = FRAME_SLOT_DBL_TMP;
    } else {
        /* Zero-STL for float locals: if both operands are local float/double
         * variables, emit LDLP+fpldnlsn/db directly with fused ops.
         * Works for both float32 and float64 (double). */
        if (op == '+' || op == '*' || op == '-' || op == '/') {
            int fv1 = orig_first.r & VT_VALMASK;
            int fv2 = orig_second.r & VT_VALMASK;
            int f1_local = (orig_first.r & VT_LVAL) && fv1 == VT_LOCAL &&
                           !(orig_first.r & VT_SYM);
            int f2_local = (orig_second.r & VT_LVAL) && fv2 == VT_LOCAL &&
                           !(orig_second.r & VT_SYM);

            if (f1_local && f2_local) {
                /* Skip gv2 — load directly from local float slots.
                 * For + and *: use fused form for second operand.
                 * Pattern: LDLP a; fpldnlsn; LDLP b; fpldnl{add,mul}sn */
                gv2(RC_FLOAT, RC_FLOAT);  /* still needed for vstack sync */
                sv_first = vtop[-1];
                sv_second = vtop[0];

                abcreg_stack_depth = 0;
                fpureg_stack_depth = 0;

                /* Load first operand from its original local offset */
                emit_local_addr(orig_first.c.i);
                emit_instr(TP_OP_OPR, is_db ? TP_FPU_LDNLDB : TP_FPU_LDNLSN);
                abcreg_pop(1);
                fpureg_push(1);

                if (op == '+' || op == '*') {
                    /* Fused: load second AND operate in one instruction */
                    emit_local_addr(orig_second.c.i);
                    if (op == '+')
                        emit_instr(TP_OP_OPR, is_db ? TP_FPU_LDNLADDDB : TP_FPU_LDNLADDSN);
                    else
                        emit_instr(TP_OP_OPR, is_db ? TP_FPU_LDNLMULDB : TP_FPU_LDNLMULSN);
                    abcreg_pop(1);
                } else {
                    /* Sub/div: no fused form, load second separately */
                    emit_local_addr(orig_second.c.i);
                    emit_instr(TP_OP_OPR, is_db ? TP_FPU_LDNLDB : TP_FPU_LDNLSN);
                    abcreg_pop(1);
                    fpureg_push(1);
                    emit_instr(TP_OP_OPR, op == '-' ? TP_FPU_SUB : TP_FPU_DIV);
                    fpureg_pop(1);
                }

                vtop--;
                tp_store_fpu_result_to_slot(vtop->r & VT_VALMASK, bt);
                abcreg_stack_depth = 0;
                fpureg_stack_depth = 0;
                return;
            }
        }

        /* Standard path */
        gv2(RC_FLOAT, RC_FLOAT);
        sv_first = vtop[-1];
        sv_second = vtop[0];
    }

    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;

    /* Detect fused load-and-operate opportunity for + and * (T800).
     * fpldnladdsn/fpldnlmulsn load from Mem[AReg] and combine with FAreg
     * in a single instruction, eliminating FPU stack push overhead.
     * Now also tries swapping operands (FPREV) to fuse with first operand. */
    {
        int fpu_fuse = 0;
        int fpu_fuse_swap = 0;
        if (op == '+' || op == '*') {
            int fv = sv_second.r & VT_VALMASK;
            int fv1 = sv_first.r & VT_VALMASK;
            if (!(sv_second.r & VT_LVAL) && fv < VT_CONST)
                fpu_fuse = 1;
            else if (!(sv_first.r & VT_LVAL) && fv1 < VT_CONST)
                fpu_fuse_swap = 1;  /* swap: fuse with first operand */
        }

        if (fpu_fuse_swap) {
            /* Load second first, then fuse-load first */
            tp_push_float_sv(&sv_second);
            /* fused path handled in switch below */
        } else {
            tp_push_float_sv(&sv_first);
            if (!fpu_fuse)
                tp_push_float_sv(&sv_second);
        }

    /* Transputer FPU operations (T800) */
    switch (op) {
    case '+':
        if (fpu_fuse) {
            /* Fused: LDLP slot → fpldnladdsn/db. FAreg += Mem[AReg]. */
            Tp_LDLP(sv_second.r & VT_VALMASK);
            emit_instr(TP_OP_OPR, is_db ? TP_FPU_LDNLADDDB : TP_FPU_LDNLADDSN);
            abcreg_pop(1);
        } else if (fpu_fuse_swap) {
            /* Fused with swapped operands: FBreg has second, fuse-load first.
             * Addition is commutative so order doesn't matter. */
            Tp_LDLP(sv_first.r & VT_VALMASK);
            emit_instr(TP_OP_OPR, is_db ? TP_FPU_LDNLADDDB : TP_FPU_LDNLADDSN);
            abcreg_pop(1);
        } else {
            emit_instr(TP_OP_OPR, TP_FPU_ADD);
        }
        vtop--;
        tp_store_fpu_result_to_slot(vtop->r & VT_VALMASK, bt);
        abcreg_stack_depth = 0;
        fpureg_stack_depth = 0;
        break;
    case '-':
        emit_instr(TP_OP_OPR, TP_FPU_SUB);
        vtop--;
        tp_store_fpu_result_to_slot(vtop->r & VT_VALMASK, bt);
        abcreg_stack_depth = 0;
        fpureg_stack_depth = 0;
        break;
    case '*':
        if (fpu_fuse) {
            /* Fused: LDLP slot → fpldnlmulsn/db. FAreg *= Mem[AReg]. */
            Tp_LDLP(sv_second.r & VT_VALMASK);
            emit_instr(TP_OP_OPR, is_db ? TP_FPU_LDNLMULDB : TP_FPU_LDNLMULSN);
            abcreg_pop(1);
        } else if (fpu_fuse_swap) {
            /* Fused with swapped operands. Multiplication is commutative. */
            Tp_LDLP(sv_first.r & VT_VALMASK);
            emit_instr(TP_OP_OPR, is_db ? TP_FPU_LDNLMULDB : TP_FPU_LDNLMULSN);
            abcreg_pop(1);
        } else {
            emit_instr(TP_OP_OPR, TP_FPU_MUL);
        }
        vtop--;
        tp_store_fpu_result_to_slot(vtop->r & VT_VALMASK, bt);
        abcreg_stack_depth = 0;
        fpureg_stack_depth = 0;
        break;
    case '/':
        emit_instr(TP_OP_OPR, TP_FPU_DIV);
        vtop--;
        tp_store_fpu_result_to_slot(vtop->r & VT_VALMASK, bt);
        abcreg_stack_depth = 0;
        fpureg_stack_depth = 0;
        break;
    case TOK_EQ: case TOK_NE: case TOK_GT:
    case TOK_LT: case TOK_GE: case TOK_LE:
        tp_emit_fp_compare(op);
        fpureg_stack_depth = 0;
        abcreg_stack_depth = 1;
        vtop--;
        vset_VT_CMP(TOK_NE);
        break;
    default:
        tcc_error("unsupported float operator 0x%x", op);
    }
    } /* fpu_fuse block */
}

#endif /* !TARGET_DEFS_ONLY */
