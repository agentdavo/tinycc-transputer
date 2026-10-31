/* vres.c - resource channels and ALTs over virtual links between two
 * T9000s, through <t9000.h>.
 * Expected (node 0): VRES OK\nH
 *
 * Link 1 carries three virtual links, set up as in vlink.c.
 *
 *  1. Node 0 runs a user process per virtual link; each sends ROUNDS
 *     requests on its link's output channel and checks the answer that
 *     comes back on the input channel.  Node 1 has marked its three virtual
 *     input channels into one resource - a virtual channel is marked on the
 *     server's node - and serves them with grant: input the request from the
 *     granted channel, answer on the same link, mark the channel again.
 *  2. Node 1 unmarks them and ALTs over the three virtual input channels
 *     while node 0 sends on one link at a time, in a scrambled order.
 *  3. A mixed ALT on node 1: link 0 as a channel guard, links 1 and 2 as a
 *     resource; a resource that wins is then granted.
 *
 * bytestream.map wiring; see vres.sh. */
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

#define NVL     3
#define ROUNDS  16
static unsigned pkt[NVL][8];
static int bad;

static void fail(const char *what, unsigned got, unsigned want)
{
    bad++;
    us(what); us(" got="); uh(got); us(" want="); uh(want); up('\n');
}

static void setup(void)
{
    int vl;
    unsigned char h;

    t9_vcp_channels(NVL);
    t9_link_virtual(1, 0);
    __stconf(T9_CR_VCPLINKMAXHDR(1), NVL - 1);
    for (vl = 0; vl < NVL; vl++) {
        h = (unsigned char)vl;
        t9_vlink(vl, pkt[vl], 1, &h, 1);
    }
    t9_vcp_start(NVL);
}

static void put(int vl, int v) { ChanOut((Channel *)T9_VOUT(vl), &v, 4); }
static int get(int vl) { int v; ChanIn((Channel *)T9_VIN(vl), &v, 4); return v; }

/* the order node 0 sends in, parts 2 and 3 */
static const int order[] = { 2, 0, 1, 1, 2, 0, 0, 2, 1, 2 };
#define NORDER (int)(sizeof order / sizeof order[0])

#if NODE == 0
static Semaphore finished = SEMAPHOREINIT(0);
static int ws[NVL][384];

static void user_run(int vl)
{
    int m;
    for (m = 0; m < ROUNDS; m++) {
        int req = vl * 1000 + m;
        put(vl, req);
        if (get(vl) != req * 2 + 1)
            bad++;
    }
    SemSignal(&finished);
}
static void user0(void) { user_run(0); PStop(); }
static void user1(void) { user_run(1); PStop(); }
static void user2(void) { user_run(2); PStop(); }

int test_main(void)
{
    int i, v;

    setup();
    /* 1 */
    PRun(PSetup(ws[0], sizeof ws[0], user0) | PRI_LOW);
    PRun(PSetup(ws[1], sizeof ws[1], user1) | PRI_LOW);
    PRun(PSetup(ws[2], sizeof ws[2], user2) | PRI_LOW);
    for (i = 0; i < NVL; i++)
        SemWait(&finished);
    if (bad)
        fail("resource answers", (unsigned)bad, 0);
    /* 2 and 3: one link at a time; node 1 echoes back which link it took */
    for (i = 0; i < 2 * NORDER; i++) {
        int vl = order[i % NORDER];
        put(vl, 500 + i);
        v = get(vl);
        if (v != (i < NORDER ? vl : 100 + vl) * 10000 + 500 + i)
            fail(i < NORDER ? "alt" : "mixed alt", (unsigned)v,
                 (unsigned)((i < NORDER ? vl : 100 + vl) * 10000 + 500 + i));
    }
    us(bad ? "VRES FAIL\n" : "VRES OK\n");
    return bad;
}
#else
static t9_rds rds;
static unsigned extrc[T9_EXTRC_BYTES(NVL) / 4];

int test_main(void)
{
    int i, n, x, seen[NVL] = { 0, 0, 0 };
    volatile int *chans[NVL];
    t9_rds *rl[1];

    setup();
    t9_vcp_resources(extrc, NVL);
    /* 1: the server */
    t9_rds_init(&rds);
    for (i = 0; i < NVL; i++)
        t9_mark(T9_VIN(i), &rds, (unsigned)i);
    for (n = 0; n < NVL * ROUNDS; n++) {
        unsigned id = t9_grant(&rds);
        if (id >= NVL) {
            fail("grant id", id, 0);
            break;
        }
        x = get((int)id);
        if (x != (int)id * 1000 + seen[id])
            fail("request order", (unsigned)x, id * 1000 + seen[id]);
        seen[id]++;
        put((int)id, x * 2 + 1);
        if (seen[id] < ROUNDS)
            t9_mark(T9_VIN(id), &rds, id);
    }
    for (i = 0; i < NVL; i++)
        if (seen[i] != ROUNDS)
            fail("served", (unsigned)seen[i], ROUNDS);
    /* 2: a plain ALT over virtual channels (granted channels are unmarked) */
    for (i = 0; i < NVL; i++)
        chans[i] = (volatile int *)T9_VIN(i);
    for (i = 0; i < NORDER; i++) {
        int k = ChanAlt((Channel **)chans, NVL);
        if (k != order[i])
            fail("alt index", (unsigned)k, (unsigned)order[i]);
        if (k < 0 || k >= NVL)
            break;
        x = get(k);
        put(k, k * 10000 + x);
    }
    /* 3: link 0 a channel guard, links 1 and 2 a resource */
    t9_rds_init(&rds);
    t9_mark(T9_VIN(1), &rds, 1);
    t9_mark(T9_VIN(2), &rds, 2);
    rl[0] = &rds;
    for (i = 0; i < NORDER; i++) {
        int k = t9_alt(chans, 1, rl, 1), vl;
        if (k == 1) {
            vl = (int)t9_grant(&rds);
        } else {
            vl = k;
        }
        if (vl != order[i])
            fail("mixed index", (unsigned)vl, (unsigned)order[i]);
        if (vl < 0 || vl >= NVL)
            break;
        x = get(vl);
        put(vl, (100 + vl) * 10000 + x);
        if (vl != 0)
            t9_mark(T9_VIN(vl), &rds, (unsigned)vl);
    }
    us(bad ? "node1 FAIL\n" : "node1 done\n");
    return bad;
}
#endif
