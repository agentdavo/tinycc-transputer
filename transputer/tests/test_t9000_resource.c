/*
 * Target: -mt9000
 * Expected: SRES-OK\nH
 *
 * test_t9000_resource.c - the T9000's semaphores (wait/signal, ISM 8.6) and
 * resources (mkrc/grant/unmkrc and friends, ISM 8.8) from C, through
 * <t9000.h>, on one node.
 *
 *  1. Mutual exclusion: three low-priority processes take a semaphore,
 *     deschedule (a timer wait) inside the critical section, and count how
 *     often another one got in.
 *  2. A counting semaphore between a producer and a consumer.
 *  3. A resource with user-marked channels: three users mark their own
 *     channel before every request; one server grants, inputs the request
 *     and answers on the granted channel's reply word.
 *  4. Server-marked channels: users output on plain channels first, then the
 *     server marks them (the queued path of mkrc) and re-marks after each.
 *  5. An ALT over a plain channel and a resource (enbg/disg): each ready on
 *     its own, then both, where the channel has priority.
 *  6. unmkrc, ldresptr/stresptr, and erdsq/irdsq moving a queue of claims.
 */
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

static int bad;
static void fail(const char *what, unsigned got, unsigned want)
{
    bad++;
    us(what); us(" got="); uh(got); us(" want="); uh(want); up('\n');
}
#define CHECK(what, got, want) \
    do { if ((unsigned)(got) != (unsigned)(want)) fail(what, got, want); } while (0)

#define NOTPROC 0x80000000u

/* completion: any number of processes may signal one semaphore, where a
 * channel holds only one waiting outputter */
static Semaphore finished = SEMAPHOREINIT(0);
static int ws[4][384];

/* ---- 1. mutual exclusion ------------------------------------------------ */
#define MX_ITERS 40
static Semaphore mutex = SEMAPHOREINIT(1);
static int in_cs, overlaps, total;

static void mx_run(void)
{
    int i;
    for (i = 0; i < MX_ITERS; i++) {
        SemWait(&mutex);
        if (++in_cs != 1)
            overlaps++;
        ProcWait(2);                /* deschedule inside the section */
        total++;
        in_cs--;
        SemSignal(&mutex);
    }
    SemSignal(&finished);
}
static void mx0(void) { mx_run(); PStop(); }
static void mx1(void) { mx_run(); PStop(); }
static void mx2(void) { mx_run(); PStop(); }

/* ---- 2. counting semaphore ---------------------------------------------- */
#define PC_ITEMS 25
static Semaphore items;
static int produced;

static void producer(void)
{
    int i;
    for (i = 0; i < PC_ITEMS; i++) {
        produced++;
        SemSignal(&items);
        if (i % 3 == 0)
            ProcWait(1);
    }
    SemSignal(&finished);
    PStop();
}

/* ---- 3. user-marked resource channels ----------------------------------- */
#define NUSER 3
#define ROUNDS 12
static t9_rds rds;
static t9_reschan rc[NUSER];
static int user_bad;

static void user_run(int u)
{
    int m;
    for (m = 0; m < ROUNDS; m++) {
        int req = u * 1000 + m, ans;
        t9_mark(&rc[u].chan, &rds, (unsigned)u);
        ChanOutInt(&rc[u].chan, req);
        ans = ChanInInt(&rc[u].reply);
        if (ans != req * 2 + 1)
            user_bad++;
        if (m % 4 == u)
            ProcWait(1);
    }
    SemSignal(&finished);
}
static void user0(void) { user_run(0); PStop(); }
static void user1(void) { user_run(1); PStop(); }
static void user2(void) { user_run(2); PStop(); }

/* ---- 4. server-marked: users output on ordinary channels ----------------- */
static t9_rds rds2;

static void puser_run(int u)
{
    int m;
    for (m = 0; m < ROUNDS; m++)
        ChanOutInt(&rc[u].chan, 100 * u + m);
    SemSignal(&finished);
}
static void puser0(void) { puser_run(0); PStop(); }
static void puser1(void) { puser_run(1); PStop(); }
static void puser2(void) { puser_run(2); PStop(); }

/* ---- 5 and 6: one-shot senders ------------------------------------------ */
static volatile int *send_to;
static int send_val;
static void sender(void)
{
    volatile int *c = send_to;
    int v = send_val;
    ChanOutInt((Channel *)c, v);
    SemSignal(&finished);
    PStop();
}
static void start_sender(int slot, volatile int *c, int v)
{
    send_to = c;
    send_val = v;
    PRun(PSetup(ws[slot], sizeof ws[slot], sender) | PRI_LOW);
    ProcWait(3);                    /* let it start and reach its output */
}

int test_main(void)
{
    int i, u, n, x, seen[NUSER];

    /* 1 */
    PRun(PSetup(ws[0], sizeof ws[0], mx0) | PRI_LOW);
    PRun(PSetup(ws[1], sizeof ws[1], mx1) | PRI_LOW);
    PRun(PSetup(ws[2], sizeof ws[2], mx2) | PRI_LOW);
    for (i = 0; i < 3; i++)
        SemWait(&finished);
    CHECK("mutex overlaps", overlaps, 0);
    CHECK("mutex total", total, 3 * MX_ITERS);
    CHECK("mutex count", mutex.count, 1);
    CHECK("mutex queue", (unsigned)mutex.front, NOTPROC);

    /* 2 */
    SemInit(&items, 0);
    PRun(PSetup(ws[0], sizeof ws[0], producer) | PRI_LOW);
    for (i = 0; i < PC_ITEMS; i++)
        SemWait(&items);
    SemWait(&finished);
    CHECK("items produced", produced, PC_ITEMS);
    CHECK("items count", items.count, 0);

    /* 3 */
    t9_rds_init(&rds);
    for (u = 0; u < NUSER; u++) {
        t9_reschan_init(&rc[u]);
        seen[u] = 0;
    }
    PRun(PSetup(ws[0], sizeof ws[0], user0) | PRI_LOW);
    PRun(PSetup(ws[1], sizeof ws[1], user1) | PRI_LOW);
    PRun(PSetup(ws[2], sizeof ws[2], user2) | PRI_LOW);
    for (n = 0; n < NUSER * ROUNDS; n++) {
        unsigned id = t9_grant(&rds);
        if (id >= NUSER) {
            fail("grant id", id, 0);
            break;
        }
        x = ChanInInt(&rc[id].chan);
        if (x != (int)id * 1000 + seen[id])
            fail("request order", (unsigned)x, id * 1000 + seen[id]);
        seen[id]++;
        ChanOutInt(&rc[id].reply, x * 2 + 1);
    }
    for (i = 0; i < 3; i++)
        SemWait(&finished);
    for (u = 0; u < NUSER; u++)
        CHECK("served", seen[u], ROUNDS);
    CHECK("user answers", user_bad, 0);
    CHECK("rds idle", rds.proc, NOTPROC);
    CHECK("rds empty", rds.front, NOTPROC);

    /* 4: the users block in ordinary outputs, then the server marks */
    t9_rds_init(&rds2);
    for (u = 0; u < NUSER; u++) {
        t9_reschan_init(&rc[u]);
        seen[u] = 0;
    }
    PRun(PSetup(ws[0], sizeof ws[0], puser0) | PRI_LOW);
    PRun(PSetup(ws[1], sizeof ws[1], puser1) | PRI_LOW);
    PRun(PSetup(ws[2], sizeof ws[2], puser2) | PRI_LOW);
    ProcWait(3);
    for (u = 0; u < NUSER; u++)
        t9_mark(&rc[u].chan, &rds2, (unsigned)u);
    for (n = 0; n < NUSER * ROUNDS; n++) {
        unsigned id = t9_grant(&rds2);
        if (id >= NUSER) {
            fail("grant2 id", id, 0);
            break;
        }
        x = ChanInInt(&rc[id].chan);
        if (x != 100 * (int)id + seen[id])
            fail("request2 order", (unsigned)x, 100 * id + seen[id]);
        if (++seen[id] < ROUNDS)
            t9_mark(&rc[id].chan, &rds2, id);   /* again, at any time */
    }
    for (i = 0; i < 3; i++)
        SemWait(&finished);
    for (u = 0; u < NUSER; u++)
        CHECK("served2", seen[u], ROUNDS);

    /* 5: ALT over a channel and a resource */
    {
        static Channel plain = NOPROCESS;
        volatile int *chans[1];
        t9_rds *rl[1];

        chans[0] = &plain;
        rl[0] = &rds;
        t9_rds_init(&rds);
        t9_reschan_init(&rc[0]);
        t9_mark(&rc[0].chan, &rds, 7);

        start_sender(0, &plain, 11);            /* the channel alone */
        CHECK("alt chan", t9_alt(chans, 1, rl, 1), 0);
        CHECK("alt chan data", ChanInInt(&plain), 11);
        SemWait(&finished);

        start_sender(0, &rc[0].chan, 22);       /* the resource alone */
        CHECK("alt res", t9_alt(chans, 1, rl, 1), 1);
        CHECK("alt res grant", t9_grant(&rds), 7);
        CHECK("alt res data", ChanInInt(&rc[0].chan), 22);
        SemWait(&finished);

        t9_mark(&rc[0].chan, &rds, 8);          /* both: channel first */
        start_sender(0, &rc[0].chan, 33);
        start_sender(1, &plain, 44);
        CHECK("alt both", t9_alt(chans, 1, rl, 1), 0);
        CHECK("alt both chan", ChanInInt(&plain), 44);
        CHECK("alt both res", t9_alt(chans, 1, rl, 1), 1);
        CHECK("alt both grant", t9_grant(&rds), 8);
        CHECK("alt both res data", ChanInInt(&rc[0].chan), 33);
        SemWait(&finished);
        SemWait(&finished);

        /* the ALT waits for a resource claim that comes later */
        t9_mark(&rc[0].chan, &rds, 9);
        send_to = &rc[0].chan;
        send_val = 55;
        PRun(PSetup(ws[0], sizeof ws[0], sender) | PRI_LOW);
        CHECK("alt wait", t9_alt(chans, 1, rl, 1), 1);
        CHECK("alt wait grant", t9_grant(&rds), 9);
        CHECK("alt wait data", ChanInInt(&rc[0].chan), 55);
        SemWait(&finished);
    }

    /* 6: unmkrc, rc.Ptr, and moving a queue */
    {
        void *front, *back;

        t9_rds_init(&rds);
        t9_reschan_init(&rc[0]);
        t9_reschan_init(&rc[1]);
        t9_mark(&rc[0].chan, &rds, 3);
        CHECK("ldresptr", __ldresptr((void *)&rc[0].chan), (unsigned)&rds);
        __stresptr((void *)&rc[0].chan, 0x1234u);
        CHECK("stresptr", rc[0].reply, 0x1234);
        __stresptr((void *)&rc[0].chan, (unsigned)&rds);
        CHECK("marked id", rc[0].id, 3);
        CHECK("marked word", (unsigned)rc[0].chan, 0x80000002u);   /* ResChan */
        t9_unmark(&rc[0].chan);
        CHECK("unmarked id", rc[0].id, NOTPROC);
        CHECK("unmarked word", (unsigned)rc[0].chan, NOTPROC);
        start_sender(0, &rc[0].chan, 66);       /* now an ordinary channel */
        CHECK("plain again", ChanInInt(&rc[0].chan), 66);
        SemWait(&finished);

        t9_mark(&rc[0].chan, &rds, 4);          /* two claims queue up */
        t9_mark(&rc[1].chan, &rds, 5);
        start_sender(0, &rc[0].chan, 77);
        start_sender(1, &rc[1].chan, 88);
        CHECK("queue front", rds.front, (unsigned)&rc[0].chan);
        CHECK("queue back", rds.back, (unsigned)&rc[1].chan);
        front = __erdsq(&rds, &back);
        CHECK("erdsq front", (unsigned)front, (unsigned)&rc[0].chan);
        CHECK("erdsq back", (unsigned)back, (unsigned)&rc[1].chan);
        CHECK("erdsq emptied", rds.front, NOTPROC);
        __irdsq(&rds, front, back);
        CHECK("irdsq front", rds.front, (unsigned)&rc[0].chan);
        CHECK("irdsq grant 1", t9_grant(&rds), 4);
        CHECK("irdsq data 1", ChanInInt(&rc[0].chan), 77);
        CHECK("irdsq grant 2", t9_grant(&rds), 5);
        CHECK("irdsq data 2", ChanInInt(&rc[1].chan), 88);
        SemWait(&finished);
        SemWait(&finished);
    }

    if (bad) {
        us("RES: FAIL ");
        uh((unsigned)bad);
        up('\n');
    } else {
        us("RES-OK\n");
    }
    return bad;
}
