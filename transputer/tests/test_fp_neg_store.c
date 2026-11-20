/*
 * test_fp_neg_store.c - a negated FP result stored through a pointer.
 *
 * Expected: SFPNEGOK\nH
 *
 * gen_opf(TOK_NEG) flips a float's sign bit in its slot (`ldl x; mint;
 * xor; stl x`).  The statement-end cleanup turned `ldlp x; fpstnlsn;
 * ldl x; <address>; stnl k` into `<address>; fpstnlsn`, taking every
 * integer instruction between `ldl x` and `stnl` for address code: here
 * `mint; xor`, which pops x, so `m[0] = -(a + b)` stored a + b (found in
 * dEngine's gluLookAt, whose translation column `- DotProduct(eye, u)` came
 * out with the wrong sign).  The address code must only push above x.
 * Also covers doubles, a store through an indexed pointer, a struct
 * member, a computed address, and a plain negated variable.
 */
typedef unsigned int u32;
#define UART (*(volatile unsigned int *)0x10000000u)
static void uc(int c) { UART = (unsigned char)c; }
static void us(const char *s) { while (*s) uc(*s++); }

static int bad;
static void check(int got, int want) { if (got != want) bad++; }

typedef float vec3[3];
#define DOT(x, y) ((x)[0] * (y)[0] + (x)[1] * (y)[1] + (x)[2] * (y)[2])
struct pose { float pad, z; double d; };

void look(const vec3 eye, float *m)
{
    vec3 u;
    u[0] = 0.5f; u[1] = 0.25f; u[2] = 2.0f;
    m[12] = -DOT(eye, u);
}
void neg_sum(float *m, float x, float y) { m[0] = -(x + y); }
void neg_prod(float *m, int i, float x, float y) { m[i] = -(x * y); }
void neg_var(float *m, float x) { m[1] = -x; }
void neg_member(struct pose *p, float x, float y) { p->z = -(x - y); }
void neg_double(double *m, double x, double y) { m[2] = -(x * y + 1.0); }
void neg_double_member(struct pose *p, double x) { p->d = -(x + x); }
void neg_deref(float **pp, float x, float y) { **pp = -(x + y); }

int test_main(void)
{
    vec3 eye;
    float m[16], *mp = &m[3];
    double d[4];
    struct pose p;
    eye[0] = 40.0f; eye[1] = 110.0f; eye[2] = -200.0f;
    look(eye, m);
    check((int)m[12], 352);
    neg_sum(m, 5.0f, 7.0f); check((int)m[0], -12);
    neg_prod(m, 2, 3.0f, 4.0f); check((int)m[2], -12);
    neg_var(m, 5.0f); check((int)m[1], -5);
    neg_member(&p, 2.0f, 9.0f); check((int)p.z, 7);
    neg_double(d, 3.0, 4.0); check((int)d[2], -13);
    neg_double_member(&p, 2.5); check((int)p.d, -5);
    neg_deref(&mp, 1.0f, 2.0f); check((int)m[3], -3);
    us(bad ? "FPNEGBAD" : "FPNEGOK");
    uc('\n');
    return 0;
}
