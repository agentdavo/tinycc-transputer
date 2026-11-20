/* Expected: SSWTHREADOK\nH */
/*
 * test_switch_thread.c - tp_switch_thread (transputer-fnopt.h): a loop's
 * way back to its switch, threaded on the state it knows.
 *
 *   lex      CoreMark's core_state_transition shape: states set by
 *            constants, kept by cases, an INVALID state that ends the loop
 *   shared   several values to one case, a default, a state computed
 *            (not a constant) on one way back
 *   esc      the state's address is taken: never threaded
 *
 * Each runs over many inputs against the same machine written with a
 * volatile state (a function naming a volatile is never threaded).
 */

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c) { UART_DATA = (unsigned)(unsigned char)c; }
static void us(const char *s) { while (*s) up(*s++); }

enum st { S0, INV, S1, SINT, SFLT, SEXP, SSCI, NST };

static int isd(int c) { return c >= '0' && c <= '9'; }

static int lex(const unsigned char **ps, unsigned *cnt)
{
    const unsigned char *s = *ps;
    enum st state = S0;
    for (; *s && state != INV; s++) {
        int c = *s;
        if (c == ',') { s++; break; }
        switch (state) {
        case S0:
            if (isd(c)) state = SINT;
            else if (c == '+' || c == '-') state = S1;
            else if (c == '.') state = SFLT;
            else { state = INV; cnt[INV]++; }
            cnt[S0]++;
            break;
        case S1:
            if (isd(c)) state = SINT;
            else if (c == '.') state = SFLT;
            else state = INV;
            cnt[S1]++;
            break;
        case SINT:
            if (c == '.') { state = SFLT; cnt[SINT]++; }
            else if (!isd(c)) { state = INV; cnt[SINT]++; }
            break;
        case SFLT:
            if (c == 'E' || c == 'e') { state = S1 + 3; cnt[SFLT]++; }
            else if (!isd(c)) { state = INV; cnt[SFLT]++; }
            break;
        case SEXP:
            if (c == '+' || c == '-') state = SSCI;
            else state = INV;
            cnt[SEXP]++;
            break;
        case SSCI:
            if (!isd(c)) { state = INV; cnt[INV]++; }
            break;
        case INV:               /* never reached: a seventh case, a table */
            cnt[INV] += 100;
            break;
        default:
            break;
        }
    }
    *ps = s;
    return state;
}

static int lex_ref(const unsigned char **ps, unsigned *cnt)
{
    const unsigned char *s = *ps;
    volatile enum st state = S0;
    for (; *s && state != INV; s++) {
        int c = *s;
        if (c == ',') { s++; break; }
        switch (state) {
        case S0:
            if (isd(c)) state = SINT;
            else if (c == '+' || c == '-') state = S1;
            else if (c == '.') state = SFLT;
            else { state = INV; cnt[INV]++; }
            cnt[S0]++;
            break;
        case S1:
            if (isd(c)) state = SINT;
            else if (c == '.') state = SFLT;
            else state = INV;
            cnt[S1]++;
            break;
        case SINT:
            if (c == '.') { state = SFLT; cnt[SINT]++; }
            else if (!isd(c)) { state = INV; cnt[SINT]++; }
            break;
        case SFLT:
            if (c == 'E' || c == 'e') { state = S1 + 3; cnt[SFLT]++; }
            else if (!isd(c)) { state = INV; cnt[SFLT]++; }
            break;
        case SEXP:
            if (c == '+' || c == '-') state = SSCI;
            else state = INV;
            cnt[SEXP]++;
            break;
        case SSCI:
            if (!isd(c)) { state = INV; cnt[INV]++; }
            break;
        case INV:               /* never reached: a seventh case, a table */
            cnt[INV] += 100;
            break;
        default:
            break;
        }
    }
    *ps = s;
    return state;
}

/* several values share a case, a default, one state computed */
static unsigned shared(const unsigned char *s, int n, int mode)
{
    int state = 0, i;
    unsigned acc = 0;
    for (i = 0; i < n; i++) {
        int c = s[i];
        switch (state) {
        case 0: case 3: case 5:
            acc = acc * 3 + c;
            state = (c & 1) ? 1 : 2;
            break;
        case 1:
            acc ^= c;
            state = mode ? (c & 7) : 4;     /* computed: not threaded */
            break;
        case 2: case 4:
            acc += c << 2;
            state = 5;
            break;
        case 6:
            acc -= c;
            state = 0;
            break;
        default:
            acc += 7;
            state = 0;
            break;
        }
    }
    return acc + (unsigned)state;
}

static unsigned shared_ref(const unsigned char *s, int n, int mode)
{
    volatile int state = 0;
    int i;
    unsigned acc = 0;
    for (i = 0; i < n; i++) {
        int c = s[i];
        switch (state) {
        case 0: case 3: case 5:
            acc = acc * 3 + c;
            state = (c & 1) ? 1 : 2;
            break;
        case 1:
            acc ^= c;
            state = mode ? (c & 7) : 4;
            break;
        case 2: case 4:
            acc += c << 2;
            state = 5;
            break;
        case 6:
            acc -= c;
            state = 0;
            break;
        default:
            acc += 7;
            state = 0;
            break;
        }
    }
    return acc + (unsigned)state;
}

static void poke(int *p, int c) { if (c == 'x') *p = 6; }

/* the state's address escapes: a callee may change it */
static unsigned esc(const unsigned char *s, int n)
{
    int state = 0, i;
    unsigned acc = 0;
    for (i = 0; i < n; i++) {
        int c = s[i];
        switch (state) {
        case 0: acc += c; state = 1; poke(&state, c); break;
        case 1: acc ^= c; state = 2; break;
        case 2: acc += 3; state = 0; break;
        case 6: acc *= 5; state = 1; break;
        default: acc = ~acc; state = 0; break;
        }
    }
    return acc + (unsigned)state;
}

static unsigned esc_ref(const unsigned char *s, int n)
{
    volatile int state = 0;
    int i;
    unsigned acc = 0;
    for (i = 0; i < n; i++) {
        int c = s[i], st = state;
        switch (st) {
        case 0: acc += c; st = 1; if (c == 'x') st = 6; break;
        case 1: acc ^= c; st = 2; break;
        case 2: acc += 3; st = 0; break;
        case 6: acc *= 5; st = 1; break;
        default: acc = ~acc; st = 0; break;
        }
        state = st;
    }
    return acc + (unsigned)state;
}

static unsigned seed = 4242;
static unsigned rnd(void)
{
    seed = seed * 1103515245u + 12345u;
    return seed >> 16;
}

int test_main(void)
{
    static const char alpha[] = "0123456789+-.eE,x a";
    unsigned char buf[64];
    int bad = 0, k, i;

    for (k = 0; k < 400; k++) {
        int len = 1 + (int)(rnd() % 40);
        unsigned c1[NST], c2[NST];
        const unsigned char *p1, *p2;
        for (i = 0; i < len; i++)
            buf[i] = (unsigned char)alpha[rnd() % (sizeof alpha - 1)];
        buf[len] = 0;
        for (i = 0; i < NST; i++)
            c1[i] = c2[i] = 0;
        p1 = p2 = buf;
        while (*p1 || *p2) {
            int r1 = lex(&p1, c1), r2 = lex_ref(&p2, c2);
            if (r1 != r2 || p1 != p2) {
                bad++;
                break;
            }
        }
        for (i = 0; i < NST; i++)
            if (c1[i] != c2[i])
                bad++;
        if (shared(buf, len, k & 1) != shared_ref(buf, len, k & 1))
            bad++;
        if (esc(buf, len) != esc_ref(buf, len))
            bad++;
    }
    us(bad ? "SWTHREADFAIL\n" : "SWTHREADOK\n");
    return 0;
}
