/* msglat.c - what one message start costs between two T9000s through an
 * IMS C104 (msglat.sh), the planner's O_MSG.
 * Expected (node 0): MSGLAT OK
 *
 * Every phase prints a T4_T9K_MARKS line; msglat.sh turns the gaps into
 * cycles per step.
 *   pp      ping-pong of LEN bytes on one virtual link, __vout/__vin
 *   par     exchange steps - each node sends LEN bytes and takes its peer's
 *           at once - with the output in a process of its own, the way
 *           ffn_cube.c did it before VXFER
 *   vxfer   the same exchange as one __vxfer of {out, in}
 *   vxfer3  three virtual links each way in one __vxfer, the shape of an
 *           all-gather step across three planes (here all on one link)
 * Inside a phase a message carries its step number in its first word, so
 * the loop does no more than a real collective step would; the whole of the
 * last message of each phase is checked. */
#include <conc.h>
#include <t9000.h>
#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void us(const char *s) { while (*s) UART_DATA = (unsigned)(unsigned char)*s++; }

#ifndef LEN
#define LEN     4                       /* bytes, at least 4 */
#endif
#define STEPS   64
#define NVL     3
#define PEER    (NODE ^ 1)

static unsigned vlpkt[NVL][8];
static unsigned tx[NVL][(LEN + 3) / 4], rx[NVL][(LEN + 3) / 4];
static int bad;

static void setup(void)
{
    unsigned char hdr[2];
    int vl;

    t9_vcp_channels(NVL);
    t9_link_virtual(1, 0);
    __stconf(T9_CR_VCPLINKMAXHDR(1), NVL - 1);
    for (vl = 0; vl < NVL; vl++) {
        hdr[0] = (unsigned char)PEER;           /* the switch routes on it */
        hdr[1] = (unsigned char)vl;             /* and deletes it */
        t9_vlink(vl, vlpkt[vl], 1, hdr, 2);
    }
    t9_vcp_start(NVL);
}

static unsigned char pat(int vl, int j, int node)
{
    return (unsigned char)(j * 3 + vl * 11 + node * 101);
}

/* a phase's messages: the pattern, with the step in the first word */
static void fill(int vl, int node)
{
    int j;
    for (j = 0; j < LEN; j++)
        ((unsigned char *)tx[vl])[j] = pat(vl, j, node);
}

static void check(int vl, int step, int node)
{
    int j;
    if (rx[vl][0] != (unsigned)step)
        bad++;
    for (j = 4; j < LEN; j++)
        if (((unsigned char *)rx[vl])[j] != pat(vl, j, node))
            bad++;
}

#define STAMP(vl, s)    (tx[vl][0] = (unsigned)(s))
#define SEEN(vl, s)     (bad += rx[vl][0] != (unsigned)(s))

/* par: the output runs in a worker that reports on its own channel */
static Channel go = NOPROCESS, done = NOPROCESS;
static int ws[256];

static void sender(void)
{
    int n = ChanInInt(&go);
    __vout(LEN, T9_VOUT(0), tx[0]);
    ChanOutInt(&done, n);
    PStop();
}

int test_main(void)
{
    t9_xfer xl[2 * NVL];
    int s, vl;

    setup();
    for (vl = 0; vl < NVL; vl++) {
        fill(vl, NODE);
        xl[2 * vl].chan = T9_VOUT(vl);
        xl[2 * vl].buf = tx[vl];
        xl[2 * vl].len = LEN;
        xl[2 * vl + 1].chan = T9_VIN(vl);
        xl[2 * vl + 1].buf = rx[vl];
    }
    /* barrier: one exchange puts both nodes on one timeline */
    STAMP(0, 0);
    xl[1].len = LEN;
    __vxfer(xl, 2);
    check(0, 0, PEER);
    us("start\n");

    for (s = 0; s < STEPS; s++) {
#if NODE == 0
        STAMP(0, s);
        __vout(LEN, T9_VOUT(0), tx[0]);
        bad += __vin(LEN, T9_VIN(0), rx[0]) != LEN;
        SEEN(0, s);
#else
        bad += __vin(LEN, T9_VIN(0), rx[0]) != LEN;
        SEEN(0, s);
        STAMP(0, s);
        __vout(LEN, T9_VOUT(0), tx[0]);
#endif
    }
    us("pp\n");
    check(0, STEPS - 1, PEER);

    for (s = 0; s < STEPS; s++) {
        STAMP(0, s);
        PRun(PSetup(ws, sizeof ws, sender) | PRI_LOW);
        ChanOutInt(&go, s);
        bad += __vin(LEN, T9_VIN(0), rx[0]) != LEN;
        ChanInInt(&done);
        SEEN(0, s);
    }
    us("par\n");
    check(0, STEPS - 1, PEER);

    for (s = 0; s < STEPS; s++) {
        STAMP(0, s);
        xl[1].len = LEN;
        __vxfer(xl, 2);
        bad += xl[1].len != LEN;
        SEEN(0, s);
    }
    us("vxfer\n");
    check(0, STEPS - 1, PEER);

    for (s = 0; s < STEPS; s++) {
        for (vl = 0; vl < NVL; vl++) {
            STAMP(vl, s);
            xl[2 * vl + 1].len = LEN;
        }
        __vxfer(xl, 2 * NVL);
        for (vl = 0; vl < NVL; vl++)
            SEEN(vl, s);
    }
    us("vxfer3\n");
    for (vl = 0; vl < NVL; vl++)
        check(vl, STEPS - 1, PEER);

    us(bad ? "MSGLAT BAD\n" : "MSGLAT OK\n");
    return bad;
}
