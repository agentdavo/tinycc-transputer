/*
 * CoreMark port for Transputer T800 (T4 emulator)
 * Timer: LDTIMER low-priority = 64us per tick = 15625 Hz
 */
#include "coremark.h"
#include "core_portme.h"
#if COREMARK_SMP_TEST
#include <time.h>
#include <unistd.h>
#endif

#if VALIDATION_RUN
volatile ee_s32 seed1_volatile = 0x3415;
volatile ee_s32 seed2_volatile = 0x3415;
volatile ee_s32 seed3_volatile = 0x66;
#endif
#if PERFORMANCE_RUN
volatile ee_s32 seed1_volatile = 0x0;
volatile ee_s32 seed2_volatile = 0x0;
volatile ee_s32 seed3_volatile = 0x66;
#endif
#if PROFILE_RUN
volatile ee_s32 seed1_volatile = 0x8;
volatile ee_s32 seed2_volatile = 0x8;
volatile ee_s32 seed3_volatile = 0x8;
#endif
volatile ee_s32 seed4_volatile = ITERATIONS;
volatile ee_s32 seed5_volatile = 0;

/* LDTIMER helper — defined in transputer_ldtimer.S */
extern unsigned int transputer_ldtimer(void);

static CORETIMETYPE
barebones_clock(void)
{
#if COREMARK_SMP_TEST
    struct timespec now;
    /* T4's LDTIMER epoch belongs to a CPU and STTIMER can reset it. The
       coordinating task may migrate between start and stop, so use the
       shared Linux clock and retain the port's 64-us tick units. */
    if (clock_gettime(CLOCK_MONOTONIC, &now)) {
        ee_printf("COREMARK SMP FAIL clock_gettime\n");
        _exit(1);
    }
    return (ee_u32)now.tv_sec * 15625u + (ee_u32)now.tv_nsec / 64000u;
#else
    return (CORETIMETYPE)transputer_ldtimer();
#endif
}

/* T800 low-priority timer: 15625 ticks per second (64us per tick) */
#define GETMYTIME(_t)              (*_t = barebones_clock())
#define MYTIMEDIFF(fin, ini)       ((fin) - (ini))
#define TIMER_RES_DIVIDER          1
#define SAMPLE_TIME_IMPLEMENTATION 1
#define EE_TICKS_PER_SEC           (15625 / TIMER_RES_DIVIDER)

static CORETIMETYPE start_time_val, stop_time_val;

void
start_time(void)
{
    GETMYTIME(&start_time_val);
}

void
stop_time(void)
{
    GETMYTIME(&stop_time_val);
}

CORE_TICKS
get_time(void)
{
    CORE_TICKS elapsed
        = (CORE_TICKS)(MYTIMEDIFF(stop_time_val, start_time_val));
    return elapsed;
}

secs_ret
time_in_secs(CORE_TICKS ticks)
{
    secs_ret retval = ((secs_ret)ticks) / (secs_ret)EE_TICKS_PER_SEC;
    return retval;
}

ee_u32 default_num_contexts = MULTITHREAD;

/* Optional OS validation; timings include timer and affinity checking overhead. */
#if COREMARK_SMP_TEST
#include <signal.h>
#include <string.h>
#if MULTITHREAD < 2 || !USE_PTHREAD
#error COREMARK_SMP_TEST requires multiple pthread contexts
#endif
extern long __transputer_syscall(long, long, long, long, long, long, long);
struct smp_worker {
    core_results *res;
    unsigned cpu;
    long tid;
    volatile sig_atomic_t signals, bad_signal;
    int error;
};
static struct smp_worker smp_workers[MULTITHREAD];
static unsigned smp_started, smp_ready, smp_joined;
static int smp_abort, smp_setup_error, smp_action_saved, smp_mask_saved;
static pthread_mutex_t smp_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t smp_cond = PTHREAD_COND_INITIALIZER;
static struct sigaction smp_old_action;
static sigset_t smp_old_mask;

static long smp_call(long nr, long a, long b, long c)
{
    return __transputer_syscall(nr, a, b, c, 0, 0, 0);
}
static int smp_cpu_is(unsigned wanted)
{
    unsigned cpu = ~0u;
    return smp_call(168, (long)&cpu, 0, 0) == 0 && cpu == wanted;
}
static void smp_signal(int signo, siginfo_t *info, void *context)
{
    int i;
    struct smp_worker *w;
    (void)context;
    if (!info || signo != SIGUSR1) return;
    i = info->si_value.sival_int;
    if (i < 0 || i >= MULTITHREAD) return;
    w = &smp_workers[i];
    if (info->si_code != SI_TIMER || smp_call(178, 0, 0, 0) != w->tid ||
        !smp_cpu_is(w->cpu)) w->bad_signal = 1;
    w->signals++;
}
static void smp_setup(void)
{
    struct sigaction action;
    sigset_t mask;
    memset(&action, 0, sizeof(action));
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    if (sigprocmask(SIG_BLOCK, &mask, &smp_old_mask)) {
        smp_setup_error = 1;
        return;
    }
    smp_mask_saved = 1;
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = smp_signal;
    action.sa_flags = SA_SIGINFO;
    if (sigaction(SIGUSR1, &action, &smp_old_action)) smp_setup_error = 1;
    else smp_action_saved = 1;
}
static void *smp_iterate(void *arg)
{
    struct smp_worker *w = arg;
    struct sigevent event;
    struct itimerspec armed;
    sigset_t mask;
    timer_t timer;
    unsigned affinity = 1u << w->cpu, actual = 0;
    int have_timer = 0, run;
    memset(&event, 0, sizeof(event));
    memset(&armed, 0, sizeof(armed));
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    w->tid = smp_call(178, 0, 0, 0);
    if (smp_setup_error || w->tid <= 0 ||
        smp_call(122, 0, sizeof(affinity), (long)&affinity) != 0 ||
        smp_call(123, 0, sizeof(actual), (long)&actual) <= 0 ||
        actual != affinity || !smp_cpu_is(w->cpu)) w->error = 1;
    event.sigev_notify = SIGEV_THREAD_ID;
    event.sigev_signo = SIGUSR1;
    event.sigev_value.sival_int = w->cpu;
    event._sigev_un._tid = w->tid;
    if (!w->error) {
        if (timer_create(CLOCK_MONOTONIC, &event, &timer)) w->error = 2;
        else have_timer = 1;
    }
    pthread_mutex_lock(&smp_mutex);
    if (w->error) smp_abort = 1;
    smp_ready++;
    pthread_cond_broadcast(&smp_cond);
    while (smp_ready < default_num_contexts && !smp_abort)
        pthread_cond_wait(&smp_cond, &smp_mutex);
    run = !smp_abort;
    pthread_mutex_unlock(&smp_mutex);
    if (run) {
        armed.it_interval.tv_nsec = 250000000;
        armed.it_value = armed.it_interval;
        if (timer_settime(timer, 0, &armed, 0) ||
            sigprocmask(SIG_UNBLOCK, &mask, 0)) w->error = 3;
        else {
            iterate(w->res);
            if (!smp_cpu_is(w->cpu)) w->error = 4;
        }
    } else if (!w->error) w->error = 5;
    if (sigprocmask(SIG_BLOCK, &mask, 0)) w->error = 6;
    if (have_timer && timer_delete(timer)) w->error = 7;
    if (!w->error && (!w->signals || w->bad_signal)) w->error = 8;
    return 0;
}
#endif

#if MULTITHREAD > 1 && USE_PTHREAD
ee_u8
core_start_parallel(core_results *res)
{
    res->port.thread = 0;
#if COREMARK_SMP_TEST
    struct smp_worker *w;
    if (!smp_started) smp_setup();
    if (smp_started >= MULTITHREAD) return 1;
    w = &smp_workers[smp_started];
    w->res = res;
    w->cpu = smp_started++;
    if (pthread_create(&res->port.thread, 0, smp_iterate, w)) {
        w->error = 9;
        pthread_mutex_lock(&smp_mutex);
        smp_abort = 1;
        pthread_cond_broadcast(&smp_cond);
        pthread_mutex_unlock(&smp_mutex);
#else
    if (pthread_create(&res->port.thread, 0, iterate, res)) {
#endif
        res->err++;
        return 1;
    }
    return 0;
}

ee_u8
core_stop_parallel(core_results *res)
{
    if (!res->port.thread || pthread_join(res->port.thread, 0)) {
        res->err++;
        return 1;
    }
#if COREMARK_SMP_TEST
    {
        unsigned i;
        for (i = 0; i < smp_started; i++) if (smp_workers[i].res == res) {
            struct smp_worker *w = &smp_workers[i];
            ee_printf("SMP worker cpu=%u tid=%ld signals=%d bad=%d error=%d\n",
                      w->cpu, w->tid, w->signals, w->bad_signal, w->error);
            smp_joined++;
            break;
        }
    }
#endif
    return 0;
}
#endif

void
portable_init(core_portable *p, int *argc, char *argv[])
{
    (void)argc;
    (void)argv;

    if (sizeof(ee_ptr_int) != sizeof(ee_u8 *))
    {
        ee_printf(
            "ERROR! Please define ee_ptr_int to a type that holds a "
            "pointer!\n");
    }
    if (sizeof(ee_u32) != 4)
    {
        ee_printf("ERROR! Please define ee_u32 to a 32b unsigned type!\n");
    }
    p->portable_id = 1;
}

void
portable_fini(core_portable *p)
{
#if COREMARK_SMP_TEST
    unsigned i;
    int failed = smp_setup_error || smp_abort ||
                 smp_joined != default_num_contexts;
    for (i = 0; i < smp_started; i++)
        if (smp_workers[i].error || smp_workers[i].bad_signal ||
            !smp_workers[i].signals) failed = 1;
    if (smp_action_saved && sigaction(SIGUSR1, &smp_old_action, 0)) failed = 1;
    if (smp_mask_saved && sigprocmask(SIG_SETMASK, &smp_old_mask, 0)) failed = 1;
    ee_printf("COREMARK SMP %s contexts=%u\n", failed ? "FAIL" : "PASS",
              default_num_contexts);
    /* core_main resets res->err during CRC validation, so keep an independent
       verdict and failing process status for the SMP-specific checks. */
    if (failed) _exit(1);
#endif
    p->portable_id = 0;
}
