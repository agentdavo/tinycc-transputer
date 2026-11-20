/* Whole-object argument staging: seven, sixteen and sixty-five byte values. */
typedef unsigned int u32;
#define UART (*(volatile unsigned int *)0x10000000u)
unsigned int __ldtimer(void);
static void put(const char *s) { while (*s) UART = (unsigned char)*s++; }
static void num(u32 n) { char b[12]; int i = 0; do { b[i++] = '0' + n % 10; } while (n /= 10); while (i) UART = b[--i]; }
struct a { unsigned char b[7]; };
struct b { unsigned char b[16]; };
struct c { unsigned char b[65]; };
static struct a a;
static struct b b;
static struct c c;
static u32 __attribute__((noinline)) take(struct a x, struct b y, struct c z, u32 n)
{ return x.b[n % 7] + y.b[n % 16] + z.b[n % 65] + n; }
void test_main(void)
{
    u32 i, h = 0, t;
    for (i = 0; i < 7; i++) a.b[i] = i * 7;
    for (i = 0; i < 16; i++) b.b[i] = i * 5;
    for (i = 0; i < 65; i++) c.b[i] = i * 3;
    t = __ldtimer();
    for (i = 0; i < 2000; i++) h = (h * 33) ^ take(a, b, c, i);
    t = __ldtimer() - t;
    put("cycles "); num(t); put("\nhash "); num(h); put("\n");
}
