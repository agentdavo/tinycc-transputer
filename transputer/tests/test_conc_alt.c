/* conc.h: ALT multiplexing over channels, and process start via PSetup/PRun.
 *
 * ALT is the reason the header exists - a process serving several links
 * cannot poll them, because a blocked `in` on the wrong link deadlocks the
 * pair.  What is checked here:
 *
 *   1. ChanSkipAlt returns -1 when nothing is ready (it must not block).
 *   2. ChanAlt blocks until a writer appears and names the right channel.
 *   3. Arbitration order: with both channels ready, the lower index wins.
 *   4. ChanTimerAlt times out rather than blocking forever.
 *   5. PSetup/PRun actually schedule a process that reaches its code.
 *
 * Soft channels stand in for links: the instructions are the same ones
 * (in/out/enbc/disc), and a link needs a second node to exercise.
 */

#include <conc.h>

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void uart_putc(int c) { UART_DATA = (unsigned int)(unsigned char)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

static int pass = 1;

static void check(const char *name, int ok)
{
    if (!ok) {
        pass = 0;
        uart_puts("FAIL:");
        uart_puts(name);
        uart_putc('\n');
    }
}

static Channel ca = NOPROCESS;
static Channel cb = NOPROCESS;
static Channel started = NOPROCESS;

static int wsa[128];
static int wsb[128];

/* Each writer blocks in ChanOut until the ALT selects it. */
static void writer_a(void) { int v = 0xA1; ChanOut(&ca, &v, 4); PStop(); }
static void writer_b(void) { int v = 0xB2; ChanOut(&cb, &v, 4); PStop(); }

/* Proves PSetup/PRun reached the process body at all. */
static void announcer(void) { int v = 1; ChanOut(&started, &v, 4); PStop(); }

void test_main(void)
{
    Channel *set[2];
    int got, sel, t0;
    static int wsc[128];

    set[0] = &ca;
    set[1] = &cb;

    /* 1. Nothing ready: must return immediately, not block. */
    check("skip-empty", ChanSkipAlt(set, 2) == -1);

    /* 5. A process started by PSetup/PRun runs. */
    PRun(PSetup(wsc, sizeof wsc, announcer) | PRI_LOW);
    ChanIn(&started, &got, 4);
    check("psetup-runs", got == 1);

    /* 4. Timer guard fires when no channel becomes ready. */
    t0 = Time();
    check("timeralt-timeout", ChanTimerAlt(t0 + 100, set, 2) == -1);
    check("timeralt-waited", TimeAfter(Time(), t0));

    /* 2. Blocking select names the channel that became ready. */
    PRun(PSetup(wsb, sizeof wsb, writer_b) | PRI_LOW);
    sel = ChanAlt(set, 2);
    check("alt-selects-b", sel == 1);
    ChanIn(set[sel], &got, 4);
    check("alt-value-b", got == 0xB2);

    /* 3. Both ready: the lower index wins, because disc only fires while
     *    Temp still reads NoneSelected. */
    PRun(PSetup(wsa, sizeof wsa, writer_a) | PRI_LOW);
    PRun(PSetup(wsb, sizeof wsb, writer_b) | PRI_LOW);
    ProcWait(200);                     /* let both reach their ChanOut */
    check("alt-priority", ChanSkipAlt(set, 2) == 0);
    ChanIn(&ca, &got, 4);
    check("alt-value-a", got == 0xA1);
    sel = ChanAlt(set, 2);
    check("alt-second", sel == 1);
    ChanIn(&cb, &got, 4);
    check("alt-value-b2", got == 0xB2);

    uart_puts(pass ? "\nALL-PASS\n" : "\nFAILED\n");
}
