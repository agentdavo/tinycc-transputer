/* xlink_recover.c - the recovery protocol of INMOS technical note 1's
 * example 2, on a byte-stream link that is "unplugged" for a while, through
 * <conc.h>.  Both nodes run the same protocol on Link1; Link2 only carries
 * node 1's figures to node 0 at the end.
 * Expected (node 0): XREC OK
 *
 * The protocol, at each end:
 *
 *   - First synchronisation: a timed input in parallel with a timed output
 *     of SYNC(0), retried until both complete.  The local time then is T0,
 *     the origin of a cycle of three PHASE_MS phases that both ends now
 *     share (their clocks differ by the time the exchange took to finish).
 *   - Running: a sender process outputs a tick every TICK_MS, as an output
 *     the manager can kill.  The manager inputs with a SILENCE_MS deadline;
 *     one second without input is a failure.
 *   - On failure: kill the sender and fall silent.  Then, for each cycle k
 *     from the next one on: at its start reinitialise both link channels;
 *     at the second phase, a timed input in parallel with a timed output of
 *     SYNC(k), both by the end of that phase; the third phase is silence.
 *     When both complete, restart the sender.
 *
 * A phase is longer than the ends' clocks differ, so each end's reset falls
 * in the other's silence and the exchanges overlap.  Inputs ignore what
 * they did not expect - a tick, or an old SYNC - and keep listening: after
 * a failure the link can still hold the start of a message sent before
 * it, until the input is reinitialised (on t4 a whole packet: see
 * tests/net/xlink.c, case 12).
 *
 * The fault: from UNPLUG_MS to REPLUG_MS after T0, node 0 sends a parity
 * error down the cable (WrongParity) whenever its end of the link is
 * running, which is within 20 ms of every reinitialise.  Both ends' links
 * then halt (HRM 13.5).  So every recovery attempt in that window fails
 * and the first one after it succeeds.  Each node checks that ticks flowed
 * before the fault, that it saw exactly one failure, that it tried more
 * than once, that it recovered only after the replug, and that ticks flowed
 * again, in order, afterwards.
 *
 * xlink_recover.sh runs the two nodes as one threaded t4 network. */
#include <conc.h>
#include <t9000.h>

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c) { UART_DATA = (unsigned)(unsigned char)c; }
static void us(const char *s) { while (*s) up(*s++); }
static void ud(int v)
{
    char b[12];
    int i = 0;

    if (v < 0) {
        up('-');
        v = -v;
    }
    do
        b[i++] = (char)('0' + v % 10);
    while ((v /= 10) != 0);
    while (i)
        up(b[--i]);
}

#define MS(n)           ((n) * 125 / 8)         /* low-priority ticks: 64us */
#define TICK_MS         250
#define SILENCE_MS      1000
#define PHASE_MS        200
#define CYCLE           (3 * MS(PHASE_MS))
#define UNPLUG_MS       1500
#define REPLUG_MS       4000
#define END_MS          7000

#define TICK(n)         (0x71C00000 | ((n) & 0xFFFF))
#define SYNC(k)         (0x5C000000 | ((k) & 0xFFFF))
#define KIND(w)         ((w) & 0xFFFF0000)

static int hws[PROC_WS_MIN / 4];        /* the manager's TimeFail helper */
static int hws2[PROC_WS_MIN / 4];       /* the sender's or the SYNC output's */
static int T0, end;

/* What each node finds; node 1 sends its copy to node 0 at the end. */
static struct stats {
    int before;         /* ticks received before the failure */
    int after;          /* ticks received after recovery */
    int disorder;       /* ticks not after the one before */
    int failures;
    int attempts;       /* recovery cycles tried */
    int recovered_ms;   /* when, after T0 */
    int ignored;        /* unexpected words dropped */
    int reinit_err;     /* a reinitialise found a channel in use */
    int faults;         /* node 0: parity errors sent */
} st;

/* ---- the sender: a tick every TICK_MS until killed ---------------- */

static Channel skill = NOPROCESS, sfin = NOPROCESS;
static int sws[4 * PROC_WS_MIN / 4];
static int tseq;

static void sender(void)
{
    int next = Time(), w;
    Channel *g = &skill;

    for (;;) {
        w = TICK(tseq);
        if (ChanOutChanFail(LINK1OUT, &w, 4, &skill, hws2, sizeof hws2) != 0)
            break;
        tseq++;
        next += MS(TICK_MS);
        if (ChanTimerAlt(next, &g, 1) == 0) {
            (void)ChanInInt(&skill);
            break;
        }
    }
    ChanOutInt(&sfin, 0);
}

static void stop_sender(void)
{
    ChanOutInt(&skill, 1);
    (void)ChanInInt(&sfin);
}

/* ---- SYNC(k): a timed input in parallel with a timed output ------- */

static Channel ores = NOPROCESS;
static int ows[4 * PROC_WS_MIN / 4];
static int oword, odeadline;

static void sync_out(void)
{
    int r = ChanOutTimeFail(LINK1OUT, &oword, 4, odeadline, hws2, sizeof hws2);
    ChanOutInt(&ores, r);
}

/* 0 if both directions completed by `deadline`. */
static int sync_exchange(int k, int deadline)
{
    int m, in_fail;

    oword = SYNC(k);
    odeadline = deadline;
    PRun(PSetup(ows, sizeof ows, sync_out) | PRI_LOW);
    for (;;) {
        in_fail = ChanInTimeFail(LINK1IN, &m, 4, deadline, hws, sizeof hws);
        if (in_fail || m == SYNC(k))
            break;
        st.ignored++;                   /* left over from before */
    }
    return in_fail | ChanInInt(&ores);
}

/* ---- the fault ----------------------------------------------------- */

static Channel ifin = NOPROCESS;
static int iws[2 * PROC_WS_MIN / 4];

static void injector(void)
{
    ProcAfter(T0 + MS(UNPLUG_MS));
    while (TimeAfter(T0 + MS(REPLUG_MS), Time())) {
        if (__ldconf(T9_CR_LINKSTATUS(1)) & T9_LS_LINKSTARTED) {
            __stconf(T9_CR_LINKCOMMAND(1), T9_LC_WRONGPARITY);
            st.faults++;
        }
        ProcWait(MS(20));
    }
    ChanOutInt(&ifin, 0);
}

/* ---- the manager ---------------------------------------------------- */

/* Input ticks until SILENCE_MS pass without one (return 1) or the test
 * ends (return 0). */
static int run(int *count)
{
    int m, dl, last = -1;

    tseq = 0;       /* not reset by the far end: each run starts afresh */
    PRun(PSetup(sws, sizeof sws, sender) | PRI_LOW);
    for (;;) {
        dl = Time() + MS(SILENCE_MS);
        if (TimeAfter(dl, end))
            dl = end;
        if (ChanInTimeFail(LINK1IN, &m, 4, dl, hws, sizeof hws)) {
            stop_sender();
            return TimeAfter(end, Time());
        }
        if (KIND(m) != KIND(TICK(0))) {
            st.ignored++;
            continue;
        }
        if ((m & 0xFFFF) <= last)
            st.disorder++;
        last = m & 0xFFFF;
        (*count)++;
    }
}

/* From the next cycle on, until the exchange succeeds: the cycle's index,
 * or -1 if the test ended first. */
static int recover(void)
{
    int k, a;

    for (k = (Time() - T0) / CYCLE + 1; ; k++) {
        a = T0 + k * CYCLE;
        if (TimeAfter(a, end))
            return -1;
        ProcAfter(a);                           /* silent until here */
        (void)__testerr();
        ChanReinit(LINK1IN);
        ChanReinit(LINK1OUT);
        if (!__testerr())
            st.reinit_err++;
        ProcAfter(a + MS(PHASE_MS));            /* the other end has reset */
        st.attempts++;
        if (sync_exchange(k, a + 2 * MS(PHASE_MS)) == 0)
            return k;
    }
}

static void manage(void)
{
    int k;

    __stconf(T9_CR_LINKMODE(1), T9_LM_LOCALIZEERROR);
    while (sync_exchange(0, Time() + MS(500)) != 0)
        ;
    T0 = Time();
    end = T0 + MS(END_MS);
#if NODE == 0
    PRun(PSetup(iws, sizeof iws, injector) | PRI_LOW);
#endif
    if (!run(&st.before))
        return;                                 /* no failure at all */
    st.failures++;
    k = recover();
    if (k < 0)
        return;
    st.recovered_ms = (Time() - T0) * 8 / 125;
    if (run(&st.after))
        st.failures++;
}

#if NODE == 1

void test_main(void)
{
    manage();
    ChanOut(LINK2OUT, &st, sizeof st);
    __stopp();          /* no 'H' from the boot stub in node 0's report */
}

#else

static int bad;

static void check(const char *what, int ok)
{
    if (ok)
        return;
    bad++;
    us(what);
    us(":F ");
}

static void show(const char *who, struct stats *s)
{
    us(who);
    us(": before=");    ud(s->before);
    us(" failures=");   ud(s->failures);
    us(" attempts=");   ud(s->attempts);
    us(" recovered=");  ud(s->recovered_ms);
    us("ms after=");    ud(s->after);
    us(" disorder=");   ud(s->disorder);
    us(" ignored=");    ud(s->ignored);
    us(" reinit_err="); ud(s->reinit_err);
    us(" faults=");     ud(s->faults);
    up('\n');
}

static void verify(const char *who, struct stats *s)
{
    us(who);
    us(" ");
    check("ticks-before", s->before >= 4);
    check("one-failure", s->failures == 1);
    check("retried", s->attempts >= 2);
    check("recovered-after-replug", s->recovered_ms > REPLUG_MS);
    check("ticks-after", s->after >= 6);
    check("in-order", s->disorder == 0);
    check("reinit-idle", s->reinit_err == 0);
}

void test_main(void)
{
    struct stats s1;

    manage();
    (void)ChanInInt(&ifin);
    ChanIn(LINK2IN, &s1, sizeof s1);
    up('\n');
    show("n0", &st);
    show("n1", &s1);
    verify("n0", &st);
    verify("n1", &s1);
    check("faults", st.faults >= 2);
    us(bad ? "\nXREC FAIL\n" : "\nXREC OK\n");
}

#endif
