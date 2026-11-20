/* Expected: SRANGEFLAGSOK\nH */
/*
 * test_range_flags.c - the second CoreMark round's post-pass rules
 * (transputer-fnopt.h):
 *
 *   tpn_late           range rules after the scheduler: a switch bounds
 *                      check on a state that only holds case values, `eqc 1`
 *                      of a truth value, `lo <= x & x <= hi` as one unsigned
 *                      test, (a & m) op (b & m) as (a op b) & m
 *   tp_thread_jumps    a jump to a `j` goes to its target
 *   tp_thread_flags    a flag set to a constant just before a short block
 *                      that tests it: the test resolved on that way in
 *   tp_inline_leaves   a tiny leaf's body in place of its direct call
 *
 * Each result is checked against a value computed another way, at the
 * edges of the ranges involved.
 */

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c) { UART_DATA = (unsigned)(unsigned char)c; }
static void us(const char *s) { while (*s) up(*s++); }
static void uh(unsigned v)
{
    int i;
    for (i = 28; i >= 0; i -= 4) {
        int n = (v >> i) & 0xF;
        up(n < 10 ? '0' + n : 'a' + n - 10);
    }
}

static int bad;
static void chk(int line, unsigned got, unsigned want)
{
    if (got != want) {
        bad++;
        us("\nFAIL line ");
        uh((unsigned)line);
        us(" got ");
        uh(got);
        us(" want ");
        uh(want);
    }
}
#define CHK(g, w) chk(__LINE__, (unsigned)(g), (unsigned)(w))

/* ---- range tests ---- */
static unsigned char isdig(unsigned char c)
{
    unsigned char r;
    r = ((c >= '0') & (c <= '9')) ? 1 : 0;
    return r;
}
static int in_int(int x) { return (x >= -5) & (x <= 7); }
static int in_int2(int x) { return (x <= 100) & (x >= 100); }
static int in_low(int x) { return (x >= 0) & (x <= 9); }
static int in_char(signed char c) { return (c >= -3) & (c <= 3); }

/* ---- a state machine whose state only holds case values ---- */
static int run_states(const char *s, int *counts)
{
    int state = 0;
    for (; *s; s++) {
        char c = *s;
        switch (state) {
        case 0: state = isdig(c) ? 1 : (c == '-' ? 2 : 7); break;
        case 1: if (!isdig(c)) state = c == '.' ? 3 : 7; break;
        case 2: state = isdig(c) ? 1 : 7; break;
        case 3: state = isdig(c) ? 4 : 7; break;
        case 4: if (c == 'e') state = 5; else if (!isdig(c)) state = 7; break;
        case 5: state = isdig(c) ? 6 : 7; break;
        case 6: if (!isdig(c)) state = 7; break;
        default: break;
        }
        counts[state]++;
    }
    return state;
}

/* only constants are stored to st: its jump table needs no bounds check */
static int states2(const char *s)
{
    int st = 0, n = 0;
    for (; *s; s++) {
        switch (st) {
        case 0: if (*s == 'a') st = 1; else st = 2; break;
        case 1: st = 3; n++; break;
        case 2: st = 4; break;
        case 3: st = 5; break;
        case 4: st = 6; break;
        case 5: st = 0; n += 2; break;
        case 6: st = 1; break;
        }
    }
    return st * 100 + n;
}

/* ---- masks ---- */
static unsigned mx(unsigned a, unsigned b) { return (a & 1) ^ (b & 1); }
static unsigned mo(unsigned a, unsigned b) { return (a & 0xff) | (b & 0xff); }
static unsigned ma(unsigned a, unsigned b) { return (a & 15) & (b & 15); }

/* ---- a carry flag, as crcu8 has it ---- */
static unsigned short crc_flag(unsigned char data, unsigned short crc)
{
    unsigned char i, x16, carry;
    for (i = 0; i < 8; i++) {
        x16 = (unsigned char)((data & 1) ^ ((unsigned char)crc & 1));
        data >>= 1;
        if (x16 == 1) {
            crc ^= 0x4002;
            carry = 1;
        } else
            carry = 0;
        crc >>= 1;
        if (carry)
            crc |= 0x8000;
        else
            crc &= 0x7fff;
    }
    return crc;
}
static unsigned short crc_ref(unsigned char data, unsigned short crc)
{
    int i;
    for (i = 0; i < 8; i++) {
        unsigned b = (data ^ crc) & 1;
        data >>= 1;
        crc = (unsigned short)(b ? ((crc ^ 0x4002) >> 1) | 0x8000
                                 : (crc >> 1) & 0x7fff);
    }
    return crc;
}
/* the flag is read again later, so its store must stay */
static int flag_live(int a, int *out)
{
    int f;
    if (a > 10) {
        a -= 3;
        f = 1;
    } else
        f = 0;
    a += 1;
    if (f)
        a *= 2;
    *out = f;
    return a;
}
/* ok = 0 falls into the test, which the conditional jumps over each
 * store also reach (through the padding in front of it): only the way
 * that stored 0 may skip it (test_reloc_static_data's shape) */
static int all_ok(int a, int b, int c)
{
    int ok = 1;
    if (a != 1)
        ok = 0;
    if (b != 2)
        ok = 0;
    if (c != 3)
        ok = 0;
    if (ok)
        return 100;
    return 200;
}

/* ---- loads hoisted out of loops that store nothing (tp_hoist) ---- */
struct node { struct node *next; short *info; };
static struct node *find(struct node *l, const short *key)
{
    while (l && (l->info[1] != key[1]))     /* key[1] loaded once */
        l = l->next;
    return l;
}
/* the loop writes through a pointer: key[0] must be read every time */
static int find_store(short *a, short *key, int n)
{
    int i;
    for (i = 0; i < n && a[i] != key[0]; i++)
        if (i == 2)
            key[0] = 9;
    return i;
}
/* a local whose address is out, written in the loop, read through p */
static int esc_sum(void)
{
    int x = 1, s = 0, i;
    int *p = &x;
    for (i = 0; i < 5; i++) {
        s += *p;
        x = i * 10;
    }
    return s;
}

/* ---- halfword locals (T800: stored as two sb, now as words) ---- */
static unsigned pack2(unsigned a, unsigned b)
{
    unsigned short x = (unsigned short)a, y = (unsigned short)b;
    int i;
    for (i = 0; i < 3; i++) {
        x = (unsigned short)(x * 3 + 1);
        y = (unsigned short)(y + x);
    }
    return ((unsigned)x << 16) | y;
}
static unsigned half_addr(unsigned a)
{
    unsigned short z = (unsigned short)a, w = 7;
    unsigned short *pz = &z;
    *pz = (unsigned short)(*pz + 0x8001);
    z = (unsigned short)(z ^ w);
    return ((unsigned)z << 16) | w;
}
static int half_signed(int a)
{
    short s = (short)a;
    s = (short)(s * 2);
    return s;
}
/* !(K > x) as x > K - 1, at the edges */
static int not_gt(int x) { int r = 0; if (!(5 > x - 2)) r = 1; return r; }
static int not_gtu(unsigned x) { int r = 0; if (!(1u > x)) r = 1; return r; }

/* ---- char loop counters: the loop test bounds the increment ---- */
static int cnt_loop(void)
{
    unsigned char i;
    int s = 0;
    for (i = 0; i < 8; i++)
        s += i;
    return s * 100 + i;
}
/* an inner loop steps i past 255 too: the mask must stay */
static int cnt_inner(void)
{
    unsigned char i;
    int n = 0;
    for (i = 250; i <= 252; i++)
        while (n < 5 && (n++, 1))
            i++;
    return i * 1000 + n;
}

/* ---- strength reduction (tp_strength_reduce) ---- */
static void mm(const short *A, const short *B, int *C, unsigned N)
{
    unsigned i, j, k;
    for (i = 0; i < N; i++)
        for (j = 0; j < N; j++) {
            C[i * N + j] = 0;
            for (k = 0; k < N; k++)
                C[i * N + j] += A[i * N + k] * B[k * N + j];
        }
}
/* the same product, k outermost: a different shape to compare against */
static void mm_ref(const short *A, const short *B, int *C, unsigned N)
{
    unsigned i, j, k;
    for (i = 0; i < N * N; i++)
        C[i] = 0;
    for (k = 0; k < N; k++)
        for (i = 0; i < N; i++) {
            int a = A[i * N + k];
            for (j = 0; j < N; j++)
                C[i * N + j] += a * B[k * N + j];
        }
}
/* k read after the loop: the counter must stay */
static int sum_after(const int *a, unsigned n)
{
    unsigned k;
    int s = 0;
    for (k = 0; k < n; k++)
        s += a[k];
    return s * 100 + (int)k;
}
/* k used beside the address */
static int sum_k(const int *a, int n)
{
    int k, s = 0;
    for (k = 0; k < n; k++)
        s += a[k] * k;
    return s;
}

/* ---- the bottom test's load kept for the top (tp_loop_cse) ---- */
static int walk(const char *str, int *n)
{
    int state = 0;
    for (; *str && state != 1; str++) {
        char c = *str;
        if (c == ',')
            break;
        state = c == 'x' ? 1 : 2;
        (*n)++;
    }
    return state;
}
/* c read after the loop: its last value must be the body's */
static int walk_after(const char *str)
{
    char c = 0;
    for (; *str; str++)
        c = *str;
    return c;
}
/* the loop writes memory between the two loads: no merging */
static int walk_store(char *str)
{
    int n = 0;
    for (; *str; str++) {
        char c = *str;
        n += c;
        str[1] = str[1] == 'z' ? 0 : str[1];
    }
    return n;
}

/* ---- jumps to jumps ---- */
static int chain(int a, int b)
{
    int r = 0;
    if (a) {
        if (b)
            r = 1;
        else
            r = 2;
    } else {
        if (b > 3)
            r = 3;
    }
    return r;
}

/* ---- leaves: inlined, and ones that must not be ---- */
static int sq(int x) { return x * x; }
static unsigned lo4(unsigned x) { return x & 15; }
static int two(int a, int b) { return a - b; }
int __attribute__((weak)) weakleaf(int x) { return x + 1; }

int main(void)
{
    int i, counts[8] = {0}, f;
    static const char *const nums = "12.5e3-7x";

    for (i = 0; i < 256; i++)
        CHK(isdig((unsigned char)i), i >= '0' && i <= '9');
    for (i = -40; i < 40; i++) {
        CHK(in_int(i), i >= -5 && i <= 7);
        CHK(in_int2(i + 80), i + 80 == 100);
        CHK(in_low(i), i >= 0 && i <= 9);
        CHK(in_char((signed char)i), i >= -3 && i <= 3);
    }
    CHK(in_int(0x7fffffff), 0);
    CHK(in_int(-0x7fffffff - 1), 0);
    CHK(in_low(-0x7fffffff - 1), 0);
    CHK(in_low(0x7fffffff), 0);

    CHK(run_states(nums, counts), 7);
    CHK(counts[1], 2); CHK(counts[3], 1); CHK(counts[4], 1);
    CHK(counts[5], 1); CHK(counts[6], 1); CHK(counts[7], 3);
    CHK(run_states("-0.25", counts), 4);
    CHK(states2("abcdefgh"), 103);
    CHK(states2("xa"), 400);
    CHK(states2(""), 0);

    CHK(mx(3, 2), 1); CHK(mx(3, 5), 0); CHK(mx(0xfffffffe, 1), 1);
    CHK(mo(0x1234, 0x5678), 0x7c); CHK(ma(0xff, 0x3c), 0xc);

    for (i = 0; i < 256; i++) {
        CHK(crc_flag((unsigned char)i, 0), crc_ref((unsigned char)i, 0));
        CHK(crc_flag((unsigned char)i, 0xbeef),
            crc_ref((unsigned char)i, 0xbeef));
    }
    CHK(flag_live(20, &f), 36); CHK(f, 1);
    CHK(flag_live(5, &f), 6); CHK(f, 0);
    CHK(all_ok(1, 2, 3), 100); CHK(all_ok(1, 2, 4), 200);
    CHK(all_ok(0, 2, 3), 200); CHK(all_ok(1, 0, 3), 200);
    CHK(pack2(0x12345, 0xfffe), 0xb8545f93);
    CHK(half_addr(0x1fffe), 0x7ff80007);
    CHK(half_signed(0x4001), -32766);
    CHK(half_signed(-3), -6);
    CHK(not_gt(6), 0); CHK(not_gt(7), 1); CHK(not_gt(-100), 0);
    CHK(not_gtu(0), 0); CHK(not_gtu(1), 1); CHK(not_gtu(0xffffffffu), 1);
    {
        static short A[25], B[25];
        static int C1[25], C2[25], ia[7] = {3, -1, 4, 1, -5, 9, 2};
        unsigned N, q;
        for (q = 0; q < 25; q++) {
            A[q] = (short)(q * 7 - 40);
            B[q] = (short)(300 - q * 13);
        }
        for (N = 0; N <= 5; N++) {
            for (q = 0; q < 25; q++)
                C1[q] = C2[q] = 0x5a5a;
            mm(A, B, C1, N);
            mm_ref(A, B, C2, N);
            for (q = 0; q < 25; q++)
                CHK(C1[q], C2[q]);
        }
        CHK(sum_after(ia, 7), 1307);
        CHK(sum_after(ia, 0), 0);
        CHK(sum_k(ia, 7), -1 + 8 + 3 - 20 + 45 + 12);
        CHK(sum_k(ia, 0), 0);
    }
    {
        int cnt = 0;
        static char ws[] = "abzcd";
        CHK(walk("abc", &cnt), 2); CHK(cnt, 3);
        cnt = 0; CHK(walk("abxcd", &cnt), 1); CHK(cnt, 3);
        cnt = 0; CHK(walk("a,b", &cnt), 2); CHK(cnt, 1);
        cnt = 0; CHK(walk("", &cnt), 0); CHK(cnt, 0);
        CHK(walk_after("hello"), 'o');
        CHK(walk_after(""), 0);
        CHK(walk_store(ws), 'a' + 'b');
    }
    CHK(cnt_loop(), 2808);
    CHK(cnt_inner(), 253005);
    {
        static short i0[2] = {0, 5}, i1[2] = {0, 6}, i2[2] = {0, 7};
        static struct node n2 = {0, i2}, n1 = {&n2, i1}, n0 = {&n1, i0};
        short k6[2] = {0, 6}, k8[2] = {0, 8};
        short arr[6] = {1, 2, 3, 4, 9, 6}, key[1] = {5};
        CHK(find(&n0, k6) == &n1, 1);
        CHK(find(&n0, k8) == 0, 1);
        CHK(find_store(arr, key, 6), 4);
        CHK(esc_sum(), 1 + 0 + 10 + 20 + 30);
    }

    CHK(chain(1, 1), 1); CHK(chain(1, 0), 2);
    CHK(chain(0, 5), 3); CHK(chain(0, 2), 0);

    CHK(sq(7) + sq(-3), 58);
    CHK(lo4(0x1234567f) + lo4(3), 18);
    CHK(two(9, 4), 5);
    CHK(weakleaf(41), 42);

    us(bad ? "\nRANGEFLAGSFAIL\n" : "RANGEFLAGSOK\n");
    return 0;
}
