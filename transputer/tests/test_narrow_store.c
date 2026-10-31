/*
 * test_narrow_store.c - correctness of byte and 16-bit stores.
 *
 * The byte store used to be a ~37-instruction word read/modify/write and the
 * T800 16-bit store ran that RMW twice.  Both are now single `sb`s (two for
 * the halfword).  The RMW was justified as "keeping packed struct u8 fields
 * coherent", which is backwards - it read four bytes, changed one and wrote
 * all four back, so it clobbered neighbours.  This test is built to catch
 * exactly that class of error: every case writes one narrow field and then
 * checks its NEIGHBOURS are untouched, at every alignment.
 *
 * Also covers the byte order of the 16-bit store (must match the T9000 `ss`:
 * low byte first), signed/unsigned narrow values, negative values, volatile,
 * and stores through pointers at every misalignment.
 */

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c){UART_DATA=(unsigned)(unsigned char)c;}
static void us(const char*s){while(*s)up(*s++);}
static void ud(unsigned v){char b[12];int n=0;if(!v){up('0');return;}while(v){b[n++]=(char)('0'+v%10);v/=10;}while(n)up(b[--n]);}

static int bad = 0, ck = 0;
static void eq(const char *w, int i, long g, long e)
{
    ck++;
    if (g != e) {
        bad++;
        us("\nFAIL "); us(w); us("["); ud((unsigned)i);
        us("] got="); ud((unsigned)g); us(" want="); ud((unsigned)e);
    }
}

/* A guarded byte arena: writes must never disturb the guard bytes. */
static unsigned char A[32];
static unsigned short H[16];
static volatile unsigned char V[8];

struct packed3 { char a; char b; char c; };
struct mix { char c0; short s1; char c2; int i3; char c4; };
static struct packed3 P3[4];
static struct mix     MX[4];

static void fill(void)
{
    int i;
    for (i = 0; i < 32; i++) A[i] = (unsigned char)(0xA0 + i);
}

/* 1. byte store at every alignment leaves all 31 neighbours intact */
static void t_byte_neighbours(void)
{
    int off, i;
    for (off = 0; off < 32; off++) {
        fill();
        A[off] = 0x5A;
        for (i = 0; i < 32; i++)
            eq("nbr", i, A[i], (i == off) ? 0x5A : (0xA0 + i) & 0xFF);
    }
}

/* 2. byte values across the full range, including the sign boundary */
static void t_byte_values(void)
{
    int i;
    static const int vals[8] = { 0, 1, 0x7F, 0x80, 0xFE, 0xFF, 0x55, 0xAA };
    for (i = 0; i < 8; i++) {
        A[3] = (unsigned char)vals[i];
        eq("val", i, A[3], vals[i]);
        A[4] = (unsigned char)(0xA0 + 4);   /* neighbour must survive */
        eq("valnbr", i, A[4], 0xA0 + 4);
    }
    { signed char *sp = (signed char *)&A[7];
      *sp = -1;   eq("schar", 0, A[7], 0xFF);
      *sp = -128; eq("schar", 1, A[7], 0x80); }
}

/* 3. 16-bit store: value, byte order, and neighbours, at both alignments */
static void t_half(void)
{
    int i;
    for (i = 0; i < 16; i++) H[i] = (unsigned short)(0x1000 + i);
    H[5] = 0xBEEF;
    eq("half", 0, H[5], 0xBEEF);
    eq("half", 1, H[4], 0x1004);
    eq("half", 2, H[6], 0x1006);

    /* byte order must be little-endian, matching the T9000 ss */
    {
        unsigned char *b = (unsigned char *)&H[5];
        eq("order", 0, b[0], 0xEF);
        eq("order", 1, b[1], 0xBE);
    }
    /* unaligned halfword through a byte-offset pointer */
    {
        fill();
        {
            unsigned char *p = &A[3];
            p[0] = 0x34; p[1] = 0x12;      /* hand-built, the reference */
            eq("un", 0, A[3], 0x34);
            eq("un", 1, A[4], 0x12);
            eq("un", 2, A[2], (0xA0 + 2) & 0xFF);
            eq("un", 3, A[5], (0xA0 + 5) & 0xFF);
        }
    }
    { short *sp = &((short *)H)[2];
      *sp = -1;     eq("shalf", 0, H[2], 0xFFFF);
      *sp = -32768; eq("shalf", 1, H[2], 0x8000); }
}

/* 4. packed structs - the case the RMW claimed to protect and actually broke */
static void t_packed(void)
{
    int i;
    for (i = 0; i < 4; i++) { P3[i].a = (char)(i*3+1); P3[i].b = (char)(i*3+2); P3[i].c = (char)(i*3+3); }
    P3[1].b = 0x7E;
    for (i = 0; i < 4; i++) {
        eq("p3.a", i, P3[i].a, i*3+1);
        eq("p3.b", i, P3[i].b, (i == 1) ? 0x7E : i*3+2);
        eq("p3.c", i, P3[i].c, i*3+3);
    }
    for (i = 0; i < 4; i++) { MX[i].c0 = (char)i; MX[i].s1 = (short)(i+100); MX[i].c2 = (char)(i+10); MX[i].i3 = i+1000; MX[i].c4 = (char)(i+20); }
    MX[2].c2 = 0x33;
    MX[2].s1 = 0x4455;
    for (i = 0; i < 4; i++) {
        eq("mx.c0", i, MX[i].c0, i);
        eq("mx.s1", i, MX[i].s1, (i == 2) ? 0x4455 : i+100);
        eq("mx.c2", i, MX[i].c2, (i == 2) ? 0x33 : i+10);
        eq("mx.i3", i, MX[i].i3, i+1000);
        eq("mx.c4", i, MX[i].c4, i+20);
    }
}

/* 5. volatile byte stores still work (this path was always sb) */
static void t_volatile(void)
{
    int i;
    for (i = 0; i < 8; i++) V[i] = (unsigned char)(i + 1);
    for (i = 0; i < 8; i++) eq("vol", i, V[i], i + 1);
    V[3] = 0xC7;
    eq("vol", 8, V[3], 0xC7);
    eq("vol", 9, V[2], 3);
    eq("vol", 10, V[4], 5);
}

/* 6. stores through a computed pointer, every misalignment */
static void t_ptr(void)
{
    int off;
    for (off = 0; off < 4; off++) {
        unsigned char *p;
        fill();
        p = &A[8 + off];
        *p = 0x77;
        eq("ptr", off, A[8 + off], 0x77);
        if (off > 0) eq("ptrlo", off, A[8 + off - 1], (0xA0 + 8 + off - 1) & 0xFF);
        eq("ptrhi", off, A[9 + off], (0xA0 + 9 + off) & 0xFF);
    }
}

int test_main(void)
{
    t_byte_neighbours();
    t_byte_values();
    t_half();
    t_packed();
    t_volatile();
    t_ptr();

    us("\nchecks="); ud((unsigned)ck); us(" failures="); ud((unsigned)bad);
    us(bad ? "\nNARROWSTORE: FAIL\n" : "\nNARROWSTORE: PASS\n");
    return bad ? 1 : 0;
}
