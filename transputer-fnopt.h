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
    int tab;          /* switch jump table t: t + 1 on its entries (each a J
                         and the padding before it), -(t + 1) on the six
                         instructions of its dispatch */
    int icall;        /* the tail of an indirect call: `ldc 5; ldpi; stl -4;
                         ajw -4; gcall` (tp_relax_icalls_find) */
    int workspace_extent; /* contiguous words read by a generated MOVE */
    int dup_load;     /* second LDL of a proven nonvolatile adjacent pair */
};

/* Bytes of `stl -4; ajw -4; gcall`, which that tail's ldpi skips. */
#define TP_ICALL_TAIL_BYTES 5

/* Give the entries of each jump table one width: that of the widest. */
static int tp_swtab_level(struct tp_relax_insn *ins, int n)
{
    int t, i, changed = 0;

    for (t = 1; t <= swtab_count; t++) {
        int mx = 0;
        for (i = 0; i < n; i++)
            if (ins[i].tab == t && ins[i].opcode == TP_ENC_J &&
                ins[i].new_len > mx)
                mx = ins[i].new_len;
        for (i = 0; i < n; i++)
            if (ins[i].tab == t && ins[i].opcode == TP_ENC_J &&
                ins[i].new_len != mx) {
                ins[i].new_len = mx;
                changed = 1;
            }
    }
    return changed;
}

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
        stats = tp_knob(TPK_TP_RELAX_STATS) ? 1 : 0;
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
        case TP_OPR_BITREVWORD:
            *reads = 1; *delta = 0;  return 1;
        case TP_OPR_CRCBYTE:                  /* A the result, B old C */
            *reads = 3; *delta = -2; return 1;
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
 * bit 12 = S11, bit 13 = S12, bit 14 = block-local copies), for bisecting a suspected miscompile:
 * TP_SCHED_MASK=0x3f3f disables S7, and so on.  S8 has no switch. */
static int tps_mask = -1;
static int tps_on(int which)
{
    if (tps_mask < 0) {
        const char *e = tp_knob(TPK_TP_SCHED_MASK);
        tps_mask = (e && *e) ? (int)strtol(e, NULL, 0) : 0x7f7f;
    }
    return (tps_mask >> which) & 1;
}

static void tps_note(int which)
{
    if (tps_stats_on < 0)
        tps_stats_on = tp_knob(TPK_TP_SCHED_STATS) ? 1 : 0;
    if (tps_stats_on)
        tps_stats[which]++;
}

static void tps_bail(const char *why)
{
    if (tps_stats_on < 0)
        tps_stats_on = tp_knob(TPK_TP_SCHED_STATS) ? 1 : 0;
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
        const char *e = tp_knob(TPK_TP_SCHED_BISECT);
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

/* Local scalar cost for comparing equivalent schedules. Charge the
 * encoded instruction and every prefix byte, plus the extra cycle of
 * a workspace load in the T800 timing model. REV and DUP therefore
 * cost one and two respectively. This is not a T9000 grouping model. */
static int tps_scalar_cost(int opcode, int operand)
{
    return tp_encode_len(opcode, operand) + (opcode == TP_ENC_LDL);
}

/* What the phases of tp_sched_function() share. */
struct tps {
    struct tp_relax_insn *ins;
    struct tps_ann *an;
    int *idx;               /* byte offset -> instruction index */
    int n, size;
    int top;                /* words tp_gaddr_cache put over the locals */
    /* Slots this pass is allowed to treat as pseudo-registers.  The frame's
     * reserved region can now be smaller than NB_REGS, in which case locals
     * occupy low slot numbers; an LDLP of one of *these* must disqualify the
     * whole function. */
    int nreg;

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
     *   - the pre-scan identifies objects whose addresses escape through
     *     LDLP. Their words stay live throughout the function, so a rule
     *     cannot drop a store that might be read through a pointer.
     *   - incoming arguments live *above* the frame (slot 13 in the example),
     *     so limiting the range to the frame's own words keeps the caller's
     *     workspace out of it.
     *
     * Capped at 32 because `liveout` is a 32-bit mask - which happens to be
     * the T9000 workspace-cache size as well. */
    int nslot;
    unsigned int escaped_slots;
    /* No LDLP hands out the address of a workspace word, and there is no
     * inline assembly, so nothing outside this function can write one. */
    int frame_private;
    int resv_escape;        /* a reserved word's address escapes (S10) */
    int removed;            /* instructions removed so far */
};

/* TP_SCHED_DBL=0: an `ldlp r; fpldnldb` (or fpstnldb, or an integer
 * conversion's word) on a pseudo-register declines the function, as every
 * one did until 2026-11-11.  For an A/B run. */
static int tps_dbl_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = tp_knob(TPK_TP_SCHED_DBL);
        on = !(e && *e == '0');
    }
    return on;
}

/* TP_SCHED_DBL=1: FP accesses to pseudo-registers are followed, but one to
 * a local still gives up the local range, as until later on 2026-11-11. */
static int tps_localfp_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = tp_knob(TPK_TP_SCHED_DBL);
        on = !(e && (*e == '0' || *e == '1'));
    }
    return on;
}

/* Words an FP instruction reaches from the address in A, for the forms
 * tps_annotate records as reads of those words; 0 for anything else. */
static int tps_fp_extent(const struct tp_relax_insn *c)
{
    if (c->opcode != TP_ENC_OPR)
        return 0;
    switch (c->operand) {
    case TP_FPU_LDNLSN: case TP_FPU_LDNLADDSN: case TP_FPU_LDNLMULSN:
    case TP_FPU_STNLSN:
        return 1;
    case TP_OPR_FPI32TOR32: case TP_OPR_FPI32TOR64:
    case TP_OPR_FPB32TOR64: case TP_OPR_FPSTNLI32:
    case TP_FPU_LDNLDB: case TP_FPU_LDNLADDDB: case TP_FPU_LDNLMULDB:
    case TP_FPU_STNLDB:
        return tps_dbl_on() ? (c->operand == TP_FPU_LDNLDB ||
                               c->operand == TP_FPU_LDNLADDDB ||
                               c->operand == TP_FPU_LDNLMULDB ||
                               c->operand == TP_FPU_STNLDB ? 2 : 1) : 0;
    }
    return 0;
}

/* Known LDLP consumers name their workspace words in the annotations.
 * Otherwise a local's address can be retained and used by arbitrary code.
 * Keep that object's words live everywhere, including across calls. */
static void tps_prescan(struct tps *s)
{
    struct tp_relax_insn *ins = s->ins;
    const int n = s->n;
    int i, escapes_local = 0, escapes_reserved = 0;

    s->frame_private = !func_uses_inline_asm;
    s->escaped_slots = 0;
    for (i = 0; i < n; i++) {
        int j, opd = ins[i].operand;
        if (ins[i].opcode != TP_ENC_LDLP)
            continue;
        for (j = i + 1; j < n && ins[j].opcode == -1; j++)
            ;
        if (j < n && (ins[j].opcode == TP_ENC_STNL ||
                      ins[j].opcode == TP_ENC_LDNL))
            continue;
        if (tp_relax_byte_read(ins, n, i))
            continue;

        /* Copy propagation cannot follow FP readers or aliases through a
         * pointer, even when the word is outside the liveness mask. */
        s->frame_private = 0;
        if (j < n && tps_fp_extent(&ins[j]) && tps_localfp_on())
            continue;
        if (opd >= 0 && opd < func_frame_reserved)
            escapes_reserved = 1;
        if (opd >= 0 && opd < s->nslot)
            escapes_local = 1;
    }

    if (func_uses_inline_asm || (escapes_local &&
        (escapes_reserved || tp_loc_marks_bad || !tps_localfp_on()))) {
        s->nslot = s->nreg;
        return;
    }
    if (escapes_local)
        for (i = func_frame_reserved; i < s->nslot; i++)
            if (tps_word_escapes(ins, n, i, s->top))
                s->escaped_slots |= 1u << i;
}

/* ---- 1. annotate, and refuse anything this model cannot describe ---- */
static int tps_annotate(struct tps *s)
{
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;
    const int *idx = s->idx;
    const int n = s->n;
    const int size = s->size;
    const int nreg = s->nreg;
    const int nslot = s->nslot;
    int i;

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
            /* A double-length access reaches this word and the next, and the
             * integer conversions one word.  Stores count as reads here
             * too, so the pair is live wherever one of these can see it
             * and no rule drops or carries a word across the access. */
            if (!func_uses_inline_asm && j < n && ins[j].opcode == TP_ENC_OPR &&
                (ins[j].operand == TP_FPU_LDNLDB ||
                 ins[j].operand == TP_FPU_LDNLADDDB ||
                 ins[j].operand == TP_FPU_LDNLMULDB ||
                 ins[j].operand == TP_FPU_STNLDB) && tps_dbl_on()) {
                indirect = 1;
                if (opd >= 0 && opd < nslot)
                    an[i].uses |= 1u << opd;
                if (opd + 1 >= 0 && opd + 1 < nslot)
                    an[i].uses |= 1u << (opd + 1);
            }
            if (!func_uses_inline_asm && j < n && ins[j].opcode == TP_ENC_OPR &&
                (ins[j].operand == TP_OPR_FPI32TOR32 ||
                 ins[j].operand == TP_OPR_FPI32TOR64 ||
                 ins[j].operand == TP_OPR_FPB32TOR64 ||
                 ins[j].operand == TP_OPR_FPSTNLI32) && tps_dbl_on()) {
                indirect = 1;
                if (opd >= 0 && opd < nslot)
                    an[i].use = opd;
            }
            if (tp_relax_byte_read(ins, n, i)) {
                indirect = 1;
                if (opd >= 0 && opd < nslot)
                    an[i].use = opd;
            }
            /* The pre-scan handles local escapes. A pseudo-register's
             * address in an unknown use still disqualifies the function. */
            if (!indirect && opd >= 0 && opd < nreg) {
                tps_bail("ldlp-escape");
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
            if (i != first && !ins[i].icall &&
                !(j < n && ins[j].opcode == TP_ENC_OPR &&
                  ins[j].operand == TP_OPR_RET)) {
                tps_bail("ajw-midfunction");
                return 0;
            }
        } else if (opc == TP_ENC_OPR && opd == TP_OPR_GAJW) {
            tps_bail("gajw");
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
                return 0;
            }
            an[i].succ1 = -1;
        }
        /* A jump table's dispatch (a gcall) falls into the first entry, and
         * each entry, besides going to its case, leads on to the next: so
         * whatever any case reads is live at the dispatch.  The last entry
         * leads into the jump to the default, which is one more case. */
        if (ins[i].tab > 0 && opc == TP_ENC_J)
            an[i].succ2 = (i + 1 < n) ? i + 1 : -1;
    }
    return 1;
}

/* ---- 2. branch targets.  The emitter zeroes its evaluation-stack
 * bookkeeping at every label, so a target is a point where the depth is
 * known to be zero. ---- */
static void tps_mark_heads(struct tps *s)
{
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;
    const int *idx = s->idx;
    const int n = s->n;
    const int size = s->size;
    int i;

    an[0].head = 1;
    for (i = 0; i < n; i++) {
        int k;
        if (ins[i].tab > 0)
            an[i].head = 1;             /* a table entry is entered by gcall */
        if (!ins[i].is_branch)
            continue;
        if (ins[i].target_old < 0 || ins[i].target_old > size)
            continue;
        k = idx[ins[i].target_old];
        if (k >= 0)
            an[k].head = 1;
    }
}

/* ---- 2a. block-local copy propagation ----
 *
 *   ldl x; stl y             ... ldl y          ->  ... ldl x
 *   ldl x; ldnlp k; stl y    ... ldl y; ldnl m  ->  ... ldl x; ldnl m+k
 *                                  (stnl, ldnlp likewise)
 *
 * from the copy to the end of its basic block, while neither x nor y is
 * written.  Nothing is deleted here: the copy goes later, by S6, if no read
 * of y is left anywhere (liveness runs after this).  tps_copy_propagate
 * does it function-wide when x and y have one definition each; this needs
 * none, which is what a loop's temporaries have: `C[i] += v` parks C + i
 * and copies it for the store (`ldl 4; stl 5; ldl 4; ldnl 0; bsub; ldl 5;
 * stnl 0`), `t[k]++` computes t + k into one slot and copies it to another.
 * A call, a jump, a branch target, and an LDLP of x or y end the scan, and
 * so does a store through a pointer when x or y is a local whose address a
 * pointer might hold.  TP_SCHED_MASK bit 14 turns it off. */
static void tps_block_copies(struct tps *s)
{
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;
    const int n = s->n;
    const int nslot = s->nslot;
    const int L = func_frame_words - s->top;
    int i, k, ldlp_above = 0;

    if (!tps_on(14) || !tps_bisect_ok())
        return;
    for (i = 0; i < n; i++)
        if (ins[i].opcode == TP_ENC_LDLP && ins[i].operand >= L)
            ldlp_above = 1;
    for (i = 0; i < n; i++) {
        int x, y, off = 0, has_off = 0, st, k1, private_xy;

        if (an[i].dead || ins[i].pinned || ins[i].opcode != TP_ENC_LDL)
            continue;
        x = ins[i].operand;
        if (x < 0 || x >= 32)
            continue;
        if (x >= nslot && (x <= L || ldlp_above))
            continue;           /* a parameter only, never addressed */
        k1 = tps_next(ins, an, n, i);
        if (k1 < 0)
            continue;
        st = k1;
        if (ins[k1].opcode == TP_ENC_LDNLP && !ins[k1].pinned) {
            off = ins[k1].operand;
            has_off = 1;
            st = tps_next(ins, an, n, k1);
            if (st < 0)
                continue;
        }
        if (ins[st].opcode != TP_ENC_STL || !tps_free(ins, an, i, st))
            continue;
        y = ins[st].operand;
        if (y < 0 || y >= nslot || y == x || y < func_max_stack_words ||
            (s->escaped_slots & (1u << y)) ||
            (x < nslot && (s->escaped_slots & (1u << x))))
            continue;
        if (!has_off &&
            tp_encode_len(TP_ENC_LDL, x) > tp_encode_len(TP_ENC_LDL, y))
            continue;
        private_xy = s->frame_private ||
                     (y < s->nreg && (x < s->nreg || x >= nslot));
        for (k = st + 1; k < n; k++) {
            int opc = ins[k].opcode, opd = ins[k].operand, dl, rd, m;

            if (an[k].head)
                break;
            if (opc == -1 || an[k].dead)
                continue;
            if (opc == TP_ENC_J || opc == TP_ENC_CALL ||
                (opc == TP_ENC_OPR && (opd == TP_OPR_GCALL ||
                                       opd == TP_OPR_RET)))
                break;
            if (opc == TP_ENC_STL && (opd == x || opd == y))
                break;
            /* any address of x or y (an FP store through it writes the
             * word: the annotation calls that a use), or of the word
             * before (a double covers two) */
            if (opc == TP_ENC_LDLP && (opd == x || opd == y ||
                                       opd == x - 1 || opd == y - 1))
                break;
            if (an[k].def == x || an[k].def == y)
                break;
            if (!private_xy &&
                (opc == TP_ENC_STNL ||
                 (opc == TP_ENC_OPR &&
                  (!tps_effect(opc, opd, &dl, &rd) || opd == TP_OPR_SB ||
                   opd == TP_OPR_SS || opd == TP_FPU_STNLSN ||
                   opd == TP_FPU_STNLDB))))
                break;
            if (opc != TP_ENC_LDL || opd != y || ins[k].pinned)
                continue;
            if (!has_off) {
                ins[k].operand = x;
                an[k].use = x < nslot ? x : -1;
                continue;
            }
            m = tps_next(ins, an, n, k);
            if (m < 0 || ins[m].pinned || an[m].head ||
                (ins[m].opcode != TP_ENC_LDNL && ins[m].opcode != TP_ENC_STNL &&
                 ins[m].opcode != TP_ENC_LDNLP) || !tps_free(ins, an, k, m))
                continue;
            ins[k].operand = x;
            an[k].use = x < nslot ? x : -1;
            ins[m].operand += off;
            if (tp_encode_len(TP_ENC_LDL, x) > ins[k].new_len)
                ins[k].new_len = tp_encode_len(TP_ENC_LDL, x);
            if (tp_encode_len(ins[m].opcode, ins[m].operand) > ins[m].new_len)
                ins[m].new_len = tp_encode_len(ins[m].opcode, ins[m].operand);
        }
    }
}

/* Record the original pair before S4 removes the address staging store.
 * Both reads must be proven nonvolatile, and the intervening store must
 * not change the index. Branch entries and relocations are barriers. */
static void tps_note_indexed_pairs(struct tps *s)
{
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;
    const int n = s->n;
    int i;

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
}

/* ---- 3. evaluation-stack depth within each block ---- */
static void tps_depths(struct tps *s)
{
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;
    const int n = s->n;
    int i;

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
}

/* ---- 4. backward liveness over the pseudo-register slots ---- */
static int tps_liveness(struct tps *s)
{
    struct tps_ann *an = s->an;
    const int n = s->n;
    int i, pass;

    for (pass = 0; pass < 128; pass++) {
        int changed = 0;
        for (i = n - 1; i >= 0; i--) {
            unsigned int out = s->escaped_slots;
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
        return 0;
    }
    return 1;
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
static void tps_drop_unread_results(struct tps *s)
{
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;
    const int n = s->n;
    const int nslot = s->nslot;
    int i;

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
                s->removed++;
                tps_note(8);
            }
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
static void tps_copy_propagate(struct tps *s)
{
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;
    const int n = s->n;
    const int nslot = s->nslot;
    const int frame_private = s->frame_private;
    int i;

    if (frame_private) {
        for (i = 0; i < n; i++) {
            int x, y, j, k, ok = 1;

            if (an[i].dead || ins[i].pinned || ins[i].opcode != TP_ENC_LDL)
                continue;
            /* `LDC k ; STL y` was tried as a second source form and measured:
             * 326 instructions and 76 groups over the whole kernel corpus,
             * 0.09%.  Almost no constant copy survives, and the ones that do
             * are already caught by S1/S4/S5/S6, so it was s->removed again. */
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
            s->removed += 2;
            i = j;
        }
    }
}

/* For S10 beyond the liveness mask: is every LDLP of a reserved word
 * one access tps_dead_after can see? */
static void tps_reserved_escape(struct tps *s)
{
    struct tp_relax_insn *ins = s->ins;
    const int n = s->n;
    int i;

    for (i = 0; i < n && !s->resv_escape; i++) {
        int j, lo, t = ins[i].operand;
        if (ins[i].opcode != TP_ENC_LDLP || t < 0 || t >= func_frame_reserved)
            continue;
        for (j = i + 1; j < n && ins[j].opcode == -1; j++)
            ;
        if (j == n || tpg_ldlp_extent(&ins[j], &lo) < 0)
            s->resv_escape = 1;
    }
}

/* The rules of one matching round.  Each is tried at instruction i and
 * returns 1 if it rewrote something; where it gives *next, matching goes
 * on after that instruction.  For the rules that start at a store,
 * `STL a` is at i and i1 is the instruction after it. */

static int tps_s1(struct tps *s, int i, int a, int i1, int *next)
{
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;

    /* S1 */
    if (tps_on(0) &&
        ins[i1].opcode == TP_ENC_LDL && ins[i1].operand == a &&
        !(an[i1].liveout & (1u << a)) && tps_free(ins, an, i, i1)) {
        an[i].dead = an[i1].dead = 1;
        s->removed += 2; tps_note(0);
        *next = i1;
        return 1;
    }
    return 0;
}

static int tps_s2_s3_s7(struct tps *s, int i, int a, int i1, int *next)
{
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;
    const int n = s->n;
    const int nslot = s->nslot;
    int b, i2, i3;

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
                s->removed += 4; tps_note(1);
                *next = i3;
                return 1;
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
                s->removed += 3; tps_note(2);
                *next = i3;
                return 1;
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
            tps_scalar_cost(TP_ENC_OPR, TP_OPR_REV) <
                tps_scalar_cost(TP_ENC_STL, a) + tps_scalar_cost(TP_ENC_LDL, a) &&
            tps_free(ins, an, i, i2)) {
            ins[i].opcode = TP_ENC_OPR;
            ins[i].operand = TP_OPR_REV;
            ins[i].is_branch = 0;
            an[i].known = tps_effect(TP_ENC_OPR, TP_OPR_REV,
                                     &an[i].delta, &an[i].reads);
            an[i].use = an[i].def = -1;
            an[i2].dead = 1;
            s->removed += 1; tps_note(6);
            *next = i2;
            return 1;
        }
    }
    return 0;
}

static int tps_s11(struct tps *s, int i, int a, int i1)
{
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;
    const int n = s->n;
    const int top = s->top;
    const int nreg = s->nreg;
    const int nslot = s->nslot;
    const int frame_private = s->frame_private;
    int j;

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
     * a is dead after.  The three s->removed instructions push and pop
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
             * s->removed its store (c-testsuite 00182's `*p++`). */
            if (x < 32) {
                int q;
                for (q = p0; q < use; q++)
                    an[q].liveout |= 1u << x;
            }
            s->removed += p1 >= 0 ? 3 : 2;
            tps_note(12);
            return 1;
        }
    }
    return 0;
}

static int tps_s4_s5(struct tps *s, int i, int a, int i1, int *next)
{
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;
    const int n = s->n;
    int b, j, r, maxr;

    /* S4 / S5: carry the value across a balanced run of code.
     * `r` is the stack depth relative to the point just after the
     * STL, so the carried value sits at r == -1 and <e> must never
     * try to read there. */
    if (an[i].depth < 1 || an[i].depth > 3 || !an[i].known)
        return 0;
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
                s->removed += 2; tps_note(3);
            } else {
                if (!tps_on(4) ||
                    tps_scalar_cost(TP_ENC_OPR, TP_OPR_REV) >=
                        tps_scalar_cost(TP_ENC_STL, a) + tps_scalar_cost(TP_ENC_LDL, a))
                    break;
                /* One value of <e> is stacked above the carry, so
                 * they come back swapped: reuse the LDL slot as REV. */
                ins[j].opcode = TP_ENC_OPR;
                ins[j].operand = TP_OPR_REV;
                ins[j].is_branch = 0;
                an[j].known = tps_effect(TP_ENC_OPR, TP_OPR_REV,
                                         &an[j].delta, &an[j].reads);
                an[j].use = an[j].def = -1;
                an[i].dead = 1;
                s->removed += 1; tps_note(4);
            }
            /* Everything from here to the reload now runs one word
             * deeper; a later round must see that. The reload itself
             * is included because S5 keeps it (as REV). */
            for (b = i + 1; b <= j; b++)
                if (an[b].depth >= 0)
                    an[b].depth++;
            break;
        }
        if (an[j].use == a || an[j].def == a || (an[j].uses & (1u << a)))
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
        *next = (j > i) ? j : i;
        return 1;
    }
    return 0;
}

static int tps_s12(struct tps *s, int i, int *next)
{
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;
    const int n = s->n;
    int i1;

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
        s->removed += 1; tps_note(13);
        *next = i1;
        return 1;
    }
    return 0;
}

static int tps_s6_s10(struct tps *s, int i, int *next)
{
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;
    const int n = s->n;
    const int nslot = s->nslot;
    const int resv_escape = s->resv_escape;
    int b, i1, i2, i3;

    /* S6: a push straight into a slot nobody reads.  Deleting the
     * pair is depth-neutral, and only ever leaves more of the stack
     * intact than the original did. */
    if (ins[i].opcode != TP_ENC_LDL && ins[i].opcode != TP_ENC_LDC)
        return 0;
    i1 = tps_next(ins, an, n, i);
    /* S6 through an LDNLP: an address computed into a slot nobody reads
     * (what tps_block_copies leaves of `ldl x; ldnlp k; stl y`). */
    if (ins[i].opcode == TP_ENC_LDL && i1 >= 0 &&
        ins[i1].opcode == TP_ENC_LDNLP &&
        (i2 = tps_next(ins, an, n, i1)) >= 0 &&
        ins[i2].opcode == TP_ENC_STL && tps_on(5) &&
        (b = ins[i2].operand) >= 0 && b < nslot &&
        !(an[i2].liveout & (1u << b)) && tps_free(ins, an, i, i2)) {
        an[i].dead = an[i1].dead = an[i2].dead = 1;
        s->removed += 3; tps_note(5);
        *next = i2;
        return 1;
    }
    if (i1 < 0 || ins[i1].opcode != TP_ENC_STL)
        return 0;
    b = ins[i1].operand;
    if (b < 0)
        return 0;

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
        s->removed += 2; tps_note(9);
        *next = i3;
        return 1;
    }
    if (b >= nslot || !tps_on(5))
        return 0;
    if (an[i1].liveout & (1u << b))
        return 0;
    if (!tps_free(ins, an, i, i1))
        return 0;
    an[i].dead = an[i1].dead = 1;
    s->removed += 2; tps_note(5);
    *next = i1;
    return 1;
}

/* ---- 5. pattern matching: one round.  Returns 1 if anything changed. ---- */
static int tps_round(struct tps *s)
{
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;
    const int n = s->n;
    const int nslot = s->nslot;
    int i, changed = 0;

    for (i = 0; i < n; i++) {
        int a, i1;

        if (an[i].dead || ins[i].pinned || ins[i].opcode == -1)
            continue;
        if (ins[i].opcode == TP_ENC_STL) {
            a = ins[i].operand;
            if (a < 0 || a >= nslot)
                continue;
            i1 = tps_next(ins, an, n, i);
            if (i1 < 0)
                continue;
            if (tps_s1(s, i, a, i1, &i) || tps_s2_s3_s7(s, i, a, i1, &i) ||
                tps_s11(s, i, a, i1) || tps_s4_s5(s, i, a, i1, &i))
                changed = 1;
        } else if (tps_s12(s, i, &i) || tps_s6_s10(s, i, &i)) {
            changed = 1;
        }
    }
    return changed;
}

/* S4 may expose:
 *   LDL i; LDLP a; WSUB; LDL i; LDLP a; FPLDNLSNI
 * The already-computed address can instead be duplicated:
 *   LDL i; LDLP a; WSUB; DUP; FPLDNLSN
 * The floating-point access remains in place and occurs exactly once. */
static void tps_dup_indexed_pair(struct tps *s)
{
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;
    const int n = s->n;
    int i;

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
        if (tps_scalar_cost(TP_ENC_OPR, TP_OPR_DUP) +
                tps_scalar_cost(TP_ENC_OPR, TP_FPU_LDNLSN) >=
            tps_scalar_cost(TP_ENC_LDL, ins[j[3]].operand) +
                tps_scalar_cost(TP_ENC_LDLP, ins[j[4]].operand) +
                tps_scalar_cost(TP_ENC_OPR, TP_OPR_FPLDNLSNI))
            continue;
        ins[j[3]].opcode = TP_ENC_OPR; ins[j[3]].operand = TP_OPR_DUP;
        ins[j[3]].new_len = tp_encode_len(TP_ENC_OPR, TP_OPR_DUP);
        an[j[3]].use = -1;
        an[j[3]].known = tps_effect(ins[j[3]].opcode, ins[j[3]].operand, &an[j[3]].delta, &an[j[3]].reads);
        ins[j[4]].opcode = -1; an[j[4]].dead = 1;
        an[j[4]].known = 1; an[j[4]].delta = an[j[4]].reads = 0;
        ins[j[5]].operand = TP_FPU_LDNLSN;
        an[j[5]].known = tps_effect(ins[j[5]].opcode, ins[j[5]].operand, &an[j[5]].delta, &an[j[5]].reads);
        s->removed++;
    }
}

/* Record the proof while the workspace operands still match local_patches.
 * Price the rewrite later, once workspace slot numbering is final. */
static void tps_mark_dup_loads(struct tps *s)
{
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;
    int i;

    if (tp_t414() || func_uses_inline_asm)
        return;
    for (i = 0; i < s->n; i++) {
        int j, slot = ins[i].operand;
        if (an[i].dead || ins[i].pinned || ins[i].opcode != TP_ENC_LDL)
            continue;
        j = tps_next(ins, an, s->n, i);
        if (j < 0 || ins[j].opcode != TP_ENC_LDL || ins[j].operand != slot ||
            !tps_free(ins, an, i, j) ||
            !tps_proven_nonvolatile_ldl(ins, i) ||
            !tps_proven_nonvolatile_ldl(ins, j))
            continue;
        ins[j].dup_load = 1;
    }
}

/* LDL; DUP leaves the same stack as two loads of the same word. A
 * one-byte LDL ties on modeled cycles and saves one byte over DUP. */
static int tps_apply_dup_loads(struct tp_relax_insn *ins, int n)
{
    int i, prev = -1, changed = 0;
    for (i = 0; i < n; i++) {
        if (ins[i].opcode == -1)
            continue;
        if (ins[i].dup_load && prev >= 0 &&
            ins[i].opcode == TP_ENC_LDL && ins[prev].opcode == TP_ENC_LDL &&
            ins[i].operand == ins[prev].operand &&
            tps_scalar_cost(TP_ENC_OPR, TP_OPR_DUP) <
                tps_scalar_cost(TP_ENC_LDL, ins[i].operand)) {
            ins[i].opcode = TP_ENC_OPR;
            ins[i].operand = TP_OPR_DUP;
            ins[i].new_len = tp_encode_len(TP_ENC_OPR, TP_OPR_DUP);
            changed = 1;
        }
        prev = i;
    }
    return changed;
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
static void tps_rev_swap_pushes(struct tps *s)
{
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;
    const int n = s->n;
    int i;

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
        s->removed += 1;
        tps_note(7);
    }
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
static void tps_rev_hoist_push(struct tps *s)
{
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;
    const int n = s->n;
    int i;

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
        s->removed += 1;
        tps_note(7);
    }
}

/* A three-word copy loop often computes the indexed destination before
 * loading the indexed local source, then uses REV for STNL. Both address
 * calculations only read workspace words. Load the source first, then
 * compute the destination, so STNL receives them in its natural order.
 * Restrict this to an empty stack and the exact indexed-local sequence. */
static void tps_copy_loop_order(struct tps *s)
{
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;
    const int n = s->n;
    int i;

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
        s->removed++;
        tps_note(7);
    }
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
static void tps_cj_over_j(struct tps *s)
{
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;
    const int *idx = s->idx;
    const int n = s->n;
    const int size = s->size;
    int i;

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
static void tps_rev_commutative(struct tps *s)
{
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;
    const int n = s->n;
    int i;

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
            s->removed += 1;
            tps_note(7);
            break;
        default:
            break;
        }
    }
}

/* ---- 5b. rev ; rev  ==  nothing ----
 * REV exchanges AReg and BReg, so two in a row restore the original
 * stack.  Pairs arise when gen_store_addr already leaves AReg=address /
 * BReg=value and store()'s narrow path adds its own REV to guarantee that
 * order: every initialised byte of `char buf[16] = "hello"` emitted one.
 * The second REV must not be a branch target, or control could arrive
 * having executed only one of the pair. */
static void tps_rev_rev(struct tps *s)
{
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;
    const int n = s->n;
    int i;

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
        s->removed += 2;
        tps_note(7);
    }
}

/* A table index can shift right and then scale back up by the same
 * power of two (e.g. table[bits >> 4] with 16-byte rows). Clear the
 * low bits directly. Match the decoded sequence so spills removed by
 * scheduling do not hide it, and preserve every interior entry point. */
static void tps_shift_mask(struct tps *s)
{
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;
    const int n = s->n;
    int i;

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
        s->removed += 2;
    }
}

/*
 * Mark the redundant workspace round trips in a decoded function.  `top` is
 * the number of words tp_gaddr_cache put over the locals.
 * Returns the number of instructions removed.
 */
/* ---- F1: values that need not visit the workspace (2026-11-20) ----
 *
 * Three rules over the scheduled stream, each with its own knob:
 *
 *   S13 (TCC_TRANSPUTER_PARK_UNARY)
 *       Q ; STL a ; U ; LDL a  ->  U ; Q          a dead after
 *     Q pushes one value q over a value v, and U is a stack-neutral run that
 *     works on v alone.  The emitter parked q to reach v; doing U first
 *     needs no park.  core_list_find's `*(p1+2) - *(p2+2)`:
 *         ...; adc 2; ldl 12; adc 2; stl 10; lsx; ldl 10; lsx; diff
 *      -> ...; adc 2; lsx; ldl 12; adc 2; lsx; diff
 *     Neither run stores, calls or branches, so they commute.
 *
 *   S14 (TCC_TRANSPUTER_POSTINC_SINK)
 *       LDL d ; STL t ; LDL d ; M ; STL d ; R ; LDL t
 *    -> R ; LDL d ; LDL d ; M ; STL d               t dead after
 *     `*d++ = <R>`: the old d waits in t while R computes the value; read
 *     it where it is used and do the update there.  Dhrystone's strcpy,
 *     2 of its 17 instructions a byte.  R must not touch d or t, and the
 *     update needs two stack places over what R leaves.
 *
 *   TCC_TRANSPUTER_DUP_STL=1 (off by default: no instruction fewer)
 *       STL a ; LDL a  ->  DUP ; STL a                a live after
 *     The value stays in A instead of coming back from the workspace.
 */
static int tps_movable(const struct tp_relax_insn *c, const struct tps_ann *a)
{
    if (c->pinned || c->synth || c->is_branch || c->tab || c->icall ||
        c->workspace_extent || !a->known || a->dead)
        return 0;
    switch (c->opcode) {
    case TP_ENC_LDL: case TP_ENC_LDC: case TP_ENC_LDNL: case TP_ENC_LDNLP:
    case TP_ENC_ADC: case TP_ENC_EQC:
        return 1;
    case TP_ENC_OPR:
        switch (c->operand) {
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
            return 1;
        }
    }
    return 0;
}

static int tpk_off(int knob, int *cache, int dflt_on)
{
    if (*cache < 0) {
        const char *e = tp_knob(knob);
        if (dflt_on)
            *cache = (e && *e == '0') ? 1 : 0;
        else
            *cache = (e && *e && *e != '0') ? 0 : 1;
    }
    return *cache;
}

/* Rewrite the live records at pos[0..np-1] to hold, in order, the contents
 * of the records listed in from[0..nf-1] (taken before any is written), the
 * rest deleted.  Liveness over the span becomes the union plus `live`;
 * depths are re-simulated from the first position's. */
static void tps_permute(struct tps *s, const int *pos, int np, const int *from,
                        int nf, unsigned int live)
{
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;
    struct tp_relax_insn ci[2 * TPS_MAXWIN + 8];
    struct tps_ann ca[2 * TPS_MAXWIN + 8];
    unsigned int lv = live;
    int k, d = an[pos[0]].depth;

    for (k = 0; k < nf; k++) {
        ci[k] = ins[from[k]];
        ca[k] = an[from[k]];
    }
    for (k = 0; k < np; k++)
        lv |= an[pos[k]].liveout;
    for (k = 0; k < np; k++) {
        int p = pos[k];
        if (k < nf) {
            ins[p].opcode = ci[k].opcode;
            ins[p].operand = ci[k].operand;
            ins[p].old_len = ci[k].old_len;
            ins[p].new_len = ci[k].new_len;
            ins[p].dup_load = 0;
            an[p].use = ca[k].use;
            an[p].def = ca[k].def;
            an[p].uses = ca[k].uses;
            an[p].reads = ca[k].reads;
            an[p].delta = ca[k].delta;
            an[p].known = ca[k].known;
            an[p].proven_pair = 0;
            an[p].dead = 0;
        } else {
            an[p].dead = 1;
        }
        an[p].liveout = lv;
        an[p].depth = d;
        if (d >= 0 && !an[p].dead)
            d += an[p].delta;
    }
}

static int tps_park_unary(struct tps *s)
{
    static int off = -1;
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;
    const int n = s->n;
    int i, hits = 0;

    if (tpk_off(TPK_TCC_TRANSPUTER_PARK_UNARY, &off, 1))
        return 0;
    for (i = 0; i < n; i++) {
        int a, j, l = -1, h, cand, best = -1, p;
        int pos[2 * TPS_MAXWIN + 8], from[2 * TPS_MAXWIN + 8];
        int q[TPS_MAXWIN + 2], u[TPS_MAXWIN + 2], nq = 0, nu = 0, np = 0, k;
        if (an[i].dead || ins[i].pinned || ins[i].opcode != TP_ENC_STL ||
            an[i].head)
            continue;
        a = ins[i].operand;
        if (a < 0 || a >= s->nslot || an[i].depth < 2)
            continue;
        /* U: from h = 1 (v alone), stack-neutral, never below v */
        h = 1;
        for (j = tps_next(ins, an, n, i); j >= 0 && j - i < TPS_MAXWIN;
             j = tps_next(ins, an, n, j)) {
            if (ins[j].opcode == TP_ENC_LDL && ins[j].operand == a) {
                l = j;
                break;
            }
            if (!tps_movable(&ins[j], &an[j]) || an[j].use == a ||
                an[j].def == a || (an[j].uses & (1u << a)) ||
                an[j].reads > h || nu >= TPS_MAXWIN)
                break;
            h += an[j].delta;
            if (h < 1)
                break;
            u[nu++] = j;
        }
        if (l < 0 || h != 1 || nu == 0 || (an[l].liveout & (1u << a)))
            continue;
        /* Q: the shortest run before i that pushes exactly one value and
         * reads nothing it did not push */
        p = tps_prev(ins, an, i);
        for (cand = p; cand >= 0 && p - cand < TPS_MAXWIN;
             cand = tps_prev(ins, an, cand)) {
            int r = 0, ok = 1, m;
            if (!tps_movable(&ins[cand], &an[cand]))
                break;
            for (m = cand; m >= 0 && m <= p; m = tps_next(ins, an, n, m)) {
                if (an[m].reads > r) {
                    ok = 0;
                    break;
                }
                r += an[m].delta;
            }
            if (ok && r == 1) {
                best = cand;
                break;
            }
        }
        if (best < 0 || an[best].head || !tps_free(ins, an, best, l))
            continue;
        for (k = best; k >= 0 && k <= p; k = tps_next(ins, an, n, k)) {
            if (nq >= TPS_MAXWIN)
                break;
            q[nq++] = k;
        }
        if (k >= 0 && k <= p)
            continue;
        for (k = 0; k < nq; k++) pos[np++] = q[k];
        pos[np++] = i;
        for (k = 0; k < nu; k++) pos[np++] = u[k];
        pos[np++] = l;
        for (k = 0; k < nu; k++) from[k] = u[k];
        for (k = 0; k < nq; k++) from[nu + k] = q[k];
        tps_permute(s, pos, np, from, nu + nq, 0);
        s->removed += 2;
        hits++;
        i = l;
    }
    return hits;
}

static int tps_postinc_sink(struct tps *s)
{
    static int off = -1;
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;
    const int n = s->n;
    int i, hits = 0;

    if (tpk_off(TPK_TCC_TRANSPUTER_POSTINC_SINK, &off, 1))
        return 0;
    for (i = 0; i < n; i++) {
        int t, d, p0, p2, p4 = -1, uu = -1, j, h, mh, ok = 1, k;
        int m[TPS_MAXWIN + 2], r[TPS_MAXWIN + 2], nm = 0, nr = 0;
        int pos[2 * TPS_MAXWIN + 8], from[2 * TPS_MAXWIN + 8], np = 0, nf = 0;
        if (an[i].dead || ins[i].pinned || ins[i].opcode != TP_ENC_STL)
            continue;
        t = ins[i].operand;
        if (t < 0 || t >= s->nslot || t >= 32)
            continue;
        p0 = tps_prev(ins, an, i);
        if (p0 < 0 || ins[p0].opcode != TP_ENC_LDL || ins[p0].pinned)
            continue;
        d = ins[p0].operand;
        if (d <= 0 || d == t || d >= 32 || an[p0].depth < 0)
            continue;
        if (!s->frame_private && d >= s->nreg &&
            tps_word_escapes(ins, n, d, s->top))
            continue;
        p2 = tps_next(ins, an, n, i);
        if (p2 < 0 || ins[p2].opcode != TP_ENC_LDL || ins[p2].operand != d)
            continue;
        /* M: the update, stack-neutral over the copy of d */
        h = 1; mh = 1;
        for (j = tps_next(ins, an, n, p2); j >= 0 && j - p2 < TPS_MAXWIN;
             j = tps_next(ins, an, n, j)) {
            if (ins[j].opcode == TP_ENC_STL && ins[j].operand == d && h == 1) {
                p4 = j;
                break;
            }
            if (!tps_movable(&ins[j], &an[j]) || an[j].use == d ||
                an[j].use == t || an[j].reads > h || nm >= TPS_MAXWIN) {
                ok = 0;
                break;
            }
            h += an[j].delta;
            if (h < 1) {
                ok = 0;
                break;
            }
            if (h > mh)
                mh = h;
            m[nm++] = j;
        }
        if (!ok || p4 < 0 || nm == 0)
            continue;
        /* R: up to the reload of t; touches neither d nor t */
        for (j = tps_next(ins, an, n, p4); j >= 0 && j - p4 < TPS_MAXWIN;
             j = tps_next(ins, an, n, j)) {
            int o = ins[j].opcode, x = ins[j].operand;
            if (o == TP_ENC_LDL && x == t) {
                uu = j;
                break;
            }
            if (ins[j].pinned || ins[j].synth || ins[j].is_branch ||
                ins[j].tab || ins[j].icall || !an[j].known ||
                an[j].use == d || an[j].def == d || an[j].use == t ||
                an[j].def == t || (an[j].uses & ((1u << d) | (1u << t))) ||
                (o == TP_ENC_STL && (x == d || x == t)) ||
                (o == TP_ENC_LDLP && (x == d || x == d - 1 || x == t ||
                                      x == t - 1)) ||
                o == TP_ENC_AJW || o == TP_ENC_J || o == TP_ENC_CJ ||
                ((o == TP_ENC_CALL || (o == TP_ENC_OPR &&
                                       x == TP_OPR_GCALL)) &&
                 (d < func_max_outgoing_words ||
                  t < func_max_outgoing_words)) ||
                nr >= TPS_MAXWIN) {
                ok = 0;
                break;
            }
            if (o == TP_ENC_LDLP) {
                int q2 = tps_next(ins, an, n, j);
                if (q2 >= 0 && ins[q2].opcode == TP_ENC_STNL &&
                    (x + ins[q2].operand == d || x + ins[q2].operand == t)) {
                    ok = 0;
                    break;
                }
            }
            r[nr++] = j;
        }
        if (!ok || uu < 0 || an[uu].depth < 0 ||
            an[uu].depth + 1 + mh > 3 || (an[uu].liveout & (1u << t)) ||
            !tps_free(ins, an, p0, uu))
            continue;
        pos[np++] = p0;
        pos[np++] = i;
        pos[np++] = p2;
        for (k = 0; k < nm; k++) pos[np++] = m[k];
        pos[np++] = p4;
        for (k = 0; k < nr; k++) pos[np++] = r[k];
        pos[np++] = uu;
        /* new order: R, LDL d (p0's), LDL d (p2's), M, STL d; the two
         * leftovers (STL t and LDL t) go */
        for (k = 0; k < nr; k++) from[nf++] = r[k];
        from[nf++] = p0;
        from[nf++] = p2;
        for (k = 0; k < nm; k++) from[nf++] = m[k];
        from[nf++] = p4;
        tps_permute(s, pos, np, from, nf, 1u << d);
        s->removed += 2;
        hits++;
        i = uu;
    }
    return hits;
}

static void tps_dup_stl(struct tps *s)
{
    static int off = -1;
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;
    int i, j, a;

    if (tpk_off(TPK_TCC_TRANSPUTER_DUP_STL, &off, 0) || tp_t414() ||
        func_uses_inline_asm)
        return;
    for (i = 0; i < s->n; i++) {
        if (an[i].dead || ins[i].pinned || ins[i].opcode != TP_ENC_STL)
            continue;
        a = ins[i].operand;
        j = tps_next(ins, an, s->n, i);
        if (a < 0 || a >= s->nslot || j < 0 || ins[j].opcode != TP_ENC_LDL ||
            ins[j].operand != a || an[i].depth < 1 || an[i].depth > 2 ||
            !tps_free(ins, an, i, j) ||
            (a >= s->nreg && !tps_proven_nonvolatile_ldl(ins, j)))
            continue;
        ins[i].opcode = TP_ENC_OPR;
        ins[i].operand = TP_OPR_DUP;
        ins[i].new_len = tp_encode_len(TP_ENC_OPR, TP_OPR_DUP);
        an[i].known = tps_effect(TP_ENC_OPR, TP_OPR_DUP, &an[i].delta,
                                 &an[i].reads);
        an[i].use = an[i].def = -1;
        ins[j].opcode = TP_ENC_STL;
        ins[j].new_len = tp_encode_len(TP_ENC_STL, a);
        ins[j].dup_load = 0;
        an[j].known = tps_effect(TP_ENC_STL, a, &an[j].delta, &an[j].reads);
        an[j].use = -1;
        an[j].def = a;
        an[j].depth = an[i].depth + 1;
        i = j;
    }
}

/* ---- bit-serial reflected CRC loops -> crcbyte (-mcrc-idiom) ----
 *
 * A loop that feeds 8 bits into a reflected CRC one at a time,
 *
 *     for (i = 0; i < 8; i++) {
 *         if ((data ^ crc) & 1) crc = (crc >> 1) ^ P; else crc >>= 1;
 *         data >>= 1;
 *     }
 *
 * in any of its spellings, is one crcbyte (ISM: B the checksum, A the data
 * in its top byte, C the generator; result in A).  Reflected, the bits go
 * in the other way round, so
 *
 *     crc' = bitrevword(crcbyte(A = 0, B = bitrevword(crc ^ data), C = gen))
 *
 * with gen the W-bit polynomial reflected back, at the top of the word
 * (P = #A001, W = 16: gen = #80050000).
 *
 * Recognised by what the code computes, not how it is written: a straight
 * region [S, E] ending in the loop's back edge is executed symbolically.
 * Every value is 32 bits, each an affine function over GF(2) of the input
 * bits (the words the region reads before writing), so xor, shifts by
 * constants, and/or with constants, sign extensions and the 0/1 tests stay
 * exact; the loop counter is a constant all the way.  A branch on a
 * one-bit condition runs both ways, and the two paths must meet again with
 * every value differing by a constant: then the merged value is affine too
 * (v0 ^ f.(v0 ^ v1)).  Anything else - memory, calls, a test of two
 * variables, an add of a variable - and the region is not one.
 *
 * At the exit exactly one word written inside may be live, and its value
 * must be T(XOR of the inputs, each masked to its low w bits), T being the
 * 8-step zero-input CRC of some W-bit reflected P (8 <= W <= 32): P is
 * read off the result (the response to the inputs' bit 7), W is its top
 * bit, and every input bit's response is compared with T's.  That is the
 * whole function, so the replacement is exact for every input, not just a
 * tested few.  Off unless -mcrc-idiom (or TCC_TRANSPUTER_CRC_IDIOM=1);
 * not on the T414, which has neither instruction. */
#define TCRC_NW    4                /* 64-bit words of input-bit mask */
#define TCRC_MAXIN (TCRC_NW * 2)    /* input words, 32 bits each */
#define TCRC_SLOTS 64
#define TCRC_STATES 4
#define TCRC_STEPS 6000

struct tcrc_bit { unsigned long long m[TCRC_NW]; unsigned char c; };
struct tcrc_val { struct tcrc_bit b[32]; };
struct tcrc_st {
    int pc, cond, cp;               /* cond: cf == cp on this path */
    struct tcrc_bit cf;
    int sp;                         /* stk[0] is A */
    struct tcrc_val stk[3];
    unsigned long long wr;
    struct tcrc_val slot[TCRC_SLOTS];
};
struct tcrc {
    struct tps *s;
    int inslot[TCRC_MAXIN], nin;
    struct tcrc_st *st[TCRC_STATES];
    int nst;
};

static int tcrc_bit_const(const struct tcrc_bit *b)
{
    int k;
    for (k = 0; k < TCRC_NW; k++)
        if (b->m[k])
            return 0;
    return 1;
}
static int tcrc_bit_eq(const struct tcrc_bit *a, const struct tcrc_bit *b)
{
    return !memcmp(a->m, b->m, sizeof a->m) && a->c == b->c;
}
static int tcrc_mask_eq(const struct tcrc_bit *a, const struct tcrc_bit *b)
{
    return !memcmp(a->m, b->m, sizeof a->m);
}
static void tcrc_bit_xor(struct tcrc_bit *d, const struct tcrc_bit *a)
{
    int k;
    for (k = 0; k < TCRC_NW; k++)
        d->m[k] ^= a->m[k];
    d->c ^= a->c;
}
static void tcrc_const(struct tcrc_val *v, unsigned int k)
{
    int i;
    memset(v, 0, sizeof *v);
    for (i = 0; i < 32; i++)
        v->b[i].c = (k >> i) & 1;
}
static int tcrc_isconst(const struct tcrc_val *v, unsigned int *k)
{
    int i;
    unsigned int r = 0;
    for (i = 0; i < 32; i++) {
        if (!tcrc_bit_const(&v->b[i]))
            return 0;
        r |= (unsigned int)v->b[i].c << i;
    }
    *k = r;
    return 1;
}
/* bits 1..31 the constant 0: a truth value, bit 0 its formula */
static int tcrc_isbit(const struct tcrc_val *v)
{
    int i;
    for (i = 1; i < 32; i++)
        if (!tcrc_bit_const(&v->b[i]) || v->b[i].c)
            return 0;
    return 1;
}

static int tcrc_read(struct tcrc *c, struct tcrc_st *t, int s,
                     struct tcrc_val *v)
{
    int k, i;
    if (s < 0 || s >= TCRC_SLOTS)
        return 0;
    if ((t->wr >> s) & 1) {
        *v = t->slot[s];
        return 1;
    }
    for (k = 0; k < c->nin; k++)
        if (c->inslot[k] == s)
            break;
    if (k == c->nin) {
        if (c->nin == TCRC_MAXIN)
            return 0;
        c->inslot[c->nin++] = s;
    }
    memset(v, 0, sizeof *v);
    for (i = 0; i < 32; i++) {
        int x = k * 32 + i;
        v->b[i].m[x >> 6] = 1ULL << (x & 63);
    }
    return 1;
}

static int tcrc_push(struct tcrc_st *t, const struct tcrc_val *v)
{
    t->stk[2] = t->stk[1];
    t->stk[1] = t->stk[0];
    t->stk[0] = *v;
    if (t->sp < 3)
        t->sp++;
    return 1;
}
static int tcrc_pop(struct tcrc_st *t, struct tcrc_val *v)
{
    if (t->sp <= 0)
        return 0;
    *v = t->stk[0];
    t->stk[0] = t->stk[1];
    t->stk[1] = t->stk[2];
    t->sp--;
    return 1;
}

/* One instruction, not a CJ; 0: the region is not a CRC loop */
static int tcrc_step(struct tcrc *c, struct tcrc_st *t)
{
    struct tps *s = c->s;
    struct tp_relax_insn *in = &s->ins[t->pc];
    struct tcrc_val a, b, r;
    unsigned int ka, kb;
    int i, opc = in->opcode, opd = in->operand;

    switch (opc) {
    case -1:
        t->pc++;
        return 1;
    case TP_ENC_LDC:
        tcrc_const(&r, (unsigned int)opd);
        tcrc_push(t, &r);
        break;
    case TP_ENC_LDL:
        if (!tcrc_read(c, t, opd, &r))
            return 0;
        tcrc_push(t, &r);
        break;
    case TP_ENC_STL:
        if (opd < 0 || opd >= s->nslot || opd >= TCRC_SLOTS ||
            ((s->escaped_slots >> opd) & 1) || !tcrc_pop(t, &a))
            return 0;
        t->slot[opd] = a;
        t->wr |= 1ULL << opd;
        break;
    case TP_ENC_LDLP: {
        /* `ldlp x; [adc k;] lb|lbx|ls|lsx`: part of word x (little
         * endian), the narrow read of a local or parameter */
        int q = t->pc + 1, off = 0, bits, sx, from;
        while (q < s->n && s->ins[q].opcode == -1)
            q++;
        if (q < s->n && s->ins[q].opcode == TP_ENC_ADC &&
            !s->ins[q].pinned) {
            off = s->ins[q].operand;
            for (q++; q < s->n && s->ins[q].opcode == -1; q++)
                ;
        }
        if (q >= s->n || s->ins[q].opcode != TP_ENC_OPR || s->ins[q].pinned ||
            s->an[q].head)
            return 0;
        switch (s->ins[q].operand) {
        case TP_OPR_LB:  bits = 8;  sx = 0; break;
        case TP_OPR_LBX: bits = 8;  sx = 1; break;
        case TP_OPR_LS:  bits = 16; sx = 0; break;
        case TP_OPR_LSX: bits = 16; sx = 1; break;
        default: return 0;
        }
        if (off < 0 || off + bits / 8 > 4 || (bits == 16 && (off & 1)))
            return 0;
        for (i = t->pc + 1; i <= q; i++)
            if (s->an[i].head)
                return 0;
        if (!tcrc_read(c, t, opd, &a))
            return 0;
        memset(&r, 0, sizeof r);
        from = off * 8;
        for (i = 0; i < 32; i++) {
            if (i < bits)
                r.b[i] = a.b[from + i];
            else if (sx)
                r.b[i] = a.b[from + bits - 1];
        }
        tcrc_push(t, &r);
        t->pc = q + 1;
        return 1;
    }
    case TP_ENC_ADC:
        if (!tcrc_pop(t, &a) || !tcrc_isconst(&a, &ka))
            return 0;
        tcrc_const(&r, ka + (unsigned int)opd);
        tcrc_push(t, &r);
        break;
    case TP_ENC_EQC:
        if (!tcrc_pop(t, &a))
            return 0;
        if (tcrc_isconst(&a, &ka)) {
            tcrc_const(&r, ka == (unsigned int)opd);
        } else if (tcrc_isbit(&a) && (opd == 0 || opd == 1)) {
            r = a;
            r.b[0].c ^= opd == 0;
        } else {
            return 0;
        }
        tcrc_push(t, &r);
        break;
    case TP_ENC_J:
        if (s->an[t->pc].succ1 < 0)
            return 0;
        t->pc = s->an[t->pc].succ1;
        return 1;
    case TP_ENC_OPR:
        switch (opd) {
        case TP_OPR_DUP:
            if (t->sp < 1)
                return 0;
            a = t->stk[0];
            tcrc_push(t, &a);
            break;
        case TP_OPR_REV:
            if (t->sp < 2)
                return 0;
            a = t->stk[0];
            t->stk[0] = t->stk[1];
            t->stk[1] = a;
            break;
        case TP_OPR_MINT:
            tcrc_const(&r, 0x80000000u);
            tcrc_push(t, &r);
            break;
        case TP_OPR_NOT:
            if (!tcrc_pop(t, &a))
                return 0;
            for (i = 0; i < 32; i++)
                a.b[i].c ^= 1;
            tcrc_push(t, &a);
            break;
        case TP_OPR_LDIFF: {    /* A = B - A - C, B = the borrow */
            struct tcrc_val cc;
            unsigned int kc;
            unsigned long long dd;
            if (t->sp < 3 || !tcrc_pop(t, &a) || !tcrc_pop(t, &b) ||
                !tcrc_pop(t, &cc) || !tcrc_isconst(&a, &ka) ||
                !tcrc_isconst(&b, &kb) || !tcrc_isconst(&cc, &kc))
                return 0;
            dd = (unsigned long long)kb - ka - (kc & 1);
            tcrc_const(&r, (unsigned int)(dd >> 63) & 1);
            tcrc_push(t, &r);
            tcrc_const(&r, (unsigned int)dd);
            tcrc_push(t, &r);
            break;
        }
        case TP_OPR_XBWORD: case TP_OPR_XSWORD:
            if (!tcrc_pop(t, &a))
                return 0;
            for (i = opd == TP_OPR_XBWORD ? 8 : 16; i < 32; i++)
                a.b[i] = a.b[opd == TP_OPR_XBWORD ? 7 : 15];
            tcrc_push(t, &a);
            break;
        default:
            if (!tcrc_pop(t, &a) || !tcrc_pop(t, &b))
                return 0;
            switch (opd) {
            case TP_OPR_XOR:
                r = b;
                for (i = 0; i < 32; i++)
                    tcrc_bit_xor(&r.b[i], &a.b[i]);
                break;
            case TP_OPR_AND: case TP_OPR_OR:
                for (i = 0; i < 32; i++) {
                    const struct tcrc_bit *x = &a.b[i], *y = &b.b[i];
                    if (!tcrc_bit_const(x)) {
                        const struct tcrc_bit *z = x;
                        x = y;
                        y = z;
                    }
                    if (!tcrc_bit_const(x))
                        return 0;   /* variable & variable */
                    if (x->c == (opd == TP_OPR_AND))
                        r.b[i] = *y;
                    else
                        r.b[i] = *x;
                }
                break;
            case TP_OPR_SHL: case TP_OPR_SHR:
                if (!tcrc_isconst(&a, &ka) || ka > 31)
                    return 0;
                memset(&r, 0, sizeof r);
                for (i = 0; i < 32; i++) {
                    int f = opd == TP_OPR_SHL ? i - (int)ka : i + (int)ka;
                    if (f >= 0 && f < 32)
                        r.b[i] = b.b[f];
                }
                break;
            case TP_OPR_XWORD:              /* B sign-extended at mask A */
                if (!tcrc_isconst(&a, &ka) || !ka || (ka & (ka - 1)))
                    return 0;
                for (i = 0; ka >> (i + 1); i++)
                    ;
                r = b;
                for (int j = i + 1; j < 32; j++) {
                    if (!tcrc_bit_const(&b.b[j]) || b.b[j].c)
                        return 0;           /* outside xword's domain */
                    r.b[j] = b.b[i];
                }
                break;
            default:
                if (!tcrc_isconst(&a, &ka) || !tcrc_isconst(&b, &kb))
                    return 0;
                switch (opd) {
                case TP_OPR_SUM: case TP_OPR_ADD: case TP_OPR_BSUB:
                    tcrc_const(&r, kb + ka); break;
                case TP_OPR_DIFF: case TP_OPR_SUB:
                    tcrc_const(&r, kb - ka); break;
                case TP_OPR_PROD: case TP_OPR_MUL:
                    tcrc_const(&r, kb * ka); break;
                case TP_OPR_WSUB:
                    tcrc_const(&r, ka + 4 * kb); break;
                case TP_OPR_GT:
                    tcrc_const(&r, (int)kb > (int)ka); break;
                case TP_OPR_GTU:
                    tcrc_const(&r, kb > ka); break;
                default:
                    return 0;
                }
            }
            tcrc_push(t, &r);
        }
        break;
    default:
        return 0;
    }
    t->pc++;
    return 1;
}

/* Two paths that split on one condition meet at the same instruction */
static int tcrc_merge(struct tcrc *c, struct tcrc_st *x, struct tcrc_st *y)
{
    struct tcrc_st *z = x->cp == 0 ? x : y, *o = x->cp == 0 ? y : x;
    unsigned long long w = x->wr | y->wr;
    int s, i, k;

    if (!x->cond || !y->cond || x->cp == y->cp || !tcrc_bit_eq(&x->cf, &y->cf))
        return 0;
    for (s = 0; s < TCRC_SLOTS; s++) {
        struct tcrc_val v0, v1;
        if (!((w >> s) & 1))
            continue;
        if (!tcrc_read(c, z, s, &v0) || !tcrc_read(c, o, s, &v1))
            return 0;
        for (i = 0; i < 32; i++) {
            if (tcrc_bit_eq(&v0.b[i], &v1.b[i]))
                continue;
            if (!tcrc_mask_eq(&v0.b[i], &v1.b[i]))
                return 0;           /* the paths differ by a variable */
            tcrc_bit_xor(&v0.b[i], &z->cf);
        }
        z->slot[s] = v0;
    }
    z->wr = w;
    if (z->sp != o->sp)
        return 0;
    for (k = 0; k < z->sp; k++)
        for (i = 0; i < 32; i++) {
            if (tcrc_bit_eq(&z->stk[k].b[i], &o->stk[k].b[i]))
                continue;
            if (!tcrc_mask_eq(&z->stk[k].b[i], &o->stk[k].b[i]))
                return 0;
            tcrc_bit_xor(&z->stk[k].b[i], &z->cf);
        }
    z->cond = 0;
    if (z != x)
        *x = *z;
    return 1;
}

static void tcrc_free(struct tcrc *c)
{
    int k;
    for (k = 0; k < TCRC_STATES; k++)
        if (c->st[k]) {
            tcc_free(c->st[k]);
            c->st[k] = NULL;
        }
}

/* Run [S, E] from S with empty stacks; the state at X, the one way out,
 * in *out.  0 if the region does anything outside the model. */
/* padding between the region's end and its exit is the exit */
#define TCRC_NORM(p) ((p) > E && (p) < X ? X : (p))
static int tcrc_run(struct tcrc *c, int S, int E, int X, struct tcrc_st **out)
{
    struct tps *s = c->s;
    int steps = 0, k;

    c->nin = 0;
    c->st[0] = tcc_mallocz(sizeof(struct tcrc_st));
    c->st[0]->pc = S;
    c->nst = 1;
    for (;;) {
        struct tcrc_st *t;
        int m = 0, same = -1, pc, nx;
        if (c->nst == 0 || ++steps > TCRC_STEPS)
            return 0;
        for (k = 1; k < c->nst; k++)
            if (c->st[k]->pc < c->st[m]->pc)
                m = k;
        for (k = 0; k < c->nst; k++)
            if (k != m && c->st[k]->pc == c->st[m]->pc) {
                if (same >= 0)
                    return 0;
                same = k;
            }
        if (same >= 0) {
            if (s->an[c->st[m]->pc].head || c->st[m]->pc == X)
                c->st[m]->sp = c->st[same]->sp = 0;
            if (!tcrc_merge(c, c->st[m], c->st[same]))
                return 0;
            tcc_free(c->st[same]);
            c->st[same] = c->st[--c->nst];
            c->st[c->nst] = NULL;
            if (m == c->nst)
                m = same;
        }
        t = c->st[m];
        pc = t->pc;
        if (pc == X) {
            if (c->nst != 1 || t->cond)
                return 0;
            *out = t;
            return 1;
        }
        if (pc < S || pc > E)
            return 0;
        if (s->ins[pc].pinned || s->ins[pc].tab || s->ins[pc].icall)
            return 0;
        if (s->an[pc].head && pc != S)
            t->sp = 0;          /* nothing on the stack survives a label */
        if (s->ins[pc].opcode == TP_ENC_CJ) {
            struct tcrc_val v;
            unsigned int kv;
            int tg = s->an[pc].succ2;
            if (tg < 0 || t->sp < 1)
                return 0;
            v = t->stk[0];
            if (tcrc_isconst(&v, &kv)) {
                if (kv == 0) {
                    nx = tg;
                } else {
                    tcrc_pop(t, &v);
                    nx = pc + 1;
                }
            } else if (!tcrc_isbit(&v)) {
                return 0;
            } else if (t->cond) {
                /* the same condition again: decided on this path */
                if (!tcrc_mask_eq(&v.b[0], &t->cf))
                    return 0;
                if ((v.b[0].c ^ t->cf.c ^ t->cp) == 0) {
                    tcrc_const(&t->stk[0], 0);
                    nx = tg;
                } else {
                    tcrc_pop(t, &v);
                    nx = pc + 1;
                }
            } else {
                struct tcrc_st *u;
                if (c->nst == TCRC_STATES)
                    return 0;
                u = tcc_malloc(sizeof *u);
                *u = *t;
                c->st[c->nst++] = u;
                t->cond = u->cond = 1;
                t->cf = u->cf = v.b[0];
                t->cp = 0;              /* taken: the bit is 0 */
                tcrc_const(&t->stk[0], 0);
                t->pc = TCRC_NORM(tg);
                u->cp = 1;
                tcrc_pop(u, &v);
                u->pc = TCRC_NORM(pc + 1);
                if (tg < pc)
                    return 0;
                continue;
            }
            if (nx < pc && (c->nst != 1 || t->cond))
                return 0;               /* a loop round a split path */
            t->pc = TCRC_NORM(nx);
            continue;
        }
        if (!tcrc_step(c, t))
            return 0;
        if (t->pc < pc && (c->nst != 1 || t->cond))
            return 0;
        t->pc = TCRC_NORM(t->pc);
    }
}
#undef TCRC_NORM

/* What the region leaves on the stack at X (a loop test's borrow, say) is
 * nobody's if the code from X reads nothing it did not push, up to a ret
 * or a label (where the emitter assumes an empty stack). */
static int tcrc_leftover_dead(struct tps *s, int X, int sp)
{
    int q, r = 0, steps = 0;
    if (sp == 0 || s->an[X].head)
        return 1;
    for (q = X; q < s->n && steps < 40; q++) {
        int o = s->ins[q].opcode;
        if (o == -1)
            continue;
        steps++;
        if (q > X && s->an[q].head)
            return 1;
        if (o == TP_ENC_OPR && s->ins[q].operand == TP_OPR_RET)
            return 1;
        if (o == TP_ENC_J || o == TP_ENC_CJ || o == TP_ENC_CALL ||
            !s->an[q].known || s->an[q].reads > r)
            return 0;
        r += s->an[q].delta;
    }
    return 0;
}

static int tps_crc_lower(struct tps *s)
{
    struct tp_relax_insn *ins = s->ins;
    struct tps_ann *an = s->an;
    const int n = s->n;
    int k, hits = 0;
    static int env = -2;

    if (env == -2) {
        const char *e = tp_knob(TPK_TCC_TRANSPUTER_CRC_IDIOM);
        env = !e || !*e ? -1 : *e != '0';
    }
    if (!(env >= 0 ? env : tcc_state->tp_crc_idiom) || tp_t414() ||
        func_uses_inline_asm || func_has_volatile)
        return 0;
    for (k = 0; k < n; k++) {
        int h, E = k, X, S, tries = 0, nxor = 0, nshr = 0, j;
        if (ins[k].opcode == TP_ENC_CJ)
            h = an[k].succ2;
        else if (ins[k].opcode == TP_ENC_J)
            h = an[k].succ1;
        else
            continue;
        if (h < 0 || h > k || k - h > 600)
            continue;
        for (j = h; j <= k; j++) {
            int o = ins[j].opcode, d = ins[j].operand;
            if (o == TP_ENC_OPR && d == TP_OPR_XOR) nxor++;
            if (o == TP_ENC_OPR && d == TP_OPR_SHR) nshr++;
            if (o == TP_ENC_CALL || o == TP_ENC_LDNL ||
                o == TP_ENC_STNL || o == TP_ENC_AJW ||
                (o == TP_ENC_OPR && (d == TP_OPR_GCALL || d == TP_OPR_RET ||
                                     d == TP_OPR_SB)))
                break;
        }
        if (j <= k || !nxor || !nshr)
            continue;
        for (X = k + 1; X < n && ins[X].opcode == -1; X++)
            ;
        if (X >= n)
            continue;
        /* S: back from the loop to a point with nothing on the stack, as
         * far as the straight code before it goes (the entry masks are
         * what show the high bits do not matter) */
        for (S = h; S >= 0 && tries < 24; S--) {
            struct tcrc c;
            struct tcrc_st *f = NULL;
            int ok = 0, o = -1, i, q, b, nw = 0;
            unsigned int livein, P = 0, wmask[TCRC_MAXIN], gen = 0, N = 0;
            int W = 0, ord[TCRC_MAXIN], no = 0;
            if (ins[S].opcode == -1)
                continue;
            if (ins[S].opcode == TP_ENC_CALL || ins[S].opcode == TP_ENC_AJW ||
                ins[S].opcode == TP_ENC_STNL ||
                ins[S].opcode == TP_ENC_LDNL || ins[S].pinned ||
                (ins[S].opcode == TP_ENC_OPR &&
                 (ins[S].operand == TP_OPR_GCALL ||
                  ins[S].operand == TP_OPR_RET)))
                break;
            if (an[S].depth != 0)
                continue;
            tries++;
            /* nothing outside may jump into the middle */
            for (i = 0; i < n; i++) {
                int t2;
                if (i >= S && i <= E)
                    continue;
                t2 = ins[i].opcode == TP_ENC_CJ ? an[i].succ2 :
                     ins[i].opcode == TP_ENC_J ? an[i].succ1 : -1;
                if (ins[i].tab > 0 && ins[i].is_branch)
                    t2 = an[i].succ1;
                if (t2 > S && t2 <= E)
                    break;
            }
            if (i < n)
                continue;
            memset(&c, 0, sizeof c);
            c.s = s;
            if (!tcrc_run(&c, S, E, X, &f))
                f = NULL;
            if (f && tcrc_leftover_dead(s, X, f->sp)) {
                livein = an[X].liveout;
                if (an[X].def >= 0)
                    livein &= ~(1u << an[X].def);
                if (an[X].use >= 0)
                    livein |= 1u << an[X].use;
                livein |= an[X].uses;
                for (i = 0; i < 32; i++)
                    if (((f->wr >> i) & 1) && ((livein >> i) & 1)) {
                        o = i;
                        nw++;
                    }
                ok = nw == 1;
            }
            if (ok) {
                const struct tcrc_val *v = &f->slot[o];
                unsigned int col[TCRC_MAXIN][32];
                memset(col, 0, sizeof col);
                for (i = 0; i < 32 && ok; i++) {
                    if (v->b[i].c)
                        ok = 0;
                    for (q = 0; q < c.nin * 32; q++)
                        if ((v->b[i].m[q >> 6] >> (q & 63)) & 1)
                            col[q >> 5][q & 31] |= 1u << i;
                }
                for (q = 0; q < c.nin && ok && !P; q++)
                    P = col[q][7];
                if (ok && P) {
                    for (W = 32; !((P >> (W - 1)) & 1); W--)
                        ;
                    if (W < 8)
                        ok = 0;
                } else {
                    ok = 0;
                }
                for (q = 0; q < c.nin && ok; q++) {
                    int w = 0;
                    for (b = 0; b < W; b++) {
                        unsigned int x = 1u << b;
                        int st;
                        for (st = 0; st < 8; st++)
                            x = (x & 1) ? (x >> 1) ^ P : x >> 1;
                        if (col[q][b] != x)
                            break;
                        w++;
                    }
                    for (b = w; b < 32; b++)
                        if (col[q][b])
                            ok = 0;
                    if (w == 0)
                        ok = 0;     /* an input with no part in it */
                    wmask[q] = w == 32 ? 0xffffffffu : (1u << w) - 1;
                }
            }
            if (ok) {
                int oc[48], od[48], m = 0, depth = 0, gf, cnt = 0, rr;
                for (b = 0; b < W; b++)
                    if ((P >> b) & 1)
                        N |= 1u << (W - 1 - b);
                gen = W == 32 ? N : N << (32 - W);
                /* masked inputs first: each mask needs a place */
                for (q = 0; q < c.nin; q++)
                    if (wmask[q] != 0xffffffffu)
                        ord[no++] = q;
                for (q = 0; q < c.nin; q++)
                    if (wmask[q] == 0xffffffffu)
                        ord[no++] = q;
                for (q = 0; q < no; q++) {
                    int d = (q ? 2 : 1) + (wmask[ord[q]] != 0xffffffffu);
                    if (d > depth)
                        depth = d;
                }
                gf = 1 + depth <= 3;
                if (gf) { oc[m] = TP_ENC_LDC; od[m++] = (int)gen; }
                for (q = 0; q < no; q++) {
                    oc[m] = TP_ENC_LDL; od[m++] = c.inslot[ord[q]];
                    if (wmask[ord[q]] != 0xffffffffu) {
                        oc[m] = TP_ENC_LDC; od[m++] = (int)wmask[ord[q]];
                        oc[m] = TP_ENC_OPR; od[m++] = TP_OPR_AND;
                    }
                    if (q) { oc[m] = TP_ENC_OPR; od[m++] = TP_OPR_XOR; }
                }
                oc[m] = TP_ENC_OPR; od[m++] = TP_OPR_BITREVWORD;
                if (!gf) {
                    oc[m] = TP_ENC_LDC; od[m++] = (int)gen;
                    oc[m] = TP_ENC_OPR; od[m++] = TP_OPR_REV;
                }
                oc[m] = TP_ENC_LDC; od[m++] = 0;
                oc[m] = TP_ENC_OPR; od[m++] = TP_OPR_CRCBYTE;
                oc[m] = TP_ENC_OPR; od[m++] = TP_OPR_BITREVWORD;
                oc[m] = TP_ENC_STL; od[m++] = o;
                for (i = S; i <= E; i++)
                    cnt++;
                if (cnt < m)
                    ok = 0;
                for (i = S, rr = 0; ok && i <= E; i++) {
                    if (rr < m) {
                        ins[i].opcode = oc[rr];
                        ins[i].operand = od[rr];
                        ins[i].new_len = tp_encode_len(oc[rr], od[rr]);
                        rr++;
                    } else {
                        ins[i].opcode = -1;
                        ins[i].operand = 0;
                    }
                    ins[i].is_branch = 0;
                    ins[i].dup_load = 0;
                }
                if (ok) {
                    if (tp_knob(TPK_TP_NARROW_STATS))
                        fprintf(stderr, "[CRC] %s: W=%d P=%#x gen=%#x "
                                "inputs %d, %d instructions -> %d\n",
                                funcname ? funcname : "?", W, P, gen,
                                c.nin, E - S + 1, m);
                    hits++;
                }
            }
            tcrc_free(&c);
            if (ok) {
                k = E;
                break;
            }
        }
    }
    return hits;
}

static int tp_sched_function(struct tp_relax_insn *ins, int n, int size,
                             int top)
{
    struct tps s;
    int i, round;
    static int sched_off = -1;

    if (sched_off < 0) {
        sched_off = tp_knob(TPK_TP_NO_SCHED) ? 1 : 0;
    }
    if (sched_off || n <= 0 || size <= 0)
        return 0;

    s.ins = ins;
    s.n = n;
    s.size = size;
    s.top = top;
    s.removed = 0;
    s.resv_escape = 0;
    s.nreg = (func_frame_reserved < NB_REGS) ? func_frame_reserved : NB_REGS;
    s.nslot = func_frame_words;
    if (s.nslot > 32)
        s.nslot = 32;
    if (s.nslot < s.nreg)
        s.nslot = s.nreg;
    tps_prescan(&s);

    s.an = tcc_mallocz(sizeof(*s.an) * n);
    s.idx = tcc_malloc(sizeof(int) * (size + 1));
    for (i = 0; i <= size; i++)
        s.idx[i] = -1;
    for (i = 0; i < n; i++)
        if (!ins[i].synth)              /* a jump lands on the record */
            s.idx[ins[i].old_off] = i;
    s.idx[size] = -1;                     /* a branch past the last byte */

    if (!tps_annotate(&s))
        goto done;
    tps_mark_heads(&s);
    /* CRC loops first, on the code as emitted; a rewrite renumbers
     * nothing but changes the flow graph, so the analyses start again */
    tps_depths(&s);
    if (tps_liveness(&s) && tps_crc_lower(&s)) {
        s.removed = 0;
        memset(s.an, 0, sizeof(*s.an) * n);
        if (!tps_annotate(&s))
            goto done;
        tps_mark_heads(&s);
    } else {
        for (i = 0; i < n; i++)
            s.an[i].liveout = 0;
    }
    tps_block_copies(&s);
    tps_note_indexed_pairs(&s);
    tps_depths(&s);
    if (!tps_liveness(&s))
        goto done;
    tps_drop_unread_results(&s);
    tps_copy_propagate(&s);
    tps_reserved_escape(&s);

    for (round = 0; round < TPS_ROUNDS; round++)
        if (!tps_round(&s))
            break;
    if (tps_park_unary(&s) + tps_postinc_sink(&s))
        for (round = 0; round < TPS_ROUNDS; round++)
            if (!tps_round(&s))
                break;

    tps_dup_indexed_pair(&s);
    tps_rev_swap_pushes(&s);
    tps_rev_hoist_push(&s);
    tps_copy_loop_order(&s);
    tps_cj_over_j(&s);
    tps_rev_commutative(&s);
    tps_rev_rev(&s);
    tps_shift_mask(&s);
    tps_dup_stl(&s);
    tps_mark_dup_loads(&s);

    /* ---- 6. commit ---- */
    for (i = 0; i < n; i++) {
        if (s.an[i].dead) {
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
                funcname ? funcname : "?", s.removed);
done:
    tcc_free(s.idx);
    tcc_free(s.an);
    return s.removed;
}

static int tpg_ldlp_extent(const struct tp_relax_insn *c, int *lo);

/* What the phases of tp_slot_remap() share.  F is the frame's words, R the
 * reserved region's, P the pinned low slots (slot 0 and the outgoing stack
 * arguments). */
struct tpsm_work {
    struct tp_relax_insn *ins;
    int n;
    int F, R, P;
    char *cut;                  /* cut[s]: a unit starts at slot s */
    int pin_top;                /* an LDLP of the frame top pins the last unit */
    int *unit, *ustart, *usize, *uweight, *uorder, *unew;
    int nu;
    int lo, hi;                 /* the movable slots are [lo, hi) */
    int stats;
};

/* No GAJW, and no AJW but the prologue's and an epilogue's. */
static int tpsm_shape_ok(struct tpsm_work *g)
{
    struct tp_relax_insn *ins = g->ins;
    const int n = g->n;
    int first, i, j;

    for (first = 0; first < n && ins[first].opcode == -1; first++)
        ;
    for (i = 0; i < n; i++) {
        if (ins[i].opcode == TP_ENC_OPR && ins[i].operand == TP_OPR_GAJW)
            return 0;
        if (ins[i].opcode == TP_ENC_AJW && i != first && !ins[i].icall) {
            for (j = i + 1; j < n && ins[j].opcode == -1; j++)
                ;
            if (!(j < n && ins[j].opcode == TP_ENC_OPR &&
                  ins[j].operand == TP_OPR_RET))
                return 0;
        }
    }
    return 1;
}

/* cut[s]: a unit starts at slot s.  Every reserved slot is its own unit;
 * locals are cut at each recorded `loc` that falls on a word boundary. */
static int tpsm_cuts(struct tpsm_work *g)
{
    struct tp_relax_insn *ins = g->ins;
    const int n = g->n;
    const int F = g->F;
    const int R = g->R;
    const int P = g->P;
    char *cut;
    int pin_top = 0;
    int i, j, s;

    cut = g->cut = tcc_mallocz(F + 1);
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
        if (ins[i].workspace_extent)
            extent = ins[i].workspace_extent;
        else if (j < n)
            extent = tpg_ldlp_extent(&ins[j], &bias);
        /* A known wide access ties its words together, including when
         * its first word is pinned outgoing storage.  Unknown pseudo-
         * register escapes retain the conservative path below. */
        if (extent > 1)
            recognised = 1;
        if (recognised) {
            int64_t begin = (int64_t)s + bias;
            int64_t end = begin + extent - 1;
            if (begin < INT32_MIN || end > INT32_MAX)
                return 0;
            t = begin;
            last = end;
            if ((s < 0 || s >= F) != (t < 0 || t >= F) ||
                (s < 0 || s >= F) != (last < 0 || last >= F)) {
                if (s < P && t < P && last < P)
                    continue;               /* staging, which stays put */
                return 0;                  /* reaches across the frame edge */
            }
            if (s >= 0 && s < F && (t != s || last != s)) {
                int a = s < t ? s : t, b = s > last ? s : last, k;
                for (k = a + 1; k <= b; k++)
                    cut[k] = 0;
            }
        } else if (s >= 0 && s < R) {
            if (s >= P)
                return 0;                  /* a pseudo-register escapes */
        } else if (s > R && s < F) {
            cut[s] = 0;                     /* maybe one past the unit below */
        } else if (s == F) {
            pin_top = 1;
        }
    }
    g->pin_top = pin_top;
    return 1;
}

/* Units, their weights, and the pinned ends: [0, lo) holds slot 0 and the
 * staging words, [hi, F) the unit an LDLP of the frame top points past. */
static void tpsm_units(struct tpsm_work *g)
{
    struct tp_relax_insn *ins = g->ins;
    const int n = g->n;
    const int F = g->F;
    const int P = g->P;
    char *cut = g->cut;
    const int pin_top = g->pin_top;
    int *unit, *ustart, *usize, *uweight, *uorder, *unew;
    int nu = 0, lo, hi;
    int i, s;

    unit = g->unit = tcc_malloc(F * sizeof(int));
    ustart = g->ustart = tcc_malloc((F + 1) * sizeof(int));
    usize = g->usize = tcc_malloc((F + 1) * sizeof(int));
    uweight = g->uweight = tcc_mallocz((F + 1) * sizeof(int));
    uorder = g->uorder = tcc_malloc((F + 1) * sizeof(int));
    unew = g->unew = tcc_malloc((F + 1) * sizeof(int));
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
    g->nu = nu;
    g->lo = lo;
    g->hi = hi;
}

/* Lay the movable units out from lo, densest first; insertion sort keeps
 * equal densities in frame order. */
static void tpsm_layout(struct tpsm_work *g)
{
    const int F = g->F;
    int *unit = g->unit;
    int *ustart = g->ustart;
    int *usize = g->usize;
    int *uweight = g->uweight;
    int *uorder = g->uorder;
    int *unew = g->unew;
    const int nu = g->nu;
    const int lo = g->lo;
    const int hi = g->hi;
    int pos;

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
}

/* Is the function smaller with the new numbering? */
static int tpsm_gain(struct tpsm_work *g)
{
    struct tp_relax_insn *ins = g->ins;
    const int n = g->n;
    int *unit = g->unit;
    int *ustart = g->ustart;
    int *unew = g->unew;
    const int lo = g->lo;
    const int hi = g->hi;
    const int stats = g->stats;
    int old_bytes = 0, new_bytes = 0;
    int i, s;

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
        return 0;
    return 1;
}

/* Rewrite every access to a moved slot. */
static void tpsm_apply(struct tpsm_work *g)
{
    struct tp_relax_insn *ins = g->ins;
    const int n = g->n;
    int *unit = g->unit;
    int *ustart = g->ustart;
    int *unew = g->unew;
    const int lo = g->lo;
    const int hi = g->hi;
    int i, s;

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
static int tp_slot_remap(struct tp_relax_insn *ins, int n, int top)
{
    static int off = -1, stats = -1;
    int F = func_frame_words - top, R = func_frame_reserved;
    int P = func_max_stack_words > 1 ? func_max_stack_words : 1;
    struct tpsm_work g;
    int ok;

    if (off < 0) {
        off = tp_knob(TPK_TP_NO_SLOTMAP) ? 1 : 0;
        stats = tp_knob(TPK_TP_SLOTMAP_STATS) ? 1 : 0;
    }
    if (off || func_uses_inline_asm || tp_loc_marks_bad ||
        F <= P + 1 || F > 4096 || R > F)
        return 0;

    memset(&g, 0, sizeof g);
    g.ins = ins;
    g.n = n;
    g.F = F;
    g.R = R;
    g.P = P;
    g.stats = stats;
    ok = tpsm_shape_ok(&g) && tpsm_cuts(&g);
    if (ok) {
        tpsm_units(&g);
        tpsm_layout(&g);
        ok = tpsm_gain(&g);
    }
    if (ok)
        tpsm_apply(&g);
    tcc_free(g.unit); tcc_free(g.ustart); tcc_free(g.usize);
    tcc_free(g.uweight); tcc_free(g.uorder); tcc_free(g.unew);
    tcc_free(g.cut);
    return ok;
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
 * When a function uses one symbol's address often enough, keep it in a
 * frame word and replace repeated uses with `ldl`. Two uses in one
 * block can instead fill the word at the first use, paying for the
 * setup only when that block runs. For the entry cache the load can
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
 * (tp_gaddr_commit): the initializer's relocation stays at the first use
 * or moves to the entry load, and the other uses' relocations are deleted.
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

/* What the phases of tp_gaddr_cache() share. */
struct tpg_work {
    struct tp_relax_insn *ins;
    int n;
    int *tidx, size;
    int F;                      /* frame words before the new ones */
    int first;                  /* the prologue's ajw */
    int *u, nu;                 /* the uses, TPG_U ints each */
    int *block;                 /* first record of each basic block */
    int pick[TPG_MAX], plen[TPG_MAX], key[TPG_MAX];
    int init[TPG_MAX];          /* insert before this record */
    int at_use[TPG_MAX];        /* initialize after an existing LDC */
    int w;                      /* words chosen */
    int stats;
};

/* The frame's shape: `ajw -F` first, `ajw F; ret` to leave, and every
 * other access to a frame word an LDL, STL or LDLP. */
static int tpg_frame_ok(struct tpg_work *g)
{
    struct tp_relax_insn *ins = g->ins;
    const int n = g->n;
    const int F = g->F;
    int first;
    int i, j;

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
        if (ins[i].icall)
            continue;           /* names no frame word; Wptr comes back */
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
    g->first = first;
    return 1;
}

/* The uses.  A symbol's: an LDC_ABS relocation on an untouched
 * placeholder, seven PFIX 0 and `ldc 0`, which the pinning step decoded
 * as eight records.  A constant's: an `ldc` of six bytes or more, a
 * float's bits or a wide mask, but not an `ldc; ldpi` offset, which
 * moves with the code. */
static int tpg_uses(struct tpg_work *g)
{
    Section *sec = cur_text_section;
    struct tp_relax_insn *ins = g->ins;
    int *tidx = g->tidx;
    const int n = g->n;
    const int size = g->size;
    const int base = func_start_ind;
    int *u;
    int nu = 0;
    int i, j, k, nrel;
    ElfW_Rel *rel;

    rel = (ElfW_Rel *)sec->reloc->data;
    nrel = sec->reloc->data_offset / sizeof(*rel);
    if (nrel < func_start_nrel)
        return 0;
    u = g->u = tcc_malloc(sizeof(int) * TPG_U * (nrel - func_start_nrel + n));
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
    if (nu < 1)
        return 0;
    g->nu = nu;
    return 1;
}

/* A use under a backward branch is in a loop. */
static void tpg_loops(struct tpg_work *g)
{
    struct tp_relax_insn *ins = g->ins;
    int *u = g->u;
    const int n = g->n;
    const int nu = g->nu;
    int *lp;
    int nb = 0;
    int i, j;

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
}

/* Include labeled entries as well as the targets of decoded branches and
 * switch entries. End the block at calls, transfers and returns. */
static void tpg_blocks(struct tpg_work *g)
{
    struct tp_relax_insn *ins = g->ins;
    Section *sec = cur_text_section;
    ElfSym *es = (ElfSym *)symtab_section->data + func_start_nsym;
    ElfSym *end = (ElfSym *)(symtab_section->data + symtab_section->data_offset);
    Sym *label;
    int i, start = 0, next = 0, *block;

    for (i = 0; i < g->n; i++)
        if (ins[i].opcode == TP_ENC_OPR && (ins[i].operand == TP_OPR_LEND ||
            (ins[i].operand == TP_OPR_GCALL && !ins[i].icall && !ins[i].tab)))
            return;                 /* a transfer with unknown successors */
    block = g->block = tcc_mallocz(sizeof(*block) * g->n);
    for (i = 0; i < g->n; i++) {
        int t = ins[i].target_old;
        if (ins[i].is_branch && t >= 0 && t < g->size && g->tidx[t] >= 0)
            block[g->tidx[t]] = 1;
    }
    for (label = global_label_stack; label; label = label->prev) {
        int t = label->jnext - func_start_ind;
        if (label->r == LABEL_DEFINED && t >= 0 && t < g->size && g->tidx[t] >= 0)
            block[g->tidx[t]] = 1;
    }
    for (; es < end; es++) {
        int t = (int)es->st_value - func_start_ind;
        if (es->st_shndx == sec->sh_num && t >= 0 && t < g->size && g->tidx[t] >= 0)
            block[g->tidx[t]] = 1;
    }
    for (i = 0; i < g->n; i++) {
        if (next || block[i] || ins[i].tab)
            start = i;
        block[i] = start;
        next = ins[i].is_branch || ins[i].tab || ins[i].opcode == TP_ENC_CALL ||
               ins[i].opcode == TP_ENC_AJW || (ins[i].opcode == TP_ENC_OPR &&
               (ins[i].operand == TP_OPR_RET || ins[i].operand == TP_OPR_GCALL));
    }
}

static int tpg_pair_in_block(struct tpg_work *g, int first, int slot)
{
    const int *block = g->block;
    int a = g->u[TPG_U * first + 2];
    int b = g->u[TPG_U * (first + 1) + 2];
    int ld = tp_encode_len(TP_ENC_LDL, slot);
    int st = tp_encode_len(TP_ENC_STL, slot);

    /* Both complete relocation placeholders must lie on the same path.
     * LDC; STL; LDL restores the original stack without a fourth cell. */
    return block && block[a] == block[b + 7] && st + 2 * ld < 8;
}

/* The ones worth a word, heaviest first. */
static int tpg_pick(struct tpg_work *g)
{
    int *u = g->u;
    int *pick = g->pick;
    int *plen = g->plen;
    const int nu = g->nu;
    int w = 0;
    int i, j, k;

    qsort(u, nu, sizeof(int) * TPG_U, tpg_use_cmp);
    for (;;) {
        int best = -1, bestlen = 0, bestwt = 0, best_at_use = 0;
        for (i = 0; i < nu; i += k) {
            int wt = 0, at_use = 0;
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
            if (k == 2 && u[TPG_U * i] == 0 &&
                tpg_pair_in_block(g, i, g->F + w))
                at_use = 1;
            if (!at_use && wt <= 2)
                continue;
            if (j == w && wt > bestwt) {
                best = i;
                bestlen = k;
                bestwt = wt;
                best_at_use = at_use;
            }
        }
        if (best < 0 || w == TPG_MAX)
            break;
        pick[w] = best;
        g->at_use[w] = best_at_use;
        plen[w++] = bestlen;
    }
    if (!w)
        return 0;
    g->w = w;
    return 1;
}

/* Make room: the new words are F .. F+w-1. */
static void tpg_room(struct tpg_work *g)
{
    struct tp_relax_insn *ins = g->ins;
    const int n = g->n;
    const int F = g->F;
    const int w = g->w;
    int i;

    for (i = 0; i < n; i++) {
        int opc = ins[i].opcode;
        if ((opc == TP_ENC_LDL || opc == TP_ENC_STL) && ins[i].operand >= F)
            ins[i].operand += w;
        else if (opc == TP_ENC_LDLP && tpg_ldlp_above(ins, n, i, F) > 0)
            ins[i].operand += w;
        else if (opc == TP_ENC_AJW && !ins[i].icall)
            ins[i].operand += ins[i].operand < 0 ? -w : w;
        else
            continue;
        if (tp_encode_len(opc, ins[i].operand) > ins[i].new_len)
            ins[i].new_len = tp_encode_len(opc, ins[i].operand);
    }
}

/* Every use becomes `ldl word`; a placeholder's PFIX 0 go. */
static void tpg_rewrite(struct tpg_work *g)
{
    struct tp_relax_insn *ins = g->ins;
    int *u = g->u;
    int *pick = g->pick;
    int *plen = g->plen;
    int *key = g->key;
    const int F = g->F;
    const int w = g->w;
    const int stats = g->stats;
    int i, j, k;

    tpg.ndrop = 0;
    for (j = 0; j < w; j++) {
        int sym = u[TPG_U * pick[j]] == 0;
        key[j] = u[TPG_U * pick[j] + 1];
        tpg.move_rel[j] = -1;
        g->init[j] = g->first + 1;
        for (i = pick[j]; i < pick[j] + plen[j]; i++) {
            int r = u[TPG_U * i + 2];
            if (g->at_use[j] && i == pick[j]) {
                g->init[j] = r + 8;
                continue;
            }
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
                fprintf(stderr, "[GADDR] %s: %s, %d uses, weight %d%s\n",
                        funcname ? funcname : "?",
                        (char *)symtab_section->link->data + es->st_name,
                        plen[j], wt, g->at_use[j] ? " at first use" : "");
            } else {
                fprintf(stderr, "[GADDR] %s: #%08X, %d uses, weight %d\n",
                        funcname ? funcname : "?", (unsigned)key[j],
                        plen[j], wt);
            }
        }
    }
}

/* Insert each initializer's two instructions. Branches to an original
 * instruction still land on it. The first-use form is inside its block. */
static void tpg_insert(struct tpg_work *g)
{
    int *tidx = g->tidx;
    int *key = g->key;
    const int size = g->size;
    const int F = g->F;
    const int w = g->w;
    struct tp_relax_insn *old = g->ins, *ins;
    int n = g->n;
    int i, j, k, out = 0;
    int order[TPG_MAX];

    for (j = 0; j < w; j++) {
        for (k = j; k > 0 && g->init[order[k - 1]] > g->init[j]; k--)
            order[k] = order[k - 1];
        order[k] = j;
    }
    ins = g->ins = tcc_malloc(sizeof(*ins) * (n + 2 * w + 1));
    for (i = 0, k = 0; i < n; i++) {
        for (; k < w && g->init[order[k]] == i; k++) {
            struct tp_relax_insn *a = &ins[out], *b = a + 1;
            j = order[k];
            memset(a, 0, 2 * sizeof(*a));
            a->old_off = b->old_off = old[i].old_off;
            a->synth = b->synth = 1;
            if (g->at_use[j]) {
                a->opcode = TP_ENC_STL;
                b->opcode = TP_ENC_LDL;
                a->operand = b->operand = F + j;
                a->new_len = tp_encode_len(a->opcode, a->operand);
            } else {
                a->opcode = TP_ENC_LDC;
                if (tpg.move_rel[j] >= 0) {
                    a->new_len = 8;
                    a->pinned = 1;
                } else {
                    a->operand = key[j];
                    a->new_len = tp_encode_len(a->opcode, a->operand);
                }
                b->opcode = TP_ENC_STL;
                b->operand = F + j;
                tpg.load_rec[j] = out;
            }
            b->new_len = tp_encode_len(b->opcode, b->operand);
            out += 2;
        }
        ins[out++] = old[i];
    }
    tcc_free(old);
    for (i = 0; i <= size; i++)
        tidx[i] = -1;
    for (i = 0; i < out; i++)
        if (!ins[i].synth)
            tidx[ins[i].old_off] = i;
    g->n = out;
}

/* Returns 1 if it rewrote anything; *pins may then have moved and *pn
 * grown, and tidx is rebuilt. */
static int tp_gaddr_cache(struct tp_relax_insn **pins, int *pn, int *tidx,
                          int size)
{
    static int off = -1, stats = -1;
    Section *sec = cur_text_section;
    struct tpg_work g;
    int F = func_frame_words;
    int ok;

    tpg.n = 0;
    if (off < 0) {
        const char *e = tp_knob(TPK_TCC_TRANSPUTER_GADDR);
        off = (e && *e == '0') ? 1 : 0;
        stats = tp_knob(TPK_TP_GADDR_STATS) ? 1 : 0;
    }
    if (off || func_uses_inline_asm || !sec->reloc || F <= 0)
        return 0;

    g.ins = *pins;
    g.n = *pn;
    g.tidx = tidx;
    g.size = size;
    g.F = F;
    g.u = NULL;
    g.block = NULL;
    g.stats = stats;
    ok = tpg_frame_ok(&g) && tpg_uses(&g);
    if (ok) {
        tpg_loops(&g);
        tpg_blocks(&g);
        ok = tpg_pick(&g);
    }
    if (ok) {
        tpg_room(&g);
        tpg_rewrite(&g);
        tpg_insert(&g);
        func_frame_words = F + g.w;
        tpg.n = g.w;
        *pins = g.ins;
        *pn = g.n;
    }
    tcc_free(g.u);
    tcc_free(g.block);
    return ok;
}

/* Relocations of calls tp_inline_leaves replaced, deleted on commit. */
static int tpi_drop[256], tpi_ndrop;

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
    for (i = 0; tpg.n && i < tpg.ndrop; i++)
        gone[tpg.drop[i] - func_start_nrel] = 1;
    for (i = 0; i < tpi_ndrop; i++)         /* inlined calls' */
        if (tpi_drop[i] >= func_start_nrel && tpi_drop[i] < nrel)
            gone[tpi_drop[i] - func_start_nrel] = 1;
    tpi_ndrop = 0;
    for (i = k = func_start_nrel; i < nrel; i++)
        if (!gone[i - func_start_nrel])
            rel[k++] = rel[i];
    sec->reloc->data_offset = k * sizeof(*rel);
    tcc_free(gone);
    tpg.n = 0;
}

/* What the phases of tp_relax_function() share: the function's bytes
 * decoded into instructions, and the new layout as it settles. */
struct tp_relax {
    Section *sec;
    uint8_t *code;              /* the function's bytes, at base */
    int base, size;
    struct tp_relax_insn *ins;
    int n;
    int *tidx;                  /* old offset -> instruction index */
    int *map;                   /* old offset -> new offset */
    uint8_t *out;               /* the new bytes */
    int outlen;
    int grown;                  /* an instruction may now need more bytes */
};

static int tp_copy3_is(const struct tp_relax_insn *p, int opcode, int operand)
{
    return p->opcode == opcode && p->operand == operand;
}

/* The statement pass has already checked the types, alignment and empty
 * expression stack. Keep that proof attached to the patched first load. */
static int tp_copy3_marked(const struct tp_relax_insn *p, int frame)
{
    const struct local_patch *lp;
    int end = func_start_ind + p->old_off + p->old_len;

    for (lp = local_patches; lp; lp = lp->next)
        if (lp->stack_copy && lp->instr_offset + lp->placeholder_size == end &&
            p->operand == frame + lp->tcc_offset / 4)
            return 1;
    return 0;
}

static int tp_copy3_next(const struct tp_relax_insn *ins, int n, int i)
{
    while (++i < n && ins[i].opcode == -1)
        ;
    return i;
}

/* The argument's home may only be written by the marked prologue capture.
 * Reject both escaped addresses and explicit indirect accesses to the word. */
static int tp_copy3_destination(struct tpg_work *g, int base, int top)
{
    struct tp_relax_insn *ins = g->ins;
    const struct local_patch *lp;
    Sym *arg;
    int frame = g->F - top;
    int capture = -1, load = -1, prev = -1, end, i;

    for (arg = func_start_sym->type.ref->next; arg; arg = arg->next) {
        Sym *local = sym_find(arg->v);
        if (local && local->c == (base - frame) * 4 && !local->a.addrtaken &&
            (local->type.t & (VT_BTYPE | VT_VOLATILE)) == VT_PTR)
            break;
    }
    if (!arg || tps_word_escapes(ins, g->n, base, top))
        return 0;

    for (i = tp_copy3_next(ins, g->n, -1); i < g->n;
         prev = i, i = tp_copy3_next(ins, g->n, i)) {
        int bias, extent;
        int64_t begin;
        int j;
        if (tp_copy3_is(&ins[i], TP_ENC_STL, base)) {
            if (capture >= 0 || prev <= g->first ||
                g->block[i] != g->block[g->first + 1])
                return 0;
            capture = i;
            load = prev;
        }
        if (ins[i].opcode != TP_ENC_LDLP)
            continue;
        j = tp_copy3_next(ins, g->n, i);
        if (j == g->n)
            continue;
        extent = tpg_ldlp_extent(&ins[j], &bias);
        begin = (int64_t)ins[i].operand + bias;
        if (extent > 0 && base >= begin && base < begin + extent)
            return 0;
    }
    if (capture < 0)
        return 0;
    end = func_start_ind + ins[capture].old_off + ins[capture].old_len;
    for (lp = local_patches; lp; lp = lp->next)
        if (lp->incoming_pointer && lp->instr_offset + lp->placeholder_size == end &&
            base == frame + lp->tcc_offset / 4 &&
            tp_copy3_is(&ins[load], TP_ENC_LDL, g->F + lp->incoming_pointer))
            return 1;
    return 0;
}

/* After MOVE the three integer registers can differ from the scalar path.
 * Follow fallthrough while every read uses values produced since the copy.
 * Once all three are equal the rest of the function may use them freely. */
static int tp_copy3_continues(const struct tpg_work *g, int last, int base,
                              int tmp, int *keep_park)
{
    const struct tp_relax_insn *ins = g->ins;
    const int n = g->n;
    const int bt = func_vt.t & VT_BTYPE;
    int first = tp_copy3_next(ins, n, last);
    int second = tp_copy3_next(ins, n, first);
    int third = tp_copy3_next(ins, n, second);
    int depth = 0, i;

    *keep_park = 1;
    if (second < n && bt == VT_VOID && tp_copy3_is(&ins[first], TP_ENC_AJW, g->F) &&
        tp_copy3_is(&ins[second], TP_ENC_OPR, TP_OPR_RET)) {
        *keep_park = 0;
        return 1;
    }
    /* A following field copy can overwrite the parked address before use. */
    if (third < n && g->block[last] == g->block[third] &&
        tp_copy3_is(&ins[first], TP_ENC_LDL, base) && ins[second].opcode == TP_ENC_LDNLP &&
        tp_copy3_is(&ins[third], TP_ENC_STL, tmp))
        *keep_park = 0;

    for (i = first; i < n; i = tp_copy3_next(ins, n, i)) {
        int delta, reads;
        if (tp_copy3_is(&ins[i], TP_ENC_OPR, TP_OPR_RET))
            return bt == VT_VOID || (bt == VT_INT && depth >= 1);
        if (ins[i].pinned || ins[i].is_branch || ins[i].tab || ins[i].icall ||
            !tps_effect(ins[i].opcode, ins[i].operand, &delta, &reads) || reads > depth)
            return 0;
        depth += delta;
        if (depth == 3)
            return 1;
    }
    return 0;
}

/* Three typed copies from consecutive words in this invocation's frame.
 * An unchanged incoming pointer cannot point to those new local objects. */
static int tp_copy3_match(const struct tp_relax *r, int i, int frame, int *copy)
{
    const struct tp_relax_insn *ins = r->ins;
    int base, tmp, src, k, word;
    const int R = func_frame_reserved;

    base = ins[i].operand;
    if (ins[i].opcode != TP_ENC_LDL || base < R || base >= frame)
        return 0;
    copy[0] = i;
    for (k = 1; k < 26; k++) {
        i = copy[k] = tp_copy3_next(ins, r->n, i);
        if (i == r->n)
            return 0;
        if (k == 1 && ins[i].opcode != TP_ENC_STL)
            return 0;
    }
    tmp = ins[copy[1]].operand;
    src = ins[copy[3]].operand;
    if (tmp <= 0 || tmp >= R || src < R || src > frame - 6)
        return 0;

    for (word = 0, k = 0; word < 6; word += 2) {
        if (!tp_copy3_is(&ins[copy[k++]], TP_ENC_LDL, base) ||
            (word && !tp_copy3_is(&ins[copy[k++]], TP_ENC_LDNLP, word)) ||
            !tp_copy3_is(&ins[copy[k++]], TP_ENC_STL, tmp) ||
            !tp_copy3_is(&ins[copy[k]], TP_ENC_LDL, src + word + 1) ||
            !tp_copy3_marked(&ins[copy[k++]], frame) ||
            !tp_copy3_is(&ins[copy[k++]], TP_ENC_LDL, src + word) ||
            !tp_copy3_is(&ins[copy[k++]], TP_ENC_LDL, tmp) ||
            !tp_copy3_is(&ins[copy[k++]], TP_ENC_STNL, 0) ||
            !tp_copy3_is(&ins[copy[k++]], TP_ENC_LDL, tmp) ||
            !tp_copy3_is(&ins[copy[k++]], TP_ENC_STNL, 1))
            return 0;
    }

    for (i = copy[0]; i <= copy[25]; i++)
        if (ins[i].pinned || ins[i].synth || ins[i].tab || ins[i].icall || ins[i].is_branch)
            return 0;
    return 1;
}

static void tp_copy3_move(struct tp_relax *r, int top)
{
    static const int op[] = { TP_ENC_LDLP, TP_ENC_LDL, TP_ENC_LDC, TP_ENC_OPR,
                              TP_ENC_LDL, TP_ENC_LDNLP, TP_ENC_STL };
    struct tp_relax_insn *ins = r->ins;
    struct tpg_work g = { 0 };
    int copy[26], i;

    if (func_var || func_uses_inline_asm || tp_knob(TPK_TP_NO_COPY3_MOVE))
        return;
    g.ins = ins;
    g.n = r->n;
    g.size = r->size;
    g.F = func_frame_words;
    g.tidx = r->tidx;

    for (i = 0; i < r->n; i++) {
        int base, tmp, src, last, keep_park, k;
        if (!tp_copy3_match(r, i, g.F - top, copy))
            continue;
        if (!g.block) {
            if (!tpg_frame_ok(&g))
                break;
            tpg_blocks(&g);
            if (!g.block)
                break;
        }
        base = ins[i].operand;
        tmp = ins[copy[1]].operand;
        src = ins[copy[3]].operand;
        last = copy[25];
        if (g.block[i] != g.block[last] || !tp_copy3_destination(&g, base, top) ||
            tps_word_escapes(ins, r->n, tmp, top) ||
            !tp_copy3_continues(&g, last, base, tmp, &keep_park))
            continue;

        /* Restore the last scalar store's parked address if it may be used. */
        {
            int args[] = { src, base, 24, TP_OPR_MOVE, base, 4, tmp };
            for (k = i; k <= last; k++)
                ins[k].opcode = -1;
            for (k = 0; k < (keep_park ? 7 : 4); k++) {
                struct tp_relax_insn *p = &ins[copy[k]];
                p->opcode = op[k];
                p->operand = args[k];
                p->new_len = tp_encode_len(p->opcode, p->operand);
            }
        }
        ins[i].workspace_extent = 6;
        r->grown = 1;
        i = last;
    }
    tcc_free(g.block);
}

/*************************************************************/
/* Narrow locals kept as words                               */
/*************************************************************/
/*
 * A char or short local has a workspace word of its own (tccgen word-aligns
 * every transputer local) but is read and written through its address:
 * `ldlp s; lb` and `ldlp s; sb`, `ls`/`lsx`/`ss` on the T9000, `lbx` for a
 * signed char there and `lb; ldc 128; xword` on the T8xx.  When nothing
 * else names word s - no LDL or STL of it, no other use of its address, no
 * escaping LDLP whose object reaches it - the word can hold the value
 * extended to 32 bits instead, as the narrow load would leave it in A:
 *
 *   read      ldlp s; lb                  -> ldl s
 *   write     <v>; ldlp s; sb             -> <v>; stl s        (v in range)
 *                                         -> <v>; ldc 255; and; stl s
 *   param     ldl p; stl s                -> ldl p; ldc 255; and; stl s
 *
 * and likewise for the other kinds (xbword/xsword truncate a signed char
 * or short on the T9000 in one instruction, so they cost nothing there).
 * Whether <v> is in range is decided by simulating the straight-line code
 * before the store on intervals: a byte load, a constant, `and` with a
 * small mask, a comparison, a shift of a non-negative value ... .  The
 * caller of a function with a char parameter passes a word that this
 * function cannot vouch for, so the entry copy is truncated.
 *
 * A slot is converted only when that saves instructions, counting each one
 * inside k loops 4^k times.  It runs before every other pass, so the
 * scheduler and the slot remapper then see ordinary word traffic.
 * TCC_TRANSPUTER_NARROW_LOCAL=0 turns it off; TP_NARROW_STATS=1 lists the
 * slots converted.
 */
#define TPN_MAXF   128
#define TPN_U8     1
#define TPN_S8     2      /* signed char read by lbx (T9000) */
#define TPN_S8X    3      /* signed char read by lb; ldc 128; xword */
#define TPN_U16    4
#define TPN_S16    5      /* signed short read by lsx (T9000) */

/* Read forms of a narrow local, as seen at its LDLP. */
#define TPN_R_LB   1
#define TPN_R_LBX  2
#define TPN_R_S8X  3      /* lb; ldc 128; xword */
#define TPN_R_LS   4
#define TPN_R_LSX  5
#define TPN_W_SB   6
#define TPN_W_SS   7
#define TPN_W_SB2  8      /* a halfword as two sb (T800, store() case 2) */

struct tpn_val {
    int known;
    long long lo, hi;
};

struct tpn_slot {
    int bad;              /* named some other way: never convert */
    int ldlp_any;         /* an LDLP may reach it */
    int n_lb, n_lbx, n_s8x, n_ls, n_lsx, n_sb, n_ss;
    int n_sb2;            /* T800 halfword stores, two sb */
    int n_ldlm;           /* T800 halfword reads: ldl s; ldc 65535; and */
    int kind;
    long long gain;
};

static int tpn_prev(const struct tp_relax_insn *ins, int i)
{
    for (i--; i >= 0 && ins[i].opcode == -1 && !ins[i].pinned; i--)
        ;
    return i;
}

static int tpn_next(const struct tp_relax_insn *ins, int n, int i)
{
    for (i++; i < n && ins[i].opcode == -1; i++)
        ;
    return i;
}

static int tpn_is_ins(const struct tp_relax_insn *c, int opc, int opd)
{
    return c->opcode == opc && c->operand == opd && !c->pinned;
}

/* store()'s halfword without ss (T800), after `ldlp s` at i:
 *   stl SCR; [stl SCR+1;] ldl r; ldl SCR; sb; ldl r; ldc 8; shr;
 *   ldl SCR; adc 1; sb
 * with the value in A when `stl SCR+1` parks it (r = SCR+1).  Returns the
 * index of the last sb, or -1; *vr = r, *ina = whether it came in A. */
static int tpn_sb2(const struct tp_relax_insn *ins, int n, int i, int *vr,
                   int *ina)
{
    const int S = FRAME_SLOT_SCRATCH;
    int q[11], k, r;

    for (k = 0, q[0] = tpn_next(ins, n, i); k < 10 && q[k] < n; k++)
        q[k + 1] = tpn_next(ins, n, q[k]);
    if (k < 10 || q[10] >= n || !tpn_is_ins(&ins[q[0]], TP_ENC_STL, S))
        return -1;
    *ina = tpn_is_ins(&ins[q[1]], TP_ENC_STL, S + 1);
    k = *ina ? 2 : 1;
    if (ins[q[k]].opcode != TP_ENC_LDL || ins[q[k]].pinned)
        return -1;
    r = ins[q[k]].operand;
    if ((*ina && r != S + 1) || r == S ||
        !tpn_is_ins(&ins[q[k + 1]], TP_ENC_LDL, S) ||
        !tpn_is_ins(&ins[q[k + 2]], TP_ENC_OPR, TP_OPR_SB) ||
        !tpn_is_ins(&ins[q[k + 3]], TP_ENC_LDL, r) ||
        !tpn_is_ins(&ins[q[k + 4]], TP_ENC_LDC, 8) ||
        !tpn_is_ins(&ins[q[k + 5]], TP_ENC_OPR, TP_OPR_SHR) ||
        !tpn_is_ins(&ins[q[k + 6]], TP_ENC_LDL, S) ||
        !tpn_is_ins(&ins[q[k + 7]], TP_ENC_ADC, 1) ||
        !tpn_is_ins(&ins[q[k + 8]], TP_ENC_OPR, TP_OPR_SB))
        return -1;
    *vr = r;
    return q[k + 8];
}

/* `ldl s; ldc 65535; and` at i: a T800 halfword read. */
static int tpn_ldlm(const struct tp_relax_insn *ins, int n, int i)
{
    int j = tpn_next(ins, n, i), k = j < n ? tpn_next(ins, n, j) : n;

    return k < n && tpn_is_ins(&ins[j], TP_ENC_LDC, 65535) &&
           tpn_is_ins(&ins[k], TP_ENC_OPR, TP_OPR_AND);
}

/* The access an LDLP at i feeds, TPN_R_* / TPN_W_*, or 0. */
static int tpn_form(const struct tp_relax_insn *ins, int n, int i)
{
    int j = tpn_next(ins, n, i), k, l, vr, ina;

    if (j < n && tpn_is_ins(&ins[j], TP_ENC_STL, FRAME_SLOT_SCRATCH))
        return tpn_sb2(ins, n, i, &vr, &ina) >= 0 ? TPN_W_SB2 : 0;

    if (j >= n || ins[j].opcode != TP_ENC_OPR || ins[j].pinned)
        return 0;
    switch (ins[j].operand) {
    case TP_OPR_LB:
        k = tpn_next(ins, n, j);
        l = k < n ? tpn_next(ins, n, k) : n;
        if (l < n && ins[k].opcode == TP_ENC_LDC && ins[k].operand == 128 &&
            ins[l].opcode == TP_ENC_OPR && ins[l].operand == TP_OPR_XWORD)
            return TPN_R_S8X;
        return TPN_R_LB;
    case TP_OPR_LBX: return TPN_R_LBX;
    case TP_OPR_LS:  return TPN_R_LS;
    case TP_OPR_LSX: return TPN_R_LSX;
    case TP_OPR_SB:  return TPN_W_SB;
    case TP_OPR_SS:  return TPN_W_SS;
    }
    return 0;
}

/* Could the address of word x, handed out, reach word s (both below the
 * frame's top)?  The object holding x is its block of tp_loc_marks, as in
 * tps_word_escapes; a reserved word is its own, or a double's first. */
static int tpn_reaches(int x, int s)
{
    int L = func_frame_words, R = func_frame_reserved, lo = R, hi = L, i;

    if (x >= L)
        return 0;
    if (x < R || s < R)
        return s == x || s == x + 1;
    if (tp_loc_marks_bad)
        return 1;
    for (i = 0; i < tp_loc_marks_count; i++) {
        int m = tp_loc_marks[i], b;
        if (m > 0 || (m & 3))
            continue;
        b = L + m / 4;
        if (b <= s && b > lo)
            lo = b;
        if (b > s && b < hi)
            hi = b;
    }
    return x >= lo && x <= hi;
}

static void tpn_set(struct tpn_val *v, long long lo, long long hi)
{
    v->known = lo >= -2147483648LL && hi <= 2147483647LL && lo <= hi;
    v->lo = lo;
    v->hi = hi;
}

static void tpn_push(struct tpn_val *st, struct tpn_val v)
{
    st[2] = st[1];
    st[1] = st[0];
    st[0] = v;
}

static struct tpn_val tpn_pop(struct tpn_val *st)
{
    struct tpn_val v = st[0];
    st[0] = st[1];
    st[1] = st[2];
    st[2].known = 0;
    return v;
}

static long long tpn_mask_above(long long hi)
{
    long long m = 0;
    while (m < hi)
        m = 2 * m + 1;
    return m;
}

/* Interval of A just before instruction `at`, from the straight-line code
 * since the last branch target or barrier.  known = 0 when unknown. */
/* slotr (may be NULL): the range of a word slot below nslotr that
 * holds one at every read (a promoted narrow local, a slot only ever
 * written with constants). */
static struct tpn_val tpn_range_before(const struct tp_relax_insn *ins,
                                       const char *head, int at,
                                       const struct tpn_val *slotr,
                                       int nslotr)
{
    struct tpn_val st[3], a, b, c, none;
    int map_slot[8], nmap = 0, start = at, k, i, q;
    struct tpn_val map_val[8];
    int dl, rd;

    memset(st, 0, sizeof st);
    none.known = 0;
    none.lo = none.hi = 0;
    for (k = at - 1, q = 0; k >= 0 && q < 40; k--) {
        int opc = ins[k].opcode;
        /* A jump may land on the padding in front of an instruction:
         * that is a join as much as a label on the instruction itself
         * (difftest seed 42149: a `j` to the padding before `ldl x;
         * ldl y; diff` let the window run on into the fall-through
         * path's `ldc -1; stl y`, and a u16 cast's mask went). */
        if (opc == -1) {
            if (head[k])
                break;
            continue;
        }
        q++;
        if (ins[k].pinned || ins[k].tab || ins[k].icall ||
            opc == TP_ENC_J || opc == TP_ENC_CJ || opc == TP_ENC_CALL ||
            !tps_effect(opc, ins[k].operand, &dl, &rd)) {
            /* tps_effect lacks xdble/lshr; the simulation knows them */
            if (!(opc == TP_ENC_OPR && !ins[k].pinned && !ins[k].synth &&
                  (ins[k].operand == TP_OPR_XDBLE ||
                   ins[k].operand == TP_OPR_LSHR))) {
                start = k + 1;
                break;
            }
        }
        start = k;
        if (head[k])
            break;
    }
    for (i = start; i < at; i++) {
        int opc = ins[i].opcode, opd = ins[i].operand;
        if (opc == -1)
            continue;
        switch (opc) {
        case TP_ENC_LDC:
            tpn_set(&a, opd, opd);
            tpn_push(st, a);
            break;
        case TP_ENC_LDL:
            a = none;
            if (slotr && opd >= 0 && opd < nslotr)
                a = slotr[opd];
            for (k = nmap - 1; k >= 0; k--)
                if (map_slot[k] == opd) {
                    a = map_val[k];
                    break;
                }
            tpn_push(st, a);
            break;
        case TP_ENC_STL:
            a = tpn_pop(st);
            for (k = 0; k < nmap; k++)
                if (map_slot[k] == opd)
                    break;
            if (k == nmap && nmap < 8)
                nmap++;
            if (k < nmap) {
                map_slot[k] = opd;
                map_val[k] = a;
            }
            break;
        case TP_ENC_LDLP:
            tpn_push(st, none);
            break;
        case TP_ENC_LDNL: case TP_ENC_LDNLP:
            st[0] = none;
            break;
        case TP_ENC_ADC:
            if (st[0].known)
                tpn_set(&st[0], st[0].lo + opd, st[0].hi + opd);
            break;
        case TP_ENC_EQC:
            tpn_set(&st[0], 0, 1);
            break;
        case TP_ENC_STNL:
            tpn_pop(st);
            tpn_pop(st);
            nmap = 0;
            break;
        case TP_ENC_AJW:
            nmap = 0;
            break;
        case TP_ENC_OPR:
            switch (opd) {
            case TP_OPR_LB:  tpn_set(&st[0], 0, 255); break;
            case TP_OPR_LBX: case TP_OPR_XBWORD:
                tpn_set(&st[0], -128, 127); break;
            case TP_OPR_LS:  tpn_set(&st[0], 0, 65535); break;
            case TP_OPR_LSX: case TP_OPR_XSWORD:
                tpn_set(&st[0], -32768, 32767); break;
            case TP_OPR_GT: case TP_OPR_GTU:
                tpn_pop(st);
                tpn_set(&st[0], 0, 1);
                break;
            case TP_OPR_AND:
                a = tpn_pop(st);
                b = st[0];
                if (a.known && a.lo >= 0 && b.known && b.lo >= 0)
                    tpn_set(&st[0], 0, a.hi < b.hi ? a.hi : b.hi);
                else if (a.known && a.lo >= 0)
                    tpn_set(&st[0], 0, a.hi);
                else if (b.known && b.lo >= 0)
                    tpn_set(&st[0], 0, b.hi);
                else
                    st[0] = none;
                break;
            case TP_OPR_OR: case TP_OPR_XOR:
                a = tpn_pop(st);
                b = st[0];
                if (a.known && a.lo >= 0 && b.known && b.lo >= 0)
                    tpn_set(&st[0], 0, tpn_mask_above(a.hi > b.hi ? a.hi
                                                                  : b.hi));
                else
                    st[0] = none;
                break;
            case TP_OPR_SHR:
                a = tpn_pop(st);
                b = st[0];
                if (a.known && a.lo == a.hi && a.lo >= 0 && a.lo < 32 &&
                    b.known && b.lo >= 0)
                    tpn_set(&st[0], b.lo >> a.lo, b.hi >> a.lo);
                else if (a.known && a.lo >= 1 && a.hi < 32)
                    tpn_set(&st[0], 0, 0xffffffffLL >> a.lo);
                else
                    st[0] = none;
                break;
            case TP_OPR_SUM: case TP_OPR_BSUB: case TP_OPR_ADD:
                a = tpn_pop(st);
                b = st[0];
                if (a.known && b.known)
                    tpn_set(&st[0], a.lo + b.lo, a.hi + b.hi);
                else
                    st[0] = none;
                break;
            case TP_OPR_DIFF: case TP_OPR_SUB:   /* B - A */
                a = tpn_pop(st);
                b = st[0];
                if (a.known && b.known)
                    tpn_set(&st[0], b.lo - a.hi, b.hi - a.lo);
                else
                    st[0] = none;
                break;
            case TP_OPR_PROD: case TP_OPR_MUL:
                a = tpn_pop(st);
                b = st[0];
                if (a.known && b.known && a.lo >= 0 && b.lo >= 0 &&
                    a.hi < 65536 && b.hi < 65536)
                    tpn_set(&st[0], a.lo * b.lo, a.hi * b.hi);
                else
                    st[0] = none;
                break;
            case TP_OPR_XWORD:       /* B sign-extended at the bit A */
                a = tpn_pop(st);
                if (a.known && a.lo == a.hi && a.lo > 0 &&
                    !(a.lo & (a.lo - 1)) && a.lo <= 0x40000000LL)
                    tpn_set(&st[0], -a.lo, a.lo - 1);
                else
                    st[0] = none;
                break;
            case TP_OPR_REV:
                a = st[0];
                st[0] = st[1];
                st[1] = a;
                break;
            case TP_OPR_DUP:
                tpn_push(st, st[0]);
                break;
            case TP_OPR_MINT:
                tpn_set(&a, -2147483648LL, -2147483648LL);
                tpn_push(st, a);
                break;
            case TP_OPR_XDBLE:       /* A = x, B = its sign word */
                a = tpn_pop(st);
                if (a.known && a.lo >= 0)
                    tpn_set(&b, 0, 0);
                else
                    b = none;
                tpn_push(st, b);
                tpn_push(st, a);
                break;
            case TP_OPR_LSHR:        /* C:B >> A into B:A */
                a = tpn_pop(st);
                b = tpn_pop(st);
                c = tpn_pop(st);
                if (a.known && a.lo == a.hi && a.lo >= 0 && a.lo < 32 &&
                    b.known && b.lo >= 0 && c.known && c.lo == 0 &&
                    c.hi == 0) {
                    tpn_set(&c, 0, 0);
                    tpn_set(&b, b.lo >> a.lo, b.hi >> a.lo);
                } else {
                    c = none;
                    b = none;
                }
                tpn_push(st, c);
                tpn_push(st, b);
                break;
            case TP_OPR_SB: case TP_OPR_SS:
                tpn_pop(st);
                tpn_pop(st);
                nmap = 0;
                break;
            default:
                if (!tps_effect(opc, opd, &dl, &rd))
                    return none;
                for (k = 0; k < rd; k++)
                    tpn_pop(st);
                for (k = 0; k < rd + dl; k++)
                    tpn_push(st, none);
                if (opd == TP_OPR_MOVE)
                    nmap = 0;
                break;
            }
            break;
        default:
            return none;
        }
    }
    return st[0];
}

/* Is the lb at j followed by `ldc m; and` with m < 256, so that the low
 * byte of a short needs no mask of its own? */
static int tpn_masked_after(const struct tp_relax_insn *ins, int n, int j)
{
    int k = tpn_next(ins, n, j), l = k < n ? tpn_next(ins, n, k) : n;

    return l < n && ins[k].opcode == TP_ENC_LDC && ins[k].operand >= 0 &&
           ins[k].operand <= 255 && ins[l].opcode == TP_ENC_OPR &&
           ins[l].operand == TP_OPR_AND && !ins[k].pinned;
}

/* What converting an access costs or saves, in instructions. */
static int tpn_read_gain(int kind, int form)
{
    if (kind == TPN_S8X && form == TPN_R_S8X)
        return 3;
    if ((kind == TPN_U8 && (form == TPN_R_LB || form == TPN_R_S8X)) ||
        (kind == TPN_S8 && form == TPN_R_LBX) ||
        (kind == TPN_U16 && form == TPN_R_LS) ||
        (kind == TPN_S16 && form == TPN_R_LSX))
        return 1;
    return 0;                 /* lbx/lsx of an unsigned word: ldl; xbword */
}

/* Instructions a truncation to `kind` takes. */
static int tpn_trunc_len(int kind)
{
    switch (kind) {
    case TPN_U8: case TPN_U16: return 2;
    case TPN_S8: case TPN_S16: return 1;
    }
    return 4;
}

static void tpn_range_of(int kind, long long *lo, long long *hi)
{
    switch (kind) {
    case TPN_U8:  *lo = 0;      *hi = 255;   break;
    case TPN_U16: *lo = 0;      *hi = 65535; break;
    case TPN_S16: *lo = -32768; *hi = 32767; break;
    default:      *lo = -128;   *hi = 127;   break;
    }
}

/* The truncation to `kind`, as opcode/operand pairs; returns the count. */
static int tpn_trunc_code(int kind, int *opc, int *opd)
{
    switch (kind) {
    case TPN_S8:
        opc[0] = TP_ENC_OPR; opd[0] = TP_OPR_XBWORD;
        return 1;
    case TPN_S16:
        opc[0] = TP_ENC_OPR; opd[0] = TP_OPR_XSWORD;
        return 1;
    }
    opc[0] = TP_ENC_LDC; opd[0] = kind == TPN_U16 ? 65535 : 255;
    opc[1] = TP_ENC_OPR; opd[1] = TP_OPR_AND;
    if (kind != TPN_S8X)
        return 2;
    opc[2] = TP_ENC_LDC; opd[2] = 128;
    opc[3] = TP_ENC_OPR; opd[3] = TP_OPR_XWORD;
    return 4;
}

static void tpn_put(struct tp_relax_insn *p, int opc, int opd)
{
    p->opcode = opc;
    p->operand = opd;
    p->is_branch = 0;
    p->dup_load = 0;
    p->new_len = opc == -1 ? 0 : tp_encode_len(opc, opd);
}

/* Value ranges also make two common sequences shorter:
 *
 *   <v in [0, m]>; ldc m; and        ->  <v>          (m = 2^k - 1)
 *   <v in [0, 1]>; eqc 0; eqc 0      ->  <v>
 *   <v >= 0>; xdble; ldc k; lshr     ->  <v>; ldc k; shr
 *
 * the second being an arithmetic right shift of a value that is not
 * negative, typically an unsigned char or short promoted to int. */
/* TCC_TRANSPUTER_RANGE_PEEP=0 turns off the rules below that came with
 * the second CoreMark round (compare folds, `eqc 1`, range tests, masks). */
static int tpn_peep_on = -1;
static int tpn_ext_on = 1;
static int tpn_stats[4];
static int tpn_in_late;     /* tpn_late's runs: shapes after the scheduler */

/* The value a lone push at k (ldc or ldl) puts on the stack. */
static struct tpn_val tpn_push_val(const struct tp_relax_insn *ins, int n,
                                   const char *head, int k,
                                   const struct tpn_val *slotr, int nslotr)
{
    struct tpn_val v;

    v.known = 0;
    v.lo = v.hi = 0;
    if (ins[k].pinned)
        return v;
    if (ins[k].opcode == TP_ENC_LDC)
        tpn_set(&v, ins[k].operand, ins[k].operand);
    else if (ins[k].opcode == TP_ENC_LDL)
        v = tpn_range_before(ins, head, tpn_next(ins, n, k), slotr, nslotr);
    return v;
}

/* Instructions i[0..m-1], the next m live ones from i0: 0 if they run out,
 * one is pinned, or one after the first is a branch target. */
static int tpn_run(const struct tp_relax_insn *ins, int n, const char *head,
                   int i0, int m, int *at)
{
    int k, q;

    for (k = 0, q = i0; k < m; k++) {
        if (q >= n || ins[q].opcode == -1 || ins[q].pinned ||
            (k > 0 && head[q]))
            return 0;
        at[k] = q;
        q = tpn_next(ins, n, q);
    }
    /* no branch target among the deleted padding in between either */
    for (q = at[0] + 1; q <= at[m - 1]; q++)
        if (head[q])
            return 0;
    return 1;
}

static int tpn_is(const struct tp_relax_insn *c, int opc, int opd)
{
    return c->opcode == opc && (opc != TP_ENC_OPR || c->operand == opd);
}

static int tpn_peephole(struct tp_relax *r, const struct tpn_val *slotr,
                        int nslotr)
{
    int changed;
    struct tp_relax_insn *ins = r->ins;
    const int n = r->n;
    char *head = tcc_mallocz(n + 1);
    int i, j, l, k, first = -1;
    struct tpn_val v;
    int at[9];

    for (i = 0; i < n; i++) {
        if (first < 0 && ins[i].opcode != -1)
            first = i;
        if (ins[i].tab > 0)
            head[i] = 1;
        if (ins[i].is_branch && ins[i].target_old >= 0 &&
            ins[i].target_old < r->size &&
            (k = r->tidx[ins[i].target_old]) >= 0)
            head[k] = 1;
    }
    if (first >= 0)
        head[first] = 1;
    if (tpn_peep_on < 0) {
        const char *e = tp_knob(TPK_TCC_TRANSPUTER_RANGE_PEEP);
        tpn_peep_on = !(e && *e == '0');
        e = tp_knob(TPK_TCC_TRANSPUTER_EXT_DROP);
        tpn_ext_on = !(e && *e == '0');
    }
    for (i = 0; i < n; i++) {
        struct tpn_val va, vb;
        if (ins[i].pinned || ins[i].opcode == -1)
            continue;
        if (!tpn_peep_on)
            goto old_rules;
        /* <b>; <a>; gt|gtu; cj L with b > a for every value they can
         * have: the test never jumps, so all four go (a switch's bounds
         * check on a state that only ever holds case values).  The first
         * may be a branch target: a jump there lands on what follows. */
        if ((ins[i].opcode == TP_ENC_LDC || ins[i].opcode == TP_ENC_LDL) &&
            tpn_run(ins, n, head, i, 4, at) &&
            (ins[at[1]].opcode == TP_ENC_LDC ||
             ins[at[1]].opcode == TP_ENC_LDL) &&
            (tpn_is(&ins[at[2]], TP_ENC_OPR, TP_OPR_GT) ||
             tpn_is(&ins[at[2]], TP_ENC_OPR, TP_OPR_GTU)) &&
            ins[at[3]].opcode == TP_ENC_CJ && ins[at[3]].is_branch) {
            vb = tpn_push_val(ins, n, head, at[0], slotr, nslotr);
            va = tpn_push_val(ins, n, head, at[1], slotr, nslotr);
            if (va.known && vb.known && vb.lo > va.hi &&
                (ins[at[2]].operand == TP_OPR_GT || va.lo >= 0)) {
                for (k = 0; k < 4; k++)
                    tpn_put(&ins[at[k]], -1, 0);
                tpn_stats[0]++;
                i = at[3];
                continue;
            }
        }
        if (head[i])
            continue;
        /* push a; push b; rev  ->  push b; push a  (lone ldl/ldc pushes;
         * an inlined body's `ldc k; rev` meets the argument's ldl) */
        if (tpn_in_late && tpn_is(&ins[i], TP_ENC_OPR, TP_OPR_REV)) {
            int b2 = tpn_prev(ins, i), a2 = b2 >= 0 ? tpn_prev(ins, b2) : -1;
            if (a2 >= 0 && !head[b2] && !ins[a2].pinned && !ins[b2].pinned &&
                (ins[a2].opcode == TP_ENC_LDL || ins[a2].opcode == TP_ENC_LDC) &&
                (ins[b2].opcode == TP_ENC_LDL || ins[b2].opcode == TP_ENC_LDC)) {
                int oc = ins[a2].opcode, od = ins[a2].operand;
                for (k = a2 + 1; k < i; k++)
                    if (head[k])
                        break;
                if (k == i) {
                    tpn_put(&ins[a2], ins[b2].opcode, ins[b2].operand);
                    tpn_put(&ins[b2], oc, od);
                    tpn_put(&ins[i], -1, 0);
                    tpn_stats[3]++;
                    continue;
                }
            }
        }
        /* A sign extension nothing needs (TCC_TRANSPUTER_EXT_DROP): xbword,
         * or T800's `ldc 128; xword` (xsword, `ldc 32768; xword` for
         * halfwords), goes when
         *   - the value is already in the signed range: it is the identity;
         *   - only a zero test reads it (eqc 0, cj) and the value is in
         *     [-128, 255]: the extension is 0 exactly when the value is;
         *   - only a byte (halfword) store reads it, after the address is
         *     pushed: the stored bits are the same.
         * strcpy's `ldl c; xbword; eqc 0` on a byte lbx loaded. */
        if (tpn_ext_on && ((ins[i].opcode == TP_ENC_OPR &&
              (ins[i].operand == TP_OPR_XBWORD ||
               ins[i].operand == TP_OPR_XSWORD)) ||
             (ins[i].opcode == TP_ENC_LDC &&
              (ins[i].operand == 128 || ins[i].operand == 32768)))) {
            int e1 = i, half, drop = 0, rr, st;
            if (ins[i].opcode == TP_ENC_LDC) {
                e1 = tpn_next(ins, n, i);
                if (e1 >= n || head[e1] || ins[e1].pinned ||
                    !tpn_is(&ins[e1], TP_ENC_OPR, TP_OPR_XWORD))
                    goto ext_done;
                half = ins[i].operand == 32768;
            } else {
                half = ins[i].operand == TP_OPR_XSWORD;
            }
            {
                long long m = half ? 32768 : 128;
                v = tpn_range_before(ins, head, i, slotr, nslotr);
                j = tpn_next(ins, n, e1);
                if (v.known && v.lo >= -m && v.hi <= m - 1)
                    drop = 1;
                else if (v.known && v.lo >= -m && v.hi <= 2 * m - 1 &&
                         j < n && !head[j] && !ins[j].pinned &&
                         ((ins[j].opcode == TP_ENC_EQC && ins[j].operand == 0) ||
                          ins[j].opcode == TP_ENC_CJ))
                    drop = 1;
                for (k = j, rr = 0, st = 0; !drop && k < n && st < 6;
                     k = tpn_next(ins, n, k), st++) {
                    int dl, rd;
                    if (head[k] || ins[k].pinned)
                        break;
                    if (rr == 1 && tpn_is(&ins[k], TP_ENC_OPR,
                                          half ? TP_OPR_SS : TP_OPR_SB)) {
                        drop = 1;
                        break;
                    }
                    if (ins[k].opcode == TP_ENC_STL || ins[k].opcode == TP_ENC_STNL ||
                        ins[k].opcode == TP_ENC_J || ins[k].opcode == TP_ENC_CJ ||
                        ins[k].opcode == TP_ENC_CALL ||
                        !tps_effect(ins[k].opcode, ins[k].operand, &dl, &rd) ||
                        rd > rr)
                        break;
                    rr += dl;
                }
            }
            if (drop) {
                tpn_put(&ins[i], -1, 0);
                if (e1 != i)
                    tpn_put(&ins[e1], -1, 0);
                tpn_stats[1]++;
                i = e1;
                continue;
            }
        }
    ext_done:
        /* <v in [0, 1]>; eqc 1  ->  <v> */
        if (ins[i].opcode == TP_ENC_EQC && ins[i].operand == 1) {
            v = tpn_range_before(ins, head, i, slotr, nslotr);
            if (v.known && v.lo >= 0 && v.hi <= 1) {
                tpn_put(&ins[i], -1, 0);
                tpn_stats[1]++;
            }
            continue;
        }
        /* ldc lo; ldl x; gt; eqc 0; ldl x; ldc hi; gt; eqc 0; and
         * (lo <= x & x <= hi, either way round)  ->
         * ldc hi-lo+1; ldl x; adc -lo; gtu: one unsigned test.  The adc
         * only when x's range shows it cannot overflow, ldc lo; diff
         * otherwise. */
        if (tpn_run(ins, n, head, i, 9, at) &&
            tpn_is(&ins[at[2]], TP_ENC_OPR, TP_OPR_GT) &&
            tpn_is(&ins[at[3]], TP_ENC_EQC, 0) && ins[at[3]].operand == 0 &&
            tpn_is(&ins[at[6]], TP_ENC_OPR, TP_OPR_GT) &&
            tpn_is(&ins[at[7]], TP_ENC_EQC, 0) && ins[at[7]].operand == 0 &&
            tpn_is(&ins[at[8]], TP_ENC_OPR, TP_OPR_AND)) {
            int x = -1, lo = 0, hi = 0, ok = 0;
            /* x >= lo is `ldc lo; ldl x; gt; eqc 0`,
             * x <= hi is `ldl x; ldc hi; gt; eqc 0` */
            if (ins[at[0]].opcode == TP_ENC_LDC &&
                ins[at[1]].opcode == TP_ENC_LDL &&
                ins[at[4]].opcode == TP_ENC_LDL &&
                ins[at[5]].opcode == TP_ENC_LDC &&
                ins[at[1]].operand == ins[at[4]].operand) {
                x = at[1]; lo = ins[at[0]].operand; hi = ins[at[5]].operand;
                ok = 1;
            } else if (ins[at[0]].opcode == TP_ENC_LDL &&
                       ins[at[1]].opcode == TP_ENC_LDC &&
                       ins[at[4]].opcode == TP_ENC_LDC &&
                       ins[at[5]].opcode == TP_ENC_LDL &&
                       ins[at[0]].operand == ins[at[5]].operand) {
                x = at[0]; hi = ins[at[1]].operand; lo = ins[at[4]].operand;
                ok = 1;
            }
            if (ok && lo <= hi && (long long)hi - lo < 0x7fffffffLL) {
                /* T9000: ldc N; x - lo; gtu.  T800, no gtu: the borrow
                 * of x - lo - N, `ldc 0; x - lo; ldc N; ldiff; rev`
                 * (ldiff: A = B - A - C, B = the borrow out) */
                int xs = ins[x].operand, gtu = tp_have_t9000_feat(TP_T9K_GTU);
                int oc[9], od[9], m = 0;
                v = tpn_push_val(ins, n, head, x, slotr, nslotr);
                if (gtu) {
                    oc[m] = TP_ENC_LDC; od[m++] = hi - lo + 1;
                } else {
                    oc[m] = TP_ENC_LDC; od[m++] = 0;
                }
                oc[m] = TP_ENC_LDL; od[m++] = xs;
                if (lo == 0) {
                    ;
                } else if (v.known && v.lo - lo >= -2147483648LL &&
                           v.hi - lo <= 2147483647LL) {
                    oc[m] = TP_ENC_ADC; od[m++] = -lo;
                } else {
                    oc[m] = TP_ENC_LDC; od[m++] = lo;
                    oc[m] = TP_ENC_OPR; od[m++] = TP_OPR_DIFF;
                }
                if (gtu) {
                    oc[m] = TP_ENC_OPR; od[m++] = TP_OPR_GTU;
                } else {
                    oc[m] = TP_ENC_LDC; od[m++] = hi - lo + 1;
                    oc[m] = TP_ENC_OPR; od[m++] = TP_OPR_LDIFF;
                    oc[m] = TP_ENC_OPR; od[m++] = TP_OPR_REV;
                }
                for (k = 0; k < 9; k++)
                    tpn_put(&ins[at[k]], k < m ? oc[k] : -1,
                            k < m ? od[k] : 0);
                tpn_stats[2]++;
                i = at[8];
                continue;
            }
        }
        /* ldl a; ldc m; and; ldl b; ldc m; and; xor|or|and
         *   ->  ldl a; ldl b; xor|or|and; ldc m; and */
        if (ins[i].opcode == TP_ENC_LDL && tpn_run(ins, n, head, i, 7, at) &&
            ins[at[1]].opcode == TP_ENC_LDC &&
            tpn_is(&ins[at[2]], TP_ENC_OPR, TP_OPR_AND) &&
            ins[at[3]].opcode == TP_ENC_LDL &&
            ins[at[4]].opcode == TP_ENC_LDC &&
            ins[at[4]].operand == ins[at[1]].operand &&
            tpn_is(&ins[at[5]], TP_ENC_OPR, TP_OPR_AND) &&
            (tpn_is(&ins[at[6]], TP_ENC_OPR, TP_OPR_XOR) ||
             tpn_is(&ins[at[6]], TP_ENC_OPR, TP_OPR_OR) ||
             tpn_is(&ins[at[6]], TP_ENC_OPR, TP_OPR_AND))) {
            int m = ins[at[1]].operand, b = ins[at[3]].operand;
            int op = ins[at[6]].operand;
            tpn_put(&ins[at[1]], TP_ENC_LDL, b);
            tpn_put(&ins[at[2]], TP_ENC_OPR, op);
            tpn_put(&ins[at[3]], TP_ENC_LDC, m);
            tpn_put(&ins[at[4]], TP_ENC_OPR, TP_OPR_AND);
            tpn_put(&ins[at[5]], -1, 0);
            tpn_put(&ins[at[6]], -1, 0);
            tpn_stats[3]++;
            i = at[6];
            continue;
        }
        /* ldc K; ldl x; [adc c]; gt|gtu; eqc 0  ->  ldl x; [adc c]; ldc K-1;
         * gt|gtu: !(K > x) is x > K - 1 (K - 1 must not wrap) */
        if (tpn_in_late && ins[i].opcode == TP_ENC_LDC &&
            tpn_run(ins, n, head, i, 5, at) &&
            ins[at[1]].opcode == TP_ENC_LDL) {
            int c0 = ins[at[2]].opcode == TP_ENC_ADC ? 3 : 2, K = ins[i].operand;
            int op = ins[at[c0]].operand;
            {
                if (ins[at[c0]].opcode == TP_ENC_OPR &&
                    (op == TP_OPR_GT || op == TP_OPR_GTU) &&
                    (c0 + 1 < 5) && tpn_is(&ins[at[c0 + 1]], TP_ENC_EQC, 0) &&
                    ins[at[c0 + 1]].operand == 0 &&
                    ((op == TP_OPR_GTU && K >= 1) ||
                     (op == TP_OPR_GT && K != (int)0x80000000))) {
                    int xo = ins[at[1]].operand, ac = ins[at[2]].operand;
                    tpn_put(&ins[at[0]], TP_ENC_LDL, xo);
                    if (c0 == 3) {
                        tpn_put(&ins[at[1]], TP_ENC_ADC, ac);
                        tpn_put(&ins[at[2]], TP_ENC_LDC, K - 1);
                    } else {
                        tpn_put(&ins[at[1]], TP_ENC_LDC, K - 1);
                    }
                    tpn_put(&ins[at[c0]], TP_ENC_OPR, op);
                    tpn_put(&ins[at[c0 + 1]], -1, 0);
                    tpn_stats[0]++;
                    i = at[c0 + 1];
                    continue;
                }
            }
        }
    old_rules:
        if (head[i])
            continue;
        if (ins[i].opcode == TP_ENC_LDC && ins[i].operand > 0 &&
            !(ins[i].operand & (ins[i].operand + 1))) {
            j = tpn_next(ins, n, i);
            if (j >= n || ins[j].opcode != TP_ENC_OPR ||
                ins[j].operand != TP_OPR_AND || ins[j].pinned || head[j])
                continue;
            for (k = i + 1; k < j; k++)
                if (head[k])
                    break;
            if (k < j)
                continue;
            v = tpn_range_before(ins, head, i, slotr, nslotr);
            if (v.known && v.lo >= 0 && v.hi <= ins[i].operand) {
                tpn_put(&ins[i], -1, 0);
                tpn_put(&ins[j], -1, 0);
            }
        } else if (ins[i].opcode == TP_ENC_EQC && ins[i].operand == 0) {
            /* a truth value tested twice is itself */
            j = tpn_next(ins, n, i);
            if (j >= n || ins[j].opcode != TP_ENC_EQC || ins[j].operand != 0 ||
                ins[j].pinned || head[j])
                continue;
            for (k = i + 1; k < j; k++)
                if (head[k])
                    break;
            if (k < j)
                continue;
            v = tpn_range_before(ins, head, i, slotr, nslotr);
            if (v.known && v.lo >= 0 && v.hi <= 1) {
                tpn_put(&ins[i], -1, 0);
                tpn_put(&ins[j], -1, 0);
                i = j;
            }
        } else if (ins[i].opcode == TP_ENC_OPR &&
                   ins[i].operand == TP_OPR_XDBLE) {
            j = tpn_next(ins, n, i);
            l = j < n ? tpn_next(ins, n, j) : n;
            if (l >= n || ins[j].opcode != TP_ENC_LDC || ins[j].pinned ||
                ins[j].operand < 0 || ins[j].operand > 31 ||
                ins[l].opcode != TP_ENC_OPR || ins[l].operand != TP_OPR_LSHR ||
                ins[l].pinned)
                continue;
            for (k = i + 1; k <= l; k++)
                if (head[k])
                    break;
            if (k <= l)
                continue;
            v = tpn_range_before(ins, head, i, slotr, nslotr);
            if (v.known && v.lo >= 0) {
                tpn_put(&ins[i], -1, 0);
                tpn_put(&ins[l], TP_ENC_OPR, TP_OPR_SHR);
            }
        }
    }
    if (tp_knob(TPK_TP_NARROW_STATS) &&
        (tpn_stats[0] | tpn_stats[1] | tpn_stats[2] | tpn_stats[3]))
        fprintf(stderr, "[RANGE] %s: cmp-fold %d eqc1 %d range-test %d "
                "mask-merge %d\n", funcname ? funcname : "?", tpn_stats[0],
                tpn_stats[1], tpn_stats[2], tpn_stats[3]);
    changed = tpn_stats[0] + tpn_stats[1] + tpn_stats[2] + tpn_stats[3];
    memset(tpn_stats, 0, sizeof tpn_stats);
    tcc_free(head);
    return changed;
}

/* The range rules again, after the scheduler, whose deleted round trips
 * are what make their shapes adjacent.  A word slot's range is the union
 * of what its every STL stores (the code since the block's start shows
 * it), when nothing else can write it: no LDLP access or escaping address
 * reaches it, and it is no stacked argument a callee may change.  Two
 * rounds, the second reading the first's slot ranges. */
static int tpn_late(struct tp_relax *r, int top)
{
    int changed;
    struct tp_relax_insn *ins = r->ins;
    const int n = r->n;
    const int F = func_frame_words;
    struct tpn_val *slotr;
    char *head, *bad;
    int i, k, s, round;

    if (func_uses_inline_asm || F > 4096 || n <= 0)
        return 0;
    for (i = 0, k = -1; i < n; i++) {
        if (ins[i].opcode == -1)
            continue;
        if (k < 0)
            k = i;
        if (ins[i].opcode == TP_ENC_OPR && ins[i].operand == TP_OPR_GAJW)
            return 0;
        if (ins[i].opcode == TP_ENC_AJW && i != k && !ins[i].icall) {
            int j = tpn_next(ins, n, i);
            if (j >= n || ins[j].opcode != TP_ENC_OPR ||
                ins[j].operand != TP_OPR_RET)
                return 0;           /* the frame moves mid-function */
        }
    }
    if (F <= 0) {                   /* no locals: the rules without ranges */
        for (changed = 0, round = 0; round < 3; round++) {
            int c;
            tpn_in_late = 1;
            c = tpn_peephole(r, NULL, 0);
            tpn_in_late = 0;
            changed += c;
            if (!c)
                break;
        }
        return changed;
    }
    slotr = tcc_mallocz(sizeof(*slotr) * F);
    bad = tcc_mallocz(F + 1);
    head = tcc_mallocz(n + 1);
    for (i = 0; i < n; i++) {
        if (ins[i].tab > 0)
            head[i] = 1;
        if (ins[i].is_branch && ins[i].target_old >= 0 &&
            ins[i].target_old < r->size &&
            (k = r->tidx[ins[i].target_old]) >= 0)
            head[k] = 1;
        if (ins[i].opcode == TP_ENC_LDLP) {
            int j = tpn_next(ins, n, i), lo, ext;
            ext = j < n ? tpg_ldlp_extent(&ins[j], &lo) : -1;
            if (ext < 0) {
                lo = -1;
                ext = 3;
            }
            for (s = ins[i].operand + lo; s < ins[i].operand + lo + ext; s++)
                if (s >= 0 && s < F)
                    bad[s] = 1;
        }
    }
    for (i = 0; i < n && ins[i].opcode == -1; i++)
        ;
    if (i < n)
        head[i] = 1;
    for (s = 0; s < F; s++)
        if (s < func_max_outgoing_words || tps_word_escapes(ins, n, s, top))
            bad[s] = 1;
    for (round = 0; round < 2; round++) {
        struct tpn_val *nr = tcc_mallocz(sizeof(*nr) * F);
        char *seen = tcc_mallocz(F + 1), *unk = tcc_mallocz(F + 1);
        for (i = 0; i < n; i++) {
            struct tpn_val v;
            s = ins[i].operand;
            if (ins[i].opcode != TP_ENC_STL || s < 0 || s >= F || bad[s] ||
                unk[s])
                continue;
            v = tpn_range_before(ins, head, i, round ? slotr : NULL,
                                 round ? F : 0);
            if (!v.known || ins[i].pinned) {
                unk[s] = 1;
                continue;
            }
            if (!seen[s])
                nr[s] = v;
            else
                tpn_set(&nr[s], v.lo < nr[s].lo ? v.lo : nr[s].lo,
                        v.hi > nr[s].hi ? v.hi : nr[s].hi);
            seen[s] = 1;
        }
        for (s = 0; s < F; s++)
            if (bad[s] || unk[s] || !seen[s])
                nr[s].known = 0;
        tcc_free(slotr);
        tcc_free(seen);
        tcc_free(unk);
        slotr = nr;
    }
    tcc_free(head);
    tcc_free(bad);
    /* again while it finds more: one rule's result is another's shape */
    for (changed = 0, round = 0; round < 3; round++) {
        int c;
        tpn_in_late = 1;
        c = tpn_peephole(r, slotr, F);
        tpn_in_late = 0;
        changed += c;
        if (!c)
            break;
    }
    tcc_free(slotr);
    return changed;
}

/* A direct call to a tiny leaf earlier in this section becomes its body.
 * The callee must be exactly
 *
 *   [ajw -N;] ldl N+1; <pure stack operations>; [ajw N;] ret
 *
 * a function of its first argument alone, which the call finds in A:
 * `call` leaves it in the callee's word N+1, and the body reads it first,
 * once.  The operations below name no workspace word, store nothing and do
 * not branch; a call leaves B and C undefined, so what the body does to
 * them is no change.  ee_isdigit in CoreMark.  Not a callee that holds a
 * relocation (its bytes are not final until link) or a weak one.  The
 * call's relocation goes when the function is committed (tpi_drop).
 * TCC_TRANSPUTER_INLINE_LEAF=0 turns it off. */
#define TPI_MAXB 12

static int tpi_pure_op(int opc, int opd)
{
    switch (opc) {
    case TP_ENC_LDC: case TP_ENC_ADC: case TP_ENC_EQC: case TP_ENC_LDNL:
    case TP_ENC_LDNLP:
        return 1;
    case TP_ENC_OPR:
        switch (opd) {
        case TP_OPR_AND: case TP_OPR_OR: case TP_OPR_XOR: case TP_OPR_NOT:
        case TP_OPR_SHL: case TP_OPR_SHR: case TP_OPR_GT: case TP_OPR_GTU:
        case TP_OPR_DIFF: case TP_OPR_SUM: case TP_OPR_BSUB:
        case TP_OPR_WSUB: case TP_OPR_PROD: case TP_OPR_REV: case TP_OPR_DUP:
        case TP_OPR_LB: case TP_OPR_LDIFF:
            return 1;
        case TP_OPR_SSUB: case TP_OPR_XBWORD: case TP_OPR_XSWORD:
        case TP_OPR_LBX: case TP_OPR_LS: case TP_OPR_LSX:
            return tp_isa_has(TP_EF_T9000);
        }
    }
    return 0;
}

/* The body of the leaf at section offset t, ops after the argument's ldl,
 * into opc/opd; returns their count, or -1. */
static int tpi_leaf(Section *sec, int t, int limit, int *opc, int *opd)
{
    uint8_t *code = sec->data;
    int p = t, o, d, len, N = 0, m = 0, k;

    len = tp_decode_instr_at(code, limit, p, &o, &d);
    if (len <= 0)
        return -1;
    if (o == TP_ENC_AJW) {
        if (d >= 0)
            return -1;
        N = -d;
        p += len;
        len = tp_decode_instr_at(code, limit, p, &o, &d);
        if (len <= 0)
            return -1;
    }
    /* `ldc K; ldl N+1` (the scheduler put the constant first) is the
     * argument under K: `ldc K; rev` with the argument already in A */
    if (o == TP_ENC_LDC) {
        int o2, d2, len2 = tp_decode_instr_at(code, limit, p + len, &o2, &d2);
        if (len2 <= 0 || o2 != TP_ENC_LDL || d2 != N + 1)
            return -1;
        opc[m] = TP_ENC_LDC; opd[m++] = d;
        opc[m] = TP_ENC_OPR; opd[m++] = TP_OPR_REV;
        p += len;
        len = len2;
        o = o2;
        d = d2;
    }
    if (o != TP_ENC_LDL || d != N + 1)
        return -1;
    p += len;
    for (k = 0; k <= TPI_MAXB; k++) {
        len = tp_decode_instr_at(code, limit, p, &o, &d);
        if (len <= 0)
            return -1;
        p += len;
        if (o == TP_ENC_AJW || (o == TP_ENC_OPR && d == TP_OPR_RET)) {
            if (o == TP_ENC_AJW) {
                if (d != N)
                    return -1;
                len = tp_decode_instr_at(code, limit, p, &o, &d);
                if (len <= 0 || o != TP_ENC_OPR || d != TP_OPR_RET)
                    return -1;
                p += len;
            } else if (N) {
                return -1;
            }
            break;
        }
        if (!tpi_pure_op(o, d) || m == TPI_MAXB)
            return -1;
        opc[m] = o;
        opd[m] = d;
        m++;
    }
    if (k > TPI_MAXB)
        return -1;
    /* nothing in it the linker will patch */
    if (sec->reloc) {
        ElfW_Rel *rel = (ElfW_Rel *)sec->reloc->data;
        int nrel = sec->reloc->data_offset / sizeof(*rel), i;
        for (i = 0; i < nrel; i++)
            if ((int)rel[i].r_offset >= t && (int)rel[i].r_offset < p)
                return -1;
    }
    return m;
}

static int tp_inline_leaves(struct tp_relax *r)
{
    static int off = -1;
    struct tp_relax_insn *ins = r->ins, *out;
    const int n = r->n;
    ElfW_Rel *rel;
    int nrel, i, j, k, s, nadd = 0, cap = 0, any = 0;
    int *at = NULL, *aopc = NULL, *aopd = NULL;

    tpi_ndrop = 0;
    if (off < 0) {
        const char *e = tp_knob(TPK_TCC_TRANSPUTER_INLINE_LEAF);
        off = (e && *e == '0') ? 1 : 0;
    }
    if (off || !near_call_count || !r->sec->reloc)
        return 0;
    rel = (ElfW_Rel *)r->sec->reloc->data;
    nrel = r->sec->reloc->data_offset / sizeof(*rel);
    for (i = 0; i < near_call_count && tpi_ndrop < 256; i++) {
        int o = near_call_log[i].offset - r->base, t = near_call_log[i].target;
        int kk = (o >= 0 && o < r->size) ? r->tidx[o] : -1, m, q, ri, cnt;
        int opc[TPI_MAXB], opd[TPI_MAXB];
        ElfW(Sym) *es;

        if (kk < 0 || t < 0 || t >= r->base)
            continue;
        for (m = kk; m < n && ins[m].opcode == -1; m++)
            ;
        if (m == n || ins[m].opcode != TP_ENC_CALL ||
            r->base + ins[m].old_off + ins[m].old_len + ins[m].operand != t)
            continue;
        /* the call's relocation, and its symbol: not weak */
        for (ri = func_start_nrel; ri < nrel; ri++)
            if ((int)rel[ri].r_offset == near_call_log[i].offset)
                break;
        if (ri == nrel)
            continue;
        es = (ElfW(Sym) *)symtab_section->data + ELFW(R_SYM)(rel[ri].r_info);
        if (ELFW(ST_BIND)(es->st_info) == STB_WEAK)
            continue;
        cnt = tpi_leaf(r->sec, t, r->base, opc, opd);
        if (cnt < 0)
            continue;
        /* the body goes where the call was: no jump may land there */
        for (q = 0; q < n; q++) {
            int to = ins[q].target_old;
            if (ins[q].is_branch && !ins[q].ext && to >= 0 &&
                to < r->size && r->tidx[to] >= kk && r->tidx[to] <= m)
                break;
        }
        if (q < n)
            continue;
        /* the call and its padding go; the body goes in front */
        for (q = kk; q <= m; q++) {
            ins[q].opcode = -1;
            ins[q].operand = 0;
            ins[q].pinned = 0;
            ins[q].is_branch = 0;
            ins[q].new_len = 0;
        }
        for (j = 0; j < cnt; j++) {
            if (nadd == cap) {
                cap = cap ? 2 * cap : 32;
                at = tcc_realloc(at, sizeof(int) * cap);
                aopc = tcc_realloc(aopc, sizeof(int) * cap);
                aopd = tcc_realloc(aopd, sizeof(int) * cap);
            }
            at[nadd] = m;
            aopc[nadd] = opc[j];
            aopd[nadd] = opd[j];
            nadd++;
        }
        tpi_drop[tpi_ndrop++] = ri;
        any = 1;
    }
    if (!nadd)
        return any;
    /* by position, stable */
    for (i = 1; i < nadd; i++)
        for (j = i; j > 0 && at[j] < at[j - 1]; j--) {
            int t;
            t = at[j]; at[j] = at[j - 1]; at[j - 1] = t;
            t = aopc[j]; aopc[j] = aopc[j - 1]; aopc[j - 1] = t;
            t = aopd[j]; aopd[j] = aopd[j - 1]; aopd[j - 1] = t;
        }
    out = tcc_malloc(sizeof(*out) * (n + nadd + 1));
    for (i = 0, k = 0, s = 0; i < n; i++) {
        for (; k < nadd && at[k] == i; k++) {
            memset(&out[s], 0, sizeof(out[s]));
            out[s].old_off = ins[i].old_off;
            out[s].synth = 1;
            tpn_put(&out[s], aopc[k], aopd[k]);
            s++;
        }
        out[s++] = ins[i];
    }
    tcc_free(ins);
    r->ins = out;
    r->n = s;
    for (i = 0; i <= r->size; i++)
        r->tidx[i] = -1;
    for (i = 0; i < r->n; i++)
        if (!out[i].synth)
            r->tidx[out[i].old_off] = i;
    tcc_free(at);
    tcc_free(aopc);
    tcc_free(aopd);
    return 1;
}

/* A flag tested right after a short block, on a way in that has just set
 * it to a constant:
 *
 *   P:  ldc K; stl v; j L          L:  S; ldl v; cj M; N: ...
 *
 * (or P falling into L) becomes, on that way in, S copied and a jump
 * straight to M (K = 0) or N (K != 0); the `ldc K; stl v` goes too when
 * nothing reads v before writing it again from there.  S is straight-line
 * code that leaves the stack as it found it and does not name v.  This is
 * `if (c) { ...; f = 1; } else f = 0; ...; if (f)` in crcu8.  v is a word
 * no address reaches.  TCC_TRANSPUTER_FLAGS=0 turns it off. */
#define TPF_MAXS 8

struct tpf_add {
    int at, s0, s1;     /* before ins[at], a copy of the records s0..s1 */
    int tgt;            /* then a J to this old offset, unless < 0 */
    int rep;            /* the copy replaces ins[at]'s place: a jump to
                           ins[at] runs it (ins[at] itself follows) */
};

/* Make the insertions (r->ins, r->n and r->tidx are replaced). */
static void tpf_insert(struct tp_relax *r, struct tpf_add *add, int nins)
{
    struct tp_relax_insn *ins = r->ins, *out;
    const int n = r->n;
    int i, k, s;

    /* insert, by position */
    for (i = 1; i < nins; i++)
        for (k = i; k > 0 && add[k].at < add[k - 1].at; k--) {
            struct tpf_add t = add[k];
            add[k] = add[k - 1];
            add[k - 1] = t;
        }
    {
        int total = n + 1;
        for (i = 0; i < nins; i++)
            total += add[i].s1 - add[i].s0 + 2;
        out = tcc_malloc(sizeof(*out) * total);
    }
    for (i = 0, k = 0, s = 0; i < n; i++) {
        int first = -1, own = 0;
        for (; k < nins && add[k].at == i; k++) {
            int q;
            own |= add[k].rep;
            for (q = add[k].s0; q <= add[k].s1; q++) {
                if (ins[q].opcode == -1)
                    continue;
                out[s] = ins[q];
                out[s].old_off = ins[i].old_off;
                out[s].synth = 1;
                out[s].dup_load = 0;
                if (first < 0)
                    first = s;
                s++;
            }
            if (add[k].tgt >= 0) {
                memset(&out[s], 0, sizeof(out[s]));
                out[s].old_off = ins[i].old_off;
                out[s].synth = 1;
                out[s].opcode = TP_ENC_J;
                out[s].is_branch = 1;
                out[s].target_old = add[k].tgt;
                out[s].old_len = 0;
                out[s].new_len = 1;
                s++;
            }
        }
        out[s] = ins[i];
        if (own && first >= 0 && !ins[i].synth && !ins[i].pinned) {
            /* the first copied record takes ins[i]'s place */
            out[first].synth = 0;
            out[first].old_len = ins[i].old_len;
            out[s].synth = 1;
        }
        s++;
    }
    tcc_free(ins);
    r->ins = out;
    r->n = s;
    for (i = 0; i <= r->size; i++)
        r->tidx[i] = -1;
    for (i = 0; i < r->n; i++)
        if (!out[i].synth)
            r->tidx[out[i].old_off] = i;
}

static int tpf_target(const struct tp_relax *r, int i)
{
    int t = r->ins[i].target_old, k;

    if (r->ins[i].ext || t < 0 || t >= r->size || (k = r->tidx[t]) < 0)
        return -1;
    while (k < r->n && r->ins[k].opcode == -1 && !r->ins[k].pinned)
        k++;
    return k < r->n ? k : -1;
}

static int tpf_prev(const struct tp_relax_insn *ins, int i)
{
    for (i--; i >= 0 && ins[i].opcode == -1 && !ins[i].pinned; i--)
        ;
    return i;
}

/* Can v be read, from k on, before it is written? */
static int tpf_live(const struct tp_relax *r, int k, int v, char *seen)
{
    const struct tp_relax_insn *ins = r->ins;
    const int n = r->n;
    int *work = tcc_malloc(sizeof(int) * (n + 1)), nw = 0, live = 0;

    memset(seen, 0, n + 1);
    work[nw++] = k;
    while (nw && !live) {
        int i = work[--nw];
        for (;;) {
            int o, d, t;
            while (i < n && ins[i].opcode == -1 && !ins[i].pinned)
                i++;
            if (i >= n || seen[i])
                break;
            seen[i] = 1;
            o = ins[i].opcode;
            d = ins[i].operand;
            if (ins[i].pinned && o == -1) {
                live = 1;           /* raw bytes: cannot follow */
                break;
            }
            if (o == TP_ENC_LDL && d == v) {
                live = 1;
                break;
            }
            if (o == TP_ENC_STL && d == v)
                break;
            if (o == TP_ENC_OPR && (d == TP_OPR_RET))
                break;
            if (ins[i].tab || ins[i].icall ||
                (o == TP_ENC_OPR && d == TP_OPR_GCALL)) {
                live = 1;           /* a jump table or a call we lose */
                break;
            }
            if (o == TP_ENC_J || o == TP_ENC_CJ) {
                if (!ins[i].is_branch || (t = tpf_target(r, i)) < 0) {
                    live = 1;
                    break;
                }
                if (o == TP_ENC_J) {
                    i = t;
                    continue;
                }
                work[nw++] = t;
            }
            i++;
        }
    }
    tcc_free(work);
    return live;
}

static int tp_thread_flags(struct tp_relax *r)
{
    static int off = -1;
    struct tp_relax_insn *ins = r->ins;
    const int n = r->n;
    const int F = func_frame_words;
    char *head, *bad, *seen;
    int *npred, i, k, s, nins = 0, any = 0;
    /* each insertion: before ins[at], S = [s0, s1], then a J to tgt
     * (an old offset) unless tgt < 0 (the existing J at `at` is retargeted) */
    struct tpf_add add[64];

    if (off < 0) {
        const char *e = tp_knob(TPK_TCC_TRANSPUTER_FLAGS);
        off = (e && *e == '0') ? 1 : 0;
    }
    if (off || func_uses_inline_asm || F <= 0 || F > 4096 || n <= 0)
        return 0;
    for (i = 0, k = -1; i < n; i++) {
        if (ins[i].opcode == -1)
            continue;
        if (k < 0)
            k = i;
        if (ins[i].opcode == TP_ENC_OPR && ins[i].operand == TP_OPR_GAJW)
            return 0;
        if (ins[i].opcode == TP_ENC_AJW && i != k && !ins[i].icall) {
            int j = tpn_next(ins, n, i);
            if (j >= n || ins[j].opcode != TP_ENC_OPR ||
                ins[j].operand != TP_OPR_RET)
                return 0;
        }
    }
    head = tcc_mallocz(n + 1);
    npred = tcc_mallocz(sizeof(int) * (n + 1));
    bad = tcc_mallocz(F + 1);
    seen = tcc_mallocz(n + 1);
    for (i = 0; i < n; i++) {
        if (ins[i].tab > 0)
            head[i] = 1;
        if (ins[i].is_branch && (k = tpf_target(r, i)) >= 0) {
            head[k] = 1;
            npred[k]++;
        }
        if (ins[i].opcode == TP_ENC_LDLP) {
            int j = tpn_next(ins, n, i), lo, ext;
            ext = j < n ? tpg_ldlp_extent(&ins[j], &lo) : -1;
            if (ext < 0) {
                lo = -1;
                ext = 3;
            }
            for (s = ins[i].operand + lo; s < ins[i].operand + lo + ext; s++)
                if (s >= 0 && s < F)
                    bad[s] = 1;
        }
    }
    for (i = 0; i < n && ins[i].opcode == -1; i++)
        ;
    if (i < n)
        head[i] = 1;
    for (s = 0; s < F; s++)
        if (s < func_max_outgoing_words || tps_word_escapes(ins, n, s, 0))
            bad[s] = 1;

    /* the tests: ldl v; cj M after a straight block L..S */
    for (i = 0; i < n && nins < 60; i++) {
        int c, h, v, depth, len, q, p, M, N, dl, rd, ph;
        if (ins[i].opcode != TP_ENC_LDL || ins[i].pinned || ins[i].synth)
            continue;
        v = ins[i].operand;
        if (v < 0 || v >= F || bad[v])
            continue;
        c = tpn_next(ins, n, i);
        if (c >= n || ins[c].opcode != TP_ENC_CJ || !ins[c].is_branch ||
            ins[c].pinned || head[c] || (M = tpf_target(r, c)) < 0)
            continue;
        N = tpn_next(ins, n, c);
        if (N >= n || ins[N].synth || ins[N].pinned)
            continue;
        /* back to the block's start, a branch target */
        for (h = i, len = 0; !head[h] && len <= TPF_MAXS; len++) {
            q = tpf_prev(ins, h);
            if (q < 0 || ins[q].is_branch || ins[q].opcode == TP_ENC_J ||
                ins[q].opcode == TP_ENC_CJ)
                break;
            h = q;
        }
        if (len > TPF_MAXS || !head[h] || ins[h].synth || ins[h].pinned)
            continue;
        ph = tpf_prev(ins, h);
        /* S = [h, i): straight, balanced, does not name v */
        depth = 0;
        for (q = h; q < i; q++) {
            int o = ins[q].opcode, d = ins[q].operand;
            if (o == -1 && !ins[q].pinned)
                continue;
            if (ins[q].pinned || ins[q].tab || ins[q].icall ||
                ins[q].is_branch || ins[q].synth || ins[q].workspace_extent ||
                o == TP_ENC_J || o == TP_ENC_CJ || o == TP_ENC_CALL ||
                o == TP_ENC_AJW || o == TP_ENC_LDLP ||
                ((o == TP_ENC_LDL || o == TP_ENC_STL) && d == v) ||
                !tps_effect(o, d, &dl, &rd) || rd > depth)
                break;
            if (o == TP_ENC_OPR && (d == TP_OPR_RET || d == TP_OPR_GCALL ||
                                    d == TP_OPR_GAJW))
                break;
            depth += dl;
            if (depth > 3)
                break;
        }
        if (q < i || depth != 0)
            continue;
        /* the ways in that have just stored a constant to v */
        for (p = 0; p < n && nins < 60; p++) {
            int pj = -1, ft = 0, q1, q2, K, tgt, dead;
            if (ins[p].opcode == TP_ENC_J && ins[p].is_branch &&
                !ins[p].pinned && !ins[p].tab && !ins[p].synth &&
                tpf_target(r, p) == h) {
                pj = p;
                q2 = tpf_prev(ins, p);
            } else if (p == ph && ins[p].opcode == TP_ENC_STL) {
                ft = 1;
                q2 = p;
            } else {
                continue;
            }
            if (q2 < 0 || ins[q2].opcode != TP_ENC_STL ||
                ins[q2].operand != v || ins[q2].pinned || head[q2] ||
                (pj >= 0 && head[pj]))
                continue;
            q1 = tpf_prev(ins, q2);
            if (q1 < 0 || ins[q1].opcode != TP_ENC_LDC || ins[q1].pinned)
                continue;
            K = ins[q1].operand;
            tgt = K == 0 ? M : N;
            if (ins[tgt].synth)
                continue;
            dead = !tpf_live(r, tgt, v, seen);
            add[nins].at = ft ? h : pj;
            /* A jump to L may name the padding in front of it (a local
             * access is a PFIX 0 placeholder at first): the fall-through
             * copy goes in front of all of that, where no jump lands. */
            while (ft && add[nins].at > 0 &&
                   ins[add[nins].at - 1].opcode == -1 &&
                   !ins[add[nins].at - 1].pinned)
                add[nins].at--;
            add[nins].s0 = h;
            add[nins].s1 = i - 1;
            add[nins].tgt = ft ? ins[tgt].old_off : -1;
            add[nins].rep = 0;
            if (pj >= 0)
                ins[pj].target_old = ins[tgt].old_off;
            head[tgt] = 1;
            if (dead) {
                tpn_put(&ins[q1], -1, 0);
                tpn_put(&ins[q2], -1, 0);
            }
            nins++;
            any = 1;
            if (ft)
                break;              /* one fall-through way in */
        }
    }
    tcc_free(head);
    tcc_free(npred);
    tcc_free(bad);
    tcc_free(seen);
    if (!nins)
        return any;

    tpf_insert(r, add, nins);
    if (tp_knob(TPK_TP_NARROW_STATS))
        fprintf(stderr, "[FLAGS] %s: %d ways in\n",
                funcname ? funcname : "?", nins);
    return 1;
}

/* A `j L` to a short block takes a copy of the block instead:
 *
 *   j L ... L: B; j X       ->   B; j X
 *   stl x; j L ... L: ldl x; B (falling into F)   ->   stl x; ldl x; B; j F
 *
 * The first saves the jump; the second puts a store and its reload side
 * by side for the scheduler, which runs after.  B is straight code with no
 * branch, call or table in it.  A block left with no way in stays, as dead
 * bytes.  TCC_TRANSPUTER_TAIL_DUP=0 turns it off. */
#define TPT_MAXB 6

/* ---- a switch in a loop, threaded on its known selector ----
 *
 * A state machine `for (...) switch (state) { case A: ... state = B; ... }`
 * goes back to the loop top after each case, tests the state again
 * (`state != INVALID`) and dispatches through the table again, although on
 * every such way back the state is a known constant: set by `ldc K; stl
 * state`, or untouched since the case was entered by table entry K.  This
 * gives each such `j` its own copy of the way to the dispatch, with the
 * tests of the state decided and the dispatch replaced by a jump to case
 * K: core_state_transition's 9 instructions a character (a test, the
 * table's ldl/ldc/ldpi/wsub/gcall and the entry's j).
 *
 * The selector is the word the dispatch loads (`ldl d; ...; gcall`, the
 * scheduler having propagated the switch's copy): table entry i runs
 * exactly when d holds i.  A forward analysis gives d's value at every
 * instruction (unknown, one constant, or varying); d must be a frame word
 * no pointer reaches and no callee writes.  The copy starts at the `j`'s
 * target and follows the code: `ldl d; [eqc c;] cj X` is decided and not
 * copied, a `j` is followed, the dispatch becomes `j case`, anything else
 * is copied as it is (a `cj` on something else keeps its target, or the
 * case if it leads straight to the dispatch), and the copy ends with a
 * `j` back into the original code at the first instruction it will not
 * copy (a call, a store to d, ...).  The `j` itself is left behind the
 * copy, unreachable; a jump to it runs the copy.  Nothing is live on the
 * stack at a `j`, so the copy starts as the original does.
 * TCC_TRANSPUTER_SWITCH_THREAD=0 turns it off. */
#define TPT_MAXC 40             /* instructions in one copy */
#define TPT_MAXN 48             /* copies a function */

struct tpt_add {
    int at, n;
    int ft;                     /* fall-through only: jumps to ins[at] skip it */
    struct tp_relax_insn rec[TPT_MAXC + 1];
};

static void tpt_insert(struct tp_relax *r, struct tpt_add *add, int nadd)
{
    struct tp_relax_insn *ins = r->ins, *out;
    const int n = r->n;
    int i, k, s, total = n + 1;

    for (i = 1; i < nadd; i++)
        for (k = i; k > 0 && add[k].at < add[k - 1].at; k--) {
            struct tpt_add t = add[k];
            add[k] = add[k - 1];
            add[k - 1] = t;
        }
    for (i = 0; i < nadd; i++)
        total += add[i].n;
    out = tcc_malloc(sizeof(*out) * total);
    for (i = 0, k = 0, s = 0; i < n; i++) {
        int first = -1;
        for (; k < nadd && add[k].at == i; k++) {
            int q;
            for (q = 0; q < add[k].n; q++) {
                out[s] = add[k].rec[q];
                out[s].old_off = ins[i].old_off;
                out[s].synth = 1;
                if (first < 0 && !add[k].ft)
                    first = s;
                s++;
            }
        }
        out[s] = ins[i];
        if (first >= 0 && !ins[i].synth && !ins[i].pinned) {
            /* the copy takes ins[i]'s place: a jump to it runs the copy */
            out[first].synth = 0;
            out[first].old_len = ins[i].old_len;
            out[s].synth = 1;
        }
        s++;
    }
    tcc_free(ins);
    r->ins = out;
    r->n = s;
    for (i = 0; i <= r->size; i++)
        r->tidx[i] = -1;
    for (i = 0; i < r->n; i++)
        if (!out[i].synth)
            r->tidx[out[i].old_off] = i;
}

struct tpt_ctx {
    struct tp_relax *r;
    int d, disp, ntab;          /* selector, dispatch's first record */
    int *entry;                 /* entry i's case: a record index */
};

static void tpt_rec(struct tp_relax_insn *c, int opc, int opd)
{
    memset(c, 0, sizeof(*c));
    c->opcode = opc;
    c->operand = opd;
    c->new_len = tp_encode_len(opc, opd);
}

/* Follow the code from pc with d == K; emit into a (NULL: only resolve).
 * Returns the record where a plain copy would stop (the jump target to
 * end with), or -1 for nothing usable; *gain counts decisions. */
static int tpt_trace(struct tpt_ctx *x, int pc, int K, struct tpt_add *a,
                     int *gain, int depth, int lvl);

/* Would a copy from pc, with depth values on the stack, reach the case? */
static int tpt_reaches(struct tpt_ctx *x, int pc, int K, int depth, int lvl)
{
    struct tpt_add s;
    int g = 0;
    if (lvl > 2)
        return 0;
    s.at = 0;
    s.n = 0;
    return tpt_trace(x, pc, K, &s, &g, depth, lvl + 1) <= -2;
}

static int tpt_trace(struct tpt_ctx *x, int pc, int K, struct tpt_add *a,
                     int *gain, int depth, int lvl)
{
    struct tp_relax_insn *ins = x->r->ins;
    const int n = x->r->n;
    int steps = 0;

    while (pc >= 0 && pc < n && steps++ < 96) {
        int o = ins[pc].opcode, dd = ins[pc].operand, q, q2, tg;
        if (o == -1) {
            pc++;
            continue;
        }
        if (ins[pc].tab > 0 || ins[pc].pinned || ins[pc].icall)
            return pc;
        if (pc == x->disp) {
            if (K < 0 || K >= x->ntab || x->entry[K] < 0)
                return pc;
            (*gain)++;
            return -2 - x->entry[K];        /* end: j to the case */
        }
        if (ins[pc].tab < 0)
            return pc;                      /* inside another dispatch */
        /* the bounds check, ldc N; ldl d; gt|gtu; cj X: decided too */
        if (o == TP_ENC_LDC && !ins[pc].pinned &&
            (q = tpn_next(ins, n, pc)) < n && ins[q].opcode == TP_ENC_LDL &&
            ins[q].operand == x->d && (q2 = tpn_next(ins, n, q)) < n &&
            ins[q2].opcode == TP_ENC_OPR && (ins[q2].operand == TP_OPR_GT ||
                                             ins[q2].operand == TP_OPR_GTU)) {
            int q3 = tpn_next(ins, n, q2), v;
            if (q3 < n && ins[q3].opcode == TP_ENC_CJ && ins[q3].is_branch &&
                !ins[q3].pinned && (tg = tpf_target(x->r, q3)) >= 0) {
                if (ins[q2].operand == TP_OPR_GT)
                    v = dd > K;
                else
                    v = (unsigned int)dd > (unsigned int)K;
                (*gain)++;
                pc = v == 0 ? tg : q3 + 1;
                continue;
            }
        }
        if (o == TP_ENC_LDL && dd == x->d) {
            /* ldl d; [eqc c;] cj X: decided */
            q = tpn_next(ins, n, pc);
            if (q < n && ins[q].opcode == TP_ENC_EQC && !ins[q].pinned) {
                q2 = tpn_next(ins, n, q);
                if (q2 < n && ins[q2].opcode == TP_ENC_CJ &&
                    ins[q2].is_branch && !ins[q2].pinned &&
                    (tg = tpf_target(x->r, q2)) >= 0) {
                    int v = K == ins[q].operand;
                    (*gain)++;
                    pc = v == 0 ? tg : q2 + 1;
                    continue;
                }
            } else if (q < n && ins[q].opcode == TP_ENC_CJ &&
                       ins[q].is_branch && !ins[q].pinned &&
                       (tg = tpf_target(x->r, q)) >= 0) {
                (*gain)++;
                pc = K == 0 ? tg : q + 1;
                continue;
            }
        }
        if (o == TP_ENC_J) {
            if (!ins[pc].is_branch || (tg = tpf_target(x->r, pc)) < 0)
                return pc;
            pc = tg;
            continue;
        }
        if (o == TP_ENC_CALL || o == TP_ENC_AJW || o == TP_ENC_LDLP ||
            ((o == TP_ENC_STL || o == TP_ENC_LDLP) && dd == x->d) ||
            (o == TP_ENC_OPR && (dd == TP_OPR_RET || dd == TP_OPR_GCALL ||
                                 dd == TP_OPR_GAJW)) ||
            ins[pc].workspace_extent)
            return pc;
        if (!a) {
            return pc;              /* resolving: real code from here */
        }
        if (a->n >= TPT_MAXC - 1)
            return -1;
        if (o == TP_ENC_CJ) {
            int t2, g2 = 0, e, ft = pc + 1;
            if (!ins[pc].is_branch || (t2 = tpf_target(x->r, pc)) < 0)
                return -1;
            /* A loop's own test continues on its taken side: when only
             * that way reaches the case, test the other way round,
             * `eqc 0; cj <fall-through>`, and copy on along the taken
             * side.  Only with the tested value alone on the stack: the
             * fall-through then gets a spare 0 where it had nothing. */
            e = tpt_trace(x, t2, K, NULL, &g2, depth, lvl);
            if (e > -2 && depth == 1 && !ins[ft].synth &&
                !tpt_reaches(x, ft, K, 0, lvl) &&
                tpt_reaches(x, t2, K, 0, lvl)) {
                if (a->n >= TPT_MAXC - 2)
                    return -1;
                tpt_rec(&a->rec[a->n++], TP_ENC_EQC, 0);
                tpt_rec(&a->rec[a->n], TP_ENC_CJ, 0);
                a->rec[a->n].is_branch = 1;
                a->rec[a->n].target_old = ins[ft].old_off;
                a->n++;
                pc = t2;
                depth = 0;
                continue;
            }
            if (e <= -2) {
                t2 = -2 - e;        /* straight to the case */
                *gain += g2;
            } else if (e >= 0 && g2) {
                t2 = e;             /* past the decided tests */
                *gain += g2;
            }
            if (ins[t2].synth)
                return -1;
            a->rec[a->n] = ins[pc];
            a->rec[a->n].target_old = ins[t2].old_off;
            a->rec[a->n].dup_load = 0;
            a->n++;
            pc++;
            depth = depth > 0 ? depth - 1 : -1;
            continue;
        }
        {
            int dl, rd;
            if (depth >= 0 && tps_effect(o, dd, &dl, &rd) && rd <= depth)
                depth += dl;
            else
                depth = -1;
            if (depth > 3)
                depth = 3;
        }
        a->rec[a->n] = ins[pc];
        a->rec[a->n].is_branch = 0;
        a->rec[a->n].dup_load = 0;
        a->n++;
        pc++;
    }
    return -1;
}

static int tp_switch_thread(struct tp_relax *r, int top)
{
    static int off = -1;
    struct tp_relax_insn *ins = r->ins;
    const int n = r->n;
    const int F = func_frame_words;
    struct tpt_add *add = NULL;
    int nadd = 0, t;

    if (tpk_off(TPK_TCC_TRANSPUTER_SWITCH_THREAD, &off, 1) ||
        func_uses_inline_asm || func_has_volatile || n <= 0 || F <= 0)
        return 0;
    for (t = 1; t <= swtab_count && nadd < TPT_MAXN; t++) {
        struct tpt_ctx x;
        int i, d0 = -1, g = -1, ne = 0, *val, *st, *work, nw = 0, p;
        for (i = 0; i < n; i++) {
            if (ins[i].tab == -t && ins[i].opcode != -1) {
                if (d0 < 0)
                    d0 = i;
                if (ins[i].opcode == TP_ENC_OPR &&
                    ins[i].operand == TP_OPR_GCALL)
                    g = i;
            }
            if (ins[i].tab == t && ins[i].opcode == TP_ENC_J)
                ne++;
        }
        /* the dispatch's own records start after the index load */
        if (d0 >= 0)
            d0 = tpf_prev(ins, d0);
        if (d0 < 0 || g < 0 || ins[d0].opcode != TP_ENC_LDL ||
            ins[d0].pinned || !ne)
            continue;
        x.r = r;
        x.d = ins[d0].operand;
        x.disp = d0;
        x.ntab = ne;
        if (x.d < 0 || x.d >= F || x.d < func_max_outgoing_words ||
            tps_word_escapes(ins, n, x.d, top))
            continue;
        {
            int bad = 0;
            for (i = 0; i < n; i++)
                if (ins[i].opcode == TP_ENC_LDLP &&
                    (ins[i].operand == x.d || ins[i].operand == x.d - 1))
                    bad = 1;
            if (bad)
                continue;
        }
        x.entry = tcc_malloc(sizeof(int) * ne);
        for (i = 0, ne = 0; i < n; i++)
            if (ins[i].tab == t && ins[i].opcode == TP_ENC_J)
                x.entry[ne++] = ins[i].is_branch ? tpf_target(r, i) : -1;
        /* d's value before each record: 0 not yet, 1 const val, 2 varying */
        val = tcc_mallocz(sizeof(int) * (n + 1));
        st = tcc_mallocz(sizeof(int) * (n + 1));
        work = tcc_malloc(sizeof(int) * (4 * n + 8));
        for (i = 0; i < n && ins[i].opcode == -1; i++)
            ;
        if (i < n) {
            st[i] = 2;
            work[nw++] = i;
        }
        while (nw) {
            int k = work[--nw], os, ov, s2[64], v2[64], ns = 0, j, e;
            int o = ins[k].opcode, dd = ins[k].operand;
            os = st[k];
            ov = val[k];
            /* transfer */
            if (o == TP_ENC_STL && dd == x.d) {
                int pq = tpf_prev(ins, k);
                if (pq >= 0 && ins[pq].opcode == TP_ENC_LDC &&
                    !ins[pq].pinned) {
                    os = 1;
                    ov = ins[pq].operand;
                } else {
                    os = 2;
                }
            }
            /* successors */
            if (k == g) {
                for (i = 0, e = 0; i < n && ns < 64; i++)
                    if (ins[i].tab == t && ins[i].opcode == TP_ENC_J) {
                        s2[ns] = i;
                        v2[ns++] = e++;     /* entry e: d == e */
                    }
            } else if (o == TP_ENC_J) {
                /* an entry's value was set on the way in, from the gcall */
                if (ins[k].is_branch && (j = tpf_target(r, k)) >= 0) {
                    s2[ns] = j;
                    v2[ns++] = -1;
                }
            } else if (o == TP_ENC_CJ) {
                if (ins[k].is_branch && (j = tpf_target(r, k)) >= 0) {
                    s2[ns] = j;
                    v2[ns++] = -1;
                }
                if (k + 1 < n) {
                    s2[ns] = k + 1;
                    v2[ns++] = -1;
                }
            } else if (o == TP_ENC_OPR && dd == TP_OPR_RET) {
                ;
            } else if (k + 1 < n) {
                s2[ns] = k + 1;
                v2[ns++] = -1;
            }
            for (j = 0; j < ns; j++) {
                int sv = v2[j] >= 0 ? 1 : os, vv = v2[j] >= 0 ? v2[j] : ov;
                int q = s2[j], ns2, nv2;
                if (st[q] == 0) {
                    ns2 = sv;
                    nv2 = vv;
                } else if (st[q] == 1 && sv == 1 && val[q] == vv) {
                    continue;
                } else if (st[q] == 2) {
                    continue;
                } else {
                    ns2 = 2;
                    nv2 = 0;
                }
                st[q] = ns2;
                val[q] = nv2;
                if (nw < 4 * n + 8)
                    work[nw++] = q;
            }
        }
        /* the ways back: a j with d known */
        for (p = 0; p < n && nadd < TPT_MAXN; p++) {
            struct tpt_add a;
            int tg, e, gain = 0, K;
            if (ins[p].opcode != TP_ENC_J || !ins[p].is_branch ||
                ins[p].tab || ins[p].pinned || ins[p].synth ||
                st[p] != 1 || (tg = tpf_target(r, p)) < 0)
                continue;
            K = val[p];
            a.at = p;
            a.n = 0;
            a.ft = 0;
            e = tpt_trace(&x, tg, K, &a, &gain, 0, 0);
            if (!gain || e == -1)
                continue;
            if (e <= -2) {
                int c = -2 - e;
                if (ins[c].synth)
                    continue;
                tpt_rec(&a.rec[a.n], TP_ENC_J, 0);
                a.rec[a.n].is_branch = 1;
                a.rec[a.n].target_old = ins[c].old_off;
                a.n++;
            } else {
                if (ins[e].synth)
                    continue;
                tpt_rec(&a.rec[a.n], TP_ENC_J, 0);
                a.rec[a.n].is_branch = 1;
                a.rec[a.n].target_old = ins[e].old_off;
                a.n++;
            }
            if (!add)
                add = tcc_malloc(sizeof(*add) * TPT_MAXN);
            add[nadd++] = a;
        }
        tcc_free(val);
        tcc_free(st);
        tcc_free(work);
        tcc_free(x.entry);
    }
    if (!nadd)
        return 0;
    if (tp_knob(TPK_TP_NARROW_STATS))
        fprintf(stderr, "[SWTHREAD] %s: %d ways back threaded\n",
                funcname ? funcname : "?", nadd);
    tpt_insert(r, add, nadd);
    tcc_free(add);
    return 1;
}

static int tp_tail_dup(struct tp_relax *r)
{
    static int off = -1;
    struct tp_relax_insn *ins = r->ins;
    const int n = r->n;
    char *head;
    int *npred, i, k, nins = 0;
    struct tpf_add add[64];

    if (off < 0) {
        const char *e = tp_knob(TPK_TCC_TRANSPUTER_TAIL_DUP);
        off = (e && *e == '0') ? 1 : 0;
    }
    if (off || func_uses_inline_asm || n <= 0)
        return 0;
    head = tcc_mallocz(n + 1);
    npred = tcc_mallocz(sizeof(int) * (n + 1));
    for (i = 0; i < n; i++) {
        if (ins[i].tab > 0)
            head[i] = 1;
        if (ins[i].is_branch && (k = tpf_target(r, i)) >= 0) {
            head[k] = 1;
            npred[k]++;
        }
    }
    for (i = 0; i < n && ins[i].opcode == -1; i++)
        ;
    if (i < n)
        head[i] = 1;
    for (i = 0; i < n && nins < 64; i++) {
        int L, q, e, len = 0, term = -1, F = -1, ok = 1, pv;
        if (ins[i].opcode != TP_ENC_J || !ins[i].is_branch ||
            ins[i].pinned || ins[i].tab || ins[i].icall ||
            (L = tpf_target(r, i)) < 0 || L == i || ins[L].pinned)
            continue;
        /* the block from L */
        for (q = L, e = -1; q < n; q = tpn_next(ins, n, q)) {
            int o = ins[q].opcode, d = ins[q].operand;
            if (q != L && head[q]) {
                F = q;              /* falls into another block */
                break;
            }
            if (o == TP_ENC_J) {
                if (!ins[q].is_branch || ins[q].pinned || ins[q].tab ||
                    ins[q].ext)
                    ok = 0;
                term = q;
                break;
            }
            if (ins[q].pinned || ins[q].tab || ins[q].icall ||
                ins[q].is_branch || o == TP_ENC_CJ || o == TP_ENC_CALL ||
                o == TP_ENC_AJW || ins[q].workspace_extent ||
                (o == TP_ENC_OPR && (d == TP_OPR_RET || d == TP_OPR_GCALL ||
                                     d == TP_OPR_GAJW))) {
                ok = 0;
                break;
            }
            if (++len > TPT_MAXB) {
                ok = 0;
                break;
            }
            e = q;
        }
        if (!ok || e < 0 || (term < 0 && F < 0) || (i >= L && i <= e))
            continue;
        /* a terminating jump already given a copy of its own would be
         * followed past that copy */
        if (term >= 0 && (tpf_target(r, term) < 0 || term == i ||
                          npred[term] < 0))
            continue;
        if (term < 0) {
            /* only where a store meets its reload */
            pv = tpf_prev(ins, i);
            if (pv < 0 || ins[pv].opcode != TP_ENC_STL ||
                ins[L].opcode != TP_ENC_LDL ||
                ins[L].operand != ins[pv].operand || ins[F].synth)
                continue;
        }
        add[nins].at = i;
        add[nins].s0 = L;
        add[nins].s1 = e;
        add[nins].tgt = -1;
        add[nins].rep = 1;
        ins[i].target_old = term >= 0 ? ins[term].target_old
                                      : ins[F].old_off;
        npred[i] = -1;              /* done */
        nins++;
    }
    if (nins)
        tpf_insert(r, add, nins);
    if (tp_knob(TPK_TP_NARROW_STATS) && nins)
        fprintf(stderr, "[TAILDUP] %s: %d copies\n",
                funcname ? funcname : "?", nins);
    tcc_free(head);
    tcc_free(npred);
    return nins > 0;
}

/* Single instructions to insert: before ins[at], opc/opd, a branch to the
 * old offset tgt when tgt >= 0. */
struct tpx_one {
    int at, opc, opd, tgt;
};

static void tpx_insert(struct tp_relax *r, struct tpx_one *v, int nv)
{
    struct tp_relax_insn *ins = r->ins, *out;
    const int n = r->n;
    int i, k, s;

    for (i = 1; i < nv; i++)            /* by position, stable */
        for (k = i; k > 0 && v[k].at < v[k - 1].at; k--) {
            struct tpx_one t = v[k];
            v[k] = v[k - 1];
            v[k - 1] = t;
        }
    out = tcc_malloc(sizeof(*out) * (n + nv + 1));
    for (i = 0, k = 0, s = 0; i < n; i++) {
        for (; k < nv && v[k].at == i; k++) {
            memset(&out[s], 0, sizeof(out[s]));
            out[s].old_off = ins[i].old_off;
            out[s].synth = 1;
            tpn_put(&out[s], v[k].opc, v[k].opd);
            if (v[k].tgt >= 0) {
                out[s].is_branch = 1;
                out[s].target_old = v[k].tgt;
                out[s].new_len = 1;
            }
            s++;
        }
        out[s++] = ins[i];
    }
    tcc_free(ins);
    r->ins = out;
    r->n = s;
    for (i = 0; i <= r->size; i++)
        r->tidx[i] = -1;
    for (i = 0; i < r->n; i++)
        if (!out[i].synth)
            r->tidx[out[i].old_off] = i;
}

/* A loop tested at the bottom on `n > j`, entered by a jump to the test:
 *
 *   j T; B: body; T: ldl n; <j>; gt|gtu; eqc 0; cj B; E:
 *
 * The way in gets a test of its own, `ldl n; <j>; gt|gtu; cj E; j B`, and
 * the test at T, now reached only from the body, becomes
 * `<j>; ldl n; ldc 1; diff; gt|gtu; cj B` - j > n - 1 - whose n - 1 the
 * hoisting then computes once, before the loop: one instruction less a
 * time round.  n - 1 cannot wrap, as the body only runs when n > j.  n is
 * a word the loop does not write and no address reaches.  After the
 * scheduler, before the hoisting.  TCC_TRANSPUTER_LOOP_INVERT=0 turns it
 * off. */
static int tp_loop_invert(struct tp_relax *r, int top)
{
    static int off = -1;
    struct tp_relax_insn *ins = r->ins;
    const int n = r->n;
    const int F = func_frame_words;
    char *head, *bad;
    int *npred, i, k, nv = 0, cap = 0;
    struct tpx_one *v = NULL;

    if (off < 0) {
        const char *e = tp_knob(TPK_TCC_TRANSPUTER_LOOP_INVERT);
        off = (e && *e == '0') ? 1 : 0;
    }
    if (off || func_uses_inline_asm || n <= 0 || F > 4096)
        return 0;
    for (i = 0, k = -1; i < n; i++) {
        if (ins[i].opcode == -1)
            continue;
        if (k < 0)
            k = i;
        if (ins[i].opcode == TP_ENC_OPR && ins[i].operand == TP_OPR_GAJW)
            return 0;
        if (ins[i].opcode == TP_ENC_AJW && i != k && !ins[i].icall) {
            int j = tpn_next(ins, n, i);
            if (j >= n || ins[j].opcode != TP_ENC_OPR ||
                ins[j].operand != TP_OPR_RET)
                return 0;
        }
    }
    head = tcc_mallocz(n + 1);
    npred = tcc_mallocz(sizeof(int) * (n + 1));
    bad = tcc_mallocz(F + 2);
    for (i = 0; i < n; i++) {
        if (ins[i].tab > 0)
            head[i] = 1;
        if (ins[i].is_branch && (k = tpf_target(r, i)) >= 0) {
            head[k] = 1;
            npred[k]++;
        }
        if (ins[i].opcode == TP_ENC_LDLP) {
            int j = tpn_next(ins, n, i), lo, ext, s;
            ext = j < n ? tpg_ldlp_extent(&ins[j], &lo) : -1;
            if (ext < 0) {
                lo = -1;
                ext = 3;
            }
            for (s = ins[i].operand + lo; s < ins[i].operand + lo + ext; s++)
                if (s >= 0 && s < F)
                    bad[s] = 1;
        }
    }
    for (i = 0; i < n; i++) {
        int c = i, r0, r1, r2, r3, B, E, pj, N, q, calls = 0, ok = 1;
        if (ins[c].opcode != TP_ENC_CJ || !ins[c].is_branch ||
            ins[c].pinned || ins[c].synth || (B = tpf_target(r, c)) < 0 ||
            B > c)
            continue;
        r3 = tpf_prev(ins, c);
        r2 = r3 >= 0 ? tpf_prev(ins, r3) : -1;
        r1 = r2 >= 0 ? tpf_prev(ins, r2) : -1;
        r0 = r1 >= 0 ? tpf_prev(ins, r1) : -1;
        if (r0 < B || ins[r3].opcode != TP_ENC_EQC || ins[r3].operand ||
            ins[r2].opcode != TP_ENC_OPR ||
            (ins[r2].operand != TP_OPR_GT && ins[r2].operand != TP_OPR_GTU) ||
            (ins[r1].opcode != TP_ENC_LDL && ins[r1].opcode != TP_ENC_LDC) ||
            ins[r0].opcode != TP_ENC_LDL)
            continue;
        if (head[r1] || head[r2] || head[r3] || head[c] || !head[r0] ||
            npred[r0] != 1 || npred[B] != 1)
            continue;
        for (q = r0; q <= c; q++)
            if (ins[q].pinned || ins[q].synth || ins[q].dup_load)
                ok = 0;
        /* the way in: the J right before B, to T */
        pj = tpf_prev(ins, B);
        if (!ok || pj < 0 || ins[pj].opcode != TP_ENC_J ||
            !ins[pj].is_branch || ins[pj].pinned || ins[pj].synth ||
            head[pj] || tpf_target(r, pj) != r0)
            continue;
        E = tpn_next(ins, n, c);
        if (E >= n || ins[E].synth || ins[E].pinned)
            continue;
        /* nothing else enters the loop; n unwritten in it */
        N = ins[r0].operand;
        if (N < 0 || (N < F && bad[N]) ||
            tps_word_escapes(ins, n, N, top))
            continue;
        for (q = 0; q < n && ok; q++) {
            int t;
            if (q >= B && q <= c) {
                int o = ins[q].opcode;
                if ((o == TP_ENC_STL || o == TP_ENC_LDLP) &&
                    ins[q].operand == N)
                    ok = 0;
                if (o == TP_ENC_CALL || ins[q].icall ||
                    (o == TP_ENC_OPR && ins[q].operand == TP_OPR_GCALL))
                    calls = 1;
                continue;
            }
            if (ins[q].is_branch && q != pj && (t = tpf_target(r, q)) >= 0 &&
                t > B && t <= c)
                ok = 0;
        }
        if (!ok || (calls && N < func_max_outgoing_words))
            continue;
        if (nv + 5 > cap) {
            cap = cap ? 2 * cap : 32;
            v = tcc_realloc(v, sizeof(*v) * cap);
        }
        /* the way in: ldl n; <j>; op; cj E; then the J, now to B */
        v[nv].at = pj; v[nv].opc = TP_ENC_LDL; v[nv].opd = N;
        v[nv++].tgt = -1;
        v[nv].at = pj; v[nv].opc = ins[r1].opcode; v[nv].opd = ins[r1].operand;
        v[nv++].tgt = -1;
        v[nv].at = pj; v[nv].opc = TP_ENC_OPR; v[nv].opd = ins[r2].operand;
        v[nv++].tgt = -1;
        v[nv].at = pj; v[nv].opc = TP_ENC_CJ; v[nv].opd = 0;
        v[nv++].tgt = ins[E].old_off;
        ins[pj].target_old = ins[B].old_off;
        /* the bottom: <j>; ldl n; ldc 1; diff; op; cj B */
        v[nv].at = c; v[nv].opc = TP_ENC_OPR; v[nv].opd = ins[r2].operand;
        v[nv++].tgt = -1;
        tpn_put(&ins[r0], ins[r1].opcode, ins[r1].operand);
        tpn_put(&ins[r1], TP_ENC_LDL, N);
        tpn_put(&ins[r2], TP_ENC_LDC, 1);
        tpn_put(&ins[r3], TP_ENC_OPR, TP_OPR_DIFF);
    }
    tcc_free(head);
    tcc_free(npred);
    tcc_free(bad);
    if (!nv)
        return 0;
    tpx_insert(r, v, nv);
    tcc_free(v);
    if (tp_knob(TPK_TP_NARROW_STATS))
        fprintf(stderr, "[LOOPINV] %s: %d loops\n",
                funcname ? funcname : "?", nv / 5);
    return 1;
}

/* A narrow loop counter's mask, where the loop's own test bounds it:
 *
 *   j T; X: ...; ldl s; adc c; ldc m; and; stl s; ...; T: ldl s; ldc B; gt;
 *   cj X
 *
 * The body [X, T) is entered only by the cj, when s <= B, and stores s
 * only there; every store to s anywhere is a constant >= 0 or such an
 * increment, so s >= 0; s + c <= B + c <= m makes the `ldc m; and` a
 * no-op.  crcu8's `unsigned char i`.  Under TCC_TRANSPUTER_RANGE_PEEP. */
static int tp_induction_mask(struct tp_relax *r)
{
    struct tp_relax_insn *ins = r->ins;
    const int n = r->n;
    const int F = func_frame_words;
    int *npred, i, k, any = 0;
    char *head;

    if (!tpn_peep_on || func_uses_inline_asm || n <= 0 || F <= 0)
        return 0;
    head = tcc_mallocz(n + 1);
    npred = tcc_mallocz(sizeof(int) * (n + 1));
    for (i = 0; i < n; i++) {
        if (ins[i].tab > 0)
            head[i] = 1;
        if (ins[i].is_branch && (k = tpf_target(r, i)) >= 0) {
            head[k] = 1;
            npred[k]++;
        }
    }
    for (i = 0; i < n; i++) {
        int c = i, t0, t1, t2, X, s, B, q, ok = 1, inc = -1, px;
        if (ins[c].opcode != TP_ENC_CJ || !ins[c].is_branch || ins[c].pinned ||
            (X = tpf_target(r, c)) < 0 || X >= c)
            continue;
        t2 = tpf_prev(ins, c);
        t1 = t2 >= 0 ? tpf_prev(ins, t2) : -1;
        t0 = t1 >= 0 ? tpf_prev(ins, t1) : -1;
        if (t0 < X || !tpn_is(&ins[t2], TP_ENC_OPR, TP_OPR_GT) ||
            ins[t1].opcode != TP_ENC_LDC || ins[t0].opcode != TP_ENC_LDL ||
            ins[t0].pinned || ins[t1].pinned || head[t1] || head[t2] ||
            head[c] || npred[X] != 1)
            continue;
        s = ins[t0].operand;
        B = ins[t1].operand;
        if (s < 0 || s >= F || s < func_max_outgoing_words ||
            tps_word_escapes(ins, n, s, 0) || B < 0)
            continue;
        px = tpf_prev(ins, X);
        if (px < 0 || ins[px].opcode != TP_ENC_J)
            continue;                   /* X is reached only by the cj */
        /* nothing from outside [X, c] lands inside (X, t0) */
        for (q = 0; q < n && ok; q++) {
            int t;
            if (q >= X && q <= c)
                continue;
            if (ins[q].is_branch && (t = tpf_target(r, q)) > X && t < t0)
                ok = 0;
        }
        /* every store to s: a constant >= 0, or the masked increment;
         * in [X, t0) exactly one, the increment; no LDLP of s anywhere */
        for (q = 0; q < n && ok; q++) {
            int o = ins[q].opcode;
            if (o == TP_ENC_LDLP && ins[q].operand <= s &&
                ins[q].operand + 3 >= s)
                ok = 0;
            if (o != TP_ENC_STL || ins[q].operand != s)
                continue;
            {
                int a = tpf_prev(ins, q), b = a >= 0 ? tpf_prev(ins, a) : -1;
                int cc = b >= 0 ? tpf_prev(ins, b) : -1;
                int dd = cc >= 0 ? tpf_prev(ins, cc) : -1;
                if (a >= 0 && ins[a].opcode == TP_ENC_LDC &&
                    ins[a].operand >= 0 && !(q >= X && q < t0))
                    continue;           /* a constant, outside the body */
                if (dd >= 0 && tpn_is(&ins[a], TP_ENC_OPR, TP_OPR_AND) &&
                    ins[b].opcode == TP_ENC_LDC && ins[b].operand > 0 &&
                    !(ins[b].operand & (ins[b].operand + 1)) &&
                    ins[cc].opcode == TP_ENC_ADC && ins[cc].operand > 0 &&
                    tpn_is_ins(&ins[dd], TP_ENC_LDL, s) &&
                    !head[cc] && !head[b] && !head[a] && !head[q]) {
                    if (q >= X && q < t0) {
                        if (inc >= 0)
                            ok = 0;
                        inc = q;
                        if ((long long)B + ins[cc].operand > ins[b].operand)
                            ok = 0;
                    }
                    continue;
                }
                ok = 0;
            }
        }
        /* no inner loop runs the increment again without the test */
        for (q = X; q < c && ok && inc >= 0; q++) {
            int t;
            if (ins[q].is_branch && (t = tpf_target(r, q)) >= X && t <= q &&
                t <= inc && inc <= q)
                ok = 0;
        }
        if (!ok || inc < 0)
            continue;
        {
            int a = tpf_prev(ins, inc), b = tpf_prev(ins, a);
            tpn_put(&ins[a], -1, 0);
            tpn_put(&ins[b], -1, 0);
            any = 1;
        }
    }
    tcc_free(head);
    tcc_free(npred);
    return any;
}

/*
 * Strength reduction: an address a loop recomputes from its counter, kept
 * in a word of its own and stepped instead.
 *
 * The loop is the shape tp_loop_invert leaves: entered only by a `j B`
 * right before it, left at the bottom by `cj B`, and its counter k written
 * once, `ldl k; ldc c; bsub; stl k` or `ldl k; adc c; stl k`, in the block
 * that ends with that cj (so once each time round).  Before the update, a
 * straight run of pure arithmetic over k and words the loop does not
 * write, ending in bsub/ssub/wsub (an address), is affine in k: its
 * coefficient is a constant, or a constant times one invariant word N.
 * Such a run becomes `ldl p`; p is the run itself computed at the loop's
 * entry (where k has its first value), and after the update
 * `ldl p; adc a*c; stl p` or `ldl p; ldl S; bsub; stl p` with S = N*a*c
 * computed at the entry too.  matrix_mul_matrix: A[i*N+k] and B[k*N+j], 13
 * instructions a time round, become 2 loads and 7 for the steps.  New
 * words go on top of the frame, as the hoisting's do.  After tp_hoist.
 * TCC_TRANSPUTER_STRENGTH=0 turns it off.
 */
#define TPR_MAXRUN 12
static int tp_hoist_w;
static int tph_may_store(const struct tp_relax_insn *c);
#define TPR_MAXW 8

struct tpr_coef {
    int kind;                   /* 0 none, 1 constant, 2 constant x slot */
    long long k;
    int slot;
};

struct tpr_val {
    struct tpr_coef c;
    int single;                 /* the lone push: LDL slot or LDC */
    int s_opc, s_opd;
};

static int tpr_add(struct tpr_coef *r, struct tpr_coef a, long long ma,
                   struct tpr_coef b, long long mb)
{
    if (a.kind == 0 || ma == 0) {
        r->kind = b.kind; r->k = b.k * mb; r->slot = b.slot;
    } else if (b.kind == 0 || mb == 0) {
        r->kind = a.kind; r->k = a.k * ma; r->slot = a.slot;
    } else if (a.kind == b.kind && (a.kind == 1 || a.slot == b.slot)) {
        r->kind = a.kind; r->k = a.k * ma + b.k * mb; r->slot = a.slot;
    } else {
        return 0;
    }
    if (r->k == 0)
        r->kind = 0;
    return r->k > -0x40000000LL && r->k < 0x40000000LL;
}

/* The run from s: its last record (an address op at depth 1) and its
 * coefficient, or -1. */
static int tpr_run(const struct tp_relax_insn *ins, int n, const char *head,
                   int s, int lim, int kslot, const char *inv, int ninv,
                   struct tpr_coef *out, int *len)
{
    struct tpr_val st[3];
    int d = 0, q, best = -1, m = 0, sawk = 0;

    memset(st, 0, sizeof st);
    for (q = s; q < lim; q = tpn_next(ins, n, q)) {
        const struct tp_relax_insn *c = &ins[q];
        struct tpr_val a, b, v;
        if ((q != s && head[q]) || c->pinned || c->synth || c->tab ||
            c->dup_load || ++m > TPR_MAXRUN)
            break;
        memset(&v, 0, sizeof v);
        if (c->opcode == TP_ENC_LDL || c->opcode == TP_ENC_LDC) {
            if (d == 3)
                break;
            if (c->opcode == TP_ENC_LDL && c->operand == kslot) {
                v.c.kind = 1; v.c.k = 1;
                sawk = 1;
            } else if (c->opcode == TP_ENC_LDL &&
                       (c->operand < 0 || c->operand >= ninv ||
                        !inv[c->operand])) {
                break;
            }
            v.single = 1;
            v.s_opc = c->opcode;
            v.s_opd = c->operand;
            st[2] = st[1]; st[1] = st[0]; st[0] = v;
            d++;
            continue;
        }
        if (c->opcode == TP_ENC_ADC) {
            if (d < 1)
                break;
            st[0].single = 0;
            continue;
        }
        if (c->opcode != TP_ENC_OPR || d < 2)
            break;
        a = st[0];                  /* A */
        b = st[1];                  /* B */
        switch (c->operand) {
        case TP_OPR_BSUB: case TP_OPR_SUM:
            if (!tpr_add(&v.c, a.c, 1, b.c, 1)) return best;
            break;
        case TP_OPR_SSUB:
            if (!tpr_add(&v.c, a.c, 1, b.c, 2)) return best;
            break;
        case TP_OPR_WSUB:
            if (!tpr_add(&v.c, a.c, 1, b.c, 4)) return best;
            break;
        case TP_OPR_DIFF:           /* B - A */
            if (!tpr_add(&v.c, b.c, 1, a.c, -1)) return best;
            break;
        case TP_OPR_PROD: {
            struct tpr_val *x = 0, *y = 0;
            if (a.c.kind == 0 && a.single) { x = &a; y = &b; }
            else if (b.c.kind == 0 && b.single) { x = &b; y = &a; }
            if (!x)
                return best;
            if (y->c.kind == 0) {
                v.c.kind = 0;
            } else if (x->s_opc == TP_ENC_LDC) {
                v.c = y->c;
                v.c.k *= x->s_opd;
                if (v.c.k == 0 || v.c.k <= -0x40000000LL ||
                    v.c.k >= 0x40000000LL)
                    return best;
            } else if (y->c.kind == 1) {
                v.c.kind = 2; v.c.k = y->c.k; v.c.slot = x->s_opd;
            } else {
                return best;
            }
            break;
        }
        default:
            return best;
        }
        st[0] = v;
        st[1] = st[2];
        memset(&st[2], 0, sizeof st[2]);
        d--;
        if (d == 1 && sawk && v.c.kind &&
            (c->operand == TP_OPR_BSUB || c->operand == TP_OPR_SSUB ||
             c->operand == TP_OPR_WSUB)) {
            best = q;
            *out = v.c;
            *len = m;
        }
    }
    return best;
}

static int tp_strength_reduce(struct tp_relax *r, int top)
{
    static int off = -1;
    struct tp_relax_insn *ins = r->ins;
    const int n = r->n;
    const int F = func_frame_words;
    struct tpg_work g;
    struct {
        int s, e, word, sword, at, after, cslot;
        long long step;
        int ltr, pe, t0, t1, t2, ust, u, k;  /* the loop's test on it */
    } run[TPR_MAXW];
    int nrun = 0, w = 0, i, k, nv = 0, cap = 0;
    char *head, *inv;
    int *npred;
    struct tpx_one *v = NULL;

    if (off < 0) {
        const char *e = tp_knob(TPK_TCC_TRANSPUTER_STRENGTH);
        off = (e && *e == '0') ? 1 : 0;
    }
    if (off || func_uses_inline_asm || F <= 0 || F > 200 || n <= 0)
        return 0;
    memset(&g, 0, sizeof g);
    g.ins = ins;
    g.n = n;
    g.tidx = r->tidx;
    g.size = r->size;
    g.F = F;
    if (!tpg_frame_ok(&g))
        return 0;
    head = tcc_mallocz(n + 1);
    npred = tcc_mallocz(sizeof(int) * (n + 1));
    inv = tcc_mallocz(F + 256);
    for (i = 0; i < n; i++) {
        if (ins[i].tab > 0)
            head[i] = 1;
        if (ins[i].is_branch && (k = tpf_target(r, i)) >= 0) {
            head[k] = 1;
            npred[k]++;
        }
    }
    for (i = 0; i < n && w + 2 <= TPR_MAXW; i++) {
        int c = i, B, pj, u, kslot, q, cc = 0, ok = 1, calls = 0, ptr = 0;
        int ustart, x, first;
        if (ins[c].opcode != TP_ENC_CJ || !ins[c].is_branch ||
            ins[c].pinned || (B = tpf_target(r, c)) < 0 || B >= c)
            continue;
        /* ways into B: the cj and the entry J right before it */
        pj = tpf_prev(ins, B);
        if (pj < 0 || ins[pj].opcode != TP_ENC_J || !ins[pj].is_branch ||
            ins[pj].pinned || head[pj] || tpf_target(r, pj) != B ||
            npred[B] != 2)
            continue;
        /* nothing from outside enters (B, c] */
        for (q = 0; q < n && ok; q++) {
            int t;
            if (q >= B && q <= c)
                continue;
            if (ins[q].is_branch && q != pj && (t = tpf_target(r, q)) >= B &&
                t <= c)
                ok = 0;
        }
        if (!ok)
            continue;
        /* the counter: the last STL before c, in c's block */
        for (u = tpf_prev(ins, c); u > B && ins[u].opcode != TP_ENC_STL;
             u = tpf_prev(ins, u))
            if (head[u] || ins[u].is_branch)
                break;
        if (u <= B || ins[u].opcode != TP_ENC_STL || head[u] || ins[u].pinned)
            continue;
        for (q = u + 1; q < c; q++)
            if (head[q] || ins[q].is_branch)
                break;
        if (q < c)
            continue;
        kslot = ins[u].operand;
        {
            int a1 = tpf_prev(ins, u), a2 = a1 >= 0 ? tpf_prev(ins, a1) : -1;
            int a3 = a2 >= 0 ? tpf_prev(ins, a2) : -1;
            if (a1 >= 0 && ins[a1].opcode == TP_ENC_ADC && a2 >= 0 &&
                tpn_is_ins(&ins[a2], TP_ENC_LDL, kslot) && ins[a1].operand > 0) {
                cc = ins[a1].operand;
                ustart = a2;
            } else if (a3 >= 0 && tpn_is(&ins[a1], TP_ENC_OPR, TP_OPR_BSUB) &&
                       ins[a2].opcode == TP_ENC_LDC && ins[a2].operand > 0 &&
                       tpn_is_ins(&ins[a3], TP_ENC_LDL, kslot)) {
                cc = ins[a2].operand;
                ustart = a3;
            } else {
                continue;
            }
            if (head[ustart] || ustart <= B)
                continue;
        }
        if (kslot < 0 || kslot >= F + 254)
            continue;
        /* what the loop writes */
        memset(inv, 1, F + 256);
        for (q = B; q <= c; q++) {
            int o = ins[q].opcode, d = ins[q].operand;
            if ((o == TP_ENC_STL || o == TP_ENC_LDLP) && d >= 0 &&
                d < F + 254) {
                inv[d] = 0;
                if (o == TP_ENC_LDLP)
                    inv[d + 1] = 0;
            }
            if (o == TP_ENC_STL && d == kslot && q != u)
                ok = 0;             /* k written twice */
            if (o == TP_ENC_CALL || ins[q].icall ||
                (o == TP_ENC_OPR && d == TP_OPR_GCALL))
                calls = 1;
            if (tph_may_store(&ins[q]))
                ptr = 1;
        }
        if (!ok || tps_word_escapes(ins, n, kslot, top))
            continue;
        for (x = 0; x < F + 254; x++)
            if (inv[x] && (ptr || calls) &&
                ((calls && x < func_max_outgoing_words) ||
                 tps_word_escapes(ins, n, x, top)))
                inv[x] = 0;
        /* the runs */
        first = nrun;
        for (q = B; q < ustart && w + 2 <= TPR_MAXW && nrun < TPR_MAXW; ) {
            struct tpr_coef co;
            int len, e, cost;
            if (ins[q].opcode != TP_ENC_LDL && ins[q].opcode != TP_ENC_LDC) {
                q = tpn_next(ins, n, q);
                continue;
            }
            e = tpr_run(ins, n, head, q, ustart, kslot, inv, F + 254, &co,
                        &len);
            if (e < 0) {
                q = tpn_next(ins, n, q);
                continue;
            }
            cost = co.kind == 1 ? 3 : 4;
            if (len - 1 - cost < 1) {
                q = tpn_next(ins, n, q);
                continue;
            }
            run[nrun].s = q;
            run[nrun].e = e;
            run[nrun].at = pj;
            run[nrun].after = tpn_next(ins, n, u);
            run[nrun].step = co.k * cc;
            run[nrun].cslot = co.kind == 2 ? co.slot : -1;
            run[nrun].word = w++;
            run[nrun].sword = co.kind == 2 ? w++ : -1;
            run[nrun].ltr = 0;
            if (run[nrun].step <= -0x40000000LL ||
                run[nrun].step >= 0x40000000LL) {
                w -= co.kind == 2 ? 2 : 1;
                q = tpn_next(ins, n, q);
                continue;
            }
            nrun++;
            q = tpn_next(ins, n, e);
        }
        /* the test on k against an invariant H, `ldl k; ldl H; gt(u); cj`,
         * on a run stepped by a positive constant instead: p > p(H), with
         * p(H) the run at k = H, computed at the entry.  An address does
         * not wrap within its object, so the signed gt keeps the order.
         * Then k's update goes, when the loop reads k nowhere else and
         * nothing reads it after. */
        if (nrun > first && w < TPR_MAXW) {
            int t2 = tpf_prev(ins, c), t1 = t2 >= 0 ? tpf_prev(ins, t2) : -1;
            int t0 = t1 >= 0 ? tpf_prev(ins, t1) : -1, R = -1, ok2 = 1, j2;
            for (j2 = first; j2 < nrun; j2++)
                if (run[j2].cslot < 0 && run[j2].step > 0) {
                    R = j2;
                    break;
                }
            if (R < 0 || t0 < 0 || tpn_next(ins, n, u) != t0 ||
                !tpn_is_ins(&ins[t0], TP_ENC_LDL, kslot) ||
                ins[t1].opcode != TP_ENC_LDL || ins[t1].pinned ||
                ins[t1].operand == kslot || ins[t1].operand < 0 ||
                ins[t1].operand >= F + 254 || !inv[ins[t1].operand] ||
                !(tpn_is(&ins[t2], TP_ENC_OPR, TP_OPR_GTU) ||
                  tpn_is(&ins[t2], TP_ENC_OPR, TP_OPR_GT)))
                ok2 = 0;
            /* k read only by its update, the test and the runs */
            for (q = B; q <= c && ok2; q++) {
                int inrun = 0;
                if (ins[q].opcode != TP_ENC_LDL || ins[q].operand != kslot ||
                    q == ustart || q == t0)
                    continue;
                for (j2 = first; j2 < nrun; j2++)
                    if (q >= run[j2].s && q <= run[j2].e)
                        inrun = 1;
                if (!inrun)
                    ok2 = 0;
            }
            /* and dead wherever the loop is left */
            if (ok2) {
                char *seen = tcc_mallocz(n + 1);
                int ex = tpn_next(ins, n, c);
                if (ex >= n || tpf_live(r, ex, kslot, seen))
                    ok2 = 0;
                for (q = B; q <= c && ok2; q++) {
                    int t;
                    if (ins[q].is_branch && (t = tpf_target(r, q)) >= 0 &&
                        (t < B || t > c) && tpf_live(r, t, kslot, seen))
                        ok2 = 0;
                }
                tcc_free(seen);
            }
            if (ok2) {
                run[R].ltr = 1;
                run[R].pe = w++;
                run[R].t0 = t0;
                run[R].t1 = t1;
                run[R].t2 = t2;
                run[R].ust = ustart;
                run[R].u = u;
                run[R].k = kslot;
            }
        }
    }
    tcc_free(head);
    tcc_free(npred);
    tcc_free(inv);
    if (!nrun)
        return 0;

    /* room on top of the frame (shifts what is above it, AJWs) */
    g.w = w;
    tpg_room(&g);
    for (i = 0; i < nrun; i++) {
        int p = F + run[i].word, sw = F + run[i].sword, q;
        if (nv + TPR_MAXRUN + 12 > cap) {
            cap = cap ? 2 * cap + TPR_MAXRUN + 12 : 64;
            v = tcc_realloc(v, sizeof(*v) * cap);
        }
        /* at the entry: the run, stl p; [ldl N; ldc a*c; prod; stl S] */
        for (q = run[i].s; q <= run[i].e; q = tpn_next(ins, n, q)) {
            v[nv].at = run[i].at; v[nv].opc = ins[q].opcode;
            v[nv].opd = ins[q].operand; v[nv++].tgt = -1;
        }
        v[nv].at = run[i].at; v[nv].opc = TP_ENC_STL; v[nv].opd = p;
        v[nv++].tgt = -1;
        if (run[i].ltr) {
            int H = ins[run[i].t1].operand, pe = F + run[i].pe;
            if (nv + TPR_MAXRUN + 12 > cap) {
                cap = 2 * cap + TPR_MAXRUN + 12;
                v = tcc_realloc(v, sizeof(*v) * cap);
            }
            for (q = run[i].s; q <= run[i].e; q = tpn_next(ins, n, q)) {
                int o = ins[q].opcode, d = ins[q].operand;
                if (o == TP_ENC_LDL && d == run[i].k)
                    d = H;
                v[nv].at = run[i].at; v[nv].opc = o;
                v[nv].opd = d; v[nv++].tgt = -1;
            }
            v[nv].at = run[i].at; v[nv].opc = TP_ENC_STL; v[nv].opd = pe;
            v[nv++].tgt = -1;
            tpn_put(&ins[run[i].t0], TP_ENC_LDL, p);
            tpn_put(&ins[run[i].t1], TP_ENC_LDL, pe);
            tpn_put(&ins[run[i].t2], TP_ENC_OPR, TP_OPR_GT);
            for (q = run[i].ust; q <= run[i].u; q = tpn_next(ins, n, q))
                tpn_put(&ins[q], -1, 0);
        }
        if (run[i].sword >= 0) {
            int cs = run[i].cslot >= F ? run[i].cslot + w : run[i].cslot;
            v[nv].at = run[i].at; v[nv].opc = TP_ENC_LDL; v[nv].opd = cs;
            v[nv++].tgt = -1;
            v[nv].at = run[i].at; v[nv].opc = TP_ENC_LDC;
            v[nv].opd = (int)run[i].step; v[nv++].tgt = -1;
            v[nv].at = run[i].at; v[nv].opc = TP_ENC_OPR;
            v[nv].opd = TP_OPR_PROD; v[nv++].tgt = -1;
            v[nv].at = run[i].at; v[nv].opc = TP_ENC_STL; v[nv].opd = sw;
            v[nv++].tgt = -1;
        }
        /* after k's update: the step */
        v[nv].at = run[i].after; v[nv].opc = TP_ENC_LDL; v[nv].opd = p;
        v[nv++].tgt = -1;
        if (run[i].sword >= 0) {
            v[nv].at = run[i].after; v[nv].opc = TP_ENC_LDL; v[nv].opd = sw;
            v[nv++].tgt = -1;
            v[nv].at = run[i].after; v[nv].opc = TP_ENC_OPR;
            v[nv].opd = TP_OPR_BSUB; v[nv++].tgt = -1;
        } else {
            v[nv].at = run[i].after; v[nv].opc = TP_ENC_ADC;
            v[nv].opd = (int)run[i].step; v[nv++].tgt = -1;
        }
        v[nv].at = run[i].after; v[nv].opc = TP_ENC_STL; v[nv].opd = p;
        v[nv++].tgt = -1;
        /* in the loop: the run is the word */
        tpn_put(&ins[run[i].s], TP_ENC_LDL, p);
        for (q = tpn_next(ins, n, run[i].s); q <= run[i].e;
             q = tpn_next(ins, n, q))
            tpn_put(&ins[q], -1, 0);
    }
    func_frame_words = F + w;
    tp_hoist_w += w;
    tpx_insert(r, v, nv);
    tcc_free(v);
    if (tp_knob(TPK_TP_NARROW_STATS))
        fprintf(stderr, "[STRENGTH] %s: %d runs, %d words\n",
                funcname ? funcname : "?", nrun, w);
    return 1;
}

/* A value the loop's bottom test computes and its top computes again:
 *
 *   X: R; stl t; ...      ...; R; cj E1; ...; cj X; E2:
 *
 * with X reached only by that last cj, R a short pure run (loads too,
 * when the function names nothing volatile) and nothing between the two
 * runs storing anything.  The test keeps its value instead,
 * `R; stl t; ldl t; cj E1`, and the top's `R; stl t` goes, when t is
 * dead wherever the test leaves the loop.  core_state_transition's
 * `*str`.  TCC_TRANSPUTER_LOOP_CSE=0 turns it off. */
static int tp_loop_cse(struct tp_relax *r)
{
    static int off = -1;
    struct tp_relax_insn *ins = r->ins;
    const int n = r->n;
    char *head, *seen;
    int *npred, i, k, nv = 0;
    struct tpx_one v[32];

    if (off < 0) {
        const char *e = tp_knob(TPK_TCC_TRANSPUTER_LOOP_CSE);
        off = (e && *e == '0') ? 1 : 0;
    }
    if (off || func_uses_inline_asm || n <= 0)
        return 0;
    head = tcc_mallocz(n + 1);
    seen = tcc_mallocz(n + 1);
    npred = tcc_mallocz(sizeof(int) * (n + 1));
    for (i = 0; i < n; i++) {
        if (ins[i].tab > 0)
            head[i] = 1;
        if (ins[i].is_branch && (k = tpf_target(r, i)) >= 0) {
            head[k] = 1;
            npred[k]++;
        }
    }
    for (i = 0; i < n && nv + 2 <= 32; i++) {
        int c = i, X, px, q, e, len = 0, depth = 0, t, st, p, a, ok = 1;
        int rr[8], m;
        if (ins[c].opcode != TP_ENC_CJ || !ins[c].is_branch || ins[c].pinned ||
            (X = tpf_target(r, c)) < 0 || X >= c || npred[X] != 1)
            continue;
        px = tpf_prev(ins, X);
        if (px >= 0 && !(ins[px].opcode == TP_ENC_J ||
                         tpn_is(&ins[px], TP_ENC_OPR, TP_OPR_RET)))
            continue;               /* something falls into X */
        /* R at X, then stl t */
        for (q = X, m = 0; q < c && m < 8; q = tpn_next(ins, n, q)) {
            int o = ins[q].opcode, dl, rd;
            if ((q != X && head[q]) || ins[q].pinned || ins[q].synth ||
                ins[q].tab || ins[q].dup_load)
                break;
            if (o == TP_ENC_STL)
                break;
            if (!(o == TP_ENC_LDL || o == TP_ENC_LDC || o == TP_ENC_ADC ||
                  o == TP_ENC_LDNLP ||
                  ((o == TP_ENC_LDNL || tpn_is(&ins[q], TP_ENC_OPR, TP_OPR_LB) ||
                    tpn_is(&ins[q], TP_ENC_OPR, TP_OPR_LS) ||
                    tpn_is(&ins[q], TP_ENC_OPR, TP_OPR_LSX) ||
                    tpn_is(&ins[q], TP_ENC_OPR, TP_OPR_LBX)) &&
                   !func_has_volatile)) ||
                !tps_effect(o, ins[q].operand, &dl, &rd) || rd > depth)
                break;
            depth += dl;
            rr[m++] = q;
        }
        if (m == 0 || q >= c || depth != 1 || ins[q].opcode != TP_ENC_STL ||
            ins[q].pinned || head[q])
            continue;
        st = q;
        t = ins[st].operand;
        /* the same run in the bottom block, its value tested by a cj */
        for (p = tpf_prev(ins, c), a = -1; p > st; p = tpf_prev(ins, p)) {
            /* the run may start a block; nothing after its start may */
            if (head[c])
                break;
            if (ins[p].opcode == ins[rr[0]].opcode &&
                ins[p].operand == ins[rr[0]].operand) {
                int j2, q2 = p;
                for (j2 = 0; j2 < m && q2 < c; j2++, q2 = tpn_next(ins, n, q2))
                    if (ins[q2].opcode != ins[rr[j2]].opcode ||
                        ins[q2].operand != ins[rr[j2]].operand ||
                        ins[q2].pinned || ins[q2].dup_load ||
                        (j2 && head[q2]))
                        break;
                if (j2 == m && q2 < c && ins[q2].opcode == TP_ENC_CJ &&
                    ins[q2].is_branch && !head[q2]) {
                    a = p;
                    e = q2;
                    break;
                }
            }
            if (head[p])
                break;
        }
        if (a < 0)
            continue;
        /* between the bottom run and c: nothing stores, nothing reads t,
         * nothing lands */
        for (q = tpn_next(ins, n, e); q < c && ok; q = tpn_next(ins, n, q)) {
            int o = ins[q].opcode, dl, rd;
            if (head[q] || tph_may_store(&ins[q]) || o == TP_ENC_STL ||
                o == TP_ENC_LDLP || (o == TP_ENC_LDL && ins[q].operand == t) ||
                !tps_effect(o, ins[q].operand, &dl, &rd))
                ok = 0;
        }
        for (q = a; q <= e && ok; q = tpn_next(ins, n, q))
            if (ins[q].opcode == TP_ENC_LDL && ins[q].operand == t)
                ok = 0;
        if (!ok)
            continue;
        /* t dead where the test leaves */
        {
            int x1 = tpf_target(r, e), x2 = tpn_next(ins, n, c);
            if (x1 < 0 || tpf_live(r, x1, t, seen) ||
                (x2 < n && tpf_live(r, x2, t, seen)))
                continue;
            for (q = tpn_next(ins, n, e); q < c; q = tpn_next(ins, n, q)) {
                int y;
                if (ins[q].is_branch && (y = tpf_target(r, q)) >= 0 &&
                    tpf_live(r, y, t, seen))
                    ok = 0;
            }
            if (!ok)
                continue;
        }
        v[nv].at = e; v[nv].opc = TP_ENC_STL; v[nv].opd = t; v[nv++].tgt = -1;
        v[nv].at = e; v[nv].opc = TP_ENC_LDL; v[nv].opd = t; v[nv++].tgt = -1;
        for (k = 0; k < m; k++)
            tpn_put(&ins[rr[k]], -1, 0);
        tpn_put(&ins[st], -1, 0);
        (void)len;
    }
    tcc_free(head);
    tcc_free(seen);
    tcc_free(npred);
    if (!nv)
        return 0;
    tpx_insert(r, v, nv);
    if (tp_knob(TPK_TP_NARROW_STATS))
        fprintf(stderr, "[LOOPCSE] %s: %d\n", funcname ? funcname : "?",
                nv / 2);
    return 1;
}

/* A jump or a conditional jump to a `j M` goes to M itself.  After the
 * hoisting, so that no new way into a loop stops it.  A `cj` that skips a
 * `j` this way skips a timeslice point; loops are not timeslice points as
 * it is (CLAUDE.md, "loops do not timeslice").  TCC_TRANSPUTER_THREAD=0
 * turns it off. */
static int tp_thread_jumps(struct tp_relax *r)
{
    static int off = -1;
    struct tp_relax_insn *ins = r->ins;
    const int n = r->n;
    int i, any = 0;

    if (off < 0) {
        const char *e = tp_knob(TPK_TCC_TRANSPUTER_THREAD);
        off = (e && *e == '0') ? 1 : 0;
    }
    if (off)
        return 0;
    for (i = 0; i < n; i++) {
        int hops, t, k;
        if ((ins[i].opcode != TP_ENC_J && ins[i].opcode != TP_ENC_CJ) ||
            !ins[i].is_branch || ins[i].pinned || ins[i].ext || ins[i].tab ||
            ins[i].icall)
            continue;
        for (hops = 0; hops < 8; hops++) {
            t = ins[i].target_old;
            if (t < 0 || t >= r->size || (k = r->tidx[t]) < 0)
                break;
            while (k < n && ins[k].opcode == -1 && !ins[k].pinned)
                k++;
            if (k >= n || k == i || ins[k].opcode != TP_ENC_J ||
                !ins[k].is_branch || ins[k].pinned || ins[k].ext ||
                ins[k].tab || ins[k].icall ||
                ins[k].target_old == ins[i].target_old)
                break;
            ins[i].target_old = ins[k].target_old;
            any = 1;
        }
    }
    return any;
}

/* Returns 1 if it rewrote anything; r->ins, r->n and r->tidx are then
 * updated. */
static int tp_narrow_locals(struct tp_relax *r)
{
    static int off = -1, stats = -1;
    struct tp_relax_insn *ins = r->ins, *out;
    const int n = r->n, size = r->size;
    const int F = func_frame_words;
    const int sext = tp_have_t9000_feat(TP_T9K_SEXT);
    struct tpn_slot sl[TPN_MAXF];
    struct tpn_val cslot[TPN_MAXF];
    char cs_bad[TPN_MAXF];
    int cs_n[TPN_MAXF];
    char *head, *store_ok;
    int *depth, *ins_at, *ins_opc, *ins_opd;
    int i, j, k, s, nins = 0, cap, any = 0, first = -1;

    if (off < 0) {
        const char *e = tp_knob(TPK_TCC_TRANSPUTER_NARROW_LOCAL);
        off = (e && *e == '0') ? 1 : 0;
        stats = tp_knob(TPK_TP_NARROW_STATS) ? 1 : 0;
    }
    if (off || func_uses_inline_asm || F <= 0 || F > TPN_MAXF || n <= 0)
        return 0;

    memset(sl, 0, sizeof sl);
    memset(cslot, 0, sizeof cslot);
    memset(cs_bad, 0, sizeof cs_bad);
    memset(cs_n, 0, sizeof cs_n);
    for (i = 0; i < n; i++) {
        int opc = ins[i].opcode, opd = ins[i].operand;
        if (opc == -1)
            continue;
        if (first < 0)
            first = i;
        if (opc == TP_ENC_OPR && opd == TP_OPR_GAJW)
            return 0;
        if (opc == TP_ENC_AJW && i != first && !ins[i].icall) {
            j = tpn_next(ins, n, i);
            if (j >= n || ins[j].opcode != TP_ENC_OPR ||
                ins[j].operand != TP_OPR_RET)
                return 0;
        }
        if (opc == TP_ENC_LDL && opd >= 0 && opd < F) {
            if (tp_have_t9000_feat(TP_T9K_LOAD16) || !tpn_ldlm(ins, n, i))
                sl[opd].bad = 1;
            else
                sl[opd].n_ldlm++;   /* T800: a halfword's word, masked */
        }
        if (opc == TP_ENC_STL && opd >= 0 && opd < F) {
            /* only the entry copy of a parameter: ldl p; stl s */
            for (k = i - 1; k >= 0 && ins[k].opcode == -1; k--)
                ;
            if (k < 0 || ins[k].opcode != TP_ENC_LDL || ins[k].operand <= F)
                sl[opd].bad = 1;
        }
        if (opc == TP_ENC_LDLP) {
            int form = tpn_form(ins, n, i), lo, ext;
            if (form) {
                if (opd >= 0 && opd < F) {
                    sl[opd].ldlp_any = 1;
                    sl[opd].n_sb += form == TPN_W_SB;
                    sl[opd].n_ss += form == TPN_W_SS;
                    sl[opd].n_lb += form == TPN_R_LB;
                    sl[opd].n_lbx += form == TPN_R_LBX;
                    sl[opd].n_s8x += form == TPN_R_S8X;
                    sl[opd].n_ls += form == TPN_R_LS;
                    sl[opd].n_lsx += form == TPN_R_LSX;
                    sl[opd].n_sb2 += form == TPN_W_SB2;
                }
                continue;
            }
            j = tpn_next(ins, n, i);
            if (opd >= 0 && opd < F)
                sl[opd].bad = sl[opd].ldlp_any = 1; /* used another way */
            if (tp_relax_byte_read(ins, n, i))
                continue;                   /* reads word opd alone */
            ext = j < n ? tpg_ldlp_extent(&ins[j], &lo) : -1;
            if (ext >= 0) {
                for (k = opd + lo; k < opd + lo + ext; k++)
                    if (k >= 0 && k < F)
                        sl[k].bad = sl[k].ldlp_any = 1;
                continue;
            }
            for (s = 0; s < F; s++)        /* an address handed on */
                if (tpn_reaches(opd, s))
                    sl[s].bad = sl[s].ldlp_any = 1;
        }
    }

    /* Slots only ever written by `ldc k; stl t`: at any read they hold
     * one of those constants (or nothing yet, which is undefined). */
    for (i = 0; i < n; i++) {
        int opc = ins[i].opcode, opd = ins[i].operand;
        if (opc != TP_ENC_STL || opd < 0 || opd >= F)
            continue;
        for (k = i - 1; k >= 0 && ins[k].opcode == -1; k--)
            ;
        if (k >= 0 && ins[k].opcode == TP_ENC_LDC && !ins[k].pinned) {
            long long v = ins[k].operand;
            if (!cslot[opd].known && cs_n[opd] == 0) {
                tpn_set(&cslot[opd], v, v);
            } else if (cslot[opd].known) {
                tpn_set(&cslot[opd], v < cslot[opd].lo ? v : cslot[opd].lo,
                        v > cslot[opd].hi ? v : cslot[opd].hi);
            }
        } else {
            cs_bad[opd] = 1;
        }
        cs_n[opd]++;
    }
    for (s = 0; s < F; s++)
        if (cs_bad[s] || !cs_n[s] || s < func_max_outgoing_words ||
            sl[s].ldlp_any)
            cslot[s].known = 0;

    /* kinds; a short may also be read by lb, as its low byte */
    for (s = 0; s < F; s++) {
        struct tpn_slot *p = &sl[s];
        int w16 = p->n_ls + p->n_lsx + p->n_ss + p->n_sb2 + p->n_ldlm;
        int w8 = p->n_lbx + p->n_s8x + p->n_sb + (w16 ? 0 : p->n_lb);
        if (p->bad || (w8 && w16) || !(w8 || w16))
            continue;
        if (w8)
            p->kind = p->n_lb ? TPN_U8 : p->n_lbx ? TPN_S8
                    : p->n_s8x ? TPN_S8X : TPN_U8;
        else
            p->kind = p->n_ls ? TPN_U16 : p->n_lsx ? TPN_S16 : TPN_U16;
        /* the T800 forms are unsigned: no signed reads beside them */
        if ((p->n_sb2 || p->n_ldlm) && (p->n_lsx || p->kind != TPN_U16))
            p->kind = 0;
        if ((p->kind == TPN_S8 || p->kind == TPN_S16 || p->n_lbx ||
             p->n_lsx) && !sext)
            p->kind = 0;
    }

    /* branch targets, loop depth */
    head = tcc_mallocz(n + 1);
    store_ok = tcc_mallocz(n + 1);
    depth = tcc_mallocz(sizeof(int) * (n + 2));
    if (first >= 0)
        head[first] = 1;
    for (i = 0; i < n; i++) {
        if (ins[i].tab > 0)
            head[i] = 1;
        if (!ins[i].is_branch || ins[i].target_old < 0 ||
            ins[i].target_old >= size)
            continue;
        k = r->tidx[ins[i].target_old];
        if (k < 0)
            continue;
        head[k] = 1;
        if (k <= i) {
            depth[k]++;
            depth[i + 1]--;
        }
    }
    for (i = 1; i <= n; i++)
        depth[i] += depth[i - 1];

    /* gain of each slot */
    for (i = 0; i < n; i++) {
        int opc = ins[i].opcode, opd = ins[i].operand, form;
        long long w, lo, hi;
        struct tpn_val v;
        if ((opc != TP_ENC_LDLP && opc != TP_ENC_STL && opc != TP_ENC_LDL) ||
            opd < 0 || opd >= F || !sl[opd].kind)
            continue;
        w = 1LL << (2 * (depth[i] < 6 ? depth[i] : 6));
        if (opc == TP_ENC_LDL) {
            sl[opd].gain += 2 * w;      /* the mask goes */
            continue;
        }
        if (opc == TP_ENC_STL) {
            sl[opd].gain -= w * tpn_trunc_len(sl[opd].kind);
            continue;
        }
        form = tpn_form(ins, n, i);
        if (form == TPN_W_SB2) {
            sl[opd].gain += 8 * w;      /* 12 instructions become 4 */
            continue;
        }
        if (form == TPN_R_LB && sl[opd].kind >= TPN_U16) {
            sl[opd].gain += tpn_masked_after(ins, n, tpn_next(ins, n, i))
                          ? w : -w;
            continue;
        }
        if (form < TPN_W_SB) {
            sl[opd].gain += w * tpn_read_gain(sl[opd].kind, form);
            continue;
        }
        v = tpn_range_before(ins, head, i, cslot, F);
        tpn_range_of(sl[opd].kind, &lo, &hi);
        if (v.known && v.lo >= lo && v.hi <= hi) {
            store_ok[i] = 1;
            sl[opd].gain += w;
        } else {
            sl[opd].gain -= w * (tpn_trunc_len(sl[opd].kind) - 1);
        }
    }
    for (s = 0; s < F; s++)
        if (sl[s].kind && sl[s].gain < 0)
            sl[s].kind = 0;

    /* rewrite in place, collecting the instructions to insert */
    cap = 16;
    ins_at = tcc_malloc(sizeof(int) * cap);
    ins_opc = tcc_malloc(sizeof(int) * cap);
    ins_opd = tcc_malloc(sizeof(int) * cap);
    for (i = 0; i < n; i++) {
        int opc = ins[i].opcode, opd = ins[i].operand, form, kind, m;
        int topc[4], topd[4];
        if ((opc != TP_ENC_LDLP && opc != TP_ENC_STL) || opd < 0 || opd >= F ||
            !(kind = sl[opd].kind))
            continue;
        if (stats && !any)
            fprintf(stderr, "[NARROW] %s:", funcname ? funcname : "?");
        any = 1;
        m = 0;
        if (opc == TP_ENC_STL) {
            m = tpn_trunc_code(kind, topc, topd);
            j = i;                          /* insert before the STL */
        } else {
            form = tpn_form(ins, n, i);
            j = tpn_next(ins, n, i);
            if (form == TPN_W_SB2) {
                /* -> [ldl r;] ldc 65535; and; stl s */
                int vr, ina, last = tpn_sb2(ins, n, i, &vr, &ina), q, c = 0;
                int want[4][2];
                if (!ina) {
                    want[c][0] = TP_ENC_LDL; want[c++][1] = vr;
                }
                want[c][0] = TP_ENC_LDC; want[c++][1] = 65535;
                want[c][0] = TP_ENC_OPR; want[c++][1] = TP_OPR_AND;
                want[c][0] = TP_ENC_STL; want[c++][1] = opd;
                for (q = i, k = 0; q <= last; q = tpn_next(ins, n, q)) {
                    if (k < c) {
                        tpn_put(&ins[q], want[k][0], want[k][1]);
                        k++;
                    } else {
                        tpn_put(&ins[q], -1, 0);
                    }
                }
                i = last;
                continue;
            }
            if (form == TPN_R_LB && kind >= TPN_U16) {
                /* the low byte of a short */
                tpn_put(&ins[i], TP_ENC_LDL, opd);
                if (tpn_masked_after(ins, n, j)) {
                    tpn_put(&ins[j], -1, 0);
                    continue;
                }
                tpn_put(&ins[j], TP_ENC_LDC, 255);
                topc[0] = TP_ENC_OPR;
                topd[0] = TP_OPR_AND;
                m = 1;
                j = tpn_next(ins, n, j);    /* insert after the LDC */
                goto insert;
            }
            if (form < TPN_W_SB) {
                tpn_put(&ins[i], TP_ENC_LDL, opd);
                if (form == TPN_R_LBX && kind == TPN_U8)
                    tpn_put(&ins[j], TP_ENC_OPR, TP_OPR_XBWORD);
                else if (form == TPN_R_LSX && kind == TPN_U16)
                    tpn_put(&ins[j], TP_ENC_OPR, TP_OPR_XSWORD);
                else
                    tpn_put(&ins[j], -1, 0);
                if (form == TPN_R_S8X && kind == TPN_S8X) {
                    k = tpn_next(ins, n, j);
                    tpn_put(&ins[k], -1, 0);
                    tpn_put(&ins[tpn_next(ins, n, k)], -1, 0);
                }
                continue;
            }
            if (store_ok[i]) {
                tpn_put(&ins[i], TP_ENC_STL, opd);
                tpn_put(&ins[j], -1, 0);
                continue;
            }
            m = tpn_trunc_code(kind, topc, topd);
            tpn_put(&ins[i], topc[0], topd[0]);
            tpn_put(&ins[j], TP_ENC_STL, opd);
            for (k = 1; k < m; k++) {
                topc[k - 1] = topc[k];
                topd[k - 1] = topd[k];
            }
            m--;                            /* the rest go before the STL */
            i = j;                          /* not an entry copy: skip it */
        }
    insert:
        for (k = 0; k < m; k++) {
            if (nins == cap) {
                cap *= 2;
                ins_at = tcc_realloc(ins_at, sizeof(int) * cap);
                ins_opc = tcc_realloc(ins_opc, sizeof(int) * cap);
                ins_opd = tcc_realloc(ins_opd, sizeof(int) * cap);
            }
            ins_at[nins] = j;
            ins_opc[nins] = topc[k];
            ins_opd[nins] = topd[k];
            nins++;
        }
    }
    if (stats && any) {
        for (s = 0; s < F; s++)
            if (sl[s].kind)
                fprintf(stderr, " %d(k%d,g%lld)", s, sl[s].kind, sl[s].gain);
        fprintf(stderr, " +%d\n", nins);
    }

    /* insert (ins_at is ascending), then rebuild the offset map */
    if (nins) {
        out = tcc_malloc(sizeof(*out) * (n + nins + 1));
        for (i = 0, k = 0, j = 0; i < n; i++) {
            for (; k < nins && ins_at[k] == i; k++) {
                memset(&out[j], 0, sizeof(*out));
                out[j].old_off = ins[i].old_off;
                out[j].synth = 1;
                tpn_put(&out[j], ins_opc[k], ins_opd[k]);
                j++;
            }
            out[j++] = ins[i];
        }
        tcc_free(ins);
        r->ins = out;
        r->n = j;
        for (i = 0; i <= size; i++)
            r->tidx[i] = -1;
        for (i = 0; i < r->n; i++)
            if (!out[i].synth)
                r->tidx[out[i].old_off] = i;
    }
    tcc_free(ins_at);
    tcc_free(ins_opc);
    tcc_free(ins_opd);
    tcc_free(head);
    tcc_free(store_ok);
    tcc_free(depth);

    /* what the ranges show further, now that the slots are words */
    for (s = 0; s < F; s++) {
        if (sl[s].kind) {
            long long lo, hi;
            tpn_range_of(sl[s].kind, &lo, &hi);
            tpn_set(&cslot[s], lo, hi);
        }
    }
    tpn_peephole(r, cslot, F);
    return any;
}

/*************************************************************/
/* A condition's truth value without branches                */
/*************************************************************/
/*
 * `c ? 1 : 0`, `(a < b) ? 0 : 1` and the like are compiled as a diamond
 * that stores one constant or the other, in one of several layouts, e.g.
 *
 *   cj La; j Lb; La: j Lx; Lb: ldc 1; stl t; j Lc; Lx: ldc 0; stl t; Lc:
 *   cj La; ldc vb; stl t; j Lc; La: ldc va; stl t; Lc:
 *
 * cj is taken when A is zero.  Following each of its two ways through
 * unconditional jumps, each must store a constant to the same t and then
 * reach the same Lc, the first instruction after the whole region.  With
 * {taken, not taken} = {1, 0} the stored value is just `A == 0` (eqc 0),
 * with {0, 1} it is `A != 0` (eqc 0; eqc 0), and the region becomes
 * `eqc 0; [eqc 0;] stl t`.  Nothing outside may branch into the region;
 * Lc may have other ways in.  TCC_TRANSPUTER_BOOL_SELECT=0 turns it off.
 */
#define TPB_SPAN 64

/* The first instruction at or after k that is not padding. */
static int tp_bool_real(const struct tp_relax *r, int k)
{
    while (k >= 0 && k < r->n && r->ins[k].opcode == -1 && !r->ins[k].pinned)
        k++;
    return k >= 0 && k < r->n ? k : -1;
}

/* Where branch b lands (an instruction, padding skipped), or -1. */
static int tp_bool_target(const struct tp_relax *r, int b)
{
    int t = r->ins[b].target_old;
    if (t < 0 || t >= r->size)
        return -1;
    return tp_bool_real(r, r->tidx[t]);
}

/* Follow one way from k through unconditional jumps: it must store a
 * constant into a slot and go on to the instruction `lc` (its index), or,
 * when *lc is -1, to the next instruction of its own; returns the constant
 * and sets *slot, or returns -1. */
static int tp_bool_path(const struct tp_relax *r, int k, int lo, int hi,
                        int *slot, int *lc)
{
    const struct tp_relax_insn *ins = r->ins;
    int steps, v, st, e;

    for (steps = 0; k >= lo && k < hi && ins[k].opcode == TP_ENC_J; steps++) {
        if (steps > 3 || ins[k].pinned || ins[k].tab)
            return -1;
        k = tp_bool_target(r, k);
    }
    if (k < lo || k >= hi || ins[k].opcode != TP_ENC_LDC ||
        (ins[k].operand != 0 && ins[k].operand != 1))
        return -1;
    v = ins[k].operand;
    st = tp_bool_real(r, k + 1);
    if (st < 0 || st >= hi || ins[st].opcode != TP_ENC_STL || ins[st].pinned)
        return -1;
    *slot = ins[st].operand;
    e = tp_bool_real(r, st + 1);
    for (steps = 0; e >= lo && e < hi && ins[e].opcode == TP_ENC_J; steps++) {
        if (steps > 3 || ins[e].pinned || ins[e].tab)
            return -1;
        e = tp_bool_target(r, e);
    }
    if (e < 0 || (*lc >= 0 && e != *lc))
        return -1;
    *lc = e;
    return v;
}

static void tp_bool_select(struct tp_relax *r)
{
    static int off = -1;
    struct tp_relax_insn *ins = r->ins;
    const int n = r->n;
    int *preds, i;

    if (off < 0) {
        const char *e = tp_knob(TPK_TCC_TRANSPUTER_BOOL_SELECT);
        off = (e && *e == '0') ? 1 : 0;
    }
    if (off || func_uses_inline_asm || n <= 0)
        return;
    /* branches into each instruction; one landing on the PFIX 0 padding
     * in front of an instruction counts for the instruction */
    preds = tcc_mallocz(sizeof(int) * (n + 1));
    for (i = 0; i < n; i++) {
        int k;
        if (ins[i].tab)
            preds[i] += 2;              /* reached from the dispatch */
        if (!ins[i].is_branch)
            continue;
        k = tp_bool_target(r, i);
        if (k >= 0)
            preds[k]++;
    }
    for (i = 0; i < n; i++) {
        int k, j, lc = -1, hi, vt, vf, st, sf, ok, inside[TPB_SPAN], ni = 0;
        int first[3], nf;

        if (ins[i].opcode != TP_ENC_CJ || ins[i].pinned || ins[i].tab)
            continue;
        hi = i + TPB_SPAN < n ? i + TPB_SPAN : n;
        /* the taken way first: it fixes Lc; then the fall-through */
        lc = -1;
        if ((vt = tp_bool_path(r, tp_bool_target(r, i), i + 1, hi, &st,
                               &lc)) < 0 ||
            (vf = tp_bool_path(r, tp_bool_real(r, i + 1), i + 1, hi, &sf,
                               &lc)) < 0 ||
            st != sf || vt == vf || lc <= i || lc >= hi)
            continue;
        /* region (i, lc): every branch into it comes from inside it, and
         * every branch out of it goes to lc */
        ok = 1;
        for (k = i + 1; k < lc && ok; k++) {
            if (ins[k].pinned || ins[k].tab || ins[k].icall)
                ok = 0;
            else if (ins[k].opcode == -1)
                continue;
            else if (ins[k].opcode != TP_ENC_J && ins[k].opcode != TP_ENC_LDC &&
                     ins[k].opcode != TP_ENC_STL)
                ok = 0;
            else if (ni < TPB_SPAN)
                inside[ni++] = k;
        }
        if (!ok)
            continue;
        for (j = 0; j < ni && ok; j++) {
            int c = 0, k2 = inside[j], to;
            if (ins[k2].opcode == TP_ENC_J) {
                to = tp_bool_target(r, k2);
                if (to != lc && !(to > i && to < lc))
                    ok = 0;
            }
            if (!preds[k2])
                continue;
            for (k = i; k < lc; k++)
                if (ins[k].is_branch && tp_bool_target(r, k) == k2)
                    c++;
            if (c != preds[k2])
                ok = 0;
        }
        if (!ok || ni < 3)
            continue;
        /* rewrite: eqc 0; [eqc 0;] stl t, then fall into lc */
        for (k = i; k < lc; k++) {
            if (ins[k].opcode == -1)
                continue;
            ins[k].opcode = -1;
            ins[k].operand = 0;
            ins[k].is_branch = 0;
            ins[k].new_len = 0;
        }
        nf = 0;
        first[nf++] = i;
        for (j = 0; j < ni && nf < 3; j++)
            first[nf++] = inside[j];
        tpn_put(&ins[first[0]], TP_ENC_EQC, 0);
        k = 1;
        if (vt == 0)                        /* taken (A == 0) gives 0 */
            tpn_put(&ins[first[k++]], TP_ENC_EQC, 0);
        tpn_put(&ins[first[k]], TP_ENC_STL, st);
        preds[lc] -= 1;
        i = lc - 1;
    }

    /* `gt; eqc 0; eqc 0`: a truth value tested twice is itself */
    memset(preds, 0, sizeof(int) * (n + 1));
    for (i = 0; i < n; i++) {
        int k;
        if (ins[i].tab)
            preds[i] += 2;
        if (ins[i].is_branch && (k = tp_bool_target(r, i)) >= 0)
            preds[k]++;
    }
    for (i = 0; i < n; i++) {
        int p, j;
        if (ins[i].opcode != TP_ENC_EQC || ins[i].operand != 0 ||
            ins[i].pinned || preds[i])
            continue;
        j = tp_bool_real(r, i + 1);
        if (j < 0 || ins[j].opcode != TP_ENC_EQC || ins[j].operand != 0 ||
            ins[j].pinned || preds[j])
            continue;
        /* the producer straight before, with no way in between */
        for (p = i - 1; p >= 0 && ins[p].opcode == -1 && !ins[p].pinned &&
                        !preds[p]; p--)
            ;
        if (p >= 0 && ins[p].opcode == -1)
            continue;
        if (p < 0 || ins[p].pinned ||
            !(ins[p].opcode == TP_ENC_EQC ||
              (ins[p].opcode == TP_ENC_OPR && (ins[p].operand == TP_OPR_GT ||
                                               ins[p].operand == TP_OPR_GTU))))
            continue;
        tpn_put(&ins[i], -1, 0);
        tpn_put(&ins[j], -1, 0);
        i = j;
    }
    tcc_free(preds);
}

/*************************************************************/
/* Loop-invariant arithmetic hoisted out of loops            */
/*************************************************************/
/*
 * `C[i * N + j] += A[i * N + k] * B[k * N + j]` recomputes `i * N` and
 * the whole address of C[i * N + j] on every pass of the k loop: TinyCC
 * compiles each expression where it stands.  This pass finds, in each
 * loop (a backward branch and its target), straight-line runs that
 * compute one value from frame words the loop never writes - `ldl i;
 * ldl N; prod`, `...; ldl C; wsub` - with operations that cannot trap
 * (prod, bsub, wsub, ssub, sum, diff, and, or, xor, shifts, not, ldnlp,
 * ldlp, ldc), computes each once before the loop into a new frame word on
 * top of the locals, and reads that word in the loop instead.
 *
 * A loop qualifies when it is entered only by its head (falling into it)
 * or by one `j` straight before the head, into it: the hoisted code goes
 * in front of that.  A frame word is invariant when the loop has no STL
 * of it, no LDLP of it, and - if a pointer may reach it - no store
 * through a pointer and no call; and no call either if it is an outgoing
 * argument word a callee may write.  Nothing is read from memory, so
 * stores through pointers never matter to the values themselves.  A run
 * shorter than three instructions is left.  At most TPH_MAX words a
 * function.  TCC_TRANSPUTER_HOIST=0 turns it off, TP_HOIST_STATS=1
 * reports what moved.
 */
#define TPH_MAX     8
#define TPH_NSEQ    64

static int tp_hoist_w;          /* words tp_hoist put on top of the locals */

/* Does c compute without side effects or traps?  *rd and *dl get its
 * stack effect. */
static int tph_pure(const struct tp_relax_insn *c, int *rd, int *dl, int mem)
{
    if (c->pinned || c->synth || c->tab || c->icall)
        return 0;
    switch (c->opcode) {
    case TP_ENC_LDL: case TP_ENC_LDC: case TP_ENC_LDLP:
        *rd = 0; *dl = 1; return 1;
    case TP_ENC_LDNLP:
        *rd = 1; *dl = 0; return 1;
    case TP_ENC_LDNL: case TP_ENC_ADC:      /* only with a twin (tph_twin) */
        *rd = 1; *dl = 0; return mem;
    case TP_ENC_OPR:
        switch (c->operand) {
        case TP_OPR_LB: case TP_OPR_LBX: case TP_OPR_LS: case TP_OPR_LSX:
            *rd = 1; *dl = 0; return mem;
        case TP_OPR_PROD: case TP_OPR_BSUB: case TP_OPR_WSUB:
        case TP_OPR_WSUBDB: case TP_OPR_SSUB: case TP_OPR_SUM:
        case TP_OPR_DIFF: case TP_OPR_AND: case TP_OPR_OR: case TP_OPR_XOR:
        case TP_OPR_SHL: case TP_OPR_SHR:
            *rd = 2; *dl = -1; return 1;
        case TP_OPR_NOT:
            *rd = 1; *dl = 0; return 1;
        case TP_OPR_MINT:
            *rd = 0; *dl = 1; return 1;
        }
    }
    return 0;
}

/* Could c store through a pointer, or a callee write a frame word? */
static int tph_may_store(const struct tp_relax_insn *c)
{
    int rd, dl;
    if (c->opcode == TP_ENC_STNL || c->opcode == TP_ENC_CALL)
        return 1;
    if (c->opcode != TP_ENC_OPR)
        return 0;
    if (c->operand == TP_OPR_SB || c->operand == TP_OPR_SS ||
        c->operand == TP_OPR_GCALL || c->operand == TP_FPU_STNLSN ||
        c->operand == TP_FPU_STNLDB || c->operand == TP_OPR_FPSTNLI32)
        return 1;
    return !tps_effect(c->opcode, c->operand, &dl, &rd);
}

struct tph_seq {
    int start, end, word;       /* records [start, end], its new word */
};

/* A run that reads memory, `ldl b; [adc k;] ldnl m | lb | lbx | ls | lsx`,
 * may be hoisted only where its word has been read already: the hoisted
 * read happens even when the loop runs no times, and a read the program
 * never made could trap under T9000 memory protection.  So the straight
 * code before `at` (no label on the way back, padding included, and no
 * write of b) must read the same word through b.  Its adc ran on the same
 * value then too, so it cannot trap here either.  core_list_find's
 * `info->idx`, tested just before the loop that compares with it.
 * TCC_TRANSPUTER_HOIST_LOAD=0 turns this off. */
static int tph_load_form(const struct tp_relax_insn *ins, int lo, int hi,
                         int *b, int *word)
{
    int q = lo, off = 0, o;
    while (q <= hi && ins[q].opcode == -1) q++;
    if (q > hi || ins[q].opcode != TP_ENC_LDL)
        return 0;
    *b = ins[q].operand;
    for (q++; q <= hi && ins[q].opcode == -1; q++)
        ;
    if (q <= hi && ins[q].opcode == TP_ENC_ADC) {
        off = ins[q].operand;
        for (q++; q <= hi && ins[q].opcode == -1; q++)
            ;
    }
    if (q > hi)
        return 0;
    o = ins[q].opcode;
    if (o == TP_ENC_LDNL)
        off += 4 * ins[q].operand;
    else if (!(o == TP_ENC_OPR && (ins[q].operand == TP_OPR_LB ||
                                   ins[q].operand == TP_OPR_LBX ||
                                   ins[q].operand == TP_OPR_LS ||
                                   ins[q].operand == TP_OPR_LSX)))
        return 0;
    for (q++; q <= hi && ins[q].opcode == -1; q++)
        ;
    if (q <= hi)
        return 0;               /* more after the load: not this form */
    *word = off >= 0 ? off / 4 : -((3 - off) / 4);
    return 1;
}

static int tph_twin(const struct tp_relax_insn *ins, const char *head,
                    int s, int e, int at)
{
    int b, w, q, steps = 0;
    if (!tph_load_form(ins, s, e, &b, &w))
        return 0;
    for (q = at - 1; q >= 0 && steps < 80; q--, steps++) {
        int o = ins[q].opcode, k, b2, w2;
        if (head[q + 1] && q + 1 < at)
            return 0;           /* another way in after q */
        if ((o == TP_ENC_STL || o == TP_ENC_LDLP) && ins[q].operand == b)
            return 0;
        if (o == TP_ENC_LDL && ins[q].operand == b) {
            /* the load q starts, up to three records on */
            for (k = q; k < at && k <= q + 6; k++)
                if (k > q && head[k])
                    break;
                else if (tph_load_form(ins, q, k, &b2, &w2) && b2 == b &&
                         w2 == w)
                    return 1;
        }
    }
    return 0;
}

/* Are runs a and b the same instructions? */
static int tph_same(const struct tp_relax_insn *ins, const struct tph_seq *a,
                    const struct tph_seq *b)
{
    int x = a->start, y = b->start;

    for (;;) {
        while (x <= a->end && ins[x].opcode == -1)
            x++;
        while (y <= b->end && ins[y].opcode == -1)
            y++;
        if (x > a->end || y > b->end)
            return x > a->end && y > b->end;
        if (ins[x].opcode != ins[y].opcode ||
            ins[x].operand != ins[y].operand)
            return 0;
        x++;
        y++;
    }
}

/* A hoisted word is often copied straight into a register slot y,
 * `ldl t; stl y`, and y read later in the block: read t there instead,
 * and drop the copy when nothing else reads y.  (The scheduler, which
 * would do this, has already run.) */
static void tph_copies(struct tp_relax *r, const struct tph_seq *seq,
                       int nseq, int F)
{
    struct tp_relax_insn *ins = r->ins;
    const int n = r->n;
    char *head = tcc_mallocz(n + 1);
    int i, k, m, a, b, t, y, left;

    for (i = 0; i < n; i++) {
        if (ins[i].tab)
            head[i] = 1;
        if (ins[i].is_branch && (k = tp_bool_target(r, i)) >= 0)
            head[k] = 1;
    }
    for (i = 0; i < n; i++) {
        if (ins[i].synth || ins[i].opcode != TP_ENC_LDL ||
            ins[i].operand < F || ins[i].operand >= F + tp_hoist_w)
            continue;
        a = i;
        t = ins[a].operand;
        b = tp_bool_real(r, a + 1);
        if (b < 0 || head[b] || ins[b].opcode != TP_ENC_STL || ins[b].pinned)
            continue;
        y = ins[b].operand;
        if (y < 0 || y >= func_frame_reserved || y < func_max_stack_words)
            continue;
        for (k = b + 1; k < n; k++) {
            int o = ins[k].opcode, d = ins[k].operand;
            if (head[k] || o == TP_ENC_J || o == TP_ENC_CJ ||
                o == TP_ENC_CALL ||
                (o == TP_ENC_OPR && (d == TP_OPR_GCALL || d == TP_OPR_RET)))
                break;
            if ((o == TP_ENC_STL || o == TP_ENC_LDLP) && (d == y || d == t ||
                                                         d == y - 1))
                break;
            if (o == TP_ENC_LDL && d == y && !ins[k].pinned) {
                ins[k].operand = t;
                ins[k].dup_load = 0;
                ins[k].new_len = tp_encode_len(TP_ENC_LDL, t);
            }
        }
        /* does anything still read y? */
        left = 0;
        for (m = 0; m < n && !left; m++) {
            int o = ins[m].opcode, d = ins[m].operand;
            if ((o == TP_ENC_LDL && d == y) ||
                (o == TP_ENC_LDLP && (d == y || d == y - 1)) ||
                ins[m].icall || ins[m].tab)
                left = 1;
        }
        if (!left) {
            tpn_put(&ins[a], -1, 0);
            tpn_put(&ins[b], -1, 0);
        }
    }
    (void)seq;
    (void)nseq;
    tcc_free(head);
}

static void tp_hoist(struct tp_relax *r, int top)
{
    static int off = -1, stats = -1, load_off = -1;
    struct tp_relax_insn *ins = r->ins, *out;
    const int n = r->n;
    const int F = func_frame_words;
    struct tpg_work g;
    char *head, *taken;
    int *loop_h, *loop_b, *nidx, nloops = 0, i, j, k, w = 0;
    struct tph_seq seq[TPH_NSEQ];
    int nseq = 0, *ins_at, *ins_from, *ins_word, nins = 0, cap;
    int word_seq[TPH_MAX];      /* a run computing each word */
    int word_at[TPH_MAX];       /* where it is computed */

    tp_hoist_w = 0;
    if (off < 0) {
        const char *e = tp_knob(TPK_TCC_TRANSPUTER_HOIST);
        off = (e && *e == '0') ? 1 : 0;
        stats = tp_knob(TPK_TP_HOIST_STATS) ? 1 : 0;
    }
    if (off || func_uses_inline_asm || F <= 0 || F > 200 || n <= 0)
        return;
    memset(&g, 0, sizeof g);
    g.ins = ins;
    g.n = n;
    g.tidx = r->tidx;
    g.size = r->size;
    g.F = F;
    if (!tpg_frame_ok(&g))
        return;

    head = tcc_mallocz(n + 1);
    taken = tcc_mallocz(n + 1);
    loop_h = tcc_malloc(sizeof(int) * (n + 1));
    loop_b = tcc_malloc(sizeof(int) * (n + 1));
    head[g.first] = 1;
    for (i = 0; i < n; i++) {
        if (ins[i].tab)
            head[i] = 1;
        if (!ins[i].is_branch || (k = tp_bool_target(r, i)) < 0)
            continue;
        head[k] = 1;
        if (k <= i) {
            loop_h[nloops] = k;
            loop_b[nloops++] = i;
        }
    }
    /* innermost (shortest) first */
    for (i = 1; i < nloops; i++)
        for (j = i; j > 0 && loop_b[j] - loop_h[j] <
                              loop_b[j - 1] - loop_h[j - 1]; j--) {
            int t = loop_h[j]; loop_h[j] = loop_h[j - 1]; loop_h[j - 1] = t;
            t = loop_b[j]; loop_b[j] = loop_b[j - 1]; loop_b[j - 1] = t;
        }

    for (i = 0; i < nloops && nseq < TPH_NSEQ && w < TPH_MAX; i++) {
        int h = loop_h[i], b = loop_b[i], p, at = -1, entry_j = -1;
        int hascall = 0, ptrstore = 0, ok = 1;
        unsigned char *wr;

        /* the record before the head, and the ways in */
        for (p = h - 1; p >= 0 && ins[p].opcode == -1 && !ins[p].pinned; p--)
            ;
        if (p < 0)
            continue;
        for (k = 0; k < n && ok; k++) {
            int t;
            if (k >= h && k <= b)
                continue;
            if (!ins[k].is_branch || (t = tp_bool_target(r, k)) < h || t > b)
                continue;
            if (k == p && ins[p].opcode == TP_ENC_J && entry_j < 0)
                entry_j = p;
            else
                ok = 0;
        }
        for (k = h; k <= b && ok; k++)
            if (ins[k].tab || ins[k].icall)
                ok = 0;
        if (!ok)
            continue;
        if (entry_j >= 0) {
            if (head[p] || ins[p].pinned)
                continue;
            at = p;
        } else {
            if (ins[p].opcode == TP_ENC_J || ins[p].opcode == TP_ENC_CJ ||
                (ins[p].opcode == TP_ENC_OPR &&
                 (ins[p].operand == TP_OPR_RET ||
                  ins[p].operand == TP_OPR_GCALL)))
                continue;               /* nothing simply falls in */
            at = h;
        }
        /* what the loop writes */
        wr = tcc_mallocz(F + 64);
        for (k = h; k <= b; k++) {
            int o = ins[k].opcode, d = ins[k].operand;
            if (o == -1)
                continue;
            if ((o == TP_ENC_STL || o == TP_ENC_LDLP) && d >= 0 && d < F + 63) {
                wr[d] = 1;
                if (o == TP_ENC_LDLP)
                    wr[d + 1] = 1;      /* a double's second word */
            }
            if (o == TP_ENC_CALL || (o == TP_ENC_OPR && d == TP_OPR_GCALL))
                hascall = 1;
            if (tph_may_store(&ins[k]))
                ptrstore = 1;
        }
        /* the runs */
        for (k = h; k <= b && nseq < TPH_NSEQ; ) {
            int e, depth = 0, best = -1, cnt = 0, rd, dl, mem = 0;
            int memok = !ptrstore && !hascall && !func_has_volatile &&
                        !tpk_off(TPK_TCC_TRANSPUTER_HOIST_LOAD, &load_off, 1);
            if (ins[k].opcode != TP_ENC_LDL && ins[k].opcode != TP_ENC_LDC &&
                ins[k].opcode != TP_ENC_LDLP) {
                k++;
                continue;
            }
            for (e = k; e <= b; e++) {
                if ((e > k && head[e]) || taken[e])
                    break;
                if (ins[e].opcode == -1 && !ins[e].pinned)
                    continue;
                if (!tph_pure(&ins[e], &rd, &dl, memok) || rd > depth)
                    break;
                if (ins[e].opcode == TP_ENC_LDNL || ins[e].opcode == TP_ENC_ADC ||
                    ins[e].opcode == TP_ENC_OPR) {
                    int r2, d2;
                    if (!tph_pure(&ins[e], &r2, &d2, 0))
                        mem = 1;
                }
                if (ins[e].opcode == TP_ENC_LDL) {
                    int x = ins[e].operand;
                    if (x < 0 || x >= F + 63 || wr[x] ||
                        (hascall && x < func_max_outgoing_words) ||
                        ((ptrstore || hascall) &&
                         tps_word_escapes(ins, n, x, top)))
                        break;
                }
                if (ins[e].opcode == TP_ENC_LDLP && ins[e].operand >= F)
                    break;              /* above the frame: moves with it */
                depth += dl;
                cnt++;
                if (depth > 3)
                    break;
                /* a run with a load needs its twin; two records will do,
                 * a load being dearer than the word that replaces it */
                if (depth == 1 && (cnt >= 3 || (mem && cnt >= 2)) &&
                    (!mem || tph_twin(ins, head, k, e, at)))
                    best = e;
            }
            if (best < 0) {
                k++;
                continue;
            }
            for (e = k; e <= best; e++)
                taken[e] = 1;
            seq[nseq].start = k;
            seq[nseq].end = best;
            seq[nseq].word = -1;
            for (j = 0; j < w; j++)
                if (word_at[j] == at &&
                    tph_same(ins, &seq[nseq], &seq[word_seq[j]])) {
                    seq[nseq].word = j;
                    break;
                }
            if (seq[nseq].word < 0) {
                if (w == TPH_MAX)
                    break;
                word_seq[w] = nseq;
                word_at[w] = at;
                seq[nseq].word = w++;
            }
            nseq++;
            k = best + 1;
        }
        tcc_free(wr);
    }
    tcc_free(loop_h);
    tcc_free(loop_b);
    tcc_free(taken);
    if (!w) {
        tcc_free(head);
        return;
    }

    /* make room: the new words go on top of the locals */
    g.w = w;
    tpg_room(&g);

    /* each word's code, once, before its loop: the run, then stl */
    cap = 16;
    ins_at = tcc_malloc(sizeof(int) * cap);
    ins_from = tcc_malloc(sizeof(int) * cap);
    ins_word = tcc_malloc(sizeof(int) * cap);
    for (j = 0; j < w; j++) {
        struct tph_seq *s = &seq[word_seq[j]];
        for (k = s->start; k <= s->end + 1; k++) {
            if (k <= s->end && ins[k].opcode == -1)
                continue;
            if (nins == cap) {
                cap *= 2;
                ins_at = tcc_realloc(ins_at, sizeof(int) * cap);
                ins_from = tcc_realloc(ins_from, sizeof(int) * cap);
                ins_word = tcc_realloc(ins_word, sizeof(int) * cap);
            }
            ins_at[nins] = word_at[j];
            ins_from[nins] = k <= s->end ? k : -1;     /* -1: the stl */
            ins_word[nins] = j;
            nins++;
        }
    }
    /* by insertion point; stable, so each run stays in order */
    for (i = 1; i < nins; i++)
        for (j = i; j > 0 && ins_at[j] < ins_at[j - 1]; j--) {
            int t = ins_at[j]; ins_at[j] = ins_at[j - 1]; ins_at[j - 1] = t;
            t = ins_from[j]; ins_from[j] = ins_from[j - 1]; ins_from[j - 1] = t;
            t = ins_word[j]; ins_word[j] = ins_word[j - 1]; ins_word[j - 1] = t;
        }
    out = tcc_malloc(sizeof(*out) * (n + nins + 1));
    nidx = tcc_malloc(sizeof(int) * (n + 1));
    for (i = 0, k = 0, j = 0; i < n; i++) {
        for (; k < nins && ins_at[k] == i; k++) {
            struct tp_relax_insn *c = &out[j++];
            if (ins_from[k] >= 0) {
                *c = ins[ins_from[k]];
            } else {
                memset(c, 0, sizeof(*c));
                c->opcode = TP_ENC_STL;
                c->operand = F + ins_word[k];
            }
            c->old_off = ins[i].old_off;
            c->synth = 1;
            c->pinned = 0;
            c->is_branch = 0;
            c->dup_load = 0;
            c->workspace_extent = 0;
            c->new_len = tp_encode_len(c->opcode, c->operand);
        }
        nidx[i] = j;
        out[j++] = ins[i];
    }
    /* the uses: each run's first record reads the word, the rest go */
    for (i = 0; i < nseq; i++) {
        struct tph_seq *s = &seq[i];
        tpn_put(&out[nidx[s->start]], TP_ENC_LDL, F + s->word);
        out[nidx[s->start]].dup_load = 0;
        for (k = s->start + 1; k <= s->end; k++)
            if (out[nidx[k]].opcode != -1)
                tpn_put(&out[nidx[k]], -1, 0);
    }
    /* the global cache's entry loads moved with everything else */
    for (i = 0; i < tpg.n; i++)
        if (tpg.load_rec[i] >= 0 && tpg.load_rec[i] < n)
            tpg.load_rec[i] = nidx[tpg.load_rec[i]];
    if (stats)
        fprintf(stderr, "[HOIST] %s: %d words, %d runs, %d instructions\n",
                funcname ? funcname : "?", w, nseq, nins);
    tcc_free(nidx);
    tcc_free(ins);
    r->ins = out;
    r->n = j;
    for (i = 0; i <= r->size; i++)
        r->tidx[i] = -1;
    for (i = 0; i < r->n; i++)
        if (!out[i].synth)
            r->tidx[out[i].old_off] = i;
    func_frame_words = F + w;
    tp_hoist_w = w;
    tph_copies(r, seq, nseq, F);
    tcc_free(ins_at);
    tcc_free(ins_from);
    tcc_free(ins_word);
    tcc_free(head);
}

/* ---- a loop's accumulator in a frame word (-mstrict-aliasing) ----
 *
 * `C[i] += A[i][k] * B[k][j]` over k reads and writes C[i] through memory
 * on every pass: `<v>; ldl a; ldnl m; bsub; ldl a; stnl m`.  Keeping it in
 * a new frame word t instead - `ldl a; ldnl m; stl t` before the loop,
 * `<v>; ldl t; bsub; stl t` in it, `ldl t; ldl a; stnl m` after it - saves
 * two instructions a pass, but delays the stores until the loop ends.
 * That is only right if nothing in the loop reads that word another way,
 * which TinyCC cannot tell: it has no type-based alias analysis, and the
 * Linux kernel depends on there being none.  So this is opt-in:
 * -mstrict-aliasing says the program keeps C's aliasing rules (an object
 * is read only through its own type or a character type), and then the
 * loop's other reads, all 16-bit (`ls`/`lsx`: short lvalues), cannot see
 * the int word.  Byte reads (`lb`, `lbx`) may alias anything and word
 * reads may be the same int: a loop with any of them is left.  One caveat
 * C itself allows and this does not: reading a short member of a union
 * through which the int is written, in the same loop.
 *
 * The loop: entered only by falling into its head, left only by falling
 * out of its bottom test, the load in its straight code from the head (so
 * it runs on every entry, and its word is valid when hoisted), a stores
 * nothing else, calls nothing, names no volatile, and the pointer word a
 * stays the same.  TCC_TRANSPUTER_ACCUM=1/0 overrides the flag. */
static int tp_accum_promote(struct tp_relax *r, int top)
{
    static int env = -2;
    struct tp_relax_insn *ins = r->ins;
    const int n = r->n;
    const int F = func_frame_words;
    struct tpg_work g;
    char *head;
    int i, k, nl = 0, *lh, *lb, done = 0;
    struct { int la, ll, pa, s, h, b, x, a, m, ok, ej; } c[8];

    (void)top;
    if (env == -2) {
        const char *e = tp_knob(TPK_TCC_TRANSPUTER_ACCUM);
        env = !e || !*e ? -1 : *e != '0';
    }
    if (!(env >= 0 ? env : tcc_state->tp_strict_alias) ||
        func_uses_inline_asm || func_has_volatile || F <= 0 || F > 200 ||
        n <= 0)
        return 0;
    memset(&g, 0, sizeof g);
    g.ins = ins;
    g.n = n;
    g.tidx = r->tidx;
    g.size = r->size;
    g.F = F;
    if (!tpg_frame_ok(&g))
        return 0;
    head = tcc_mallocz(n + 1);
    lh = tcc_malloc(sizeof(int) * (n + 1));
    lb = tcc_malloc(sizeof(int) * (n + 1));
    for (i = 0; i < n; i++) {
        if (ins[i].tab)
            head[i] = 1;
        if (ins[i].is_branch && (k = tpf_target(r, i)) >= 0) {
            head[k] = 1;
            if (k <= i) {
                lh[nl] = k;
                lb[nl++] = i;
            }
        }
    }
    for (i = 0; i < nl && done < 8; i++) {
        int h = lh[i], b = lb[i], q, x, la = -1, ll = -1, pa = -1, s = -1;
        int a = -1, m = 0, ok = 1, straight = 1, h0 = h, ej;
        if (ins[b].opcode != TP_ENC_CJ)
            continue;           /* left by falling out of a bottom test */
        for (x = b + 1; x < n && ins[x].opcode == -1; x++)
            ;
        if (x >= n)
            continue;
        /* the padding in front of the head: the load goes before it */
        while (h0 > 0 && ins[h0 - 1].opcode == -1 && !ins[h0 - 1].pinned)
            h0--;
        /* or the way in is one `j` to the head straight in front of it
         * (tp_hoist's entry): then the load goes before that */
        ej = tpf_prev(ins, h0);
        if (ej >= 0 && !(ins[ej].opcode == TP_ENC_J && ins[ej].is_branch &&
                         !head[ej] && !ins[ej].pinned &&
                         tpf_target(r, ej) == h))
            ej = -1;
        /* no way in but the back edges, no way out but past b */
        for (q = 0; q < n && ok; q++) {
            int t;
            if (!ins[q].is_branch || (t = tpf_target(r, q)) < 0)
                continue;
            if (q >= h && q <= b) {
                if (t < h || t > b)
                    ok = 0;
            } else if (t >= h0 && t <= b && q != ej) {
                ok = 0;
            }
        }
        if (!ok)
            continue;
        for (q = h; q <= b && ok; q++) {
            int o = ins[q].opcode, d = ins[q].operand, dl, rd;
            if (o == -1)
                continue;
            if (ins[q].pinned || ins[q].tab || ins[q].icall ||
                ins[q].workspace_extent)
                ok = 0;
            else if (q > h && head[q])
                straight = 0;
            if (o == TP_ENC_J || o == TP_ENC_CJ)
                straight = 0;
            if (o == TP_ENC_CALL || o == TP_ENC_AJW || o == TP_ENC_LDLP)
                ok = 0;
            else if (o == TP_ENC_LDNL || o == TP_ENC_STNL) {
                int p = tpf_prev(ins, q);
                if (p < 0 || ins[p].opcode != TP_ENC_LDL || p < h)
                    ok = 0;
                else if (o == TP_ENC_LDNL && la < 0 && straight) {
                    la = p;
                    ll = q;
                    a = ins[p].operand;
                    m = d;
                } else if (o == TP_ENC_STNL && s < 0 && la >= 0 &&
                           ins[p].operand == a && d == m) {
                    pa = p;
                    s = q;
                } else {
                    ok = 0;     /* another word read or written */
                }
            } else if (o == TP_ENC_OPR) {
                switch (d) {
                case TP_OPR_LS: case TP_OPR_LSX:
                case TP_OPR_ADD: case TP_OPR_SUB: case TP_OPR_MUL:
                case TP_OPR_SUM: case TP_OPR_DIFF: case TP_OPR_PROD:
                case TP_OPR_AND: case TP_OPR_OR: case TP_OPR_XOR:
                case TP_OPR_SHL: case TP_OPR_SHR: case TP_OPR_GT:
                case TP_OPR_GTU: case TP_OPR_WSUB: case TP_OPR_BSUB:
                case TP_OPR_WSUBDB: case TP_OPR_SSUB: case TP_OPR_NOT:
                case TP_OPR_XSWORD: case TP_OPR_XBWORD: case TP_OPR_XWORD:
                case TP_OPR_MINT: case TP_OPR_DUP: case TP_OPR_REV:
                    break;
                default:
                    ok = 0;     /* byte reads, FP, moves, anything else */
                }
            } else if (!tps_effect(o, d, &dl, &rd)) {
                ok = 0;
            }
        }
        if (!ok || la < 0 || s < 0 || a < 0 || a >= F)
            continue;
        for (q = h; q <= b; q++)
            if ((ins[q].opcode == TP_ENC_STL || ins[q].opcode == TP_ENC_LDLP) &&
                ins[q].operand == a)
                break;
        if (q <= b)
            continue;           /* the pointer changes in the loop */
        /* the loop's own entry: its `j`, or the code before falls in */
        if (ej < 0) {
            int p = tpf_prev(ins, h);
            if (p < 0 || ins[p].opcode == TP_ENC_J ||
                (ins[p].opcode == TP_ENC_OPR &&
                 (ins[p].operand == TP_OPR_RET || ins[p].operand == TP_OPR_GCALL)))
                continue;
        }
        /* not a loop already chosen, nor one around it */
        for (q = 0; q < done; q++)
            if (!(b < c[q].h || h > c[q].b))
                break;
        if (q < done)
            continue;
        c[done].la = la; c[done].ll = ll; c[done].pa = pa; c[done].s = s;
        c[done].h = h; c[done].b = b; c[done].x = x; c[done].a = a;
        c[done].m = m;
        c[done].ej = ej;
        done++;
    }
    tcc_free(lh);
    tcc_free(lb);
    tcc_free(head);
    if (!done)
        return 0;
    /* new words on top of the locals, as tp_hoist's */
    g.w = done;
    tpg_room(&g);
    {
        struct tpt_add *add = tcc_malloc(sizeof(*add) * 2 * done);
        int na = 0, j;
        for (j = 0; j < done; j++) {
            int t = F + j, a = c[j].a, m = c[j].m;
            /* before the head (falling in only), after the bottom test */
            add[na].at = c[j].ej >= 0 ? c[j].ej : c[j].h;
            add[na].ft = 1;
            add[na].n = 0;
            tpt_rec(&add[na].rec[add[na].n++], TP_ENC_LDL, a);
            tpt_rec(&add[na].rec[add[na].n++], TP_ENC_LDNL, m);
            tpt_rec(&add[na].rec[add[na].n++], TP_ENC_STL, t);
            /* a jump may name the padding in front of the head: go before
             * it (none comes from outside, checked); in front of the entry
             * `j` stay after its padding, which jumps may name */
            while (c[j].ej < 0 && add[na].at > 0 &&
                   ins[add[na].at - 1].opcode == -1 &&
                   !ins[add[na].at - 1].pinned)
                add[na].at--;
            na++;
            add[na].at = c[j].x;
            add[na].ft = 1;
            add[na].n = 0;
            tpt_rec(&add[na].rec[add[na].n++], TP_ENC_LDL, t);
            tpt_rec(&add[na].rec[add[na].n++], TP_ENC_LDL, a);
            tpt_rec(&add[na].rec[add[na].n++], TP_ENC_STNL, m);
            while (add[na].at > c[j].b + 1 && ins[add[na].at - 1].opcode == -1 &&
                   !ins[add[na].at - 1].pinned)
                add[na].at--;
            na++;
            /* in the loop: the load reads t, the store writes it */
            tpn_put(&ins[c[j].la], TP_ENC_LDL, t);
            ins[c[j].la].dup_load = 0;
            tpn_put(&ins[c[j].ll], -1, 0);
            tpn_put(&ins[c[j].pa], -1, 0);
            tpn_put(&ins[c[j].s], TP_ENC_STL, t);
        }
        func_frame_words = F + done;
        tp_hoist_w += done;
        if (tp_knob(TPK_TP_NARROW_STATS))
            fprintf(stderr, "[ACCUM] %s: %d loops\n",
                    funcname ? funcname : "?", done);
        tpt_insert(r, add, na);
        tcc_free(add);
    }
    return 1;
}

/* 1. Linear decode. */
static const char *tp_relax_decode(struct tp_relax *r)
{
    uint8_t *code = r->code;
    struct tp_relax_insn *ins = r->ins;
    const int size = r->size;
    int n = 0;
    int *tidx;
    int i;
    int p;

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
            ins[n].tab = 0;
            ins[n].icall = 0;
            ins[n].workspace_extent = 0;
            ins[n].dup_load = 0;
            n++;
            p++;
            continue;
        }

        len = tp_decode_instr_at(code, size, p, &opc, &opd);
        if (len <= 0) {           /* malformed tail — bail out entirely */
            return "decode";
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
        ins[n].tab = 0;
        ins[n].icall = 0;
        ins[n].workspace_extent = 0;
        ins[n].dup_load = 0;
        n++;
        p += len;
    }

    /* An offset -> index map. Branch retargeting is done three times over
     * (shrink fixpoint, emit, validation) and a linear search there made the
     * whole pass quadratic in the size of the function. */
    r->n = n;
    tidx = r->tidx = tcc_malloc(sizeof(int) * (size + 1));
    for (i = 0; i <= size; i++)
        tidx[i] = -1;
    for (i = 0; i < n; i++)
        tidx[ins[i].old_off] = i;
    return NULL;
}

/* 2. Pin anything a relocation points into.  This function's
 * relocations are the ones added since its prologue. */
static const char *tp_relax_pin(struct tp_relax *r)
{
    Section *sec = r->sec;
    struct tp_relax_insn *ins = r->ins;
    int *tidx = r->tidx;
    const int base = r->base;
    const int size = r->size;
    const int n = r->n;
    int i;

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
                return "reloc-outside";
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
            return "branch-target";
        }
    }
    return NULL;
}

/* 2'. Switch jump tables.  Find each table's dispatch and entries, and
 * pin them through the passes below, which only need to know where
 * control goes (tp_sched_function gives a dispatch every entry as a
 * successor). */
static const char *tp_relax_tables_find(struct tp_relax *r)
{
    struct tp_relax_insn *ins = r->ins;
    int *tidx = r->tidx;
    const int base = r->base;
    const int size = r->size;
    const int n = r->n;
    int i;

    for (i = 0; i < swtab_count; i++) {
        static const int shape[6][2] = {
            { TP_ENC_LDC, TP_SWITCH_JUMP_SLOT_SIZE }, { TP_ENC_OPR, TP_OPR_PROD },
            { TP_ENC_LDC, 2 }, { TP_ENC_OPR, TP_OPR_LDPI },
            { TP_ENC_OPR, TP_OPR_BSUB }, { TP_ENC_OPR, TP_OPR_GCALL } };
        int o = swtab_log[i].scale - base, e, ok;
        int k = (o >= 0 && o < size) ? tidx[o] : -1;

        ok = k >= 0 && k + 6 <= n;
        for (e = 0; ok && e < 6; e++)
            ok = ins[k + e].opcode == shape[e][0] &&
                 ins[k + e].operand == shape[e][1] && !ins[k + e].pinned;
        for (e = 0; ok && e < 6; e++) {
            ins[k + e].tab = -(i + 1);
            ins[k + e].pinned = 1;
        }
        for (e = 0; ok && e < swtab_log[i].entries; e++) {
            int eo = swtab_log[i].table - base + e * TP_SWITCH_JUMP_SLOT_SIZE;
            int m = (eo >= 0 && eo < size) ? tidx[eo] : -1;

            for (; m >= 0 && m < n && ins[m].opcode == -1 &&
                   ins[m].old_off < eo + TP_SWITCH_JUMP_SLOT_SIZE; m++) {
                ins[m].tab = i + 1;
                ins[m].pinned = 1;
            }
            ok = m >= 0 && m < n && ins[m].opcode == TP_ENC_J && !ins[m].pinned &&
                 ins[m].old_off + ins[m].old_len == eo + TP_SWITCH_JUMP_SLOT_SIZE;
            if (ok) {
                ins[m].tab = i + 1;
                ins[m].pinned = 1;
            }
        }
        if (!ok) {
            return "switch-table";
        }
    }
    return NULL;
}

/* 2''. Indirect calls.  gfunc_call ends one with
 *     ldc 5; ldpi; stl -4; ajw -4; gcall
 * which stores the return address in the callee's frame, moves Wptr onto
 * it and jumps; the callee's ret moves Wptr back.  No instruction between
 * the AJW and the gcall names a workspace word, so a slot number means the
 * same before and after the call, and the passes below treat the pair as
 * the call it is.  The five are pinned: the `ldc 5` counts their bytes. */
static const char *tp_relax_icalls_find(struct tp_relax *r)
{
    static const int shape[5][2] = {
        { TP_ENC_LDC, TP_ICALL_TAIL_BYTES }, { TP_ENC_OPR, TP_OPR_LDPI },
        { TP_ENC_STL, -4 }, { TP_ENC_AJW, -4 }, { TP_ENC_OPR, TP_OPR_GCALL } };
    struct tp_relax_insn *ins = r->ins;
    const int n = r->n;
    int i, e, bytes;

    for (i = 0; i + 5 <= n; i++) {
        if (ins[i + 3].opcode != TP_ENC_AJW || ins[i + 3].operand != -4)
            continue;
        for (e = 0, bytes = 0; e < 5; e++) {
            if (ins[i + e].opcode != shape[e][0] ||
                ins[i + e].operand != shape[e][1] || ins[i + e].pinned ||
                ins[i + e].tab)
                break;
            if (e >= 2)
                bytes += ins[i + e].old_len;
        }
        if (e < 5 || bytes != TP_ICALL_TAIL_BYTES ||
            ins[i + 1].old_off + ins[i + 1].old_len != ins[i + 2].old_off)
            continue;       /* some other AJW: the passes refuse it */
        for (e = 0; e < 5; e++) {
            ins[i + e].pinned = 1;
            ins[i + e].icall = 1;
        }
    }
    return NULL;
}

/* 2a. Give the placeholders gfunc_epilog could not fit (wide_log) their
 * real operand and the width it needs.  This must come before the
 * scheduler, which would otherwise take them for accesses to slot 0. */
static const char *tp_relax_wide(struct tp_relax *r)
{
    struct tp_relax_insn *ins = r->ins;
    int *tidx = r->tidx;
    const int base = r->base;
    const int size = r->size;
    int i;

    for (i = 0; i < wide_log_count; i++) {
        int o = wide_log[i].offset - base;
        int k = (o >= 0 && o < size) ? tidx[o] : -1;
        if (k < 0 || ins[k].pinned || ins[k].opcode != wide_log[i].opcode) {
            return "wide";
        }
        ins[k].operand = wide_log[i].operand;
        ins[k].new_len = tp_encode_len(ins[k].opcode, ins[k].operand);
        r->grown = 1;
    }
    return NULL;
}

/* 2b+. Jump tables are sized from here on.  An entry's J takes over its
 * whole slot: the padding in front of it goes, and the J is emitted
 * padded to the width the table settles on. */
static void tp_relax_tables_open(struct tp_relax *r)
{
    struct tp_relax_insn *ins = r->ins;
    const int n = r->n;
    int i;

    for (i = 0; i < n; i++) {
        if (!ins[i].tab)
            continue;
        ins[i].pinned = 0;
        if (ins[i].tab > 0) {
            if (ins[i].opcode == TP_ENC_J)
                ins[i].new_len = TP_SWITCH_JUMP_SLOT_SIZE;
            else
                ins[i].new_len = 0;
            r->grown = 1;
        }
    }
}

/* 2b''. Narrowed direct calls (near_call_log) stayed pinned through the
 * scheduler.  Now make each a branch to its callee: that sits before this
 * function and does not move, so the call re-encodes like a J - shorter
 * as the code in front of it shrinks, longer if widening pushed it away.
 * Its pfix 0 front padding becomes deletable no-ops; the relocation,
 * on the first of them, then slides onto the call itself.  Only a call
 * still encoded with the logged callee's displacement is the logged one;
 * anything else there stays pinned, as re-targeting it would send it to
 * the wrong function. */
static void tp_relax_near_calls(struct tp_relax *r)
{
    struct tp_relax_insn *ins = r->ins;
    int *tidx = r->tidx;
    const int base = r->base;
    const int size = r->size;
    const int n = r->n;
    int i;

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
}

/* 2c. Widening pushes code apart, so a branch across a widened
 * instruction may no longer reach with the width it had. Grow those
 * first: sizes only ever increase here, so this settles, and the shrink
 * pass below then starts from widths that all fit. */
static void tp_relax_grow(struct tp_relax *r)
{
    struct tp_relax_insn *ins = r->ins;
    int *tidx = r->tidx;
    const int size = r->size;
    const int n = r->n;
    const int grown = r->grown;
    int outlen = 0;
    int i;
    int pass;

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
        if (tp_swtab_level(ins, n))
            changed = 1;
        if (!changed)
            break;
    }
}

/* 3. Shrink to fixpoint. Sizes only ever decrease, so this terminates. */
static void tp_relax_shrink(struct tp_relax *r)
{
    struct tp_relax_insn *ins = r->ins;
    int *tidx = r->tidx;
    const int size = r->size;
    const int n = r->n;
    int outlen = 0;
    int i;
    int p;
    int pass;

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
            if (ins[i].tab > 0 && ins[i].opcode == TP_ENC_J)
                continue;               /* sized as a table, below */
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
        /* A table's entries shrink together, to the width the widest still
         * needs, and not below four bytes: wsub indexes those, and a
         * narrower table would need a multiply. */
        for (p = 1; p <= swtab_count; p++) {
            int cur = 0, mx = 4;
            for (i = 0; i < n; i++) {
                int want, newt;
                if (ins[i].tab != p || ins[i].opcode != TP_ENC_J)
                    continue;
                newt = tp_relax_newt(ins, i, tidx, size, outlen);
                cur = want = ins[i].new_len;
                while (want > 4) {
                    int disp = newt - (ins[i].new_off + want - 1);
                    if (tp_encode_len(TP_ENC_J, disp) > want - 1)
                        break;
                    want--;
                }
                if (want > mx)
                    mx = want;
            }
            if (mx < cur) {
                for (i = 0; i < n; i++)
                    if (ins[i].tab == p && ins[i].opcode == TP_ENC_J)
                        ins[i].new_len = mx;
                changed = 1;
            }
        }
        if (!changed)
            break;
    }
}

/* Each table's dispatch for the width it settled on: four-byte entries
 * are indexed by wsub, and the multiply goes; any other width is the
 * multiplier.  Removing two bytes only brings branches closer. */
static const char *tp_relax_tables_dispatch(struct tp_relax *r)
{
    struct tp_relax_insn *ins = r->ins;
    const int n = r->n;
    int i;
    int p;

    for (p = 1; p <= swtab_count; p++) {
        int d[6], nd = 0, w = 0;
        for (i = 0; i < n; i++) {
            if (ins[i].tab == -p && nd < 6)
                d[nd++] = i;
            else if (ins[i].tab == p && ins[i].opcode == TP_ENC_J)
                w = ins[i].new_len;
        }
        if (nd != 6 || ins[d[0]].opcode != TP_ENC_LDC ||
            ins[d[1]].operand != TP_OPR_PROD ||
            ins[d[4]].operand != TP_OPR_BSUB || w < 4 || w > 8) {
            return "switch-dispatch";
        }
        if (w == 4) {
            ins[d[0]].opcode = ins[d[1]].opcode = -1;
            ins[d[0]].new_len = ins[d[1]].new_len = 0;
            ins[d[4]].operand = TP_OPR_WSUB;
        } else {
            ins[d[0]].operand = w;
        }
    }
    return NULL;
}

/* Final offsets, after the last change of width. */
static void tp_relax_layout(struct tp_relax *r)
{
    int i, off = 0;

    for (i = 0; i < r->n; i++) {
        r->ins[i].new_off = off;
        off += r->ins[i].new_len;
    }
    r->outlen = off;
}

/* 4. Emit the relaxed stream. */
static const char *tp_relax_emit(struct tp_relax *r)
{
    uint8_t *code = r->code;
    struct tp_relax_insn *ins = r->ins;
    int *tidx = r->tidx;
    const int size = r->size;
    const int n = r->n;
    const int outlen = r->outlen;
    uint8_t *out;
    int *map;
    int i;

    out = r->out = tcc_malloc(outlen > size ? outlen : size);
    map = r->map = tcc_malloc(sizeof(int) * (size + 1));
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
            return "encoder";
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
    return NULL;
}

/* Every relocation must land on a mapped byte, otherwise the rewrite would
 * strand it. Verify before committing anything. */
static const char *tp_relax_check_moves(struct tp_relax *r)
{
    Section *sec = r->sec;
    int *map = r->map;
    const int base = r->base;
    const int size = r->size;
    int i;

    if (sec->reloc) {
        ElfW_Rel *rel = (ElfW_Rel *)sec->reloc->data;
        int nrel = sec->reloc->data_offset / sizeof(*rel);
        for (i = func_start_nrel; i < nrel; i++) {
            int ro = (int)rel[i].r_offset - base;
            if (ro < 0 || ro > size)
                continue;
            if (map[ro] < 0) {
                return "reloc-unmapped";
            }
        }
    }
    /* The same for labels, which step 6 moves. */
    {
        Sym *s;
        for (s = global_label_stack; s; s = s->prev) {
            int o = s->jnext - base;
            if (s->r == LABEL_DEFINED && o >= 0 && o <= size && map[o] < 0) {
                return "label-unmapped";
            }
        }
    }
    return NULL;
}

static int tp_relax_commit(struct tp_relax *r)
{
    Section *sec = r->sec;
    uint8_t *code = r->code;
    struct tp_relax_insn *ins = r->ins;
    int *map = r->map;
    uint8_t *out = r->out;
    const int base = r->base;
    const int size = r->size;
    const int outlen = r->outlen;
    int saved;
    int i;

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
        if (tpg.n || tpi_ndrop)
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
    return saved;
}

/* Rewrite the finished function with the passes above and the shortest
 * encodings.  Returns the bytes saved, or 0 having left it alone. */
static int tp_relax_function(void)
{
    Section *sec = cur_text_section;
    struct tp_relax r;
    const char *why;
    int i, saved;
    int base = func_start_ind;
    int size = ind - func_start_ind;

    if (base < 0 || size <= 0 || size > TP_RELAX_MAX_INSNS)
        return tp_relax_bail("size");
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

    memset(&r, 0, sizeof r);
    r.sec = sec;
    r.base = base;
    r.size = size;
    r.code = sec->data + base;
    r.ins = tcc_malloc(sizeof(*r.ins) * (size + 1));

    if ((why = tp_relax_decode(&r)) || (why = tp_relax_pin(&r)) ||
        (why = tp_relax_tables_find(&r)) ||
        (why = tp_relax_icalls_find(&r)) || (why = tp_relax_wide(&r)))
        goto bail;

    /* 2a''. Tiny leaves called here become their bodies. */
    if (tp_inline_leaves(&r))
        r.grown = 1;

    /* 2a. `c ? 1 : 0` without branches; then char and short locals
     * nobody else names live as whole words. */
    tp_bool_select(&r);
    if (tp_narrow_locals(&r))
        r.grown = 1;
    if (tp_thread_flags(&r))
        r.grown = 1;
    if (tp_tail_dup(&r))
        r.grown = 1;

    /* 2a'. Keep the addresses of busy globals in frame words of their own.
     * This renumbers the frame, so it comes before the passes that reason
     * about frame words. */
    if (tp_gaddr_cache(&r.ins, &r.n, r.tidx, size))
        r.grown = 1;

    /* 2b. Evaluation-stack scheduling: delete the workspace round trips the
     * always-STL register model leaves behind. Deleted instructions become
     * zero-length no-ops, which the shrink pass below already knows how to
     * drop; a rewritten one keeps its slot and is re-encoded from its new
     * opcode/operand. */
    tp_hoist_w = 0;
    tp_sched_function(r.ins, r.n, size, tpg.n);

    tp_copy3_move(&r, tpg.n);
    /* and the scheduler once more over what the range rules left */
    {
        int late = tpn_late(&r, tpg.n);
        if (tp_induction_mask(&r))
            late = 1;
        if (late)
            tp_sched_function(r.ins, r.n, size, tpg.n);
    }
    /* a state machine's way back to its switch, threaded on the state */
    if (tp_switch_thread(&r, tpg.n))
        r.grown = 1;
    if (tp_loop_cse(&r))
        r.grown = 1;

    /* 2b''. Arithmetic a loop does not change, computed before it.  This
     * puts words on top of the locals and the global cache's, so `top`
     * counts both from here on.  After the scheduler, whose round trips
     * would otherwise cut the runs it looks for. */
    if (tp_loop_invert(&r, tpg.n))
        r.grown = 1;
    tp_hoist(&r, tpg.n);
    if (tp_hoist_w)
        r.grown = 1;
    {
        int sr = tp_strength_reduce(&r, tpg.n);
        static int resched_off = -1;
        if (sr)
            r.grown = 1;
        /* -mstrict-aliasing: a loop's `*p op= v` kept in a frame word */
        if (tp_accum_promote(&r, tpg.n + tp_hoist_w))
            r.grown = 1;
        /* the round trips the hoisting and the strength reduction leave
         * (`ldl p; stl t; ldl t; ldl t` for p[i] += c), scheduled away */
        if ((sr || tp_hoist_w) &&
            !tpk_off(TPK_TCC_TRANSPUTER_RESCHED, &resched_off, 1))
            tp_sched_function(r.ins, r.n, size, tpg.n + tp_hoist_w);
    }
    if (tp_thread_jumps(&r))
        r.grown = 1;

    /* 2b'''. T9000 grouping: `bsub` is the address stage, which may not
     * follow a load in a group (L* A? M? E? W?); right after one it adds
     * loaded values, `ldl p; ldnl 0; bsub` in C[i] += v, and `sum` - the
     * same A + B without the overflow check - is an ALU op that may.
     * No instruction changes count: CoreMark -1.9% groups (D79 lever 2).
     * TCC_TRANSPUTER_SUM_AFTER_LOAD=0 turns it off. */
    {
        static int off = -1;
        int i, p;
        if (!tpk_off(TPK_TCC_TRANSPUTER_SUM_AFTER_LOAD, &off, 1) &&
            tp_have_t9000_feat(TP_T9K_GTU))
            for (i = 0; i < r.n; i++) {
                if (r.ins[i].opcode != TP_ENC_OPR ||
                    r.ins[i].operand != TP_OPR_BSUB || r.ins[i].pinned ||
                    (p = tpf_prev(r.ins, i)) < 0)
                    continue;
                if (r.ins[p].opcode == TP_ENC_LDNL ||
                    (r.ins[p].opcode == TP_ENC_OPR &&
                     (r.ins[p].operand == TP_OPR_LB ||
                      r.ins[p].operand == TP_OPR_LBX ||
                      r.ins[p].operand == TP_OPR_LS ||
                      r.ins[p].operand == TP_OPR_LSX)))
                    tpn_put(&r.ins[i], TP_ENC_OPR, TP_OPR_SUM);
            }
    }

    /* 2b'. Renumber the frame by use.  Moving a cold unit up can widen an
     * access, which the growth pass below then makes room for. */
    if (tp_slot_remap(r.ins, r.n, tpg.n + tp_hoist_w))
        r.grown = 1;
    if (tps_apply_dup_loads(r.ins, r.n))
        r.grown = 1; /* commit equal-width LDL -> DUP rewrites too */

    tp_relax_tables_open(&r);
    tp_relax_near_calls(&r);
    tp_relax_grow(&r);
    tp_relax_shrink(&r);
    if ((why = tp_relax_tables_dispatch(&r)))
        goto bail;
    tp_relax_layout(&r);
    if (r.outlen == size && !r.grown) {
        why = "nogain";
        goto bail;
    }
    if ((why = tp_relax_emit(&r)) || (why = tp_relax_check_moves(&r)))
        goto bail;
    saved = tp_relax_commit(&r);

    tcc_free(r.out);
    tcc_free(r.map);
    tcc_free(r.tidx);
    tcc_free(r.ins);
    return saved;

bail:
    tpi_ndrop = 0;
    tcc_free(r.out);
    tcc_free(r.map);
    tcc_free(r.tidx);
    tcc_free(r.ins);
    return tp_relax_bail(why);
}
