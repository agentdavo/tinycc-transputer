/* xlink.c - communicate or fail on a byte-stream (T800-style) link between
 * two nodes: INMOS technical note 1, "Extraordinary use of transputer
 * links", through <conc.h>.
 * Expected (node 0): XLINK OK
 *
 * Link1 is the link under test.  Link2 is a second cable the nodes use to
 * step through the cases together and for node 1 to report what it saw.
 * Both ends set Link1's LocalizeError (an "unreliable" link, HRM 13.5.2),
 * so a link error just resets the link.
 *
 *  1. Transfers that complete: a timed output and input each way.
 *  2. A timed-out input at both ends: nobody sends.
 *  3. A timed-out output: node 1 does not input until node 0 has given up;
 *     it then gets part of the message at most, and times out as well.
 *  4. A killed input at both ends, 5. a killed output, 6. a message that
 *     beats the kill, which is then left waiting.
 *  7. Resynchronise: a quiet spell, both channels reinitialised at both
 *     ends (no Error flag: they were idle), another quiet spell, then a
 *     timed input in parallel with a timed output at each end - the link
 *     carries data again.  Repeated after each of the cases below.
 *  8. An ALT with a timer guard cannot rescue a stuck input: node 1 sends
 *     4 bytes of an 8-byte message (all that a link failing mid-message
 *     would have delivered), node 0's ALT selects the channel, and the
 *     input is then stuck past the ALT's own deadline.
 *  9. A one-sided failure: node 1 resets its link engine (ResetLink) once
 *     its message has reached node 0 but before the acknowledge is back,
 *     so node 0's input completes and node 1's output fails.  (t4 does not
 *     model the disconnect a link reset gives the far end, HRM 13.7.2,
 *     which makes this the loss of the final acknowledge alone.)  Needs a
 *     t4 whose link outputs wait for the acknowledge.
 * 10. A link error: node 1 sends a parity error down the cable (WrongParity)
 *     while node 0 waits.  Node 0's link records a parity error, halts, and
 *     halts its output, which node 1 sees as a disconnect.  Both ends' inputs
 *     time out; reinitialising restarts the links.
 * 11. The cable pulled at node 1's end (ResetOutput): a disconnect error at
 *     node 0.
 * 12. Reinitialising an input empties the link (HRM 13.7.2, TCWG 8.3: it
 *     resets the link hardware): node 1's timed output gives up while node
 *     0 is not inputting, node 0 then reinitialises its input, and a timed
 *     input there gets nothing.  (t4 used to keep the packet that had
 *     arrived, and the input took it.)
 * 13. A halted link carries nothing: node 0 resets its own link engine,
 *     then its timed output fails and node 1's timed input gets nothing.
 *     (t4 used to let the output's first packet through.)
 *
 * The aborted outputs of cases 3 and 5 are partly taken by the far end's
 * input: what had reached its link before the reset (on t4 the first
 * packet) is still there, as nobody reinitialised that end.
 *
 * xlink.sh runs the two nodes as one threaded t4 network. */
#include <conc.h>
#include <t9000.h>

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c) { UART_DATA = (unsigned)(unsigned char)c; }
static void us(const char *s) { while (*s) up(*s++); }
static void uh(unsigned v)
{
    int i;
    for (i = 28; i >= 0; i -= 4)
        up("0123456789abcdef"[(v >> i) & 15]);
}

#define MS(n)   ((n) * 125 / 8)         /* low-priority ticks are 64us */
#define BIG     2048                    /* more than one t4 link packet */
#define NSTEP   13

static int hws[PROC_WS_MIN / 4];        /* the TimeFail helpers' workspaces */
static int hws2[PROC_WS_MIN / 4];
static unsigned char big[BIG], rbuf[BIG];

/* what node 1 reports after each step */
static struct { int r[3]; int w[3]; } rep;

static int bad;

static void check(const char *what, int ok)
{
    if (ok)
        return;
    bad++;
    us(what);
    us(":F ");
}

static int tin(void *buf, int n, int ms)
{
    return ChanInTimeFail(LINK1IN, buf, n, Time() + MS(ms), hws, sizeof hws);
}

static int tout(const void *buf, int n, int ms)
{
    return ChanOutTimeFail(LINK1OUT, buf, n, Time() + MS(ms), hws, sizeof hws);
}

/* A process that sends on `killc` after a while. */
static Channel killc = NOPROCESS, kfin = NOPROCESS;
static int kdelay;
static int kws[2 * PROC_WS_MIN / 4];

static void killer(void)
{
    ProcWait(kdelay);
    ChanOutInt(&killc, 1);
    ChanOutInt(&kfin, 0);
}

static void start_killer(int ms)
{
    kdelay = MS(ms);
    PRun(PSetup(kws, sizeof kws, killer) | PRI_LOW);
}

/* A timed output in a process of its own, reporting on xres. */
static Channel xres = NOPROCESS;
static int xword, xms;
static int xws[4 * PROC_WS_MIN / 4];

static void xout(void)
{
    int r = ChanOutTimeFail(LINK1OUT, &xword, 4, Time() + MS(xms), hws2, sizeof hws2);
    ChanOutInt(&xres, r);
}

static void start_xout(int w, int ms)
{
    xword = w;
    xms = ms;
    PRun(PSetup(xws, sizeof xws, xout) | PRI_LOW);
}

/* A process that reads Link1Status after a while, reporting on sfin. */
static Channel sfin = NOPROCESS;
static int sdelay;
static int sws[2 * PROC_WS_MIN / 4];

static void sampler(void)
{
    ProcWait(sdelay);
    ChanOutInt(&sfin, (int)__ldconf(T9_CR_LINKSTATUS(1)));
}

static void start_sampler(int ms)
{
    sdelay = MS(ms);
    PRun(PSetup(sws, sizeof sws, sampler) | PRI_LOW);
}

/* A timed input in parallel with a timed output: 0 if both completed. */
static int exchange(int send, int *got, int ms)
{
    int ri;

    start_xout(send, ms);
    ri = tin(got, 4, ms);
    return ri | ChanInInt(&xres) << 1;
}

/* Both ends together - they meet on Link2 first, once each has finished
 * with Link1: stop, reinitialise both channels once the other end has
 * stopped too, and exchange a word once it has reinitialised.  0 if the
 * link carries data again; 4 if the Error flag was set (a channel was
 * still in use).  rs_status: Link1Status after the reinitialise. */
static unsigned rs_status;

static int resync(int send, int *got)
{
    int clear;

#if NODE == 0
    ChanOutInt(LINK2OUT, 0x5157);
#else
    (void)ChanInInt(LINK2IN);
#endif
    ProcWait(MS(100));
    (void)__testerr();
    ChanReinit(LINK1IN);
    ChanReinit(LINK1OUT);
    clear = __testerr();
    rs_status = __ldconf(T9_CR_LINKSTATUS(1));
    ProcWait(MS(100));
    return exchange(send, got, 500) | !clear << 2;
}

static int prefix(void)
{
    int i;

    for (i = 0; i < BIG && rbuf[i] == big[i]; i++)
        ;
    return i;
}

static void setup(void)
{
    int i;

    for (i = 0; i < BIG; i++)
        big[i] = (unsigned char)(i * 7 + 1);
    __stconf(T9_CR_LINKMODE(1), T9_LM_LOCALIZEERROR);
    (void)__testerr();
}

#define LS_MASK (T9_LS_LINKERROR | T9_LS_LINKSTARTED | T9_LS_PARITYERROR | T9_LS_DISCERROR)

#if NODE == 1

static void step(int s)
{
    int v, i;

    for (i = 0; i < 3; i++)
        rep.r[i] = rep.w[i] = -1;
    switch (s) {
    case 1:
        v = 0x11110001;
        rep.r[0] = tout(&v, 4, 500);
        rep.r[1] = tin(&rep.w[1], 4, 500);
        break;
    case 2:
        rep.r[0] = tin(&v, 4, 200);
        break;
    case 3:
    case 5:
        ProcWait(MS(s == 3 ? 400 : 300));      /* node 0 has given up */
        for (i = 0; i < BIG; i++)
            rbuf[i] = 0;
        rep.r[0] = tin(rbuf, BIG, 200);
        rep.w[0] = prefix();
        break;
    case 4:
        start_killer(150);
        rep.r[0] = ChanInChanFail(LINK1IN, &v, 4, &killc, hws, sizeof hws);
        (void)ChanInInt(&kfin);
        break;
    case 6:
        v = 0x33330003;
        rep.r[0] = tout(&v, 4, 500);
        break;
    case 8:
        v = 0xA1B2C3D4;
        rep.r[0] = tout(&v, 4, 500);
        break;
    case 9:
        start_xout(0x55550005, 400);
        ProcWait(MS(50));
        __stconf(T9_CR_LINKCOMMAND(1), T9_LC_RESETLINK);
        rep.r[0] = ChanInInt(&xres);
        break;
    case 10:
    case 11:
        ProcWait(MS(100));
        __stconf(T9_CR_LINKCOMMAND(1),
                 s == 10 ? T9_LC_WRONGPARITY : T9_LC_RESETOUTPUT);
        /* before an input's reset restarts the link and clears the bits */
        ProcWait(MS(100));
        rep.w[0] = __ldconf(T9_CR_LINKSTATUS(1));
        rep.r[0] = tin(&v, 4, 200);
        break;
    case 12:
        v = 0x12120012;
        rep.r[0] = tout(&v, 4, 200);           /* node 0 is not inputting */
        break;
    case 13:
        rep.w[0] = 0;
        rep.r[0] = tin(&rep.w[0], 4, 400);
        break;
    }
    if (s >= 7) {
        rep.r[2] = resync(0x77000000 + s, &rep.w[2]);
        rep.w[1] = rs_status;
    }
}

void test_main(void)
{
    int s;

    setup();
    while ((s = ChanInInt(LINK2IN)) != 0) {
        step(s);
        ChanOut(LINK2OUT, &rep, sizeof rep);
    }
    __stopp();          /* no 'H' from the boot stub in node 0's report */
}

#else

static void step(int s)
{
    int r = -1, v, w = 0, t, el = 0, sel, i, pend;
    int b8[2];
    unsigned st = 0;
    Channel *g;

    switch (s) {
    case 1:
        r = tin(&w, 4, 500);
        check("1-in", r == 0 && w == 0x11110001);
        v = 0x22220002;
        r = tout(&v, 4, 500);
        check("1-out", r == 0);
        break;
    case 2:
        t = Time();
        r = tin(&w, 4, 200);
        el = Time() - t;
        check("2-in-timeout", r == 1 && el >= MS(200));
        break;
    case 3:
        t = Time();
        r = tout(big, BIG, 200);
        el = Time() - t;
        check("3-out-timeout", r == 1 && el >= MS(200));
        break;
    case 4:
        start_killer(150);
        t = Time();
        r = ChanInChanFail(LINK1IN, &w, 4, &killc, hws, sizeof hws);
        el = Time() - t;
        (void)ChanInInt(&kfin);         /* the kill was taken */
        check("4-in-killed", r == 1 && el >= MS(150));
        break;
    case 5:
        start_killer(150);
        r = ChanOutChanFail(LINK1OUT, big, BIG, &killc, hws, sizeof hws);
        (void)ChanInInt(&kfin);
        check("5-out-killed", r == 1);
        break;
    case 6:
        start_killer(300);
        r = ChanInChanFail(LINK1IN, &w, 4, &killc, hws, sizeof hws);
        for (i = 0; i < 60 && killc == NOPROCESS; i++)
            ProcWait(MS(10));
        pend = killc != NOPROCESS;      /* the killer still waits to send */
        if (pend)
            (void)ChanInInt(&killc);
        (void)ChanInInt(&kfin);
        check("6-beats-kill", r == 0 && w == 0x33330003);
        check("6-kill-left", pend);
        break;
    case 8:
        t = Time() + MS(300);
        g = LINK1IN;
        sel = ChanTimerAlt(t, &g, 1);
        b8[0] = b8[1] = 0x5A5A5A5A;
        r = ChanInTimeFail(LINK1IN, b8, 8, t, hws, sizeof hws);
        check("8-alt-selects", sel == 0);
        check("8-in-stuck", r == 1 && !TimeAfter(t, Time()));
        check("8-got-part", b8[0] == (int)0xA1B2C3D4 && b8[1] == 0x5A5A5A5A);
        break;
    case 9:
        ProcWait(MS(150));
        r = tin(&w, 4, 300);
        check("9-in-done", r == 0 && w == 0x55550005);
        break;
    case 10:
    case 11:
        /* the status while the input waits: its reset will clear it */
        start_sampler(250);
        r = tin(&w, 4, 400);
        st = ChanInInt(&sfin) & LS_MASK;
        check(s == 10 ? "10-in" : "11-in", r == 1);
        check(s == 10 ? "10-parity" : "11-disc", st == (T9_LS_LINKERROR |
              (s == 10 ? T9_LS_PARITYERROR : T9_LS_DISCERROR)));
        break;
    case 12:
        ProcWait(MS(400));                      /* node 1 has given up */
        ChanReinit(LINK1IN);
        w = 0;
        r = tin(&w, 4, 200);
        check("12-in-emptied", r == 1 && w == 0);
        break;
    case 13:
        __stconf(T9_CR_LINKCOMMAND(1), T9_LC_RESETLINK);
        v = 0x13130013;
        r = tout(&v, 4, 200);
        check("13-out-halted", r == 1);
        break;
    }
    if (s >= 7) {
        w = 0;
        r = resync(0x70000000 + s, &w);
        check("resync", r == 0 && w == 0x77000000 + s);
        check("restarted", (rs_status & LS_MASK) == T9_LS_LINKSTARTED);
    }

    ChanIn(LINK2IN, &rep, sizeof rep);
    switch (s) {
    case 1:
        check("1-n1-out", rep.r[0] == 0);
        check("1-n1-in", rep.r[1] == 0 && rep.w[1] == 0x22220002);
        break;
    case 2:
        check("2-n1-in-timeout", rep.r[0] == 1);
        break;
    case 3:
    case 5:
        /* at most the part that reached the link before the reset */
        check(s == 3 ? "3-n1-partial" : "5-n1-partial",
              rep.r[0] == 1 && rep.w[0] < BIG);
        break;
    case 4:
        check("4-n1-in-killed", rep.r[0] == 1);
        break;
    case 6:
        check("6-n1-out", rep.r[0] == 0);
        break;
    case 8:
        check("8-n1-out", rep.r[0] == 0);
        break;
    case 9:
        check("9-n1-out-fails", rep.r[0] == 1);
        break;
    case 10:
        check("10-n1-in", rep.r[0] == 1);
        check("10-n1-disc", (rep.w[0] & LS_MASK) == (T9_LS_LINKERROR | T9_LS_DISCERROR));
        break;
    case 11:
        check("11-n1-in", rep.r[0] == 1);
        check("11-n1-stopped", (rep.w[0] & (T9_LS_LINKSTARTED | T9_LS_RESETOUTPUT)) ==
              T9_LS_RESETOUTPUT);
        break;
    case 12:
        check("12-n1-out-fails", rep.r[0] == 1);
        break;
    case 13:
        check("13-n1-in-nothing", rep.r[0] == 1 && rep.w[0] == 0);
        break;
    }
    if (s >= 7) {
        check("n1-resync", rep.r[2] == 0 && rep.w[2] == 0x70000000 + s);
        check("n1-restarted", (rep.w[1] & LS_MASK) == T9_LS_LINKSTARTED);
    }
    if (bad) {
        us("(step ");
        uh(s);
        us(") ");
    }
}

void test_main(void)
{
    int s, failed = 0;

    setup();
    for (s = 1; s <= NSTEP; s++) {
        ChanOutInt(LINK2OUT, s);
        step(s);
        if (bad)
            failed = 1;
        bad = 0;
    }
    ChanOutInt(LINK2OUT, 0);
    us(failed ? "\nXLINK FAIL\n" : "\nXLINK OK\n");
}

#endif
