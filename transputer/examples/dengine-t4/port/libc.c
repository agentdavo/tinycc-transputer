/* Freestanding C library subset for the dEngine T4 port.
 *
 * Everything dEngine's original sources call: stdio over a read-only ROM
 * image, printf/scanf, a heap, strtod, ctype and a software libm. The same
 * file is compiled for the host and for the T800, so the two builds run the
 * same parsing and floating-point code. It is not a general C library:
 * there is no writable file system, locale or errno. */
#include <stdarg.h>
#include <stddef.h>
#include "include/stdio.h"
#include "include/stdlib.h"
#include "include/string.h"
#include "include/math.h"
#include "include/ctype.h"
#include "include/sys/time.h"
#include "platform.h"

/* ------------------------------------------------------------------ heap */
/* First fit over an address-ordered free list with coalescing. Allocation
 * order is deterministic, so every node gets the same addresses. */
typedef struct block { size_t size; struct block *next; } block;
#define ALIGN 8u
#define HEADER ((sizeof(block)+ALIGN-1u)&~(ALIGN-1u))
static unsigned char *heap_top, *heap_end;
static block *free_list;
static size_t heap_peak, heap_live;
static int heap_segment;

/* The platform may split the heap into segments (a T4 node's RAM has the
 * framebuffer window in the middle); allocation moves to the next one when
 * the current one is used up. */
static int heap_open(int segment)
{
    unsigned char *start, *end;
    if (!de_platform_heap(segment, &start, &end)) return 0;
    start = (unsigned char *)(((size_t)start + ALIGN - 1u) & ~(size_t)(ALIGN - 1u));
    if (end <= start) return 0;
    heap_top = start; heap_end = end; heap_segment = segment;
    return 1;
}
static void heap_init(void) { if (!heap_open(0)) de_platform_fatal("no heap"); }

void *malloc(size_t n)
{
    block **link, *b;
    size_t need;
    if (!heap_end) heap_init();
    if (n > (size_t)0x7fffffffu) return 0;
    need = HEADER + ((n + ALIGN - 1u) & ~(size_t)(ALIGN - 1u));
    for (link = &free_list; (b = *link) != 0; link = &b->next) {
        if (b->size >= need) {
            if (b->size - need >= HEADER + 32u) {
                block *rest = (block *)((unsigned char *)b + need);
                rest->size = b->size - need; rest->next = b->next;
                *link = rest; b->size = need;
            } else *link = b->next;
            heap_live += b->size;
            if (heap_live > heap_peak) heap_peak = heap_live;
#ifdef DE_HEAP_TRACE
            if (n >= DE_HEAP_TRACE) printf("[heap] + %p %lu\n", (void *)((unsigned char *)b + HEADER), (unsigned long)n);
#endif
            return (unsigned char *)b + HEADER;
        }
    }
    while ((size_t)(heap_end - heap_top) < need) {
        unsigned char *old_top = heap_top, *old_end = heap_end;
        if (!heap_open(heap_segment + 1)) { de_platform_out_of_memory(n); return 0; }
        if ((size_t)(old_end - old_top) >= HEADER + 32u) {
            /* The rest of the old segment becomes a free block. */
            b = (block *)old_top;
            b->size = (size_t)(old_end - old_top) & ~(size_t)(ALIGN - 1u);
            heap_live += b->size;
            free((unsigned char *)b + HEADER);
        }
    }
    b = (block *)heap_top; b->size = need; heap_top += need;
    heap_live += need;
    if (heap_live > heap_peak) heap_peak = heap_live;
#ifdef DE_HEAP_TRACE
    if (n >= DE_HEAP_TRACE) printf("[heap] + %p %lu\n", (void *)((unsigned char *)b + HEADER), (unsigned long)n);
#endif
    return (unsigned char *)b + HEADER;
}

void free(void *p)
{
    block *b, *prev = 0, *cur;
    if (!p) return;
#ifdef DE_HEAP_TRACE
    printf("[heap] - %p\n", p);
#endif
    b = (block *)((unsigned char *)p - HEADER);
    heap_live -= b->size;
    for (cur = free_list; cur && cur < b; cur = cur->next) prev = cur;
    b->next = cur;
    if (prev) prev->next = b; else free_list = b;
    if (cur && (unsigned char *)b + b->size == (unsigned char *)cur) {
        b->size += cur->size; b->next = cur->next;
    }
    if (prev && (unsigned char *)prev + prev->size == (unsigned char *)b) {
        prev->size += b->size; prev->next = b->next; b = prev;
    }
    if ((unsigned char *)b + b->size == heap_top) {
        if (prev && prev->next == b) prev->next = 0;
        else {
            block *q;
            if (free_list == b) free_list = b->next;
            else for (q = free_list; q; q = q->next) if (q->next == b) { q->next = b->next; break; }
        }
        heap_top = (unsigned char *)b;
    }
}

void *calloc(size_t count, size_t size)
{
    size_t n;
    void *p;
    if (size && count > (size_t)0x7fffffffu / size) return 0;
    n = count * size;
    /* One zeroed byte more than asked for: dEngine's FS_OpenFile callocs a
     * file's exact size, and LoadShader passes that buffer to glShaderSource
     * as a NUL-terminated string. */
    p = malloc(n + 1);
    if (p) memset(p, 0, n + 1);
    return p;
}

void *realloc(void *p, size_t n)
{
    block *b;
    void *q;
    size_t have;
    if (!p) return malloc(n);
    if (!n) { free(p); return 0; }
    b = (block *)((unsigned char *)p - HEADER);
    have = b->size - HEADER;
    if (n <= have) {
        /* Shrink in place and free the tail: dEngine's OBJ loader allocates
         * 65535 vertices per model and then reallocs to the real count. */
        size_t need = HEADER + ((n + ALIGN - 1u) & ~(size_t)(ALIGN - 1u));
        if (b->size - need >= HEADER + 32u) {
            block *tail = (block *)((unsigned char *)b + need);
            tail->size = b->size - need;
            b->size = need;
            free((unsigned char *)tail + HEADER);   /* takes the tail off heap_live */
        }
        return p;
    }
    q = malloc(n);
    if (!q) return 0;
    memcpy(q, p, have);
    free(p);
    return q;
}

size_t de_heap_peak(void) { return heap_peak; }
size_t de_heap_live(void) { return heap_live; }

/* --------------------------------------------------------------- strings */
#ifdef __transputer__
void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst; const unsigned char *s = src;
    if ((((size_t)d | (size_t)s | n) & 3u) == 0) {
        unsigned int *dw = (unsigned int *)d; const unsigned int *sw = (const unsigned int *)s;
        size_t w = n >> 2;
        while (w--) *dw++ = *sw++;
        return dst;
    }
    while (n--) *d++ = *s++;
    return dst;
}
void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst; const unsigned char *s = src;
    if (d == s || !n) return dst;
    if (d < s || d >= s + n) return memcpy(dst, src, n);
    d += n; s += n;
    while (n--) *--d = *--s;
    return dst;
}
void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;
    if ((((size_t)d | n) & 3u) == 0) {
        unsigned int v = (unsigned char)c, *w = (unsigned int *)d;
        size_t k = n >> 2;
        v |= v << 8; v |= v << 16;
        while (k--) *w++ = v;
        return dst;
    }
    while (n--) *d++ = (unsigned char)c;
    return dst;
}
int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++) if (*x != *y) return *x < *y ? -1 : 1;
    return 0;
}
void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = s;
    for (; n; n--, p++) if (*p == (unsigned char)c) return (void *)p;
    return 0;
}
size_t strlen(const char *s) { const char *p = s; while (*p) p++; return (size_t)(p - s); }
char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++) != 0) ; return r; }
char *strncpy(char *d, const char *s, size_t n)
{
    char *r = d;
    while (n && *s) { *d++ = *s++; n--; }
    while (n--) *d++ = 0;
    return r;
}
char *strcat(char *d, const char *s) { strcpy(d + strlen(d), s); return d; }
char *strncat(char *d, const char *s, size_t n)
{
    char *e = d + strlen(d);
    while (n-- && *s) *e++ = *s++;
    *e = 0;
    return d;
}
int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
        if (!*a) return 0;
    }
    return 0;
}
int strcasecmp(const char *a, const char *b)
{
    int x, y;
    do { x = tolower((unsigned char)*a++); y = tolower((unsigned char)*b++); } while (x && x == y);
    return x - y;
}
char *strchr(const char *s, int c)
{
    for (;; s++) { if (*s == (char)c) return (char *)s; if (!*s) return 0; }
}
char *strrchr(const char *s, int c)
{
    const char *r = 0;
    for (;; s++) { if (*s == (char)c) r = s; if (!*s) return (char *)r; }
}
char *strstr(const char *h, const char *n)
{
    size_t k = strlen(n);
    if (!k) return (char *)h;
    for (; *h; h++) if (*h == *n && !strncmp(h, n, k)) return (char *)h;
    return 0;
}
#endif
char *strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

/* ----------------------------------------------------------------- ctype */
int isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isalpha(int c) { return isupper(c) || islower(c); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isprint(int c) { return c >= 32 && c < 127; }
int toupper(int c) { return islower(c) ? c - 32 : c; }
int tolower(int c) { return isupper(c) ? c + 32 : c; }

/* ---------------------------------------------------------------- stdlib */
static const double pow10_table[23] = {
    1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22
};

static int match_word(const char *p, const char *w)
{
    int n = 0;
    while (w[n]) { if (tolower((unsigned char)p[n]) != w[n]) return 0; n++; }
    return n;
}

/* Decimal mantissa of up to 19 digits scaled once by an exact power of ten:
 * correctly rounded for the 7-12 digit values in dEngine's text assets. */
double strtod(const char *s, char **end)
{
    const char *p = s, *start;
    unsigned long long mant = 0;
    int digits = 0, exp10 = 0, any = 0, negative = 0, n;
    double v;
    while (isspace((unsigned char)*p)) p++;
    if (*p == '+' || *p == '-') negative = *p++ == '-';
    if ((n = match_word(p, "infinity")) != 0 || (n = match_word(p, "inf")) != 0) {
        if (end) *end = (char *)(p + n);
        v = 1e300; v *= 1e300;
        return negative ? -v : v;
    }
    if ((n = match_word(p, "nan")) != 0) {
        double zero = 0.0;
        if (end) *end = (char *)(p + n);
        return zero / zero;
    }
    start = p;
    while (isdigit((unsigned char)*p)) {
        any = 1;
        if (digits < 19) { mant = mant * 10u + (unsigned)(*p - '0'); if (mant) digits++; }
        else exp10++;
        p++;
    }
    if (*p == '.') {
        p++;
        while (isdigit((unsigned char)*p)) {
            any = 1;
            if (digits < 19) { mant = mant * 10u + (unsigned)(*p - '0'); if (mant) digits++; exp10--; }
            p++;
        }
    }
    if (!any) { if (end) *end = (char *)s; return 0.0; }
    (void)start;
    if (*p == 'e' || *p == 'E') {
        const char *q = p + 1;
        int eneg = 0, ex = 0;
        if (*q == '+' || *q == '-') eneg = *q++ == '-';
        if (isdigit((unsigned char)*q)) {
            while (isdigit((unsigned char)*q)) { if (ex < 10000) ex = ex * 10 + (*q - '0'); q++; }
            exp10 += eneg ? -ex : ex;
            p = q;
        }
    }
    if (end) *end = (char *)p;
    v = (double)mant;
    if (mant) {
        while (exp10 > 22) { v *= 1e22; exp10 -= 22; }
        while (exp10 < -22) { v /= 1e22; exp10 += 22; }
        if (exp10 > 0) v *= pow10_table[exp10];
        else if (exp10 < 0) v /= pow10_table[-exp10];
    }
    return negative ? -v : v;
}
float strtof(const char *s, char **end) { return (float)strtod(s, end); }
double atof(const char *s) { return strtod(s, 0); }

unsigned long strtoul(const char *s, char **end, int base)
{
    const char *p = s;
    unsigned long v = 0;
    int negative = 0, any = 0;
    while (isspace((unsigned char)*p)) p++;
    if (*p == '+' || *p == '-') negative = *p++ == '-';
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && isxdigit((unsigned char)p[2])) { p += 2; base = 16; }
    else if (base == 0) base = *p == '0' ? 8 : 10;
    for (;; p++) {
        int d;
        if (isdigit((unsigned char)*p)) d = *p - '0';
        else if (isalpha((unsigned char)*p)) d = tolower((unsigned char)*p) - 'a' + 10;
        else break;
        if (d >= base) break;
        v = v * (unsigned long)base + (unsigned long)d; any = 1;
    }
    if (end) *end = (char *)(any ? p : s);
    return negative ? (unsigned long)(-(long)v) : v;
}
long strtol(const char *s, char **end, int base) { return (long)strtoul(s, end, base); }
int atoi(const char *s) { return (int)strtol(s, 0, 10); }
long atol(const char *s) { return strtol(s, 0, 10); }
int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }

static unsigned int rand_state = 1;
int rand(void) { rand_state = rand_state * 1103515245u + 12345u; return (int)((rand_state >> 16) & 0x7fffu); }
void srand(unsigned int seed) { rand_state = seed; }

void qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *))
{
    /* Shell sort: deterministic, no recursion, no scratch beyond one element. */
    unsigned char *a = base, tmp[256];
    size_t gap, i, j, k;
    if (size > sizeof(tmp)) { de_platform_fatal("qsort element too large"); return; }
    for (gap = count / 2; gap; gap /= 2)
        for (i = gap; i < count; i++)
            for (j = i; j >= gap && compare(a + (j - gap) * size, a + j * size) > 0; j -= gap)
                for (k = 0; k < size; k++) {
                    tmp[0] = a[(j - gap) * size + k];
                    a[(j - gap) * size + k] = a[j * size + k];
                    a[j * size + k] = tmp[0];
                }
}

char *getenv(const char *name)
{
    /* dEngine's FS_InitFilesystem reads CWD: paths become "./data/...". */
    if (!strcmp(name, "CWD")) return (char *)".";
    return 0;
}
int setenv(const char *name, const char *value, int overwrite) { (void)name; (void)value; (void)overwrite; return 0; }
void exit(int status) { de_platform_exit(status); for (;;) ; }
void abort(void) { de_platform_fatal("abort"); for (;;) ; }

int gettimeofday(struct timeval *tv, void *tz)
{
    unsigned int ms = de_platform_time_ms();
    (void)tz;
    tv->tv_sec = (long)(ms / 1000u);
    tv->tv_usec = (long)(ms % 1000u) * 1000;
    return 0;
}

/* ------------------------------------------------------------------ math */
static const double pio2_hi = 1.57079632673412561417e+00;
static const double pio2_lo = 6.07710050650619224932e-11;
static const double invpio2 = 6.36619772367581382433e-01;
static const double pi_d = 3.14159265358979311600e+00;
static const double pi_lo = 1.22464679914735317720e-16;

double fabs(double x) { return x < 0.0 ? -x : x; }
float fabsf(float x) { return x < 0.0f ? -x : x; }

double sqrt(double x)
{
#ifdef __transputer__
    return __fpsqrt(x);
#else
    return __builtin_sqrt(x);
#endif
}
float sqrtf(float x)
{
#ifdef __transputer__
    return __fpsqrt(x);
#else
    return __builtin_sqrtf(x);
#endif
}

static double ksin(double x)
{
    const double S1 = -1.66666666666666324348e-01, S2 = 8.33333333332248946124e-03,
                 S3 = -1.98412698298579493134e-04, S4 = 2.75573137070700676789e-06,
                 S5 = -2.50507602534068634195e-08, S6 = 1.58969099521155010221e-10;
    double z = x * x, v = z * x, r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
    return x + v * (S1 + z * r);
}
static double kcos(double x)
{
    const double C1 = 4.16666666666666019037e-02, C2 = -1.38888888888741095749e-03,
                 C3 = 2.48015872894767294178e-05, C4 = -2.75573143513906633035e-07,
                 C5 = 2.08757232129817482790e-09, C6 = -1.13596475577881948265e-11;
    double z = x * x, r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
    double hz = 0.5 * z, w = 1.0 - hz;
    return w + (((1.0 - w) - hz) + z * r);
}
/* Cody-Waite reduction by pi/2; adequate for |x| below about 1e9. */
static int reduce(double x, double *r)
{
    double fn;
    int n;
    if (fabs(x) <= 0.78539816339744830962) { *r = x; return 0; }
    fn = x * invpio2;
    n = (int)(fn >= 0.0 ? fn + 0.5 : fn - 0.5);
    fn = (double)n;
    *r = (x - fn * pio2_hi) - fn * pio2_lo;
    return n;
}
static double not_finite(double x) { return x - x; }
double sin(double x)
{
    double r;
    int n;
    if (x != x || fabs(x) > 1e9) return not_finite(x);
    n = reduce(x, &r);
    switch (n & 3) { case 0: return ksin(r); case 1: return kcos(r); case 2: return -ksin(r); default: return -kcos(r); }
}
double cos(double x)
{
    double r;
    int n;
    if (x != x || fabs(x) > 1e9) return not_finite(x);
    n = reduce(x, &r);
    switch (n & 3) { case 0: return kcos(r); case 1: return -ksin(r); case 2: return -kcos(r); default: return ksin(r); }
}
double tan(double x)
{
    double r, s, c;
    int n;
    if (x != x || fabs(x) > 1e9) return not_finite(x);
    n = reduce(x, &r);
    s = ksin(r); c = kcos(r);
    return (n & 1) ? -c / s : s / c;
}
float sinf(float x) { return (float)sin(x); }
float cosf(float x) { return (float)cos(x); }
float tanf(float x) { return (float)tan(x); }

static double atan_kernel(double t)
{
    /* Odd series for |t| <= tan(pi/12); the last term is below 2e-17. */
    double z = t * t, sum = 0.0;
    int k;
    for (k = 25; k >= 3; k -= 2) sum = z * (((k / 2) & 1 ? -1.0 : 1.0) / (double)k + sum);
    return t + t * sum;
}
double atan(double x)
{
    const double sqrt3 = 1.73205080756887719318, tan15 = 0.26794919243112270647,
                 pi6 = 5.23598775598298815659e-01;
    double a, r;
    int negative = x < 0.0, inverted = 0;
    if (x != x) return x;
    a = fabs(x);
    if (a > 1.0) { a = 1.0 / a; inverted = 1; }
    if (a > tan15) r = pi6 + atan_kernel((sqrt3 * a - 1.0) / (sqrt3 + a));
    else r = atan_kernel(a);
    if (inverted) r = (pio2_hi - r) + pio2_lo;
    return negative ? -r : r;
}
double atan2(double y, double x)
{
    if (x != x || y != y) return x + y;
    if (x > 0.0) return atan(y / x);
    if (x < 0.0) return y >= 0.0 ? (atan(y / x) + pi_d) + pi_lo : (atan(y / x) - pi_d) - pi_lo;
    if (y > 0.0) return pio2_hi + pio2_lo;
    if (y < 0.0) return -pio2_hi - pio2_lo;
    return 0.0;
}
float atan2f(float y, float x) { return (float)atan2(y, x); }
double asin(double x) { return atan2(x, sqrt((1.0 - x) * (1.0 + x))); }
double acos(double x) { return atan2(sqrt((1.0 - x) * (1.0 + x)), x); }
float acosf(float x) { return (float)acos(x); }

typedef union { double d; unsigned long long u; } double_bits;
static const double ln2_hi = 6.93147180369123816490e-01, ln2_lo = 1.90821492927058770002e-10;

double log(double x)
{
    double_bits b;
    double m, s, z, sum;
    int e, k;
    if (x != x) return x;
    if (x <= 0.0) { double zero = 0.0; return x == 0.0 ? -1.0 / zero : zero / zero; }
    b.d = x;
    e = (int)((b.u >> 52) & 0x7ffu);
    if (!e) { b.d = x * 18014398509481984.0; e = (int)((b.u >> 52) & 0x7ffu) - 54; }
    if (e == 0x7ff) return x;
    e -= 1022;
    b.u = (b.u & 0x800fffffffffffffull) | 0x3fe0000000000000ull; /* [0.5, 1) */
    m = b.d;
    if (m < 0.70710678118654752440) { m *= 2.0; e--; }
    s = (m - 1.0) / (m + 1.0);
    z = s * s; sum = 0.0;
    for (k = 21; k >= 3; k -= 2) sum = z * (1.0 / (double)k + sum);
    return (double)e * ln2_hi + ((double)e * ln2_lo + 2.0 * (s + s * sum));
}

double exp(double x)
{
    double_bits b;
    double fn, r, sum;
    int n, k;
    if (x != x) return x;
    if (x > 709.7) { double big = 1e300; return big * big; }
    if (x < -745.2) return 0.0;
    fn = x * 1.44269504088896338700;
    n = (int)(fn >= 0.0 ? fn + 0.5 : fn - 0.5);
    r = (x - (double)n * ln2_hi) - (double)n * ln2_lo;
    sum = 1.0;
    for (k = 15; k >= 1; k--) sum = 1.0 + r * sum / (double)k;
    /* Scale by 2^n in two steps so subnormal and near-overflow results work. */
    if (n > 1000) { b.u = (unsigned long long)(1023 + 1000) << 52; sum *= b.d; n -= 1000; }
    if (n < -1000) { b.u = (unsigned long long)(1023 - 1000) << 52; sum *= b.d; n += 1000; }
    b.u = (unsigned long long)(1023 + n) << 52;
    return sum * b.d;
}

double floor(double x)
{
    double t;
    if (x != x || fabs(x) >= 4503599627370496.0) return x;
    t = (double)(long long)x;
    return t > x ? t - 1.0 : t;
}
double ceil(double x)
{
    double t;
    if (x != x || fabs(x) >= 4503599627370496.0) return x;
    t = (double)(long long)x;
    return t < x ? t + 1.0 : t;
}
float floorf(float x) { return (float)floor(x); }
float ceilf(float x) { return (float)ceil(x); }
double fmod(double x, double y)
{
    double q;
    if (y == 0.0 || x != x || y != y) return not_finite(x * y);
    q = x / y;
    q = q < 0.0 ? ceil(q) : floor(q);
    return x - q * y;
}

double pow(double x, double y)
{
    double ay = fabs(y), r;
    if (y == 0.0) return 1.0;
    if (x != x || y != y) return x + y;
    if (ay <= 1024.0 && y == floor(y)) {
        unsigned int n = (unsigned int)ay;
        double base = x;
        r = 1.0;
        while (n) { if (n & 1u) r *= base; base *= base; n >>= 1; }
        return y < 0.0 ? 1.0 / r : r;
    }
    if (x == 0.0) { double zero = 0.0; return y > 0.0 ? 0.0 : 1.0 / zero; }
    if (x < 0.0) {
        double zero = 0.0;
        if (y != floor(y)) return zero / zero;
        r = exp(y * log(-x));
        return fmod(y, 2.0) != 0.0 ? -r : r;
    }
    return exp(y * log(x));
}
float powf(float x, float y) { return (float)pow(x, y); }

/* ----------------------------------------------------------------- files */
enum { FILE_ROM = 1, FILE_CONSOLE_OUT, FILE_CONSOLE_ERR, FILE_EMPTY };
struct de_file {
    const unsigned char *data;
    unsigned long size, pos;
    int kind, eof, err;
};
static struct de_file console_in = { 0, 0, 0, FILE_EMPTY, 0, 0 };
static struct de_file console_out = { 0, 0, 0, FILE_CONSOLE_OUT, 0, 0 };
static struct de_file console_err = { 0, 0, 0, FILE_CONSOLE_ERR, 0, 0 };
FILE *de_stdin = &console_in, *de_stdout = &console_out, *de_stderr = &console_err;

static char line_buffer[256];
static int line_used;
static void console_flush(void)
{
    if (line_used) de_platform_write(line_buffer, line_used);
    line_used = 0;
}
static void console_put(int c)
{
    line_buffer[line_used++] = (char)c;
    if (c == '\n' || line_used == (int)sizeof(line_buffer)) console_flush();
}

FILE *fopen(const char *path, const char *mode)
{
    const unsigned char *data;
    unsigned long size;
    FILE *f;
    for (; *mode; mode++) if (*mode == 'w' || *mode == 'a' || *mode == '+') return 0;
    data = de_rom_open(path, &size);
    if (!data) return 0;
    f = malloc(sizeof(*f));
    if (!f) return 0;
    f->data = data; f->size = size; f->pos = 0; f->kind = FILE_ROM; f->eof = f->err = 0;
    return f;
}
int fclose(FILE *f)
{
    if (!f) return EOF;
    if (f->kind == FILE_ROM) free(f);
    else if (f->kind != FILE_EMPTY) console_flush();
    return 0;
}
int fgetc(FILE *f)
{
    if (f->kind != FILE_ROM || f->pos >= f->size) { f->eof = 1; return EOF; }
    return f->data[f->pos++];
}
int ungetc(int c, FILE *f)
{
    if (c == EOF || f->kind != FILE_ROM || !f->pos) return EOF;
    f->pos--; f->eof = 0;
    return c;
}
size_t fread(void *ptr, size_t size, size_t count, FILE *f)
{
    unsigned long want, have;
    if (!size || !count) return 0;
    if (f->kind != FILE_ROM) { f->eof = 1; return 0; }
    want = (unsigned long)size * count;
    have = f->size - f->pos;
    if (want > have) { want = have - have % size; f->eof = 1; }
    memcpy(ptr, f->data + f->pos, want);
    f->pos += want;
    return want / size;
}
size_t fwrite(const void *ptr, size_t size, size_t count, FILE *f)
{
    const unsigned char *p = ptr;
    size_t n = size * count, i;
    if (f->kind != FILE_CONSOLE_OUT && f->kind != FILE_CONSOLE_ERR) { f->err = 1; return 0; }
    for (i = 0; i < n; i++) console_put(p[i]);
    return count;
}
int fseek(FILE *f, long offset, int whence)
{
    long base;
    if (f->kind != FILE_ROM) return -1;
    base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (long)f->pos : (long)f->size;
    if (base + offset < 0 || (unsigned long)(base + offset) > f->size) return -1;
    f->pos = (unsigned long)(base + offset); f->eof = 0;
    return 0;
}
long ftell(FILE *f) { return f->kind == FILE_ROM ? (long)f->pos : -1; }
void rewind(FILE *f) { fseek(f, 0, SEEK_SET); f->err = 0; }
int feof(FILE *f) { return f->eof; }
int ferror(FILE *f) { return f->err; }
int fflush(FILE *f) { (void)f; console_flush(); return 0; }
char *fgets(char *s, int n, FILE *f)
{
    int i = 0, c;
    if (n <= 0) return 0;
    while (i < n - 1) {
        c = fgetc(f);
        if (c == EOF) break;
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    if (!i) return 0;
    s[i] = 0;
    return s;
}
int fputc(int c, FILE *f)
{
    if (f->kind != FILE_CONSOLE_OUT && f->kind != FILE_CONSOLE_ERR) return EOF;
    console_put(c);
    return (unsigned char)c;
}
int fputs(const char *s, FILE *f) { while (*s) if (fputc(*s++, f) == EOF) return EOF; return 0; }
int puts(const char *s) { fputs(s, stdout); console_put('\n'); return 0; }
int putchar(int c) { console_put(c); return (unsigned char)c; }

/* ---------------------------------------------------------------- printf */
typedef struct { char *buf; size_t cap, len; FILE *file; } sink;
static void sink_put(sink *k, char c)
{
    if (k->file) fputc(c, k->file);
    else if (k->len + 1 < k->cap) k->buf[k->len] = c;
    k->len++;
}
static void sink_pad(sink *k, char c, int n) { while (n-- > 0) sink_put(k, c); }

/* Writes the decimal digits of v with `prec` fractional digits, v >= 0. */
static int fixed_digits(char *out, double v, int prec)
{
    unsigned long long scale = 1, whole, frac, q;
    char tmp[48];
    int n = 0, i;
    if (prec > 17) prec = 17;
    for (i = 0; i < prec; i++) scale *= 10u;
    if (v >= 1e18 / (double)scale) {
        /* Too large for the integer path: print the leading digits and zeros. */
        int e = 0;
        while (v >= 1e17) { v /= 10.0; e++; }
        q = (unsigned long long)(v + 0.5);
        do { tmp[n++] = (char)('0' + q % 10u); q /= 10u; } while (q);
        for (i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
        while (e--) out[n++] = '0';
        if (prec) { out[n++] = '.'; for (i = 0; i < prec; i++) out[n++] = '0'; }
        return n;
    }
    q = (unsigned long long)(v * (double)scale + 0.5);
    whole = q / scale; frac = q % scale;
    do { tmp[n++] = (char)('0' + whole % 10u); whole /= 10u; } while (whole);
    for (i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    if (prec) {
        out[n++] = '.';
        for (i = prec - 1; i >= 0; i--) { out[n + i] = (char)('0' + frac % 10u); frac /= 10u; }
        n += prec;
    }
    return n;
}
static int decimal_exponent(double *v)
{
    int e = 0;
    if (*v == 0.0) return 0;
    while (*v >= 10.0) { *v /= 10.0; e++; }
    while (*v < 1.0) { *v *= 10.0; e--; }
    return e;
}
static int exp_digits(char *out, double v, int prec, char letter)
{
    int e = decimal_exponent(&v), n, ae;
    char tmp[48];
    double scale = 1.0;
    int i;
    for (i = 0; i < prec && i < 17; i++) scale *= 10.0;
    if ((double)(unsigned long long)(v * scale + 0.5) >= 10.0 * scale) { v /= 10.0; e++; }
    n = fixed_digits(tmp, v, prec);
    memcpy(out, tmp, (size_t)n);
    out[n++] = letter;
    out[n++] = e < 0 ? '-' : '+';
    ae = e < 0 ? -e : e;
    if (ae >= 100) out[n++] = (char)('0' + ae / 100);
    out[n++] = (char)('0' + (ae / 10) % 10);
    out[n++] = (char)('0' + ae % 10);
    return n;
}

static int format(sink *k, const char *f, va_list ap)
{
    while (*f) {
        int left = 0, zero = 0, plus = 0, space = 0, alt = 0, width = 0, prec = -1, lng = 0, half = 0;
        char conv, body[96], prefix[3];
        int n = 0, np = 0, i;
        if (*f != '%') { sink_put(k, *f++); continue; }
        f++;
        for (;; f++) {
            if (*f == '-') left = 1; else if (*f == '0') zero = 1; else if (*f == '+') plus = 1;
            else if (*f == ' ') space = 1; else if (*f == '#') alt = 1; else break;
        }
        if (*f == '*') { width = va_arg(ap, int); if (width < 0) { left = 1; width = -width; } f++; }
        else while (isdigit((unsigned char)*f)) width = width * 10 + (*f++ - '0');
        if (*f == '.') {
            f++; prec = 0;
            if (*f == '*') { prec = va_arg(ap, int); f++; }
            else while (isdigit((unsigned char)*f)) prec = prec * 10 + (*f++ - '0');
        }
        for (;; f++) {
            if (*f == 'l') lng++; else if (*f == 'h') half++;
            else if (*f == 'z' || *f == 't' || *f == 'j' || *f == 'L') ; else break;
        }
        conv = *f ? *f++ : 0;
        switch (conv) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': case 'p': {
            unsigned long long v;
            int negative = 0, base = 10;
            const char *digits = conv == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            char tmp[32];
            int t = 0;
            if (conv == 'p') { v = (unsigned long long)(size_t)va_arg(ap, void *); base = 16; alt = 1; }
            else if (conv == 'd' || conv == 'i') {
                long long s = lng >= 2 ? va_arg(ap, long long) : lng ? (long long)va_arg(ap, long) : (long long)va_arg(ap, int);
                if (half == 1) s = (short)s; else if (half >= 2) s = (signed char)s;
                if (s < 0) { negative = 1; v = (unsigned long long)(-s); } else v = (unsigned long long)s;
            } else {
                v = lng >= 2 ? va_arg(ap, unsigned long long) : lng ? (unsigned long long)va_arg(ap, unsigned long) : (unsigned long long)va_arg(ap, unsigned int);
                if (half == 1) v = (unsigned short)v; else if (half >= 2) v = (unsigned char)v;
                base = conv == 'o' ? 8 : conv == 'u' ? 10 : 16;
            }
            if (v || prec != 0) do { tmp[t++] = digits[v % (unsigned)base]; v /= (unsigned)base; } while (v);
            while (t < prec) tmp[t++] = '0';
            if (negative) prefix[np++] = '-'; else if (plus) prefix[np++] = '+'; else if (space) prefix[np++] = ' ';
            if (alt && base == 16) { prefix[np++] = '0'; prefix[np++] = conv == 'X' ? 'X' : 'x'; }
            else if (alt && base == 8 && (t == 0 || tmp[t - 1] != '0')) tmp[t++] = '0';
            for (i = 0; i < t; i++) body[i] = tmp[t - 1 - i];
            n = t;
            if (prec >= 0) zero = 0;
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
            double v = va_arg(ap, double);
            if (v < 0.0 || (v == 0.0 && 1.0 / v < 0.0)) { prefix[np++] = '-'; v = -v; }
            else if (plus) prefix[np++] = '+'; else if (space) prefix[np++] = ' ';
            if (prec < 0) prec = 6;
            if (v != v) { memcpy(body, "nan", 3); n = 3; zero = 0; }
            else if (v > 1.7976931348623157e308) { memcpy(body, "inf", 3); n = 3; zero = 0; }
            else if (conv == 'f' || conv == 'F') n = fixed_digits(body, v, prec);
            else if (conv == 'e' || conv == 'E') n = exp_digits(body, v, prec, conv == 'E' ? 'E' : 'e');
            else {
                double m = v;
                int x, p = prec ? prec : 1;
                x = decimal_exponent(&m);
                {
                    double scale = 1.0;
                    for (i = 1; i < p && i < 17; i++) scale *= 10.0;
                    if ((double)(unsigned long long)(m * scale + 0.5) >= 10.0 * scale) x++;
                }
                if (v != 0.0 && (x < -4 || x >= p)) n = exp_digits(body, v, p - 1, conv == 'G' ? 'E' : 'e');
                else n = fixed_digits(body, v, v == 0.0 ? p - 1 : p - 1 - x);
                if (!alt) {
                    int dot = -1, e = n;
                    for (i = 0; i < n; i++) { if (body[i] == '.') dot = i; if (body[i] == 'e' || body[i] == 'E') { e = i; break; } }
                    if (dot >= 0) {
                        int cut = e;
                        while (cut > dot + 1 && body[cut - 1] == '0') cut--;
                        if (cut == dot + 1) cut = dot;
                        memmove(body + cut, body + e, (size_t)(n - e));
                        n -= e - cut;
                    }
                }
            }
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            int len = 0;
            if (!s) s = "(null)";
            while (s[len] && (prec < 0 || len < prec)) len++;
            if (!left) sink_pad(k, ' ', width - len);
            for (i = 0; i < len; i++) sink_put(k, s[i]);
            if (left) sink_pad(k, ' ', width - len);
            continue;
        }
        case 'c': body[0] = (char)va_arg(ap, int); n = 1; zero = 0; break;
        case 'n': *va_arg(ap, int *) = (int)k->len; continue;
        case '%': sink_put(k, '%'); continue;
        default: continue;
        }
        if (left) zero = 0;
        if (!left && !zero) sink_pad(k, ' ', width - n - np);
        for (i = 0; i < np; i++) sink_put(k, prefix[i]);
        if (zero) sink_pad(k, '0', width - n - np);
        for (i = 0; i < n; i++) sink_put(k, body[i]);
        if (left) sink_pad(k, ' ', width - n - np);
    }
    return (int)k->len;
}

int vsnprintf(char *s, size_t cap, const char *f, va_list ap)
{
    sink k;
    int n;
    k.buf = s; k.cap = cap; k.len = 0; k.file = 0;
    n = format(&k, f, ap);
    if (cap) s[k.len < cap ? k.len : cap - 1] = 0;
    return n;
}
int vsprintf(char *s, const char *f, va_list ap) { return vsnprintf(s, (size_t)0x7fffffff, f, ap); }
int vfprintf(FILE *file, const char *f, va_list ap)
{
    sink k;
    k.buf = 0; k.cap = 0; k.len = 0; k.file = file;
    return format(&k, f, ap);
}
int vprintf(const char *f, va_list ap) { return vfprintf(stdout, f, ap); }
int snprintf(char *s, size_t n, const char *f, ...) { va_list ap; int r; va_start(ap, f); r = vsnprintf(s, n, f, ap); va_end(ap); return r; }
int sprintf(char *s, const char *f, ...) { va_list ap; int r; va_start(ap, f); r = vsprintf(s, f, ap); va_end(ap); return r; }
int fprintf(FILE *file, const char *f, ...) { va_list ap; int r; va_start(ap, f); r = vfprintf(file, f, ap); va_end(ap); return r; }
int printf(const char *f, ...) { va_list ap; int r; va_start(ap, f); r = vfprintf(stdout, f, ap); va_end(ap); return r; }

/* ----------------------------------------------------------------- scanf */
typedef struct { const char *s; FILE *f; int count; } source;
static int src_get(source *in)
{
    int c;
    if (in->f) c = fgetc(in->f);
    else { c = (unsigned char)*in->s; if (c) in->s++; else c = EOF; }
    if (c != EOF) in->count++;
    return c;
}
static void src_unget(source *in, int c)
{
    if (c == EOF) return;
    in->count--;
    if (in->f) ungetc(c, in->f); else in->s--;
}
static void src_skip_space(source *in)
{
    int c;
    do c = src_get(in); while (c != EOF && isspace(c));
    src_unget(in, c);
}

static int scan(source *in, const char *f, va_list ap)
{
    int assigned = 0, converted_any = 0;
    while (*f) {
        int suppress = 0, width = 0, lng = 0, half = 0, c;
        char conv;
        if (isspace((unsigned char)*f)) {
            while (isspace((unsigned char)*f)) f++;
            src_skip_space(in);
            continue;
        }
        if (*f != '%' || f[1] == '%') {
            if (*f == '%') { f++; src_skip_space(in); }
            c = src_get(in);
            if (c != (unsigned char)*f) { src_unget(in, c); return c == EOF && !converted_any ? EOF : assigned; }
            f++;
            continue;
        }
        f++;
        if (*f == '*') { suppress = 1; f++; }
        while (isdigit((unsigned char)*f)) width = width * 10 + (*f++ - '0');
        for (;; f++) { if (*f == 'l') lng++; else if (*f == 'h') half++; else if (*f == 'L' || *f == 'z' || *f == 'j' || *f == 't') lng = 2; else break; }
        conv = *f ? *f++ : 0;
        if (conv != 'c' && conv != 'n' && conv != '[') src_skip_space(in);
        if (conv == 'n') { if (!suppress) *va_arg(ap, int *) = in->count; continue; }
        if (!width) width = conv == 'c' ? 1 : 0x7fffffff;
        if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'x' || conv == 'X' || conv == 'o') {
            char buf[64];
            int n = 0, base = conv == 'x' || conv == 'X' ? 16 : conv == 'o' ? 8 : conv == 'i' ? 0 : 10;
            unsigned long long v = 0;
            int negative = 0, digits = 0;
            c = src_get(in);
            if ((c == '+' || c == '-') && n < width) { negative = c == '-'; n++; c = src_get(in); }
            if (base == 0 || base == 16) {
                if (c == '0' && n < width) {
                    int d = src_get(in);
                    if ((d == 'x' || d == 'X') && n + 1 < width) { base = 16; n += 2; c = src_get(in); }
                    else { src_unget(in, d); if (!base) base = 8; }
                } else if (!base) base = 10;
            }
            while (c != EOF && n < width) {
                int d = isdigit(c) ? c - '0' : isalpha(c) ? tolower(c) - 'a' + 10 : 99;
                if (d >= base) break;
                v = v * (unsigned)base + (unsigned)d; digits++; n++;
                c = src_get(in);
            }
            src_unget(in, c);
            (void)buf;
            if (!digits) return c == EOF && !converted_any ? EOF : assigned;
            converted_any = 1;
            if (negative) v = (unsigned long long)(-(long long)v);
            if (!suppress) {
                if (lng >= 2) *va_arg(ap, long long *) = (long long)v;
                else if (lng) *va_arg(ap, long *) = (long)v;
                else if (half >= 2) *va_arg(ap, char *) = (char)v;
                else if (half) *va_arg(ap, short *) = (short)v;
                else *va_arg(ap, int *) = (int)v;
                assigned++;
            }
        } else if (conv == 'f' || conv == 'e' || conv == 'g' || conv == 'E' || conv == 'G' || conv == 'a') {
            char buf[80];
            int n = 0, seen_digit = 0;
            double v;
            c = src_get(in);
            if ((c == '+' || c == '-') && n < width) { buf[n++] = (char)c; c = src_get(in); }
            while (c != EOF && isdigit(c) && n < width && n < 78) { buf[n++] = (char)c; seen_digit = 1; c = src_get(in); }
            if (c == '.' && n < width && n < 78) {
                buf[n++] = (char)c; c = src_get(in);
                while (c != EOF && isdigit(c) && n < width && n < 78) { buf[n++] = (char)c; seen_digit = 1; c = src_get(in); }
            }
            if (seen_digit && (c == 'e' || c == 'E') && n < width && n < 70) {
                int save = n;
                buf[n++] = (char)c; c = src_get(in);
                if ((c == '+' || c == '-') && n < width) { buf[n++] = (char)c; c = src_get(in); }
                if (c != EOF && isdigit(c)) {
                    while (c != EOF && isdigit(c) && n < width && n < 78) { buf[n++] = (char)c; c = src_get(in); }
                } else {
                    /* Not an exponent after all: keep what can be returned. */
                    src_unget(in, c);
                    c = buf[--n];
                    if (n > save) { src_unget(in, c); c = buf[--n]; }
                }
            }
            src_unget(in, c);
            buf[n] = 0;
            if (!seen_digit) return c == EOF && !converted_any ? EOF : assigned;
            converted_any = 1;
            v = strtod(buf, 0);
            if (!suppress) {
                if (lng) *va_arg(ap, double *) = v; else *va_arg(ap, float *) = (float)v;
                assigned++;
            }
        } else if (conv == 's') {
            char *out = suppress ? 0 : va_arg(ap, char *);
            int n = 0;
            c = src_get(in);
            if (c == EOF) return !converted_any ? EOF : assigned;
            while (c != EOF && !isspace(c) && n < width) { if (out) out[n] = (char)c; n++; c = src_get(in); }
            src_unget(in, c);
            if (out) { out[n] = 0; assigned++; }
            converted_any = 1;
        } else if (conv == 'c') {
            char *out = suppress ? 0 : va_arg(ap, char *);
            int n;
            for (n = 0; n < width; n++) {
                c = src_get(in);
                if (c == EOF) return !converted_any ? EOF : assigned;
                if (out) out[n] = (char)c;
            }
            if (out) assigned++;
            converted_any = 1;
        } else return assigned;
    }
    return assigned;
}
int vsscanf(const char *s, const char *f, va_list ap) { source in; in.s = s; in.f = 0; in.count = 0; return scan(&in, f, ap); }
int vfscanf(FILE *file, const char *f, va_list ap) { source in; in.s = 0; in.f = file; in.count = 0; return scan(&in, f, ap); }
int sscanf(const char *s, const char *f, ...) { va_list ap; int r; va_start(ap, f); r = vsscanf(s, f, ap); va_end(ap); return r; }
int fscanf(FILE *file, const char *f, ...) { va_list ap; int r; va_start(ap, f); r = vfscanf(file, f, ap); va_end(ap); return r; }
