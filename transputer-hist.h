/*
 *  Transputer code generator: what was just emitted.
 *
 *  Code, not declarations: transputer-gen.c includes this once.
 *
 *  A short trailing window of the instructions emitted, newest first.  The
 *  rules that rewrite code already emitted - folding an `adc` into the
 *  access after it, cancelling an `stl r; ldl r` round trip, taking an FPU
 *  result back before it is stored, removing parking a statement turned out
 *  not to need - all decide from this window, and all take code back
 *  through tp_hist_rollback().
 *
 *  Only emit_instr() and emit_local_op() record.  Anything written with a
 *  raw g() - a relocation placeholder, a jump placeholder - leaves a gap,
 *  which shows as an entry whose `end` is not where the next one starts, or
 *  not `ind` for the newest; every reader checks for that, so such bytes
 *  end what a rule may reach for free.
 *
 *    tp_hist_record()             an instruction was emitted
 *    tp_hist_clear()              nothing before here may be touched: a
 *                                 label, a jump target, suppressed code
 *    tp_hist_rollback()           code from an offset on was taken back
 *    tp_hist_rollback_keep_last() the same, for a cancelled trailing store
 *    tp_hist_last_instr()         the instruction emit_instr() just emitted
 *    tp_hist_last_stl()           is that `stl reg`?
 *    tp_hist[], tp_hist_n         the window itself, for the rules that scan
 */

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

/* The two "last instruction" questions below see no further back than
 * these offsets.  A rollback leaves older entries in the window, where the
 * scanning rules still want them, but the instruction that is newest again
 * is not offered as "just emitted": it was not, and the rules that ask
 * (the adc fold, the store cancel) were written for the instruction their
 * caller has this moment produced.  The store question has its own bound
 * because cancelling a store shows the instruction before it again, but
 * not as a store to cancel in turn. */
static int tp_hist_instr_floor;
static int tp_hist_stl_floor;

static void tp_hist_clear(void)
{
    tp_hist_n = 0;
    tp_hist_instr_floor = 0;
    tp_hist_stl_floor = 0;
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
    tp_hist_instr_floor = off;
    tp_hist_stl_floor = off;
}

/* The trailing `stl` that started at `off` has been cancelled.  The
 * instruction before it is once again the last thing emitted, so it stays
 * visible to tp_hist_last_instr() if it was before the store - otherwise
 * an `adc` there could no longer be folded into the access that follows. */
static void tp_hist_rollback_keep_last(int off)
{
    int floor = tp_hist_instr_floor;

    tp_hist_rollback(off);
    if (tp_hist_n > 0 && tp_hist[0].kind == TP_HIST_INSTR &&
        tp_hist[0].end == off && tp_hist[0].off >= floor)
        tp_hist_instr_floor = tp_hist[0].off;
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

/* The instruction emit_instr() emitted last, if nothing at all has been
 * emitted since and no rollback has intervened; else NULL.  The entry
 * moves when the window does: read it before recording or rolling back. */
static const struct tp_hist_ent *tp_hist_last_instr(void)
{
    if (tp_hist_n > 0 && tp_hist[0].kind == TP_HIST_INSTR &&
        tp_hist[0].end == ind && tp_hist[0].off >= tp_hist_instr_floor)
        return &tp_hist[0];
    return NULL;
}

/* Where the `stl reg` that is the last thing emitted starts, or -1 if the
 * last thing emitted is anything else.  reg is a workspace-slot register;
 * a matching `ldl reg` can cancel the store instead of round-tripping the
 * value through memory. */
static int tp_hist_last_stl(int reg)
{
    if (reg >= 0 && reg < NB_REGS && tp_hist_n > 0 &&
        tp_hist[0].kind == TP_HIST_INSTR && tp_hist[0].opc == TP_OP_STL &&
        tp_hist[0].val == reg && tp_hist[0].end == ind &&
        tp_hist[0].off >= tp_hist_stl_floor)
        return tp_hist[0].off;
    return -1;
}
