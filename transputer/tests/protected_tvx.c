/* Protected SIGFPE retry on software and native TVX, with per-thread timers. */
#define _GNU_SOURCE
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#include <asm/unistd.h>

extern long __transputer_syscall(long, long, long, long, long, long, long);
extern unsigned tvx_software(unsigned, unsigned, unsigned);
extern unsigned tvx_native(unsigned, unsigned, unsigned);
#define NOTE(s) __transputer_syscall(64, 1, (long)(s "\n"), sizeof(s "\n") - 1, 0, 0, 0)

struct vector {
    unsigned op, a, b, result, flags, enable;
    int code;
};
static const struct vector vectors[] = {
    {0, 0x7fc13f80, 0x3f803f80, 0x7fc14000, 4, 8, FPE_FLTINV},
    {1, 0x7f803f80, 0x00003f80, 0x7fc03f80, 0x104, 0x200, FPE_FLTINV},
    {1, 0x7f7f3f80, 0x40003f80, 0x7f803f80, 0x11004, 0x2000, FPE_FLTOVF},
    {1, 0x00013f80, 0x3f003f80, 0x00003f80, 0x14000, 0x8000, FPE_FLTUND},
    {0, 0x3f803f80, 0x3b803f80, 0x3f804000, 0x10000, 0x20000, FPE_FLTRES},
    {2, 0xff81, 0x3f80, 0xffc10000, 0x104, 0x200, FPE_FLTINV},
    {2, 0x7f7f, 0x4000, 0x7f800000, 0x11004, 0x2000, FPE_FLTOVF},
    {2, 1, 1, 0, 0x14000, 0x8000, FPE_FLTUND},
    {3, 0x3f80, 0x3380, 0x3f800000, 0x10000, 0x20000, FPE_FLTRES}
};
static struct worker {
    long tid;
    volatile sig_atomic_t active, traps, ticks, bad, ready;
} workers[2];
static volatile sig_atomic_t start_workers;

static struct worker *this_worker(void)
{
    long tid = __transputer_syscall(__NR_gettid, 0, 0, 0, 0, 0, 0);
    if (workers[0].tid == tid) return &workers[0];
    if (workers[1].tid == tid) return &workers[1];
    return 0;
}

static void handler(int sig, siginfo_t *info, void *context)
{
    struct worker *w = this_worker();
    ucontext_t *uc = context;
    volatile double x = 1.25, y = 2.5, z;
    if (!w) _exit(90);
    if (__ldflags() & 0x2aa08) w->bad = 1;
    if (sig == SIGFPE) {
        const struct vector *v = &vectors[w->active];
        if (info->si_code != v->code ||
            (unsigned)info->si_addr != uc->uc_mcontext.iptr ||
            (uc->uc_mcontext.status & 0x3ff0c) != (0x400 | v->enable) ||
            !(uc->uc_mcontext.status & 0x08000000)) w->bad = 2;
        uc->uc_mcontext.status &= ~0x2aa08;
        w->traps++;
    } else {
        if (info->si_code != SI_TIMER) w->bad = 3;
        w->ticks++;
    }
    /* Both sigreturns must restore the interrupted FP stack and flags. */
    z = x * y;
    if (z != 3.125) w->bad = 4;
    __stflags(0x104);
}

static void *run_worker(void *arg)
{
    unsigned id = (unsigned)(long)arg, round, i, result;
    struct worker *w = &workers[id];
    struct sigevent event = {0};
    struct itimerspec period = {{0, 1000000}, {0, 1000000}};
    struct timespec start, now;
    sigset_t mask;
    timer_t timer;
    NOTE("TVX_LINUX worker entered");
    w->tid = __transputer_syscall(__NR_gettid, 0, 0, 0, 0, 0, 0);
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGFPE);
    if (sigprocmask(SIG_UNBLOCK, &mask, 0)) { w->ready = -1; return (void *)1; }
    event.sigev_notify = SIGEV_THREAD_ID;
    event.sigev_signo = SIGUSR1;
    event._sigev_un._tid = w->tid;
    if (timer_create(CLOCK_MONOTONIC, &event, &timer)) {
        w->ready = -1;
        return (void *)2;
    }
    if (timer_settime(timer, 0, &period, 0)) {
        w->ready = -1;
        timer_delete(timer);
        return (void *)3;
    }
    NOTE("TVX_LINUX timer armed");
    w->ready = 1;
    while (!start_workers || (start_workers > 0 && !workers[id ^ 1].ready))
        __transputer_syscall(__NR_sched_yield, 0, 0, 0, 0, 0, 0);
    if (start_workers < 0 || workers[id ^ 1].ready < 0) {
        timer_delete(timer);
        return (void *)4;
    }
    for (round = 0; round < 16 && !w->bad; round++) {
        for (i = 0; i < sizeof(vectors)/sizeof(vectors[0]); i++) {
            const struct vector *v = &vectors[i];
            int before = w->traps;
            w->active = i;
            __stflags(0x400 | v->enable);
            result = id ? tvx_native(v->op, v->a, v->b) : tvx_software(v->op, v->a, v->b);
            if (result != v->result || (__ldflags() & 0x3ff0c) != (0x400 | v->flags) ||
                w->traps != before + 1) w->bad = 5;
            __stflags(0);
        }
        __transputer_syscall(__NR_sched_yield, 0, 0, 0, 0, 0, 0);
    }
    clock_gettime(CLOCK_MONOTONIC, &start);
    do {
        clock_gettime(CLOCK_MONOTONIC, &now);
    } while (w->ticks < 2 && now.tv_sec - start.tv_sec < 3);
    if (timer_delete(timer) || w->ticks < 2 || w->traps != 144) w->bad = 6;
    printf("TVX_LINUX worker=%u traps=%d timer_signals=%d error=%d\n",
           id, w->traps, w->ticks, w->bad);
    fflush(stdout);
    return (void *)(long)w->bad;
}

int test_protected_tvx(void)
{
    struct sigaction action = {0}, old_fpe, old_timer;
    pthread_t threads[2];
    unsigned created, i;
    int rc = 0;
    NOTE("TVX_LINUX begin");
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = handler;
    action.sa_flags = SA_SIGINFO;
    if (sigaction(SIGFPE, &action, &old_fpe)) return 1;
    if (sigaction(SIGUSR1, &action, &old_timer)) {
        sigaction(SIGFPE, &old_fpe, 0);
        return 1;
    }
    for (created = 0; created < 2; created++)
        if (pthread_create(&threads[created], 0, run_worker, (void *)(long)created)) break;
    if (created != 2) rc = 1;
    start_workers = created == 2 ? 1 : -1;
    NOTE("TVX_LINUX workers created");
    for (i = 0; i < created; i++) {
        void *result = 0;
        if (pthread_join(threads[i], &result) || result) rc = 1;
        if (workers[i].bad || workers[i].traps != 144 || workers[i].ticks < 2)
            rc = 1;
    }
    if (!workers[0].tid || workers[0].tid == workers[1].tid) rc = 1;
    if (sigaction(SIGFPE, &old_fpe, 0) || sigaction(SIGUSR1, &old_timer, 0)) rc = 1;
    if (rc) NOTE("TVX_LINUX_TEST: FAIL");
    else NOTE("TVX_LINUX_TEST: PASS");
    return rc;
}
