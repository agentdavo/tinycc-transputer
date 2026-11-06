/* Test-only generic POV-Ray host adapter, using T4's existing iserver wire
 * protocol.  This is a single-process fixture, not an OS or the ICC process
 * runtime.  Files use real host open/read/write/seek requests.  Heap space is
 * bounded inside the 64 MiB emulator memory; anonymous mappings are retained
 * until the fixture exits.  Unsupported syscalls return ENOSYS. */
#include <conc.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Use fdlibm's IEEE entry points.  scalbn is an alias of scalbln on this
 * 32-bit-long target; the reduced declarations omit assembler aliases. */
double __ieee754_pow(double, double);
double __ieee754_exp(double);
double __ieee754_fmod(double, double);
double __ieee754_acos(double);
double __ieee754_asin(double);
double __ieee754_atan2(double, double);
double __ieee754_log(double);
double scalbln(double, long);

double pow(double x, double y) { return __ieee754_pow(x, y); }
double exp(double x) { return __ieee754_exp(x); }
double fmod(double x, double y) { return __ieee754_fmod(x, y); }
double acos(double x) { return __ieee754_acos(x); }
double asin(double x) { return __ieee754_asin(x); }
double atan2(double x, double y) { return __ieee754_atan2(x, y); }
double log(double x) { return __ieee754_log(x); }
double __ieee754_sqrt(double x) { return __fpsqrt(x); }
double scalbn(double x, int n) { return scalbln(x, n); }

int pov_isfinite(double x)
{
    union { double d; unsigned w[2]; } u;
    u.d = x;
    return (u.w[1] & 0x7ff00000u) != 0x7ff00000u;
}

static unsigned char sp[514];

static void put32(unsigned char *p, unsigned v)
{
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}

static unsigned get32(const unsigned char *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8 |
           (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

/* Requests and replies have a two-byte length, excluding the length itself.
 * The fixture has one caller, so no channel/packet-buffer lock is needed. */
static int sp_call(int n)
{
    if (n < 6)
        n = 6;
    n = (n + 1) & ~1;
    sp[0] = n;
    sp[1] = n >> 8;
    ChanOut(LINK0OUT, sp, n + 2);
    ChanIn(LINK0IN, sp, 2);
    n = sp[0] | sp[1] << 8;
    if (n > sizeof(sp) - 2) {
        /* A corrupt host reply cannot be consumed into the packet buffer. */
        while (1)
            __stopp();
    }
    ChanIn(LINK0IN, sp + 2, n);
    return sp[2];
}

static long port_open(const char *name, int flags)
{
    int n = strlen(name), mode = 1;
    if (n > 240)
        return -36;
    if (flags & 0100)
        mode = (flags & 3) == 2 ? 5 : 2;
    else if (flags & 02000)
        mode = 3;
    else if (flags & 3)
        mode = 4;
    sp[2] = 10;                  /* SP_OPEN */
    sp[3] = n;
    sp[4] = n >> 8;
    memcpy(sp + 5, name, n);
    sp[5 + n] = 1;              /* binary */
    sp[6 + n] = mode;
    if (sp_call(n + 5))
        return -2;
    return get32(sp + 3);
}

static long port_io(int fd, void *buf, unsigned n, int wr)
{
    unsigned done = 0, sz, got;
    unsigned char *p = buf;
    while (done < n) {
        sz = n - done;
        if (sz > 500)
            sz = 500;
        sp[2] = wr ? 13 : 12;    /* SP_WRITE / SP_READ */
        put32(sp + 3, fd);
        sp[7] = sz;
        sp[8] = sz >> 8;
        if (wr)
            memcpy(sp + 9, p + done, sz);
        if (sp_call(wr ? sz + 7 : 7))
            return done ? (long)done : -5;
        got = sp[3] | sp[4] << 8;
        if (got > sz)
            return done ? (long)done : -5;
        if (!wr)
            memcpy(p + done, sp + 5, got);
        done += got;
        if (got < sz)
            break;
    }
    return done;
}

/* Leave loaded text/BSS and the boot workspace below 16 MiB. */
static unsigned heap = 0x81000000u, brkptr = 0x81800000u;
long __transputer_mmap2_addr, __transputer_mmap2_len;
long __transputer_mmap2_prot, __transputer_mmap2_flags;
long __transputer_mmap2_fd, __transputer_mmap2_offset;

long __transputer_mmap2_packed(const long *a)
{
    unsigned n, result;
    /* This allocator is for libc's anonymous heap mappings only. */
    if (!(a[3] & 0x20) || a[5])
        return -38;
    if (a[1] <= 0 || (unsigned)a[1] > 0x800000u)
        return -12;
    n = ((unsigned)a[1] + 4095u) & ~4095u;
    if (n > 0x81800000u - heap)
        return -12;
    result = heap;
    heap += n;
    memset((void *)result, 0, n);
    return result;
}

long __transputer_mmap2_globals(void)
{
    long a[6];
    a[0] = __transputer_mmap2_addr;
    a[1] = __transputer_mmap2_len;
    a[2] = __transputer_mmap2_prot;
    a[3] = __transputer_mmap2_flags;
    a[4] = __transputer_mmap2_fd;
    a[5] = __transputer_mmap2_offset;
    return __transputer_mmap2_packed(a);
}

long __transputer_syscall(long nr, long a, long b, long c,
                         long d, long e, long f)
{
    switch (nr) {
    case 1024:                  /* open */
        return port_open((char *)a, b);
    case 56:                    /* openat */
        return port_open((char *)b, c);
    case 57:                    /* close */
        sp[2] = 11;
        put32(sp + 3, a);
        return sp_call(5) ? -9 : 0;
    case 63:                    /* read */
        return port_io(a, (void *)b, c, 0);
    case 64:                    /* write */
        return port_io(a, (void *)b, c, 1);
    case 66: {                  /* writev */
        long *i = (long *)b, total = 0;
        while (c--) {
            long n = port_io(a, (void *)i[0], i[1], 1);
            if (n < 0)
                return total ? total : n;
            total += n;
            i += 2;
        }
        return total;
    }
    case 1057:                  /* lseek: SP_SEEK, then SP_TELL */
        sp[2] = 17;
        put32(sp + 3, a);
        put32(sp + 7, b);
        put32(sp + 11, c + 1);
        if (sp_call(13))
            return -22;
        sp[2] = 18;
        put32(sp + 3, a);
        return sp_call(5) ? -22 : (long)get32(sp + 3);
    case 214:                   /* brk */
        if (!a)
            return brkptr;
        if ((unsigned)a < 0x81800000u || (unsigned)a >= 0x82000000u)
            return brkptr;
        brkptr = a;
        return a;
    case 222: {                 /* mmap2 */
        long args[6] = {a, b, c, d, e, f};
        return __transputer_mmap2_packed(args);
    }
    case 215:                   /* munmap: retain until fixture exit */
        return 0;
    case 93:                    /* exit */
    case 94:                    /* exit_group */
        sp[2] = 35;
        put32(sp + 3, a);
        sp_call(5);
        while (1)
            __stopp();
    case 29:                    /* ioctl: no terminal */
        return -25;
    case 1052:                  /* fcntl: fixture has no concurrent fd users */
        return 0;
    case 134:                   /* signal operations: no signals delivered */
    case 135:
        return 0;
    case 172:                   /* getpid, gettid, set_tid_address */
    case 178:
    case 96:
        return 1;
    case 174:                   /* uid/euid/gid/egid */
    case 175:
    case 176:
    case 177:
        return 0;
    default:
        return -38;
    }
}

extern void main(int, char **), __uClibc_init(void);

void test_main(void)
{
    char *args[] = {"povray", "-v", 0};
    __uClibc_init();
    main(2, args);
    exit(0);
}
