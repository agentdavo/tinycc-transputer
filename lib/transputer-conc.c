/*
 * transputer-conc.c - ALT multiplexing and process setup for conc.h.
 *
 * Everything here that could be a single instruction already is one, inline
 * in conc.h.  What is left are the sequences: ALT, which is five different
 * instructions cooperating through the workspace slots below Wptr, PSetup,
 * which has to lay out a workspace by hand, the per-process records that
 * let a process find what it owns (its VLA arena), and the communicate-or-
 * fail transfers (ChanInTimeFail and friends), which need a process of
 * their own.
 *
 * The ALT state lives at Wptr[0] (Temp), Wptr[-3] (State), Wptr[-4] (TLink)
 * and Wptr[-5] (Time) - see Temp_s/State_s/TLink_s/Time_s in
 * transputer/t4/p.c.  Two consequences:
 *
 *   - The sequence must run start to finish inside one function body with
 *     no call in between, because those slots belong to whichever function
 *     is executing.  That is why the loops below are written out rather
 *     than factored into a helper.
 *
 *   - Temp is Wptr[0], and this ABI spills the first parameter to Wptr[0].
 *     altwt writes NoneSelected_o (0xffffffff) there and disc reads it back,
 *     so the first parameter of the enclosing function is destroyed by the
 *     sequence.  Left unhandled this is silent and total: `chans` comes back
 *     as 0xffffffff, every chans[i] reads a garbage word, and disc reports
 *     the first guard ready when nothing is.
 *
 *     So the ALT sequences all live in alt_core(), whose first parameter
 *     `slot0` exists only to absorb that write - it is never read after the
 *     ALT begins.  The live values (chans, guards, n) sit at slots 1..3.
 *     Any function that writes its own ALT inline must reserve slot 0 the
 *     same way.
 */

/* How alt_core arbitrates. */
#define ALT_MODE_WAIT  0   /* block until a channel is ready       */
#define ALT_MODE_SKIP  1   /* add a skip guard; never block        */
#define ALT_MODE_TIMER 2   /* add a timer guard at `deadline`      */

#ifndef __TRANSPUTER__
#error "transputer-conc.c is for the transputer target"
#endif

#include <stdarg.h>
#include "transputer-proc.h"

typedef volatile int Channel;

#define NOPROCESS ((int)0x80000000)
#define PROC_WS_MIN 512     /* as conc.h */

#define ALT_MAX 64   /* guards per ALT; the enable loop is O(n) in any case */

/*
 * Block until one of `n` input channels has data, and return its index.
 *
 * enbc(guard, chan) marks each channel; altwt then deschedules unless one
 * is already ready.  disc(code, guard, chan) is what actually decides: it
 * returns true for exactly one channel - the first ready guard in the
 * order it is called - because it only fires while the workspace Temp slot
 * still reads NoneSelected.  So the disable loop cannot select twice, and
 * the scan order is the priority order.
 */

/*
 * The whole ALT, in one function so the sequence never crosses a call.
 *
 * `slot0` is the sacrificial first parameter described above; callers pass
 * 0.  disc() decides the outcome: it fires for exactly one channel - the
 * first ready one in scan order - because it only selects while Temp still
 * reads NoneSelected_o.  So the disable loop cannot select twice, and the
 * scan order is the arbitration priority.
 *
 * Truth values are the hardware's: true_t is 1 and false_t is 0, so the
 * results are used as plain C booleans.
 */
static int alt_core(int slot0, Channel **chans, const int *guards, int n,
                    int mode, int deadline)
{
    int i;
    int sel = -1;
    int g;

    (void)slot0;

    if (mode == ALT_MODE_TIMER)
        __talt();
    else
        __alt();

    for (i = 0; i < n; i++) {
        g = guards ? (guards[i] != 0) : 1;
        __enbc(g, chans[i]);
    }
    if (mode == ALT_MODE_SKIP)
        __enbs(1);
    else if (mode == ALT_MODE_TIMER)
        __enbt(1, deadline);

    if (mode == ALT_MODE_TIMER)
        __taltwt();
    else
        __altwt();

    for (i = 0; i < n; i++) {
        g = guards ? (guards[i] != 0) : 1;
        if (__disc(i, g, chans[i]))
            sel = i;
    }
    if (mode == ALT_MODE_SKIP)
        __diss(n, 1);
    else if (mode == ALT_MODE_TIMER)
        __dist(n, 1, deadline);

    return sel;
}

int ChanAltGuarded(Channel **chans, const int *guards, int n)
{
    if (n <= 0)
        return -1;
    return alt_core(0, chans, guards, n, ALT_MODE_WAIT, 0);
}

int ChanAlt(Channel **chans, int n)
{
    if (n <= 0)
        return -1;
    return alt_core(0, chans, 0, n, ALT_MODE_WAIT, 0);
}

/* ChanAlt2, ChanAlt3 and ChanAltGuarded3 (conc.h): alt_core for a fixed
 * number of channels, with no arrays and no loops.  slot0 absorbs altwt's
 * write to Temp, as alt_core's does; the macros pass it. */
int __tp_alt2(int slot0, Channel *c0, Channel *c1)
{
    int sel = -1;

    (void)slot0;
    __alt();
    __enbc(1, c0);
    __enbc(1, c1);
    __altwt();
    if (__disc(0, 1, c0))
        sel = 0;
    if (__disc(1, 1, c1))
        sel = 1;
    return sel;
}

int __tp_alt3(int slot0, int g0, Channel *c0, int g1, Channel *c1,
              int g2, Channel *c2)
{
    int sel = -1;

    (void)slot0;
    __alt();
    __enbc(g0 != 0, c0);
    __enbc(g1 != 0, c1);
    __enbc(g2 != 0, c2);
    __altwt();
    if (__disc(0, g0 != 0, c0))
        sel = 0;
    if (__disc(1, g1 != 0, c1))
        sel = 1;
    if (__disc(2, g2 != 0, c2))
        sel = 2;
    return sel;
}

/* Poll: the skip guard makes altwt fall straight through.  It is disabled
 * last, so a ready channel always beats it. */
int ChanSkipAlt(Channel **chans, int n)
{
    if (n <= 0)
        return -1;
    return alt_core(0, chans, 0, n, ALT_MODE_SKIP, 0);
}

/*
 * Wait until a channel is ready or the current-priority clock reaches
 * `time`, whichever is first.  Returns -1 on timeout.
 *
 * `time` is an absolute timer value, not a duration - compare it against
 * Time(), and build it with signed arithmetic so the tick wrap is handled.
 */
int ChanTimerAlt(int time, Channel **chans, int n)
{
    if (n <= 0) {
        __tin(time);
        return -1;
    }
    return alt_core(0, chans, 0, n, ALT_MODE_TIMER, time);
}

/* NULL-terminated wrappers. */

static int chan_count(Channel **chans)
{
    int n = 0;
    if (!chans)
        return 0;
    while (chans[n] && n < ALT_MAX)
        n++;
    return n;
}

int ChanAltList(Channel **chans)
{
    return ChanAlt(chans, chan_count(chans));
}

int ChanSkipAltList(Channel **chans)
{
    return ChanSkipAlt(chans, chan_count(chans));
}

int ChanTimerAltList(int time, Channel **chans)
{
    return ChanTimerAlt(time, chans, chan_count(chans));
}

/* ------------------------------------------------------------------ */
/* Process setup                                                       */
/* ------------------------------------------------------------------ */

/*
 * Lay out a workspace so that `startp` can launch func(p1..pn).
 *
 * A transputer workspace grows down: Wptr points at local 0, arguments sit
 * at increasing offsets from it, and the scheduler's slots (Iptr at -1,
 * Link at -2, ...) sit below.  So the new Wptr goes near the *top* of the
 * buffer the caller supplied, leaving the rest below it for the process's
 * own frames.
 *
 * Slot 0 is where a called function finds its return address; startp fills
 * it, so the parameters begin at slot 1.
 */
typedef int PDes;

/* ------------------------------------------------------------------ */
/* Per-process records: who owns which workspace                       */
/* ------------------------------------------------------------------ */

/*
 * Processes share globals, so anything a process must own - today its VLA
 * arena (transputer-vla.c) - cannot be a global, and no instruction tells a
 * process which one it is: the only thing that is its own is Wptr, which
 * lies inside its workspace.  So every process PSetup starts keeps a record
 * of its workspace range in its own first frame, and the records are kept on
 * a list that is searched by Wptr.
 *
 * There is no atomic instruction to build the list with, so it relies on the
 * scheduler instead: a low-priority process is only descheduled at `j` and
 * `lend` (or when it blocks), and a high-priority one only when it blocks.
 * There is one list per priority, and a process only ever touches the list
 * of its own priority, so a sequence of loads and stores with no j/lend in it
 * is atomic against every process that can touch the same list.  Linking and
 * unlinking are written that way, and so is the search, whose loop closes
 * with a cj.  Were a j ever to appear in it, another process could run
 * there and unlink - and its parent reuse - the record the search stands
 * on; `gen` counts the unlinks, and the search restarts if it moved.
 */
struct __tp_proc {
    struct __tp_arena arena;    /* first, so the arena leads to the record */
    struct __tp_proc *next, *prev;
    char *wlo, *whi;            /* the workspace: wlo <= Wptr < whi */
};

static struct __tp_proc_list {
    struct __tp_proc head;      /* sentinel: the list is circular */
    unsigned gen;
} procs[2] = {
    { { { 0 }, &procs[0].head, &procs[0].head } },     /* PRI_HIGH */
    { { { 0 }, &procs[1].head, &procs[1].head } },     /* PRI_LOW */
};

/* Straight-line: see above. */
static void proc_unlink(struct __tp_proc_list *l, struct __tp_proc *p)
{
    p->prev->next = p->next;
    p->next->prev = p->prev;
    l->gen++;
}

/*
 * The arena in the calling process's record, or 0 if PSetup did not start
 * it.  This is the search, in line rather than a function of its own: VLA
 * code calls it, and every frame costs workspace.
 *
 * Written as a do-while because that compiles with no j or lend at all
 * (the back edge is a cj): the search is then atomic like the straight-line
 * code, which PStop() depends on (see __tp_proc_stop).  The `gen` check is
 * there in case a timeslice point ever does creep in; between it and the
 * last read of *p there must still be none.
 */
struct __tp_arena *__tp_proc_arena(void)
{
    struct __tp_proc_list *l = &procs[__ldpri()];
    char *w = (char *)&l;       /* in this frame, so inside our workspace */
    struct __tp_proc *p, *next, *best;
    char *lo, *hi, *best_lo;
    unsigned gen;

again:
    gen = l->gen;
    best = 0;
    best_lo = 0;
    p = l->head.next;
    if (p != &l->head) {
        do {
            if (l->gen != gen)
                goto again;
            lo = p->wlo;
            hi = p->whi;
            next = p->next;
            /* A process's workspace may lie inside its parent's (a local
             * array), so the innermost range that holds Wptr is ours. */
            if (lo <= w && w < hi && lo > best_lo) {
                best = p;
                best_lo = lo;
            }
            p = next;
        } while (p != &l->head);
    }
    return (struct __tp_arena *)best;   /* arena is first; not ?:, a j */
}

/*
 * Where every process PSetup builds starts.  It is entered by `runp`, not
 * called: PSetup put the parameters where a CALL would have spilled them.
 * Its frame is the top of the process's workspace and holds the process's
 * record for as long as the process runs.
 */
void __tp_proc_start(void (*func)(void), char *ws)
{
    struct __tp_proc me;
    struct __tp_proc_list *l = &procs[__ldpri()];

    me.wlo = ws;
    me.whi = (char *)&func;
    /* The VLA arena is the bottom of the workspace, growing up toward the
     * stack. */
    me.arena.lo = me.arena.top = ws;
    me.arena.hi = me.whi;

    /* Straight-line: see above. */
    me.prev = &l->head;
    me.next = l->head.next;
    l->head.next->prev = &me;
    l->head.next = &me;

    func();

    proc_unlink(l, &me);
    __stopp();
}

/* PStop(): leave the list, then stop.
 *
 * There is no j or lend between the call and the stopp, as there was none
 * when PStop was a bare stopp.  That matters: a parent typically learns
 * that a process is done from its last message, then reuses the
 * workspace, and a timeslice here would let it do so while the process
 * still runs in it. */
void __tp_proc_stop(void)
{
    struct __tp_proc *p = (struct __tp_proc *)__tp_proc_arena();

    if (p)
        proc_unlink(&procs[__ldpri()], p);
    __stopp();
}

/* Use only whole words inside the supplied buffer. The descriptor's low
 * bit carries priority, so the initial Wptr must be word aligned. */
static int *initial_wptr(void *ws, int wsize)
{
    int skip = -(unsigned)ws & (sizeof(int) - 1);
    int *base;

    if (!ws || wsize < skip + 8 * (int)sizeof(int))
        return 0;
    base = (int *)((char *)ws + skip);
    return base + (wsize - skip) / (int)sizeof(int) - 6;
}

/*
 * Build a runnable process for `func` in the caller-supplied workspace and
 * return its descriptor.  PRun schedules it.
 *
 * Two hardware details shape this:
 *
 *   - `startp` cannot be used.  Its entry operand is an offset relative to
 *     the *current* IPtr, not an address, and it schedules the process
 *     immediately - neither of which suits building a descriptor from a
 *     function pointer in C.  Writing the entry address into the Iptr slot
 *     at Wptr[-1] and handing the descriptor to `runp` does both jobs and
 *     keeps setup and scheduling separate.
 *
 *   - `func` must take no arguments.  Under this ABI the first three
 *     parameters arrive in A/B/C and are spilled by the CALL instruction;
 *     a process entered by runp never executes a CALL, so those slots hold
 *     nothing.  Pass context the transputer way instead - over a channel
 *     the parent writes once the process is running:
 *
 *         static Channel ctlchan = NOPROCESS;
 *         static void worker(void) { Ctx *c; ChanIn(&ctlchan, &c, 4); ... }
 *
 *         PRun(PSetup(ws, sizeof ws, worker) | PRI_LOW);
 *         ChanOut(&ctlchan, &ctx, 4);
 *
 * The process does not start in `func` itself but in __tp_proc_start, which
 * records the workspace (see above), calls func and stops the process when
 * func returns.  PSetup hands it func and the workspace in the parameter
 * slots, where a CALL would have spilled them.
 *
 * The workspace must stay live for as long as the process runs.
 */
PDes PSetup(void *ws, int wsize, void (*func)(void))
{
    int *wptr = initial_wptr(ws, wsize);

    if (!wptr || !func)
        return 0;

    wptr[-1] = (int)__tp_proc_start;   /* Iptr_s: where the process starts */
    wptr[1] = (int)func;               /* __tp_proc_start's parameters */
    wptr[2] = (int)ws;

    /* Descriptor: workspace pointer, priority in bit 0 (0 = high). */
    return (PDes)(int)wptr;
}

/* ------------------------------------------------------------------ */
/* Communicate or fail (INMOS technical note 1, TCWG 8.3.1)            */
/* ------------------------------------------------------------------ */

/*
 * The transfer runs in a helper process; the caller ALTs on the helper's
 * completion channel and on a timer or the kill channel.  If the helper
 * wins, the transfer is done.  If the other guard wins, the caller resets
 * the channel the helper is waiting on, and the helper - on no queue and
 * no channel now - is abandoned with its transfer.
 *
 * The helper never ends.  It reports completion by outputting on `done`,
 * which nobody ever inputs: the caller's ALT sees it waiting there and
 * simply resets the word.  Ending it with stopp instead would open a window
 * in which the communication that told the caller had also rescheduled the
 * helper, and the caller had returned (and its caller reused `ws`) before
 * the helper ran its stopp - with its Iptr and queue link in that
 * workspace.  The note closes it with a PAR's endp, which C cannot express.
 * For the same reason the helper is not built by PSetup: a process that
 * never ends would stay on PSetup's record list.
 *
 * The note also requires the transfer to have started before the timeout
 * can be taken; otherwise the reset finds the channel idle and the transfer,
 * starting after it, hangs.  `cancel` and `started` replace that rule.  The
 * helper runs at high priority and tests `cancel` and starts the transfer
 * with no deschedule in between, so no process of either priority can come
 * between the two: the caller sets `cancel` before it resets the channel,
 * and if the reset finds the channel idle, `started` says whether the
 * transfer completed or will never begin.
 */
struct xfail {
    Channel *c;
    void *buf;
    int n;
    int out;
    volatile int cancel;    /* the caller has given up */
    volatile int started;   /* the helper has started the transfer */
    Channel done;           /* where the helper parks */
};

/* Entered by runp, x where a CALL would have spilled it; never returns. */
static void xfail_proc(struct xfail *x)
{
    if (!x->cancel) {
        x->started = 1;
        if (x->out)
            __out(x->n, x->c, x->buf);
        else
            __in(x->n, x->c, x->buf);
    }
    __outword(&x->done, 0);
}

static int xfail(struct xfail *x, int time, Channel *kill, void *ws, int wsize)
{
    int *wptr = initial_wptr(ws, wsize);
    Channel *g[2];
    int sel, v;

    if (!wptr || wsize < PROC_WS_MIN)
        return -1;
    x->cancel = 0;
    x->started = 0;
    x->done = NOPROCESS;

    /* As PSetup lays a workspace out, entering xfail_proc(x). */
    wptr[-1] = (int)xfail_proc;
    wptr[1] = (int)x;
    __runp((int)wptr);                  /* high priority */

    g[0] = &x->done;
    g[1] = kill;
    if (kill)
        sel = ChanAlt(g, 2);
    else
        sel = ChanTimerAlt(time, g, 1);
    if (sel == 0) {
        x->done = NOPROCESS;            /* release the parked helper */
        return 0;
    }
    if (kill)
        __in(4, kill, &v);
    x->cancel = 1;
    if (__resetch(x->c) != NOPROCESS)
        return 1;                       /* the helper went with it */
    /* The channel was idle: the transfer had finished, or it never began
     * and the helper will see `cancel`.  Either way it will park. */
    ChanAlt(g, 1);
    x->done = NOPROCESS;
    return !x->started;
}

int ChanInTimeFail(Channel *c, void *buf, int n, int time, void *ws, int wsize)
{
    struct xfail x;

    x.c = c;
    x.buf = buf;
    x.n = n;
    x.out = 0;
    return xfail(&x, time, 0, ws, wsize);
}

int ChanOutTimeFail(Channel *c, const void *buf, int n, int time, void *ws, int wsize)
{
    struct xfail x;

    x.c = c;
    x.buf = (void *)buf;
    x.n = n;
    x.out = 1;
    return xfail(&x, time, 0, ws, wsize);
}

int ChanInChanFail(Channel *c, void *buf, int n, Channel *kill, void *ws, int wsize)
{
    struct xfail x;

    if (!kill)
        return -1;
    x.c = c;
    x.buf = buf;
    x.n = n;
    x.out = 0;
    return xfail(&x, 0, kill, ws, wsize);
}

int ChanOutChanFail(Channel *c, const void *buf, int n, Channel *kill, void *ws, int wsize)
{
    struct xfail x;

    if (!kill)
        return -1;
    x.c = c;
    x.buf = (void *)buf;
    x.n = n;
    x.out = 1;
    return xfail(&x, 0, kill, ws, wsize);
}
