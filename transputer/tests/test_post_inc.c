/* Expected: SPOSTINCOK\nH */
/*
 * test_post_inc.c - x++ and x-- used for their value.
 *
 * On a word that is not volatile, inc() loads the saved value on its own
 * and lets the add read x again, instead of copying the loaded register
 * through a second workspace slot.  Each case checks the value the
 * expression yields and what x holds after: locals, globals, unsigned and
 * pointer words, members through a pointer, array elements, an lvalue
 * whose address comes from a call (which must run once), and the loop
 * shapes `while (n--)` and `*p++ = v`.  Volatile and narrow lvalues keep
 * the old path and are checked too.
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

struct s { int a; unsigned u; char *p; short h; };

static int g = 7;
static unsigned gu = 0xffffffffu;
static int arr[4] = { 10, 20, 30, 40 };
static int calls;
static struct s gs;

static int *pick(void)
{
    calls++;
    return &arr[2];
}

void test_main(void)
{
    int x = 5, y, n, sum;
    unsigned u = 0;
    char buf[8], *p = buf, *q;
    struct s st, *sp = &st;
    volatile int v = 3;
    short h = -1;
    signed char c = 127;
    int i;

    y = x++;            chk(__LINE__, y, 5); chk(__LINE__, x, 6);
    y = x--;            chk(__LINE__, y, 6); chk(__LINE__, x, 5);
    y = g++;            chk(__LINE__, y, 7); chk(__LINE__, g, 8);
    y = gu++;           chk(__LINE__, y, 0xffffffffu); chk(__LINE__, gu, 0);
    y = u--;            chk(__LINE__, y, 0); chk(__LINE__, u, 0xffffffffu);
    q = p++;            chk(__LINE__, q == buf, 1); chk(__LINE__, p == buf + 1, 1);
    q = p--;            chk(__LINE__, q == buf + 1, 1); chk(__LINE__, p == buf, 1);

    sp->a = 100; sp->u = 0x80000000u; sp->p = buf + 3; sp->h = 9;
    y = sp->a++;        chk(__LINE__, y, 100); chk(__LINE__, st.a, 101);
    y = sp->u--;        chk(__LINE__, y, 0x80000000u); chk(__LINE__, st.u, 0x7fffffffu);
    q = sp->p++;        chk(__LINE__, q == buf + 3, 1); chk(__LINE__, st.p == buf + 4, 1);
    y = sp->h++;        chk(__LINE__, y, 9); chk(__LINE__, st.h, 10);
    gs.a = -1;
    y = gs.a++;         chk(__LINE__, y, 0xffffffffu); chk(__LINE__, gs.a, 0);

    i = 1;
    y = arr[i]++;       chk(__LINE__, y, 20); chk(__LINE__, arr[1], 21);
    y = arr[i++]--;     chk(__LINE__, y, 21); chk(__LINE__, arr[1], 20); chk(__LINE__, i, 2);

    calls = 0;
    y = (*pick())++;    chk(__LINE__, y, 30); chk(__LINE__, arr[2], 31); chk(__LINE__, calls, 1);

    y = v++;            chk(__LINE__, y, 3); chk(__LINE__, v, 4);
    y = h--;            chk(__LINE__, y, 0xffffffffu); chk(__LINE__, h, 0xfffffffeu);
    y = c++;            chk(__LINE__, y, 127); chk(__LINE__, c, 0xffffff80u);

    /* the loop shapes */
    n = 4; sum = 0;
    while (n--)
        sum += n;
    chk(__LINE__, sum, 3 + 2 + 1 + 0); chk(__LINE__, n, 0xffffffffu);
    n = 3; sum = 0;
    do
        sum++;
    while (n--);
    chk(__LINE__, sum, 4); chk(__LINE__, n, 0xffffffffu);
    p = buf;
    for (i = 0; i < 8; i++)
        *p++ = (char)(i * 3);
    chk(__LINE__, p == buf + 8, 1);
    for (i = 0, sum = 0; i < 8; i++)
        sum += buf[i];
    chk(__LINE__, sum, 3 * 28);
    /* both sides of an expression */
    x = 1;
    y = x++ + x++;
    chk(__LINE__, x, 3);
    us(bad ? "\nPOSTINCFAIL\n" : "POSTINCOK\n");
}
