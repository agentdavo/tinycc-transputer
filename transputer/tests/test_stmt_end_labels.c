/*
 * test_stmt_end_labels.c - the statement-end cleanup (tp_stmt_end) must
 * not reach back across a label.
 *
 * Expected: SLABELSOK\nH
 *
 * A declaration's initializer is not followed by a statement-end cleanup,
 * so its parking - a float result stored to a register slot and copied to
 * the variable, `ldlp x; fpstnlsn; ldl x; stl k' - is still in the window
 * when the next statement ends.  When a label lies between the two (a loop
 * head with no test in front of it, a `continue' target, a goto label, a
 * case label), the cleanup rewrote the code before the label, so the code
 * moved and the jumps to the label landed inside it.  tccgen's gind() now
 * closes the window (tp_label_here).  Each function below puts one such
 * declaration in front of one kind of label; the results are hashed bit for
 * bit against an x86-64 host (cc -DHOST -ffp-contract=off).  Without
 * tp_label_here, goto_label and nested came out wrong at -mt9000 and
 * -mt800 (a loop that opens a loop pool happened to start a new window
 * anyway); the other shapes are there for whatever changes that.
 */
typedef unsigned int u32;

#ifdef HOST
#include <stdio.h>
#else
#define UART (*(volatile unsigned int *)0x10000000u)
static void uc(int c) { UART = (unsigned char)c; }
static void us(const char *s) { while (*s) uc(*s++); }
static void uh(u32 v)
{
    int i;
    for (i = 28; i >= 0; i -= 4)
        uc("0123456789abcdef"[(v >> i) & 15]);
}
#endif

static u32 h = 2166136261u;
static void hf(float f)
{
    union { float f; u32 u; } x;

    x.f = f;
    h = (h ^ x.u) * 16777619u;
}
static void hd(double d)
{
    union { double d; u32 u[2]; } x;

    x.d = d;
    h = (h ^ x.u[0]) * 16777619u;
    h = (h ^ x.u[1]) * 16777619u;
}
static void hi(int i)
{
    h = (h ^ (u32)i) * 16777619u;
}

static float out[16];

/* the head of a do-while */
static float do_head(float a, float b, int n)
{
    float s = 0.0f;
    float k = a * b + 1.0f;

    do {
        s = s + k;
        k = k * 0.5f;
    } while (--n > 0);
    return s;
}

/* the head of for (;;): no test is emitted in front of the body */
static float forever_head(float a, float b, int n)
{
    float s = 1.0f;
    float k = a - b * 0.25f;

    for (;;) {
        s = s * 0.75f + k;
        if (--n <= 0)
            break;
    }
    return s;
}

/* the head of while (1) */
static float while1_head(float a, float b, int n)
{
    float s = 2.0f;
    float k = a * a - b;

    while (1) {
        s = s - k * 0.125f;
        if (--n <= 0)
            break;
    }
    return s;
}

/* the `continue' target of a for loop, after a declaration that ends the
 * body (its initializer has the side effect) */
static float continue_target(const float *v, int n)
{
    float s = 0.0f;
    int i;

    for (i = 0; i < n; i++) {
        if (v[i] < 0.0f)
            continue;
        s = s + v[i];
        float t = (out[i] = s * 0.5f + v[i]);
    }
    return s;
}

/* a goto label */
static float goto_label(float a, float b, int n)
{
    float s = 0.0f;
    float k = a / b + 0.5f;

again:
    s = s + k * 0.25f;
    if (--n > 0)
        goto again;
    return s;
}

/* a case label, reached by the dispatch and by falling through */
static float case_label(int sel, float a, float b)
{
    float s = 0.0f;

    switch (sel) {
    case 0:
        s = a + b;
        float u = (out[0] = s * a + b);
    case 1:
        s = s * 2.0f + a;
        break;
    case 2:
        s = a - b;
        float w = (out[1] = s * b - a);
    default:
        s = s + b * 0.5f;
    }
    return s;
}

/* doubles: two-word parks */
static double do_head_d(double a, double b, int n)
{
    double s = 0.0;
    double k = a * b + 1.0;

    do {
        s = s + k;
        k = k * 0.5;
    } while (--n > 0);
    return s;
}

/* an integer declaration in front of a loop head, and a nested loop whose
 * head follows a float declaration in the outer body */
static int nested(int n)
{
    int t = 0;
    int i, j;

    for (i = 0; i < n; i++) {
        float f = (float)i * 0.5f + 1.0f;
        int m = (int)f + n;

        for (;;) {
            t = t * 3 + m;
            if (--m <= n)
                break;
        }
    }
    j = t ^ n;
    return j;
}

static void run(void)
{
    static const float v[8] = { 1.5f, -2.0f, 3.25f, 0.5f, -0.75f, 4.0f,
                                2.5f, -1.0f };
    int n, i;

    for (n = 1; n <= 5; n++) {
        float a = (float)n * 1.25f, b = 3.0f - (float)n * 0.5f;

        hf(do_head(a, b, n));
        hf(forever_head(a, b, n));
        hf(while1_head(a, b, n));
        hf(goto_label(a, b, n));
        for (i = 0; i < 4; i++)
            hf(case_label(i, a, b));
        hd(do_head_d(a, b, n));
        hi(nested(n));
    }
    hf(continue_target(v, 8));
    for (i = 0; i < 8; i++)
        hf(out[i]);
}

#define WANT 0xde2ff87eu

#ifdef HOST
int main(void)
{
    run();
    printf("hash %08x\n", h);
    return 0;
}
#else
void test_main(void)
{
    run();
    if (h == WANT)
        us("LABELSOK\n");
    else {
        us("LABELSBAD ");
        uh(h);
        uc('\n');
    }
}
#endif
