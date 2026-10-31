/* vlbw.c - what the T9000 packet protocol costs on a link: 24 KB sent
 * node 0 -> node 1 three ways, each ended by a 1-byte reply, with a UART
 * line before and after for T4_T9K_MARKS (see vlink.sh / vlbw.sh):
 *   "vl1"  one virtual link (Link1, 1-byte headers): __vout of 24 KB
 *   "vl3"  three virtual links at once, 8 KB each, from three processes
 *   "bs"   the byte-stream Link2, one out of 24 KB
 * Expected (node 0): ... VLBW OK */
#include <conc.h>
#include <t9000.h>
#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void us(const char *s) { while (*s) UART_DATA = (unsigned)(unsigned char)*s++; }
void __in(int len, Channel *c, void *buf);
void __out(int len, Channel *c, const void *buf);

#define NVL     3
#define BIG     24576
static unsigned pkt[NVL][8];
static unsigned char data[BIG];
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

/* one channel per worker: a channel holds one waiting process */
static Channel done1 = NOPROCESS, done2 = NOPROCESS;
static int ws1[256], ws2[256];
static void part(int vl)
{
#if NODE == 0
    __vout(BIG / NVL, T9_VOUT(vl), data + vl * (BIG / NVL));
#else
    if (__vin(BIG / NVL, T9_VIN(vl), data + vl * (BIG / NVL)) != BIG / NVL)
        bad++;
#endif
}
static void worker1(void) { int k = 1; part(1); ChanOut(&done1, &k, 4); PStop(); }
static void worker2(void) { int k = 2; part(2); ChanOut(&done2, &k, 4); PStop(); }

int test_main(void)
{
    int i;
    unsigned char ack = 0;

    for (i = 0; i < BIG; i++)
        data[i] = (unsigned char)(i * 7 + 1);
    setup();
#if NODE == 0
    us("start\n");
    __vout(BIG, T9_VOUT(0), data);
    __vin(1, T9_VIN(0), &ack);
    us("vl1\n");
    PRun(PSetup(ws1, sizeof ws1, worker1) | PRI_LOW);
    PRun(PSetup(ws2, sizeof ws2, worker2) | PRI_LOW);
    part(0);
    ChanIn(&done1, &i, 4);
    ChanIn(&done2, &i, 4);
    __vin(1, T9_VIN(0), &ack);
    us("vl3\n");
    __out(BIG, LINKOUT(2), data);
    __in(1, LINKIN(2), &ack);
    us("bs\n");
    us(bad ? "VLBW BAD\n" : "VLBW OK\n");
#else
    if (__vin(BIG, T9_VIN(0), data) != BIG)
        bad++;
    __vout(1, T9_VOUT(0), &ack);
    PRun(PSetup(ws1, sizeof ws1, worker1) | PRI_LOW);
    PRun(PSetup(ws2, sizeof ws2, worker2) | PRI_LOW);
    part(0);
    ChanIn(&done1, &i, 4);
    ChanIn(&done2, &i, 4);
    __vout(1, T9_VOUT(0), &ack);
    __in(BIG, LINKIN(2), data);
    __out(1, LINKOUT(2), &ack);         /* reply first: time the link alone */
    for (i = 0; i < BIG; i++)
        if (data[i] != (unsigned char)(i * 7 + 1))
            bad++;
    us(bad ? "node1 BAD\n" : "node1 done\n");
#endif
    return bad;
}
