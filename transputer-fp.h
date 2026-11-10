/*
 *  Transputer code generator: floating point on the FPU, and the clean-up
 *  at the end of a statement.
 *
 *  Code, not declarations: transputer-gen.c includes this once.
 *
 *    gen_opf()        every floating-point operator
 *    tp_stmt_end()    removes parking the statement turned out not to need
 *    tp_label_here()  a label ends the window those rewrites may reach over
 *
 *  Both work on the last instructions emitted (tp_hist): an operand still
 *  in an FPU register is used from there, not reloaded.
 */

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

static int tp_ent_is(const struct tp_hist_ent *h, int opc, int val)
{
    return h->kind == TP_HIST_INSTR && h->opc == opc && h->val == val;
}

/* The same assignment when the destination is in memory and its address
 * waits in an integer register slot p:
 *
 *   ldlp x; fpstnldb; [ldl x+1; stl h;] ldl x; ldl p; stnl 0;
 *   ldl p; ldc 4; bsub; stl p; ldl h; ldl p; stnl 0   ->   ldl p; fpstnldb
 *
 * The word copy must be the whole rest of the window, so nothing later in it
 * reads the pair, h or the advanced p, and no vstack value may name them.
 * The words go to the same two addresses the stnls wrote (stnl and fpstnldb
 * both take a word-aligned address in A), each written once as before.
 * TCC_TRANSPUTER_DBL_MEM_STORE=0 keeps the copy for an A/B run. */
static int tp_stmt_double_store_mem(struct tp_hist_ent *w, int *keep, int *mod,
                                    int n, int a, int b)
{
    static int off = -1;
    int q[12], count = 0, i, x, hi, p, f;

    if (off < 0) {
        const char *e = tp_knob(TPK_TCC_TRANSPUTER_DBL_MEM_STORE);
        off = e && *e == '0';
    }
    if (off || w[a].kind != TP_HIST_INSTR || w[a].opc != TP_OP_LDLP ||
        !tp_ent_is(&w[b], TP_OP_OPR, TP_FPU_STNLDB))
        return 0;
    x = w[a].val;
    if (x < TREG_FA || x > TREG_FC ||
        tp_reg_live_on_vstack(x, NULL) || tp_reg_live_on_vstack(x + 1, NULL))
        return 0;
    for (i = tp_next_kept(keep, n, b); i < n; i = tp_next_kept(keep, n, i)) {
        if (count == 12)
            return 0;
        q[count++] = i;
    }
    if (count != 10 && count != 12)
        return 0;
    f = count - 10;
    hi = x + 1;
    if (f) {
        if (!tp_ent_is(&w[q[0]], TP_OP_LDL, hi) ||
            w[q[1]].kind != TP_HIST_INSTR || w[q[1]].opc != TP_OP_STL)
            return 0;
        hi = w[q[1]].val;
        if (hi < TREG_FA || hi > TREG_FC || hi == x || hi == x + 1 ||
            tp_reg_live_on_vstack(hi, NULL))
            return 0;
    }
    if (w[q[f + 1]].kind != TP_HIST_INSTR || w[q[f + 1]].opc != TP_OP_LDL)
        return 0;
    p = w[q[f + 1]].val;
    if (p < 0 || p >= NB_REGS || !(reg_classes[p] & RC_INT) ||
        (p >= TREG_FA && p <= TREG_FC + 1) || tp_reg_live_on_vstack(p, NULL))
        return 0;
    if (!tp_ent_is(&w[q[f]], TP_OP_LDL, x) ||
        !tp_ent_is(&w[q[f + 2]], TP_OP_STNL, 0) ||
        !tp_ent_is(&w[q[f + 3]], TP_OP_LDL, p) ||
        !tp_ent_is(&w[q[f + 4]], TP_OP_LDC, 4) ||
        !tp_ent_is(&w[q[f + 5]], TP_OP_OPR, TP_OPR_BSUB) ||
        !tp_ent_is(&w[q[f + 6]], TP_OP_STL, p) ||
        !tp_ent_is(&w[q[f + 7]], TP_OP_LDL, hi) ||
        !tp_ent_is(&w[q[f + 8]], TP_OP_LDL, p) ||
        !tp_ent_is(&w[q[f + 9]], TP_OP_STNL, 0))
        return 0;
    w[a].opc = TP_OP_LDL;
    w[a].val = p;
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
        const char *e = tp_knob(TPK_TCC_TRANSPUTER_VPOP_DEAD);
        off = e && *e == '0';           /* =0 for an A/B run */
    }
    if (off || nocode_wanted || func_uses_inline_asm || tp_hist_n == 0 ||
        tp_hist[0].end != ind)
        return;
    {
        /* TCC_TRANSPUTER_VPOP_ONLY=name: only in functions whose name
         * contains it - for bisecting a miscompile */
        const char *only = tp_knob(TPK_TCC_TRANSPUTER_VPOP_ONLY);

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
            if (tp_stmt_double_store(w, keep, mod, n, a, b) ||
                tp_stmt_double_store_mem(w, keep, mod, n, a, b)) {
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
    if (tp_knob(TPK_TCC_TRANSPUTER_VPOP_DEAD) &&
        *tp_knob(TPK_TCC_TRANSPUTER_VPOP_DEAD) == '2') {
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
        const char *e = tp_knob(TPK_TCC_TRANSPUTER_FPSTACK);
        off = e && *e == '0';
    }
    if (off || nocode_wanted || (sv->r & (VT_LVAL | VT_SYM)) ||
        r >= VT_CONST || !(reg_classes[r] & RC_FLOAT) ||
        tp_hist_n == 0 || tp_hist[0].end != ind)
        return 0;
    {
        /* TCC_TRANSPUTER_FPSTACK_ONLY=name: only in functions whose name
         * contains it - for bisecting a miscompile */
        const char *only = tp_knob(TPK_TCC_TRANSPUTER_FPSTACK_ONLY);

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
    if (tp_knob(TPK_TCC_TRANSPUTER_FPSTACK) &&
        *tp_knob(TPK_TCC_TRANSPUTER_FPSTACK) == '2') {
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
        const char *e = tp_knob(TPK_TCC_TRANSPUTER_FPSTACK);
        off = e && *e == '0';
    }
    if (off || nocode_wanted || (sv->r & (VT_LVAL | VT_SYM)) ||
        r >= VT_CONST || !(reg_classes[r] & RC_FLOAT) ||
        tp_hist_n == 0 || tp_hist[0].end != ind ||
        tp_reg_live_on_vstack(r, sv) ||
        (is_db && tp_reg_live_on_vstack(r + 1, sv)))
        return 0;
    {
        const char *only = tp_knob(TPK_TCC_TRANSPUTER_FPSTACK_ONLY);

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
    if (tp_knob(TPK_TCC_TRANSPUTER_FPSTACK) &&
        *tp_knob(TPK_TCC_TRANSPUTER_FPSTACK) == '2')
        fprintf(stderr, "[FPSTACK] %s moved %d entries above %d\n",
                funcname, nl, nr);

    ind = tp_hist[s].off;
    cur_text_section->data_offset = ind;
    tp_drop_local_patches_from(ind);
    tp_hist_rollback(ind);
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
        const char *e = tp_knob(TPK_TCC_TRANSPUTER_FPSTACK);
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
           sv->c.i >= 0 && tp_hist_last_stl(v) >= 0;
}

/* Unary minus: flip the sign bit.  0.0 - x is not negation: it gives
 * +0.0 for x = +0.0.  Float values live in workspace slots, low word
 * first, so the sign is the top bit of the word in the last slot. */
static void tp_opf_neg(void)
{
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
}

/* Can the FPU load this operand straight from where it is? */
static int tp_opf_direct_ok(const SValue *sv)
{
    int v = sv->r & VT_VALMASK;

    return (v < VT_CONST || v == VT_CONST || v == VT_LOCAL || v == VT_LLOCAL)
           && !(sv->r & VT_MUSTBOUND);
}

/* Direct-addressable operands: skip gv2() materialization entirely and
 * let the FPU load each operand straight from its home (local slot,
 * pointer register, global, or constant pool/scratch).  gv2 would copy
 * each value through a workspace slot with integer ldnl/stl first —
 * three memory operations per operand where one fpldnl does the job.
 * Covers float32 and float64 uniformly. */
/* + - * / on two such operands, which are vtop[-1] and vtop[0]. */
static void tp_opf_direct_arith(int op, int bt, int is_db)
{
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
        !(tp_knob(TPK_TCC_TRANSPUTER_FPSTACK) &&
          *tp_knob(TPK_TCC_TRANSPUTER_FPSTACK) == '0')) {
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
        !(tp_knob(TPK_TCC_TRANSPUTER_FPSTACK) &&
          *tp_knob(TPK_TCC_TRANSPUTER_FPSTACK) == '0') &&
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
}

/* A comparison of two such operands. */
static void tp_opf_direct_compare(int op, int is_db)
{
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
}

/* A float or double local variable, read as such. */
static int tp_opf_plain_local(const SValue *sv)
{
    return (sv->r & VT_LVAL) && (sv->r & VT_VALMASK) == VT_LOCAL &&
           !(sv->r & VT_SYM);
}

/* Zero-STL for float locals: if both operands are local float/double
 * variables, emit LDLP+fpldnlsn/db directly with fused ops.
 * Works for both float32 and float64 (double).  `first` and `second` are
 * the operands as they were before gv2(). */
static void tp_opf_local_pair(int op, int bt, int is_db,
                              const SValue *first, const SValue *second)
{
    /* Skip gv2 — load directly from local float slots.
     * For + and *: use fused form for second operand.
     * Pattern: LDLP a; fpldnlsn; LDLP b; fpldnl{add,mul}sn */
    gv2(RC_FLOAT, RC_FLOAT);  /* still needed for vstack sync */

    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;

    /* Load first operand from its original local offset */
    emit_local_addr(first->c.i);
    emit_instr(TP_OP_OPR, is_db ? TP_FPU_LDNLDB : TP_FPU_LDNLSN);
    abcreg_pop(1);
    fpureg_push(1);

    if (op == '+' || op == '*') {
        /* Fused: load second AND operate in one instruction */
        emit_local_addr(second->c.i);
        if (op == '+')
            emit_instr(TP_OP_OPR, is_db ? TP_FPU_LDNLADDDB : TP_FPU_LDNLADDSN);
        else
            emit_instr(TP_OP_OPR, is_db ? TP_FPU_LDNLMULDB : TP_FPU_LDNLMULSN);
        abcreg_pop(1);
    } else {
        /* Sub/div: no fused form, load second separately */
        emit_local_addr(second->c.i);
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
}

/* Both operands are in workspace slots: push them, or fuse a load into the
 * operation, and operate. */
static void tp_opf_general(int op, int bt, int is_db,
                           const SValue *first, const SValue *second)
{
    SValue sv_first = *first, sv_second = *second;
    int fpu_fuse = 0;
    int fpu_fuse_swap = 0;

    abcreg_stack_depth = 0;
    fpureg_stack_depth = 0;

/* Detect fused load-and-operate opportunity for + and * (T800).
 * fpldnladdsn/fpldnlmulsn load from Mem[AReg] and combine with FAreg
 * in a single instruction, eliminating FPU stack push overhead.
 * Now also tries swapping operands (FPREV) to fuse with first operand. */
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
}

ST_FUNC void gen_opf(int op) {
    SValue orig_first, orig_second;
    SValue sv_first, sv_second;
    int bt, is_db, arith;

    if (op == TOK_NEG) {
        tp_opf_neg();
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
    arith = op == '+' || op == '-' || op == '*' || op == '/';

    if (tp_opf_direct_ok(&orig_first) && tp_opf_direct_ok(&orig_second)) {
        if (arith) {
            tp_opf_direct_arith(op, bt, is_db);
            return;
        }
        if (op == TOK_EQ || op == TOK_NE || op == TOK_GT ||
            op == TOK_LT || op == TOK_GE || op == TOK_LE) {
            tp_opf_direct_compare(op, is_db);
            return;
        }
    }

    if (arith && tp_opf_plain_local(&orig_first) &&
        tp_opf_plain_local(&orig_second)) {
        tp_opf_local_pair(op, bt, is_db, &orig_first, &orig_second);
        return;
    }

    if (is_db) {
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

        if (tp_opf_debug) {
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
        /* Standard path */
        gv2(RC_FLOAT, RC_FLOAT);
        sv_first = vtop[-1];
        sv_second = vtop[0];
    }
    tp_opf_general(op, bt, is_db, &sv_first, &sv_second);
}
