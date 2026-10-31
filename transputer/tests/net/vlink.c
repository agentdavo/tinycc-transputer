/* vlink.c - three virtual links multiplexed over one physical link between
 * two T9000s, driven by the include/t9000.h builtins.
 * Expected (node 0): VLINK OK\nH
 *
 * Both nodes put Link1 in virtual mode with 1-byte headers (header n names
 * the receiver's virtual link n).  Node 0 sends messages of 0..1000 bytes -
 * empty, one partial packet, exact packets, several - on each link with
 * __vout and checks the echo node 1 returns through __vin/__vout.  Then
 * three processes on each node drive the three links at once, so packets
 * of different links interleave on the cable; and a last round uses plain
 * in/out on a virtual channel, and one with __vxfer: node 0 sends and
 * receives on all three links in one group, node 1 takes the three
 * messages in one group and echoes them in another.  bytestream.map wiring,
 * see vlink.sh. */
#include <conc.h>
#include <t9000.h>
#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void us(const char *s) { while (*s) UART_DATA = (unsigned)(unsigned char)*s++; }
void __in(int len, Channel *c, void *buf);
void __out(int len, Channel *c, const void *buf);

#define NVL     3
#define MAXMSG  1024
#define ROUNDS  4
static const int lens[] = { 0, 1, 31, 32, 33, 64, 100, 1000 };
#define NLENS   (int)(sizeof lens / sizeof lens[0])

static unsigned pkt[NVL][8];
static unsigned char tx[NVL][MAXMSG], rx[NVL][MAXMSG];
static int bad;

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

#if NODE == 0
static int exchange(int vl, int len, int seed)
{
    unsigned n;
    int j;

    for (j = 0; j < len; j++)
        tx[vl][j] = (unsigned char)(seed * 31 + j * 5 + vl * 77);
    __vout(len, T9_VOUT(vl), tx[vl]);
    for (j = 0; j < MAXMSG; j++)
        rx[vl][j] = 0xEE;
    n = __vin(MAXMSG, T9_VIN(vl), rx[vl]);
    if (n != (unsigned)len)
        return 0;
    for (j = 0; j < len; j++)
        if (rx[vl][j] != tx[vl][j])
            return 0;
    return 1;
}

static int rounds(int vl)
{
    int r, ok = 1;
    for (r = 0; r < ROUNDS; r++)
        ok &= exchange(vl, lens[(r * 3 + vl) % NLENS], r + 10 * vl);
    return ok;
}
#else
static void echo(int vl, int count)
{
    while (count--) {
        unsigned n = __vin(MAXMSG, T9_VIN(vl), rx[vl]);
        __vout(n, T9_VOUT(vl), rx[vl]);
    }
}
static int rounds(int vl) { echo(vl, ROUNDS); return 1; }
#endif

/* one channel per worker: a channel holds one waiting process */
static Channel done1 = NOPROCESS, done2 = NOPROCESS;
static int ws1[256], ws2[256];
static void worker1(void) { int ok = rounds(1); ChanOut(&done1, &ok, 4); PStop(); }
static void worker2(void) { int ok = rounds(2); ChanOut(&done2, &ok, 4); PStop(); }

int test_main(void)
{
    int i, vl, ok;

    setup();
#if NODE == 0
    for (i = 0; i < NLENS; i++)
        for (vl = 0; vl < NVL; vl++)
            if (!exchange(vl, lens[i], i))
                bad++;
#else
    for (i = 0; i < NLENS; i++)
        for (vl = 0; vl < NVL; vl++)
            echo(vl, 1);
#endif
    /* all three links at once */
    PRun(PSetup(ws1, sizeof ws1, worker1) | PRI_LOW);
    PRun(PSetup(ws2, sizeof ws2, worker2) | PRI_LOW);
    if (!rounds(0))
        bad++;
    ChanIn(&done1, &ok, 4);
    if (!ok)
        bad++;
    ChanIn(&done2, &ok, 4);
    if (!ok)
        bad++;
    /* every link at once from one process, lengths coming back */
    {
        static const unsigned glen[NVL] = { 100, 1000, 33 };
        t9_xfer xl[2 * NVL];
        int j;
#if NODE == 0
        for (vl = 0; vl < NVL; vl++) {
            for (j = 0; j < (int)glen[vl]; j++)
                tx[vl][j] = (unsigned char)(j * 7 + vl * 3 + 5);
            for (j = 0; j < MAXMSG; j++)
                rx[vl][j] = 0xEE;
            xl[vl].chan = T9_VOUT(vl);
            xl[vl].buf = tx[vl];
            xl[vl].len = glen[vl];
            xl[NVL + vl].chan = T9_VIN(vl);
            xl[NVL + vl].buf = rx[vl];
            xl[NVL + vl].len = MAXMSG;
        }
        __vxfer(xl, 2 * NVL);
        for (vl = 0; vl < NVL; vl++) {
            if (xl[NVL + vl].len != glen[vl] || xl[vl].len != glen[vl])
                bad++;
            for (j = 0; j < (int)glen[vl]; j++)
                if (rx[vl][j] != tx[vl][j])
                    bad++;
        }
#else
        for (vl = 0; vl < NVL; vl++) {
            xl[vl].chan = T9_VIN(vl);
            xl[vl].buf = rx[vl];
            xl[vl].len = MAXMSG;
        }
        __vxfer(xl, NVL);
        for (vl = 0; vl < NVL; vl++)
            xl[vl].chan = T9_VOUT(vl);  /* the same buffers and lengths */
        __vxfer(xl, NVL);
#endif
        (void)j;
    }
    /* fixed-length in/out on a virtual channel */
#if NODE == 0
    for (i = 0; i < 256; i++)
        tx[0][i] = (unsigned char)(i * 13 + 1);
    __out(256, T9_VOUT(0), tx[0]);
    __in(256, T9_VIN(0), rx[0]);
    for (i = 0; i < 256; i++)
        if (rx[0][i] != tx[0][i])
            bad++;
    if (__ldconf(T9_CR_VCPSTATUS) != 0)
        bad++;
    us(bad ? "VLINK BAD\n" : "VLINK OK\n");
#else
    __in(256, T9_VIN(0), rx[0]);
    __out(256, T9_VOUT(0), rx[0]);
    us("node1 done\n");
#endif
    return bad;
}
