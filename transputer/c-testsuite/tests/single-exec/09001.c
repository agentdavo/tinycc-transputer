#include <stdio.h>

/* Targeted coverage for the store-side address/value swap: struct field
 * writes, chained assignments whose result is used, aliasing between the
 * value expression and the destination pointer, and writes through indexed
 * and nested bases. */

struct s { int a, b, c, d; };
struct t { struct s *p; int k; };

static struct s g1, g2;

static int fail;

static void chk(int got, int want)
{
    if (got != want)
        fail++;
}

/* plain field write */
static void w_field(struct s *p, int v) { p->d = v; }

/* field write from another object's field */
static void w_copy(struct s *p, struct s *q) { p->d = q->b; }

/* field write of a constant */
static void w_const(struct s *p) { p->c = 77; }

/* chained assignment: the result of the inner store is used */
static int w_chain(struct s *p, int v) { return p->d = v; }

/* the value expression reads through the very pointer being written */
static void w_self(struct s *p) { p->d = p->a + 1; }

/* the value expression reads the destination field itself */
static void w_selffield(struct s *p) { p->d = p->d + 3; }

/* nested base: write through a pointer stored in another struct */
static void w_nested(struct t *t, int v) { t->p->c = v; }

/* two adjacent writes sharing a base */
static void w_two(struct s *p, int x, int y) { p->a = x; p->b = y; }

/* write into an array element's field */
static void w_arr(struct s *a, int i, int v) { a[i].d = v; }

int main(void)
{
    struct s arr[3];
    struct t t;
    int r;

    g1.a = 1; g1.b = 2; g1.c = 3; g1.d = 4;
    g2.a = 5; g2.b = 6; g2.c = 7; g2.d = 8;

    w_field(&g1, 41);
    chk(g1.d, 41);
    chk(g1.a, 1); chk(g1.b, 2); chk(g1.c, 3);

    w_copy(&g1, &g2);
    chk(g1.d, 6);

    w_const(&g1);
    chk(g1.c, 77);

    r = w_chain(&g1, 99);
    chk(r, 99);
    chk(g1.d, 99);

    g1.a = 10;
    w_self(&g1);
    chk(g1.d, 11);

    w_selffield(&g1);
    chk(g1.d, 14);

    t.p = &g2;
    t.k = 0;
    w_nested(&t, 55);
    chk(g2.c, 55);
    chk(t.k, 0);

    w_two(&g1, 21, 22);
    chk(g1.a, 21);
    chk(g1.b, 22);

    arr[0].d = 0; arr[1].d = 0; arr[2].d = 0;
    w_arr(arr, 2, 33);
    chk(arr[2].d, 33);
    chk(arr[0].d, 0);
    chk(arr[1].d, 0);

    /* multiple assignment through two different pointers */
    {
        int v;
        v = g1.d = g2.d = 7;
        chk(v, 7);
        chk(g1.d, 7);
        chk(g2.d, 7);
    }

    printf("fail=%d\n", fail);
    if (fail)
        return 1;
    return 0;
}
