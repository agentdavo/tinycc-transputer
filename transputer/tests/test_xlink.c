/*
 * test_xlink.c - communicate or fail (INMOS technical note 1) on soft
 * channels: ChanInTimeFail/ChanOutTimeFail, ChanInChanFail/ChanOutChanFail
 * and ChanReinit, called at low priority (test_main) and at high priority.
 *
 * Expected: SXLINK-OK\nH
 *
 * A peer process acts on the channel under test after a delay, or not at
 * all, so each case either completes, times out or is killed.  Every call
 * reuses one helper workspace, which is only safe if the helper is
 * finished with it by the time the call returns.  The cases on links -
 * where a transfer can stop half-way - need two nodes: tests/net/xlink.sh
 * and tests/net/xlink_recover.sh.
 */

#include <conc.h>

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void uart_putc(int c) { UART_DATA = (unsigned int)(unsigned char)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

static int pass = 1;

static void check(const char *name, int ok)
{
    if (ok)
        return;
    uart_puts(name);
    uart_puts(":F ");
    pass = 0;
}

static int hws[PROC_WS_MIN / 4];        /* every call's helper workspace */

static Channel sc = NOPROCESS;          /* the channel under test */
static Channel killc = NOPROCESS;
static Channel ctl = NOPROCESS;         /* main -> peer: a struct cmd * */
static Channel fin = NOPROCESS;         /* peer -> main: op done */

enum { OP_NONE, OP_OUT, OP_IN, OP_KILL, OP_OUT_KILL };

struct cmd {
    int op;
    int delay;          /* low-priority ticks */
    int value;
    int got;
};

/* Low priority.  Always reports on fin once its op is done; an op that
 * cannot complete is never asked for. */
static void peer(void)
{
    struct cmd *p;

    for (;;) {
        ChanIn(&ctl, &p, 4);
        ProcWait(p->delay);
        if (p->op == OP_OUT || p->op == OP_OUT_KILL)
            ChanOutInt(&sc, p->value);
        if (p->op == OP_IN)
            p->got = ChanInInt(&sc);
        if (p->op == OP_KILL || p->op == OP_OUT_KILL)
            ChanOutInt(&killc, 1);
        ChanOutInt(&fin, 0);
    }
}

static struct cmd cmd;

static void ask(int op, int delay, int value)
{
    struct cmd *p = &cmd;

    cmd.op = op;
    cmd.delay = delay;
    cmd.value = value;
    cmd.got = 0;
    ChanOut(&ctl, &p, 4);
}

static void done(void) { (void)ChanInInt(&fin); }

static void nameapp(char *n, const char *s)
{
    while (*n)
        n++;
    *n++ = '-';
    while ((*n++ = *s++) != 0)
        ;
}

/*
 * The cases, with deadlines `t` ticks of the caller's clock ahead and the
 * peer acting `d` low-priority ticks in (d well inside t).  `tag` prefixes
 * the failure names: L for the low-priority caller, H for the high one.
 */
static void cases(const char *tag, int t, int d)
{
    int w, r, start;
    char name[16];
    int i;

#define NAME(s) (name[0] = tag[0], name[1] = 0, \
                 nameapp(name, s), name)

    /* A: nobody sends - the input times out and the channel is idle */
    start = Time();
    w = 0;
    r = ChanInTimeFail(&sc, &w, 4, start + t, hws, sizeof hws);
    check(NAME("in-timeout"), r == 1);
    check(NAME("in-late"), !TimeAfter(start + t, Time()));
    check(NAME("in-reset"), sc == NOPROCESS && w == 0);

    /* B: the peer sends in time */
    ask(OP_OUT, d, 0x12345678);
    r = ChanInTimeFail(&sc, &w, 4, Time() + t, hws, sizeof hws);
    done();
    check(NAME("in-ok"), r == 0 && w == 0x12345678);

    /* C: nobody receives - the output times out */
    w = 0x0BADF00D;
    r = ChanOutTimeFail(&sc, &w, 4, Time() + t, hws, sizeof hws);
    check(NAME("out-timeout"), r == 1 && sc == NOPROCESS);

    /* D: the peer receives in time */
    ask(OP_IN, d, 0);
    w = 0x600DF00D;
    r = ChanOutTimeFail(&sc, &w, 4, Time() + t, hws, sizeof hws);
    done();
    check(NAME("out-ok"), r == 0 && cmd.got == 0x600DF00D);

    /* E: nobody sends, the peer kills the input; the kill is taken */
    ask(OP_KILL, d, 0);
    r = ChanInChanFail(&sc, &w, 4, &killc, hws, sizeof hws);
    done();
    check(NAME("in-killed"), r == 1 && sc == NOPROCESS);

    /* F: the message beats the kill, which is then still waiting */
    ask(OP_OUT_KILL, d, 0x00C0FFEE);
    r = ChanInChanFail(&sc, &w, 4, &killc, hws, sizeof hws);
    check(NAME("in-beats-kill"), r == 0 && w == 0x00C0FFEE);
    while (killc == NOPROCESS)          /* the peer gets to its kill */
        ProcWait(1);
    (void)ChanInInt(&killc);
    done();

    /* G: nobody receives, the output is killed */
    ask(OP_KILL, d, 0);
    r = ChanOutChanFail(&sc, &w, 4, &killc, hws, sizeof hws);
    done();
    check(NAME("out-killed"), r == 1 && sc == NOPROCESS);

    /* H: a deadline already past - nothing is transferred, and the
     * channel still works afterwards */
    r = ChanInTimeFail(&sc, &w, 4, Time() - 1, hws, sizeof hws);
    check(NAME("in-past"), r == 1 && sc == NOPROCESS);
    r = ChanOutTimeFail(&sc, &w, 4, Time() - 1, hws, sizeof hws);
    check(NAME("out-past"), r == 1 && sc == NOPROCESS);
    ask(OP_OUT, d, 77);
    r = ChanInTimeFail(&sc, &w, 4, Time() + t, hws, sizeof hws);
    done();
    check(NAME("after-past"), r == 0 && w == 77);

    /* I: too small a workspace */
    check(NAME("small-ws"), ChanInTimeFail(&sc, &w, 4, Time() + t, hws,
                                           PROC_WS_MIN - 4) == -1);

    /* J: the workspace reused, timeouts and transfers in turn */
    for (i = 0; i < 12; i++) {
        if (i & 1) {
            ask(OP_OUT, 1, i);
            r = ChanInTimeFail(&sc, &w, 4, Time() + t, hws, sizeof hws);
            done();
            if (r != 0 || w != i)
                break;
        } else if (ChanInTimeFail(&sc, &w, 4, Time() + t / 4, hws, sizeof hws) != 1)
            break;
    }
    check(NAME("reuse"), i == 12 && sc == NOPROCESS);
#undef NAME
}

static int peer_ws[2 * PROC_WS_MIN / 4];
static int hi_ws[4 * PROC_WS_MIN / 4];
static Channel hi_fin = NOPROCESS;

/* High priority: 1us ticks, the peer still at low priority. */
static void hi_caller(void)
{
    cases("H", 20000, 20);
    ChanOutInt(&hi_fin, 0);
}

/* ChanReinit on a channel in use: this process is left waiting on it. */
static int stuck_ws[PROC_WS_MIN / 4];
static Channel stuck = NOPROCESS;
static void stuck_in(void) { (void)ChanInInt(&stuck); }

void test_main(void)
{
    int w;

    PRun(PSetup(peer_ws, sizeof peer_ws, peer) | PRI_LOW);

    cases("L", 300, 20);

    PRun(PSetup(hi_ws, sizeof hi_ws, hi_caller));
    (void)ChanInInt(&hi_fin);

    /* ChanReinit: no error on an idle channel ... */
    (void)__testerr();
    w = ChanReinit(&sc);
    check("reinit-idle", w == NOPROCESS && __testerr());
    /* ... the Error flag on one in use */
    PRun(PSetup(stuck_ws, sizeof stuck_ws, stuck_in) | PRI_LOW);
    while (stuck == NOPROCESS)
        ProcWait(1);
    w = ChanReinit(&stuck);
    check("reinit-busy", w != NOPROCESS && !__testerr() && stuck == NOPROCESS);

    uart_puts(pass ? "XLINK-OK\n" : "\nXLINK-FAIL\n");
}
