/*
 * test_call_stack_args.c - calls whose stacked argument words overlap the
 * workspace slots the remaining argument loads still read.
 *
 * Words 3+ of a call are staged top down into Wptr[0..n-1].  A pseudo-
 * register r is workspace slot r, so with enough stacked words a staging
 * store can land on a register that holds a later argument's address
 * (t9k_on_exec_finish's 14-argument pr_info printed rods_cache[1] and [2]
 * through overwritten pointers), on the scratch slots a halfword load uses,
 * or on FRAME_SLOT_DBL_TMP, where an indirect target is parked.
 */
#include <stdarg.h>

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c){UART_DATA=(unsigned)(unsigned char)c;}
static void us(const char*s){while(*s)up(*s++);}
static void ud(unsigned v){char b[12];int n=0;if(!v){up('0');return;}while(v){b[n++]=(char)('0'+v%10);v/=10;}while(n)up(b[--n]);}
static void uh(unsigned v){int i;for(i=28;i>=0;i-=4){int n=(v>>i)&0xF;up(n<10?('0'+n):('a'+n-10));}}

static int bad = 0, ck = 0;
static void eq(const char *w, int i, unsigned g, unsigned e)
{
    ck++;
    if (g != e) {
        bad++;
        us("\nFAIL "); us(w); us("["); ud((unsigned)i);
        us("] got="); uh(g); us(" want="); uh(e);
    }
}

static unsigned got[24];

static void rec(int n, ...)
{
    va_list ap;
    int i;

    va_start(ap, n);
    for (i = 0; i < n; i++)
        got[i] = va_arg(ap, unsigned);
    va_end(ap);
}

struct sm {
    unsigned flags, tlo, thi, plo, phi;
    unsigned pad[20];
    unsigned rods[4];
};

static int g_enf = 1, g_hard = 2;

/* The t9k_on_exec_finish shape: a variadic call with 13 values after the
 * count, most of them members reached through one pointer. */
static void t_members(struct sm *mm, unsigned load, unsigned stack)
{
    static const unsigned want[13] = {
        0x80880040, 0xc08c3fa0, 1, 2, 0xf,
        0x80880040, 0x80882670, 0x80880000, 0x808c4fc0,
        0x20000001, 0x80000000, 0xa0000003, 0xa0000001
    };
    int i;

    rec(13, load, stack, g_enf, g_hard, mm->flags, mm->tlo, mm->thi,
        mm->plo, mm->phi, mm->rods[0], mm->rods[1], mm->rods[2],
        mm->rods[3]);
    for (i = 0; i < 13; i++)
        eq("members", i, got[i], want[i]);
}

struct ops {
    unsigned (*fn)(unsigned, unsigned, unsigned, unsigned, unsigned,
                   unsigned, unsigned, unsigned, unsigned, unsigned,
                   unsigned, unsigned, unsigned, unsigned, unsigned,
                   unsigned, unsigned, unsigned, unsigned, unsigned,
                   unsigned, unsigned);
    unsigned v[22];
};

static unsigned sum22(unsigned a0, unsigned a1, unsigned a2, unsigned a3,
                      unsigned a4, unsigned a5, unsigned a6, unsigned a7,
                      unsigned a8, unsigned a9, unsigned a10, unsigned a11,
                      unsigned a12, unsigned a13, unsigned a14, unsigned a15,
                      unsigned a16, unsigned a17, unsigned a18, unsigned a19,
                      unsigned a20, unsigned a21)
{
    unsigned a[22];
    int i;
    unsigned h = 0;

    a[0] = a0; a[1] = a1; a[2] = a2; a[3] = a3; a[4] = a4; a[5] = a5;
    a[6] = a6; a[7] = a7; a[8] = a8; a[9] = a9; a[10] = a10; a[11] = a11;
    a[12] = a12; a[13] = a13; a[14] = a14; a[15] = a15; a[16] = a16;
    a[17] = a17; a[18] = a18; a[19] = a19; a[20] = a20; a[21] = a21;
    for (i = 0; i < 22; i++) {
        got[i] = a[i];
        h = h * 31 + a[i];
    }
    return h;
}

/* An indirect call with 19 stacked words: the target is parked in
 * FRAME_SLOT_DBL_TMP (slot 18) after the staging stores, and the
 * arguments come through the same pointer as the target. */
static void t_indirect(struct ops *o)
{
    int i;
    unsigned h;

    h = o->fn(o->v[0], o->v[1], o->v[2], o->v[3], o->v[4], o->v[5],
              o->v[6], o->v[7], o->v[8], o->v[9], o->v[10], o->v[11],
              o->v[12], o->v[13], o->v[14], o->v[15], o->v[16], o->v[17],
              o->v[18], o->v[19], o->v[20], o->v[21]);
    for (i = 0; i < 22; i++)
        eq("indirect", i, got[i], 0x1000u + (unsigned)i * 0x111u);
    {
        unsigned w = 0;
        for (i = 0; i < 22; i++)
            w = w * 31 + 0x1000u + (unsigned)i * 0x111u;
        eq("indirect-ret", 0, h, w);
    }
}

static void rec_s(short a0, short a1, short a2, short a3, short a4, short a5,
                  short a6, short a7, short a8, short a9, short a10, short a11,
                  short a12, short a13, short a14, short a15, short a16,
                  short a17, short a18, short a19, short a20, short a21,
                  short a22, short a23)
{
    got[0] = (unsigned)a0; got[1] = (unsigned)a1; got[2] = (unsigned)a2;
    got[3] = (unsigned)a3; got[4] = (unsigned)a4; got[5] = (unsigned)a5;
    got[6] = (unsigned)a6; got[7] = (unsigned)a7; got[8] = (unsigned)a8;
    got[9] = (unsigned)a9; got[10] = (unsigned)a10; got[11] = (unsigned)a11;
    got[12] = (unsigned)a12; got[13] = (unsigned)a13; got[14] = (unsigned)a14;
    got[15] = (unsigned)a15; got[16] = (unsigned)a16; got[17] = (unsigned)a17;
    got[18] = (unsigned)a18; got[19] = (unsigned)a19; got[20] = (unsigned)a20;
    got[21] = (unsigned)a21; got[22] = (unsigned)a22; got[23] = (unsigned)a23;
}

/* 21 stacked halfword arguments: the staging area covers the scratch slots
 * a halfword load goes through (SCRATCH/+1, DBL_TMP/+1). */
static void t_halfwords(const short *s)
{
    int i;

    rec_s(s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8], s[9],
          s[10], s[11], s[12], s[13], s[14], s[15], s[16], s[17], s[18],
          s[19], s[20], s[21], s[22], s[23]);
    for (i = 0; i < 24; i++)
        eq("halfwords", i, got[i], (unsigned)(int)(short)(-100 - i * 7));
}

/* The same through locals: halfword loads of locals use SCRATCH/+1. */
static void t_halfword_locals(void)
{
    volatile short l[24];
    short a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11;
    short a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23;
    int i;

    for (i = 0; i < 24; i++)
        l[i] = (short)(300 + i * 11);
    a0 = l[0]; a1 = l[1]; a2 = l[2]; a3 = l[3]; a4 = l[4]; a5 = l[5];
    a6 = l[6]; a7 = l[7]; a8 = l[8]; a9 = l[9]; a10 = l[10]; a11 = l[11];
    a12 = l[12]; a13 = l[13]; a14 = l[14]; a15 = l[15]; a16 = l[16];
    a17 = l[17]; a18 = l[18]; a19 = l[19]; a20 = l[20]; a21 = l[21];
    a22 = l[22]; a23 = l[23];
    rec_s(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14,
          a15, a16, a17, a18, a19, a20, a21, a22, a23);
    for (i = 0; i < 24; i++)
        eq("halfword-locals", i, got[i], (unsigned)(300 + i * 11));
}

int test_main(void)
{
    static struct sm mm;
    static struct ops o;
    static short s[24];
    int i;

    mm.flags = 0xf;
    mm.tlo = 0x80880040; mm.thi = 0x80882670;
    mm.plo = 0x80880000; mm.phi = 0x808c4fc0;
    mm.rods[0] = 0x20000001; mm.rods[1] = 0x80000000;
    mm.rods[2] = 0xa0000003; mm.rods[3] = 0xa0000001;
    t_members(&mm, 0x80880040, 0xc08c3fa0);

    o.fn = sum22;
    for (i = 0; i < 22; i++)
        o.v[i] = 0x1000u + (unsigned)i * 0x111u;
    t_indirect(&o);

    for (i = 0; i < 24; i++)
        s[i] = (short)(-100 - i * 7);
    t_halfwords(s);
    t_halfword_locals();

    us("\nchecks="); ud((unsigned)ck); us(" failures="); ud((unsigned)bad);
    us(bad ? "\nCALLARGS: FAIL\n" : "\nCALLARGS: PASS\n");
    return bad ? 1 : 0;
}
