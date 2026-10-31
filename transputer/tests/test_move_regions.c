/*
 * test_move_regions.c - block moves across the edges of T4's memory regions.
 * Expected: SMOVEREG-OK\nH
 *
 * T4 backs guest memory with several host arrays: the on-chip core
 * (0x80000000..0x80000FFF), external RAM, and - with the SDL framebuffer
 * built in - the VGA surface, which takes over 0x81E00000..0x81FD4C00 even
 * when -sm makes that range ordinary RAM.  A `move` (what memcpy inlines to)
 * whose source or destination crosses one of those edges must still copy
 * the bytes a byte-at-a-time loop would see.
 *
 * Found by tests/net/ffn_cube.c: a Q5_K block at 0x80000F60 (straddling the
 * core edge) copied into the FB window arrived as the PREVIOUS block, because
 * the FB path read its source through memrange(), whose fallback hands back
 * the bounce buffer unfilled.  Ranges running into the window from below, or
 * out of it at the top, were read from the RAM shadow instead of the window.
 *
 * Every case first copies a decoy through the same path, so a stale bounce
 * buffer cannot pass by accident.  Needs RAM up to 0x81FD5000: the runner's
 * default -sm 26.
 */
#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c){UART_DATA=(unsigned)(unsigned char)c;}
static void us(const char*s){while(*s)up(*s++);}
static void uh(unsigned v){int i;for(i=28;i>=0;i-=4){int n=(v>>i)&0xF;up(n<10?'0'+n:'a'+n-10);}}
void *memcpy(void *d, const void *s, unsigned n);

#define CORE_END 0x80001000u
#define FB_LO    0x81E00000u
#define FB_HI    0x81FD4C00u            /* 800 x 600 x 4 */
#define LEN      176                    /* one Q5_K block */

static unsigned char decoy[LEN], ram[LEN + 8], want[LEN + 8];
static int fails;

static void fill_bytes(unsigned a, unsigned n, unsigned seed)
{
    volatile unsigned char *p = (volatile unsigned char *)a;
    unsigned i;
    for (i = 0; i < n; i++)
        p[i] = (unsigned char)((i * 37 + seed) ^ (i >> 3));
}

static void snap(unsigned char *d, unsigned a, unsigned n)
{
    volatile const unsigned char *p = (volatile const unsigned char *)a;
    unsigned i;
    for (i = 0; i < n; i++)
        d[i] = p[i];
}

static void check(const char *what, unsigned dst, unsigned src, unsigned n)
{
    volatile const unsigned char *d = (volatile const unsigned char *)dst;
    unsigned i;
    for (i = 0; i < n; i++)
        if (d[i] != want[i]) {
            us(what); up(' '); uh(src); up('>'); uh(dst); up('@'); uh(i); up('\n');
            fails++;
            return;
        }
}

/* move n bytes src -> dst after priming the same path with the decoy */
static void probe(const char *what, unsigned dst, unsigned src, unsigned n)
{
    memcpy((void *)dst, decoy, n);
    snap(want, src, n);
    memcpy((void *)dst, (const void *)src, n);
    check(what, dst, src, n);
}

int test_main(void)
{
    static const unsigned offs[] = { 1, 3, 4, 87, LEN - 4, LEN - 1 };
    unsigned i, k;

    for (i = 0; i < LEN; i++)
        decoy[i] = (unsigned char)(0xA5 ^ i);

    /* the window's edges as sources and destinations: seed both sides */
    fill_bytes(FB_LO - 512, 1024, 11);
    fill_bytes(FB_HI - 512, 1024, 29);

    for (k = 0; k < sizeof offs / sizeof offs[0]; k++) {
        unsigned o = offs[k];
        /* core-edge source (the program image) into RAM, the window, and
         * across both window edges */
        probe("core>ram", (unsigned)ram, CORE_END - o, LEN);
        probe("core>fb", FB_LO + 4096, CORE_END - o, LEN);
        probe("core>fblo", FB_LO - o, CORE_END - o, LEN);
        probe("core>fbhi", FB_HI - o, CORE_END - o, LEN);
        /* sources that run into the window from below or out of it */
        probe("fblo>ram", (unsigned)ram, FB_LO - o, LEN);
        probe("fbhi>ram", (unsigned)ram, FB_HI - o, LEN);
        probe("fblo>fb", FB_LO + 8192, FB_LO - o, LEN);
        /* window-edge destinations from RAM */
        probe("ram>fblo", FB_LO - o, (unsigned)decoy, LEN);
        probe("ram>fbhi", FB_HI - o, (unsigned)decoy, LEN);
        /* re-seed what the last probes overwrote */
        fill_bytes(FB_LO - 512, 1024, 11 + k);
        fill_bytes(FB_HI - 512, 1024, 29 + k);
    }
    us(fails ? "MOVEREG-BAD\n" : "MOVEREG-OK\n");
    return fails;
}
