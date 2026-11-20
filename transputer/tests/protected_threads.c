/* Run as /init in a protected bFLT boot. See protected_threads.S. */
#define NR_EXIT 93
#define NR_GETTID 178
#define NR_YIELD 124
#define NR_WRITE 64
#define CLONE_THREAD_VM_SIGHAND 0x10900
#define ROUNDS 32

#ifdef TP_TEST_THREAD_LIFECYCLE
#define NR_FUTEX 98
#define NR_SET_TID_ADDRESS 96
#define NR_MMAP2 222
#define NR_MUNMAP_OWNED 2002
#define NR_GET_TP 2003
#define NR_SET_TP 2004
#define CLONE_TLS_AND_TIDS 0x380000
#define LIFECYCLES 16
#else
#define LIFECYCLES 1
#endif

struct probe {
    double input, output;
    int src_stride, dst_stride, rows;
    unsigned char src[48], dst[48];
};

extern long __transputer_syscall(long, long, long, long, long, long, long);
extern long tp_thread_clone(const long *args);
extern void tp_thread_probe(struct probe *state);

static struct probe probes[2];
#ifdef TP_TEST_THREAD_LIFECYCLE
static int parent_local;
static volatile int child_tid, blocked[2];
#else
static unsigned int workspace[4096];
#endif
static volatile int turn, finished, worker_tid, worker_result, failed;

static void announce(const char *message, int len)
{
    __transputer_syscall(NR_WRITE, 1, (long)message, len, 0, 0, 0);
}

#define MARK(s) announce(s "\n", sizeof(s "\n") - 1)

#ifdef TP_TEST_THREAD_LIFECYCLE
static int futex(volatile int *word, int op, int value)
{
    return __transputer_syscall(NR_FUTEX, (long)word, op, value, 0, 0, 0);
}

static int *thread_pointer(void)
{
    return (int *)__transputer_syscall(NR_GET_TP, 0, 0, 0, 0, 0, 0);
}
#endif

static void hand_off(int id)
{
    turn = id;
#ifdef TP_TEST_THREAD_LIFECYCLE
    if (futex(&turn, 1, 1) < 0)
        failed = 1;
#endif
}

static void wait_turn(int id)
{
    int old;
#ifdef TP_TEST_THREAD_LIFECYCLE
    int retries = 0;
#endif
    while ((old = turn) != id) {
#ifdef TP_TEST_THREAD_LIFECYCLE
        int result = futex(&turn, 0, old);
        if (result == 0)
            blocked[id]++;
        else if (result != -11 || ++retries == 100) {
            char message[] = "PTHREAD_TEST: FAIL futex wait 000\n";
            unsigned error = -result;
            message[30] += error / 100 % 10;
            message[31] += error / 10 % 10;
            message[32] += error % 10;
            announce(message, sizeof(message) - 1);
            failed = 1;
            return;
        }
#else
        if (__transputer_syscall(NR_YIELD, 0, 0, 0, 0, 0, 0))
            failed = 1;
#endif
    }
}

void tp_thread_switch(struct probe *state)
{
    int id = state == &probes[1];
    hand_off(1 - id);
    wait_turn(id);
}

static int run_probe(int id)
{
    struct probe *p = &probes[id];
    int round, i, row, column, valid = 1;

    p->src_stride = id ? 7 : 8;
    p->dst_stride = id ? 11 : 9;
    p->rows = id ? 2 : 4;
    wait_turn(id);
    for (round = 0; round < ROUNDS; round++) {
        p->input = id ? 42.25 + round : -13.5 - round;
        for (i = 0; i < 48; i++) {
            p->src[i] = 1 + i + round + 71 * id;
            p->dst[i] = 0;
        }
        tp_thread_probe(p);
        if (p->output != p->input)
            valid = 0;
        for (i = 0; i < 48; i++) {
            row = i / p->dst_stride;
            column = i % p->dst_stride;
            if (p->dst[i] != (row < p->rows && column < 4 ?
                             p->src[row * p->src_stride + column] : 0))
                valid = 0;
        }
    }
    return valid;
}

void tp_thread_child(void)
{
#ifdef TP_TEST_THREAD_LIFECYCLE
    int *local = thread_pointer();
    if (!local || local == &parent_local) {
        failed = 1;
    } else {
        *local = 271;
    }
#endif
    worker_tid = __transputer_syscall(NR_GETTID, 0, 0, 0, 0, 0, 0);
    MARK("PTHREAD_TEST: child entered");
    worker_result = run_probe(1);
#ifdef TP_TEST_THREAD_LIFECYCLE
    if (!local || local != thread_pointer() || *local != 271)
        failed = 1;
#endif
    finished = 1;
    __transputer_syscall(NR_EXIT, 0, 0, 0, 0, 0, 0);
    MARK("PTHREAD_TEST: child exit returned");
}

static int run_pair(int tid)
{
#ifdef TP_TEST_THREAD_LIFECYCLE
    unsigned long allocation = __transputer_syscall(NR_MMAP2, 0, 16384, 3, 34, -1, 0);
    long args[6] = { CLONE_THREAD_VM_SIGHAND | CLONE_TLS_AND_TIDS,
                     allocation + 16368, (long)&child_tid, (long)&child_tid, allocation, 0 };
    if (allocation >= (unsigned long)-4095 || !allocation)
        return 0;
#else
    long args[6] = { CLONE_THREAD_VM_SIGHAND, (long)&workspace[4092] };
#endif
    int child = tp_thread_clone(args);
    int result;

    if (child <= 0) {
        MARK("PTHREAD_TEST: FAIL clone");
        return 0;
    }
    MARK("PTHREAD_TEST: clone returned");
    result = run_probe(0);
    hand_off(1);
#ifdef TP_TEST_THREAD_LIFECYCLE
    for (;;) {
        int observed = child_tid;
        if (!observed)
            break;
        if (observed != child)
            failed = 1;
        observed = futex(&child_tid, 0, observed);
        if (observed != 0 && observed != -11)
            failed = 1;
    }
    if (thread_pointer() != &parent_local || parent_local != 901 ||
        !blocked[0] || !blocked[1])
        failed = 1;
    if (__transputer_syscall(NR_MUNMAP_OWNED, allocation, 16384, 0, 0, 0, 0))
        failed = 1;
#else
    while (!finished)
        __transputer_syscall(NR_YIELD, 0, 0, 0, 0, 0, 0);
#endif
    return result && worker_result && finished && !failed && child == worker_tid && child != tid;
}

int main(void)
{
    int i, tid, result = 1;
    MARK("PTHREAD_TEST: start");
    tid = __transputer_syscall(NR_GETTID, 0, 0, 0, 0, 0, 0);
#ifdef TP_TEST_THREAD_LIFECYCLE
    parent_local = 901;
    if (__transputer_syscall(NR_SET_TP, (long)&parent_local, 0, 0, 0, 0, 0) ||
        __transputer_syscall(NR_SET_TID_ADDRESS, 0, 0, 0, 0, 0, 0) != tid)
        failed = 1;
#endif
    for (i = 0; i < LIFECYCLES && result; i++) {
        finished = 0;
        turn = 0;
        result = run_pair(tid);
    }
    if (result)
        MARK("PTHREAD_TEST: PASS");
    else
        MARK("PTHREAD_TEST: FAIL state");
    for (;;)
        ;
}
