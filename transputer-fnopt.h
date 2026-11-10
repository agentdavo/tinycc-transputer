/*
 *  Transputer: the passes that rewrite a finished function.
 *
 *  Code, not declarations: transputer-gen.c includes this once.  It has one
 *  entry, tp_relax_function(), which gfunc_epilog() calls when the function
 *  body is complete and its frame is known.  That decodes the function's
 *  bytes, cur_text_section from func_start_ind to ind, and re-emits them:
 *
 *    tp_sched_function   evaluation-stack scheduling (rules S1..S12)
 *    tp_slot_remap       renumbers workspace slots by use
 *    tp_gaddr_cache      keeps a repeated global address in a slot
 *    relaxation          shortest prefix encoding for every operand
 *
 *  and then moves the relocations, symbols and line marks that pointed into
 *  the old bytes.  It returns the number of bytes saved.
 */

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
