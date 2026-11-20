#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/syscall.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

extern long __transputer_syscall(long, long, long, long, long, long, long);

#define NOTE(s) __transputer_syscall(64, 1, (long)(s "\n"), sizeof(s "\n") - 1, 0, 0, 0)

#ifdef TP_TEST_SMP_AFFINITY
static int pin_cpu(unsigned cpu)
{
    unsigned long mask = 1UL << cpu, actual = 0;
    unsigned observed = 99;
    return __transputer_syscall(122, 0, sizeof(mask), (long)&mask, 0, 0, 0) ||
        __transputer_syscall(123, 0, sizeof(actual), (long)&actual, 0, 0, 0) < 0 ||
        actual != mask ||
        __transputer_syscall(168, (long)&observed, 0, 0, 0, 0, 0) || observed != cpu;
}

static int test_migration(void)
{
    unsigned cpu, round;
    unsigned long mask = 63;
    for (round = 0; round < 2; round++)
        for (cpu = 0; cpu < 6; cpu++) {
            errno = 731;
            if (pin_cpu(cpu) || errno != 731 || close(-1) != -1 || errno != EBADF)
                return 1;
        }
    if (__transputer_syscall(122, 0, sizeof(mask), (long)&mask, 0, 0, 0))
        return 1;
    NOTE("USERSPACE_MIGRATION: PASS cpus=0,1,2,3,4,5 rounds=2 errno=isolated");
    return 0;
}
#endif

static int fail_step(int step)
{
    char text[] = "EVENT_TIME_TEST: step 00 errno 000\n";
    unsigned error = errno;
    text[22] += step / 10;
    text[23] += step % 10;
    text[31] += error / 100 % 10;
    text[32] += error / 10 % 10;
    text[33] += error % 10;
    __transputer_syscall(64, 1, (long)text, sizeof(text) - 1, 0, 0, 0);
    return step;
}

static void *write_pipe(void *arg)
{
    int fd = *(int *)arg;
    return (void *)(long)(write(fd, "x", 1) == 1);
}

static int test_epoll(void)
{
    int fds[2], epfd, rc, count;
    struct epoll_event event, result;
    struct timespec before, after;
    sigset_t mask, original, queried;
    pthread_t worker;
    void *written;
    char byte;

    epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0 || fcntl(epfd, F_GETFD) != FD_CLOEXEC || pipe(fds))
        return fail_step(1);
    if (fds[1] < 3) {
        int fd = fcntl(fds[1], F_DUPFD, 3);
        if (fd < 0 || close(fds[1]))
            return fail_step(2);
        fds[1] = fd;
    }
    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN;
    event.data.u64 = 0x123456789abcdef0ULL;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fds[0], &event) ||
        epoll_wait(epfd, &result, 1, 0) != 0)
        return fail_step(3);
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR2);
    if (sigprocmask(SIG_SETMASK, 0, &original) || clock_gettime(CLOCK_MONOTONIC, &before))
        return fail_step(4);
    NOTE("EPOLL_TEST: timeout");
    if (epoll_pwait(epfd, &result, 1, 30, &mask) != 0 ||
        clock_gettime(CLOCK_MONOTONIC, &after))
        return fail_step(5);
    if ((after.tv_sec - before.tv_sec) * 1000000000LL + after.tv_nsec - before.tv_nsec < 20000000LL ||
        sigprocmask(SIG_SETMASK, 0, &queried) ||
        sigismember(&queried, SIGUSR2) != sigismember(&original, SIGUSR2))
        return fail_step(6);
    if (pthread_create(&worker, 0, write_pipe, &fds[1]))
        return fail_step(7);
    count = epoll_pwait(epfd, &result, 1, 1000, &mask);
    rc = pthread_join(worker, &written);
    if (count != 1 || rc || !written || (result.events & EPOLLIN) == 0 ||
        result.data.u64 != event.data.u64 || read(fds[0], &byte, 1) != 1 || byte != 'x')
        return fail_step(8);
    event.data.u64 = 0x0987654321fedcbaULL;
    if (epoll_ctl(epfd, EPOLL_CTL_MOD, fds[0], &event) || write(fds[1], "y", 1) != 1 ||
        epoll_wait(epfd, &result, 1, 0) != 1 || result.data.u64 != event.data.u64 ||
        epoll_ctl(epfd, EPOLL_CTL_DEL, fds[0], 0) || epoll_wait(epfd, &result, 1, 0) != 0)
        return fail_step(9);
    if (close(epfd) || close(fds[0]) || close(fds[1]))
        return fail_step(10);
    NOTE("EPOLL_TEST: PASS");
    return 0;
}

static int test_timers(void)
{
    struct sigevent event;
    struct itimerspec armed, remaining, stopped;
    struct timespec limit, resolution;
    siginfo_t info;
    sigset_t mask, original;
    timer_t timer;
    int i;

    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    if (sigprocmask(SIG_BLOCK, &mask, &original) ||
        clock_getres(CLOCK_MONOTONIC, &resolution) ||
        (!resolution.tv_sec && !resolution.tv_nsec))
        return fail_step(11);
    memset(&event, 0, sizeof(event));
    event.sigev_notify = SIGEV_SIGNAL;
    event.sigev_signo = SIGUSR1;
    event.sigev_value.sival_int = 12345;
    if (timer_create(CLOCK_MONOTONIC, &event, &timer))
        return fail_step(12);
    memset(&armed, 0, sizeof(armed));
    armed.it_value.tv_nsec = 50000000;
    armed.it_interval.tv_nsec = 70000000;
    if (timer_settime(timer, 0, &armed, 0) || timer_gettime(timer, &remaining) ||
        remaining.it_interval.tv_nsec != armed.it_interval.tv_nsec ||
        (!remaining.it_value.tv_sec && !remaining.it_value.tv_nsec))
        return fail_step(13);
    limit.tv_sec = 1;
    limit.tv_nsec = 0;
    NOTE("POSIX_TIMER_TEST: waiting");
    for (i = 0; i < 2; i++) {
        memset(&info, 0, sizeof(info));
        if (sigtimedwait(&mask, &info, &limit) != SIGUSR1 || info.si_code != SI_TIMER ||
            info.si_value.sival_int != 12345 || timer_getoverrun(timer) < 0)
            return fail_step(14 + i);
    }
    memset(&stopped, 0, sizeof(stopped));
    if (timer_settime(timer, 0, &stopped, &remaining) ||
        remaining.it_interval.tv_nsec != armed.it_interval.tv_nsec ||
        timer_gettime(timer, &remaining) || remaining.it_value.tv_sec || remaining.it_value.tv_nsec ||
        timer_delete(timer) || sigprocmask(SIG_SETMASK, &original, 0))
        return fail_step(16);
    NOTE("POSIX_TIMER_TEST: PASS");
    return 0;
}

/* StatusReg is separate from the seven-word FP image. */
extern unsigned tp_read_flags(void);
extern void tp_write_flags(unsigned);
__asm__(".text\n.global tp_read_flags\ntp_read_flags:\n ldflags\n ret\n"
        ".global tp_write_flags\ntp_write_flags:\n ldl 1\n stflags\n ret\n");

static volatile sig_atomic_t handler_calls;
static volatile sig_atomic_t handler_failed;

static void timer_handler(int sig, siginfo_t *info, void *context)
{
    const ucontext_t *uc = context;
    sigset_t current;

    if (sig != SIGUSR1 || info->si_signo != SIGUSR1 ||
        info->si_code != SI_TIMER || info->si_value.sival_int != 67890 ||
        sigismember(&uc->uc_sigmask, SIGUSR1) != 1 ||
        sigismember(&uc->uc_sigmask, SIGUSR2) != 0 ||
        sigprocmask(SIG_SETMASK, 0, &current) ||
        sigismember(&current, SIGUSR1) != 1 ||
        sigismember(&current, SIGUSR2) != 1)
        handler_failed = 1;
    if ((uc->uc_mcontext.status & 0xbffccu) != 0x104u)
        handler_failed = 1;
    tp_write_flags(0x404u);
    /* A syscall must preserve the handler's own sticky exception flags. */
    if (getpid() <= 0 || tp_read_flags() != 0x404u)
        handler_failed = 1;
    ++handler_calls;
}

static int test_timer_handler(void)
{
    struct sigevent event = {0};
    struct itimerspec armed = {0};
    struct sigaction action = {0}, old, queried;
    struct epoll_event result;
    sigset_t original, outside, waiting, after;
    timer_t timer;
    int epfd, i;
    unsigned saved_flags = tp_read_flags();

    if (sigprocmask(SIG_SETMASK, 0, &original))
        return fail_step(17);
    outside = original;
    sigaddset(&outside, SIGUSR1);
    sigdelset(&outside, SIGUSR2);
    waiting = outside;
    sigdelset(&waiting, SIGUSR1);
    if (sigprocmask(SIG_SETMASK, &outside, 0))
        return fail_step(18);
    action.sa_sigaction = timer_handler;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    sigaddset(&action.sa_mask, SIGUSR2);
    if (sigaction(SIGUSR1, &action, &old) ||
        sigaction(SIGUSR1, 0, &queried) ||
        queried.sa_sigaction != timer_handler || !(queried.sa_flags & SA_SIGINFO) ||
        sigismember(&queried.sa_mask, SIGUSR2) != 1)
        return fail_step(19);

    event.sigev_notify = SIGEV_SIGNAL;
    event.sigev_signo = SIGUSR1;
    event.sigev_value.sival_int = 67890;
    epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0 || timer_create(CLOCK_MONOTONIC, &event, &timer))
        return fail_step(20);

    armed.it_value.tv_nsec = 50000000;
    NOTE("SIGNAL_HANDLER_TEST: waiting");
    for (i = 1; i <= 2; i++) {
        tp_write_flags(0x104u);
        if (timer_settime(timer, 0, &armed, 0))
            return fail_step(21);
        if (epoll_pwait(epfd, &result, 1, 1000, &waiting) != -1 ||
            errno != EINTR || handler_calls != i || handler_failed ||
            tp_read_flags() != 0x104u ||
            sigprocmask(SIG_SETMASK, 0, &after) ||
            sigismember(&after, SIGUSR1) != 1 ||
            sigismember(&after, SIGUSR2) != 0)
            return fail_step(21 + i);
    }
    if (timer_delete(timer) || close(epfd) ||
        sigaction(SIGUSR1, &old, 0) || sigprocmask(SIG_SETMASK, &original, 0))
        return fail_step(24);
    tp_write_flags(saved_flags);
    NOTE("SIGNAL_STATUS_TEST: PASS");
    NOTE("SIGNAL_HANDLER_TEST: PASS");
    return 0;
}

#define SPIN_DELIVERIES 16
#define SPIN_WORKERS 2
#define SPIN_FP_WORDS 7
#define SPIN_MOVE_WORDS 3
#define SPIN_WIDTH 4
#define SPIN_BUFFER_SIZE 48

struct spin_seed {
    double first;
    float second;
    unsigned padding;
    double third;
    unsigned rows, dest_stride, src_stride;
};

struct spin_state {
    unsigned fp[SPIN_FP_WORDS];
    unsigned move[SPIN_MOVE_WORDS];
};

static const struct spin_seed loop_seeds[SPIN_WORKERS] = {
    { 2.5, -1.75f, 0, -17.25, 4, 9, 7 },
    { 11.25, 3.5f, 0, 19.75, 5, 8, 9 },
};
static const struct spin_seed handler_seeds[SPIN_WORKERS] = {
    { -5.5, 13.5f, 0, 47.125, 3, 11, 6 },
    { 27.125, -7.25f, 0, -13.5, 2, 12, 5 },
};

struct spin_probe {
    volatile sig_atomic_t calls, failed;
    volatile sig_atomic_t captured;
    volatile sig_atomic_t ready;
    int saw_peer;
    long tid;
    int timer_id;
    struct spin_state handler_state;
};

static struct spin_probe spin_probes[SPIN_WORKERS];
static volatile sig_atomic_t spin_start;
static volatile sig_atomic_t spin_misdelivered;

extern void tp_spin_seed(const struct spin_seed *seed, int round_up);
extern unsigned tp_timer_spin(volatile sig_atomic_t *calls,
                              struct spin_state *states, unsigned deliveries,
                              volatile sig_atomic_t *captured);
extern void tp_timer_spin_resume(void);
extern void tp_spin_capture(struct spin_state *state);
extern void tp_spin_move(const unsigned char *source, unsigned char *dest, unsigned width);

/* The backward jump is the only timeslice point in this loop. Keep all
 * three integer stack registers, the FP stack and the MOVE2D setup live.
 * Capture an initial snapshot and one after each handler return.
 */
__asm__(
    ".text\n"
    ".global tp_spin_seed\n"
    "tp_spin_seed:\n"
    "  ldl 1\n"
    "  fpldnldb\n"
    "  ldl 1\n"
    "  ldnlp 2\n"
    "  fpldnlsn\n"
    "  ldl 1\n"
    "  ldnlp 4\n"
    "  fpldnldb\n"
    "  ldl 1\n"
    "  ldnl 8\n"
    "  ldl 1\n"
    "  ldnl 7\n"
    "  ldl 1\n"
    "  ldnl 6\n"
    "  move2dinit\n"
    "  ldl 2\n"
    "  cj tp_spin_round_down\n"
    "  .byte 0x2d,0xf4\n" /* fprp, round toward +infinity */
    "  ret\n"
    "tp_spin_round_down:\n"
    "  .byte 0x2d,0xf5\n" /* fprm, round toward -infinity */
    "  ret\n"
    ".global tp_spin_capture\n"
    "tp_spin_capture:\n"
    "  ldl 1\n"
    "  .byte 0x60,0xff\n" /* fpstall */
    "  ldl 1\n"
    "  ldnlp 7\n"
    "  .byte 0x61,0xf0\n" /* stmove2dinit, leaves MOVE2D state live */
    "  ldl 1\n"
    "  .byte 0x60,0xfe\n" /* fpldall */
    "  ret\n"
    ".global tp_spin_move\n"
    "tp_spin_move:\n"
    "  ldl 1\n"
    "  ldl 2\n"
    "  ldl 3\n"
    "  move2dall\n"
    "  .byte 0x2d,0xf0\n" /* fprn */
    "  ret\n"
    ".global tp_timer_spin\n"
    ".global tp_timer_spin_resume\n"
    "tp_timer_spin:\n"
    "  ajw -3\n"
    "  ldc 0\n"
    "  stl 0\n" /* completed iterations */
    "  ldc 0\n"
    "  stl 1\n" /* last observed handler count */
    "  ldl 5\n"
    "  call tp_spin_capture\n"
    "  ldl 5\n"
    "  ldnlp 10\n"
    "  stl 2\n" /* next snapshot */
    "  ldc 17\n"
    "  ldc 29\n"
    "  ldc 43\n"
    "tp_timer_spin_resume:\n"
    "  eqc 43\n"
    "  cj tp_timer_spin_fail\n"
    "  eqc 29\n"
    "  cj tp_timer_spin_fail\n"
    "  eqc 17\n"
    "  cj tp_timer_spin_fail\n"
    "  ldl 0\n"
    "  adc 1\n"
    "  stl 0\n"
    "  ldl 0\n"
    "  eqc 20000000\n"
    "  cj tp_timer_spin_check\n"
    "tp_timer_spin_fail:\n"
    "  ldc 0\n"
    "  ajw 3\n"
    "  ret\n"
    "tp_timer_spin_check:\n"
    "  ldl 4\n"
    "  ldnl 0\n"
    "  ldl 1\n"
    "  diff\n"
    "  cj tp_timer_spin_count\n"
    "  ldl 4\n"
    "  ldnl 0\n"
    "  stl 1\n"
    "  ldl 2\n"
    "  call tp_spin_capture\n"
    "  ldl 2\n"
    "  ldnlp 10\n"
    "  stl 2\n"
    "  ldl 7\n"
    "  cj tp_timer_spin_count\n"
    "  ldl 1\n"
    "  ldl 7\n"
    "  stnl 0\n" /* acknowledge the fully recorded handler return */
    "tp_timer_spin_count:\n"
    "  ldl 1\n"
    "  ldl 6\n"
    "  diff\n"
    "  cj tp_timer_spin_done\n"
    "  ldc 17\n"
    "  ldc 29\n"
    "  ldc 43\n"
    "  j tp_timer_spin_resume\n"
    "tp_timer_spin_done:\n"
    "  ldl 0\n"
    "  ajw 3\n"
    "  ret\n"
);

static void spin_timer_handler(int sig, siginfo_t *info, void *context)
{
    const ucontext_t *uc = context;
    unsigned worker = info->si_value.sival_int;
    struct spin_probe *probe, *peer;
    struct timespec now;
    struct spin_state state;

    if (worker >= SPIN_WORKERS) {
        spin_misdelivered = 1;
        return;
    }
    probe = &spin_probes[worker];
    peer = &spin_probes[1 - worker];
    if (__transputer_syscall(__NR_gettid, 0, 0, 0, 0, 0, 0) != probe->tid) {
        spin_misdelivered = 1;
        return;
    }
    if (probe->calls == SPIN_DELIVERIES)
        return;
    /* A delayed periodic timer can fire again at sigreturn, before the
     * loop records the previous return. Count only captured transitions. */
    if (probe->calls != probe->captured)
        return;
    if (sig != SIGUSR1 || info->si_code != SI_TIMER ||
        uc->uc_mcontext.iptr != (unsigned long)tp_timer_spin_resume ||
        uc->uc_mcontext.areg != 43 || uc->uc_mcontext.breg != 29 ||
        uc->uc_mcontext.creg != 17)
        probe->failed = 1;
    if (probe->calls == 0)
        probe->timer_id = info->si_timerid;
    else if (probe->timer_id != info->si_timerid)
        probe->failed = 1;
    tp_spin_seed(&handler_seeds[worker], worker != 0);
    tp_spin_capture(&probe->handler_state);
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        probe->failed = 1;
    tp_spin_capture(&state);
    if (memcmp(&state, &probe->handler_state, sizeof(state)))
        probe->failed = 1;
    if (peer->calls > 0 && peer->calls < SPIN_DELIVERIES)
        probe->saw_peer = 1;
    ++probe->calls;
}

static int run_spin_probe(unsigned worker, timer_t timer)
{
    struct spin_probe *probe = &spin_probes[worker];
    const struct spin_seed *loop_seed = &loop_seeds[worker];
    const struct spin_seed *handler_seed = &handler_seeds[worker];
    const struct spin_state *handler_state = &probe->handler_state;
    struct itimerspec armed = {0};
    struct spin_state states[SPIN_DELIVERIES + 1] = {0};
    unsigned char source[SPIN_BUFFER_SIZE], dest[SPIN_BUFFER_SIZE];
    unsigned iterations, i;

    armed.it_value.tv_nsec = 50000000 + worker * 10000000;
    armed.it_interval.tv_nsec = 50000000 + worker * 10000000;
    for (i = 0; i < sizeof(source); i++)
        source[i] = i + 1;
    memset(dest, 0xa5, sizeof(dest));
    if (timer_settime(timer, 0, &armed, 0))
        return fail_step(27);
    tp_spin_seed(loop_seed, worker == 0);
    iterations = tp_timer_spin(&probe->calls, states, SPIN_DELIVERIES, &probe->captured);
    if (!iterations || probe->calls != SPIN_DELIVERIES ||
        probe->captured != SPIN_DELIVERIES || probe->failed)
        return fail_step(28);
    tp_spin_move(source, dest, SPIN_WIDTH);
    if (states[0].fp[0] == handler_state->fp[0] ||
        !memcmp(&states[0].fp[1], &handler_state->fp[1], 6 * sizeof(unsigned)) ||
        states[0].move[0] != loop_seed->rows ||
        states[0].move[1] != loop_seed->dest_stride ||
        states[0].move[2] != loop_seed->src_stride ||
        handler_state->move[0] != handler_seed->rows ||
        handler_state->move[1] != handler_seed->dest_stride ||
        handler_state->move[2] != handler_seed->src_stride)
        return fail_step(30);
    for (i = 1; i <= SPIN_DELIVERIES; i++) {
        if (memcmp(states[i].fp, states[0].fp, sizeof(states[0].fp))) {
            unsigned word, nibble;
            const char hex[] = "0123456789abcdef";
            for (word = 0; word < 7; word++) {
                char line[] = "FP_STATE worker=0 sample=00 word=0 expected=00000000 actual=00000000\n";
                line[16] += worker;
                line[25] += i / 10;
                line[26] += i % 10;
                line[33] += word;
                for (nibble = 0; nibble < 8; nibble++) {
                    line[44 + nibble] = hex[(states[0].fp[word] >> (28 - 4*nibble)) & 15];
                    line[60 + nibble] = hex[(states[i].fp[word] >> (28 - 4*nibble)) & 15];
                }
                __transputer_syscall(64, 1, (long)line, sizeof(line)-1, 0, 0, 0);
            }
            return fail_step(31);
        }
        if (memcmp(states[i].move, states[0].move, sizeof(states[0].move)))
            return fail_step(32);
    }
    for (i = 0; i < sizeof(dest); i++) {
        unsigned row = i / loop_seed->dest_stride;
        unsigned column = i % loop_seed->dest_stride;
        unsigned char expected = 0xa5;

        if (row < loop_seed->rows && column < SPIN_WIDTH)
            expected = source[row * loop_seed->src_stride + column];
        if (dest[i] != expected)
            return fail_step(33);
    }
    return 0;
}

static void *spin_worker(void *arg)
{
    unsigned worker = (unsigned long)arg;
    struct spin_probe *probe = &spin_probes[worker];
    struct spin_probe *peer = &spin_probes[1 - worker];
    struct sigevent event = {0};
    sigset_t mask;
    timer_t timer;
    int rc;

#ifdef TP_TEST_SMP_AFFINITY
    if (pin_cpu(4 + worker)) {
        probe->ready = -1;
        return (void *)1;
    }
#endif

    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    if (sigprocmask(SIG_UNBLOCK, &mask, 0)) {
        probe->ready = -1;
        return (void *)(long)fail_step(25);
    }
    probe->tid = __transputer_syscall(__NR_gettid, 0, 0, 0, 0, 0, 0);
    event.sigev_notify = SIGEV_THREAD_ID;
    event.sigev_signo = SIGUSR1;
    event.sigev_value.sival_int = worker;
    event._sigev_un._tid = probe->tid;
    if (probe->tid <= 0 || timer_create(CLOCK_MONOTONIC, &event, &timer)) {
        probe->ready = -1;
        return (void *)(long)fail_step(26);
    }

    /* Both timers must exist before either worker enters its busy loop. */
    probe->ready = 1;
    while (spin_start == 0 || (spin_start > 0 && peer->ready == 0))
        __transputer_syscall(__NR_sched_yield, 0, 0, 0, 0, 0, 0);
    rc = spin_start > 0 && peer->ready > 0 ? run_spin_probe(worker, timer) : 1;
    if (timer_delete(timer) && !rc)
        rc = fail_step(29);
    if (!rc) {
        if (worker == 0)
            NOTE("SIGNAL_THREAD_0: PASS");
        else
            NOTE("SIGNAL_THREAD_1: PASS");
    }
    return (void *)(long)rc;
}

static int test_timer_timeslice(void)
{
    struct sigaction action = {0}, old;
    sigset_t mask, saved;
    pthread_t threads[SPIN_WORKERS];
    unsigned created, i;
    int rc = 0;

    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = spin_timer_handler;
    action.sa_flags = SA_SIGINFO;
    if (sigprocmask(SIG_BLOCK, &mask, &saved))
        return fail_step(34);
    if (sigaction(SIGUSR1, &action, &old)) {
        sigprocmask(SIG_SETMASK, &saved, 0);
        return fail_step(35);
    }

    NOTE("SIGNAL_TIMESLICE_TEST: starting two workers");
    for (created = 0; created < SPIN_WORKERS; created++) {
        rc = pthread_create(&threads[created], 0, spin_worker, (void *)(long)created);
        if (rc)
            break;
    }
    spin_start = rc ? -1 : 1;
    if (rc)
        rc = fail_step(36);
    for (i = 0; i < created; i++) {
        void *result;

        if ((pthread_join(threads[i], &result) || result) && !rc)
            rc = fail_step(37);
    }
    if ((sigaction(SIGUSR1, &old, 0) || sigprocmask(SIG_SETMASK, &saved, 0)) && !rc)
        rc = fail_step(38);
    if (rc)
        return rc;
    if (spin_misdelivered || spin_probes[0].tid == spin_probes[1].tid ||
        spin_probes[0].timer_id == spin_probes[1].timer_id ||
        !spin_probes[0].saw_peer || !spin_probes[1].saw_peer)
        return fail_step(39);
    NOTE("SIGNAL_STATE_TEST: PASS");
    NOTE("SIGNAL_TIMESLICE_TEST: PASS");
    NOTE("SIGNAL_THREADS_TEST: PASS");
    return 0;
}

static const struct spin_seed nested_seeds[2] = {
    { -31.5, 6.25f, 0, 73.125, 2, 13, 10 },
    { 81.25, -9.5f, 0, -93.5, 3, 14, 8 },
};
struct nested_probe {
    volatile sig_atomic_t outer_calls, inner_calls, failed, outer_active, ready;
    long tid;
    timer_t inner_timer;
    struct spin_state outer_state, inner_state;
};
static struct nested_probe nested_probes[2];
static volatile sig_atomic_t nested_start, nested_wrong_thread;

static struct nested_probe *nested_recipient(siginfo_t *info)
{
    unsigned worker = info->si_value.sival_int;
    if (worker >= 2 || __transputer_syscall(__NR_gettid, 0, 0, 0, 0, 0, 0) != nested_probes[worker].tid) {
        nested_wrong_thread = 1;
        return 0;
    }
    return &nested_probes[worker];
}

static int nested_context_ok(const ucontext_t *uc, int outer_blocked)
{
    return uc->uc_mcontext.iptr == (unsigned long)tp_timer_spin_resume &&
           uc->uc_mcontext.areg == 43 && uc->uc_mcontext.breg == 29 &&
           uc->uc_mcontext.creg == 17 &&
           sigismember(&uc->uc_sigmask, SIGUSR1) == outer_blocked &&
           sigismember(&uc->uc_sigmask, SIGUSR2) == 0;
}

static int nested_mask_ok(int outer_blocked, int inner_blocked)
{
    sigset_t mask;

    return sigprocmask(SIG_SETMASK, 0, &mask) == 0 &&
           sigismember(&mask, SIGUSR1) == outer_blocked &&
           sigismember(&mask, SIGUSR2) == inner_blocked;
}

static int spin_move_state_ok(const struct spin_state *state, const struct spin_seed *seed)
{
    return state->move[0] == seed->rows && state->move[1] == seed->dest_stride &&
           state->move[2] == seed->src_stride;
}

static void nested_inner_handler(int sig, siginfo_t *info, void *context)
{
    struct nested_probe *probe = nested_recipient(info);
    unsigned worker = info->si_value.sival_int;
    if (!probe) return;
    if (sig != SIGUSR2 || info->si_code != SI_TIMER ||
        !probe->outer_active || probe->outer_calls || probe->inner_calls ||
        !nested_context_ok(context, 1) || !nested_mask_ok(1, 1))
        probe->failed = 1;
    tp_spin_seed(&nested_seeds[worker], worker == 0);
    tp_spin_capture(&probe->inner_state);
    probe->inner_calls = 1;
}

static void nested_outer_handler(int sig, siginfo_t *info, void *context)
{
    struct itimerspec armed = {0};
    struct spin_state states[2];
    struct nested_probe *probe = nested_recipient(info);
    unsigned worker = info->si_value.sival_int;
    const struct spin_seed *seed = &handler_seeds[worker];
    if (!probe) return;

    if (sig != SIGUSR1 || info->si_code != SI_TIMER ||
        probe->outer_active || probe->outer_calls || probe->inner_calls ||
        !nested_context_ok(context, 0) || !nested_mask_ok(1, 0))
        probe->failed = 1;
    probe->outer_active = 1;
    armed.it_value.tv_nsec = 50000000;
    if (timer_settime(probe->inner_timer, 0, &armed, 0)) {
        probe->failed = 1;
    } else {
        tp_spin_seed(seed, worker != 0);
        if (!tp_timer_spin(&probe->inner_calls, states, 1, 0) ||
            memcmp(&states[0], &states[1], sizeof(states[0])) ||
            !spin_move_state_ok(&states[1], seed) ||
            !spin_move_state_ok(&probe->inner_state, &nested_seeds[worker]) ||
            !memcmp(states[1].fp, probe->inner_state.fp, sizeof(states[1].fp)) ||
            !nested_mask_ok(1, 0))
            probe->failed = 1;
        else
            probe->outer_state = states[1];
    }
    probe->outer_active = 0;
    probe->outer_calls = 1;
}

static void *nested_worker(void *arg)
{
    unsigned worker = (unsigned long)arg;
    struct nested_probe *probe = &nested_probes[worker];
    struct sigevent event = {0};
    struct itimerspec armed = {0};
    struct spin_state states[2];
    sigset_t mask, saved;
    timer_t outer;
    int rc = 0;

#ifdef TP_TEST_SMP_AFFINITY
    if (pin_cpu(2 + worker)) {
        probe->ready = -1;
        return (void *)1;
    }
#endif

    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGUSR2);
    if (sigprocmask(SIG_UNBLOCK, &mask, &saved)) {
        probe->ready = -1;
        return (void *)(long)fail_step(40);
    }
    probe->tid = __transputer_syscall(__NR_gettid, 0, 0, 0, 0, 0, 0);
    event._sigev_un._tid = probe->tid;
    event.sigev_notify = SIGEV_THREAD_ID;
    event.sigev_signo = SIGUSR2;
    event.sigev_value.sival_int = worker;
    if (timer_create(CLOCK_MONOTONIC, &event, &probe->inner_timer)) {
        rc = fail_step(43);
        goto restore_mask;
    }
    event.sigev_signo = SIGUSR1;
    event.sigev_value.sival_int = worker;
    if (timer_create(CLOCK_MONOTONIC, &event, &outer)) {
        rc = fail_step(44);
        goto delete_inner;
    }
    probe->ready = 1;
    while (!nested_start || (nested_start > 0 && !nested_probes[1-worker].ready))
        __transputer_syscall(__NR_sched_yield, 0, 0, 0, 0, 0, 0);
    if (nested_start < 0 || nested_probes[1-worker].ready < 0) {
        rc = 1;
        goto delete_outer;
    }
    armed.it_value.tv_nsec = 50000000;
    NOTE("SIGNAL_NESTED_TEST: spinning");
    if (timer_settime(outer, 0, &armed, 0)) {
        rc = fail_step(45);
    } else {
        tp_spin_seed(&loop_seeds[worker], worker == 0);
        if (!tp_timer_spin(&probe->outer_calls, states, 1, 0) ||
            probe->failed || probe->outer_active || probe->inner_calls != 1 ||
            memcmp(&states[0], &states[1], sizeof(states[0])) ||
            !memcmp(states[1].fp, probe->outer_state.fp, sizeof(states[1].fp)) ||
            !memcmp(states[1].fp, probe->inner_state.fp, sizeof(states[1].fp)) ||
            !spin_move_state_ok(&states[1], &loop_seeds[worker]) || !nested_mask_ok(0, 0))
            rc = fail_step(46);
    }
delete_outer:
    if (timer_delete(outer) && !rc)
        rc = fail_step(47);
delete_inner:
    if (timer_delete(probe->inner_timer) && !rc)
        rc = fail_step(48);
restore_mask:
    if (sigprocmask(SIG_SETMASK, &saved, 0) && !rc)
        rc = fail_step(49);
    if (!rc)
        NOTE("SIGNAL_NESTED_TEST: PASS");
    if (rc) probe->ready = -1;
    return (void *)(long)rc;
}

static int test_nested_timers(void)
{
    struct sigaction action = {0}, old[2];
    sigset_t mask, saved;
    pthread_t threads[2];
    unsigned created, i;
    int rc = 0;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGUSR2);
    if (sigprocmask(SIG_BLOCK, &mask, &saved)) return fail_step(50);
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO;
    action.sa_sigaction = nested_outer_handler;
    if (sigaction(SIGUSR1, &action, &old[0])) return fail_step(51);
    action.sa_sigaction = nested_inner_handler;
    if (sigaction(SIGUSR2, &action, &old[1])) return fail_step(52);
    for (created = 0; created < 2; created++)
        if (pthread_create(&threads[created], 0, nested_worker, (void *)(long)created))
            break;
    nested_start = created == 2 ? 1 : -1;
    rc = created == 2 ? 0 : fail_step(53);
    for (i = 0; i < created; i++) {
        void *result;
        if (pthread_join(threads[i], &result) || result) rc = fail_step(54);
    }
    if (sigaction(SIGUSR1, &old[0], 0) || sigaction(SIGUSR2, &old[1], 0) ||
        sigprocmask(SIG_SETMASK, &saved, 0)) rc = fail_step(55);
    if (nested_wrong_thread || nested_probes[0].tid == nested_probes[1].tid)
        rc = fail_step(56);
    if (!rc) NOTE("SIGNAL_NESTED_THREADS_TEST: PASS");
    return rc;
}

int test_protected_event_time(void)
{
    int rc = 0;
#ifdef TP_TEST_TVX
    extern int test_protected_tvx(void);
    rc = test_protected_tvx();
#endif
#ifdef TP_TEST_SMP_AFFINITY
    if (!rc)
        rc = test_migration();
#endif
    if (!rc)
        rc = test_epoll();
    if (!rc)
        rc = test_timers();
    if (!rc)
        rc = test_timer_handler();
    if (!rc)
        rc = test_timer_timeslice();
    return rc ? rc : test_nested_timers();
}
