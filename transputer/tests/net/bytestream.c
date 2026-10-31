/* bytestream.c - T4 links are byte streams, as transputer links are: an in
 * takes exactly the bytes it asks for, however the sender's outs split them.
 * Expected (node 0): SBYTESTREAM OK\nH
 *
 * Two nodes joined by link 1 (bytestream.map):
 *   node 0 sends 3000 bytes in one out; node 1 takes them as ins of 7, 100,
 *   1000 and 1893, with an ALT after the first that must find the link ready
 *   on the bytes left over from the packet the 7 came from.  Then node 1
 *   sends 5 outs of 600 and node 0 takes all 3000 in one in.
 *
 * Last, node 1 sends 3000 bytes twice more and node 0 takes them into
 * buffers straddling the start and the end of T4's framebuffer window
 * (0x81E00000..0x81FD4C00, inside RAM at -sm 26): the receive path wrote a
 * packet that ran into the window to the RAM behind it, where reads never
 * look.  Needs -sm 26. */
#include <conc.h>
#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void us(const char *s) { while (*s) UART_DATA = (unsigned)(unsigned char)*s++; }
void __in(int len, Channel *c, void *buf);
void __out(int len, Channel *c, const void *buf);

#define N 3000
static unsigned char buf[N];
static unsigned char *const edge[2] = {
    (unsigned char *)(0x81E00000u - 1500), (unsigned char *)(0x81FD4C00u - 1500)
};

int test_main(void)
{
    int i, bad = 0;
#if NODE == 0
    for (i = 0; i < N; i++)
        buf[i] = (unsigned char)(i * 7 + 3);
    __out(N, LINKOUT(1), buf);
    for (i = 0; i < N; i++)
        buf[i] = 0;
    __in(N, LINKIN(1), buf);                /* five outs of 600 */
    for (i = 0; i < N; i++)
        if (buf[i] != (unsigned char)(i * 11 + 5))
            bad++;
    {
        int e;
        for (e = 0; e < 2; e++) {
            volatile unsigned char *p = edge[e];
            for (i = 0; i < N; i++)
                p[i] = 0;
            __in(N, LINKIN(1), edge[e]);
            for (i = 0; i < N; i++)
                if (p[i] != (unsigned char)(i * 13 + e))
                    bad++;
        }
    }
    us(bad ? "BYTESTREAM BAD\n" : "BYTESTREAM OK\n");
#else
    static const int part[4] = { 7, 100, 1000, 1893 };
    Channel *in1[1];
    int k, off = 0;

    in1[0] = LINKIN(1);
    for (k = 0; k < 4; k++) {
        __in(part[k], LINKIN(1), buf + off);
        off += part[k];
        if (k == 0 && ChanAlt(in1, 1) != 0)
            bad++;
    }
    for (i = 0; i < N; i++)
        if (buf[i] != (unsigned char)(i * 7 + 3))
            bad++;
    for (i = 0; i < N; i++)
        buf[i] = (unsigned char)(i * 11 + 5);
    for (k = 0; k < 5; k++)
        __out(600, LINKOUT(1), buf + k * 600);
    for (k = 0; k < 2; k++) {
        for (i = 0; i < N; i++)
            buf[i] = (unsigned char)(i * 13 + k);
        __out(N, LINKOUT(1), buf);
    }
    us(bad ? "node1 BAD\n" : "node1 OK\n");
#endif
    return bad;
}
