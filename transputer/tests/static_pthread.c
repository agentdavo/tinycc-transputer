#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

extern long __transputer_syscall(long, long, long, long, long, long, long);
#ifdef TP_TEST_LIBC_STRESS
extern int test_threaded_libc(void);
#endif
#ifdef TP_TEST_EVENT_TIME
extern int test_protected_event_time(void);
#endif

static pthread_mutex_t mutex;
static pthread_cond_t change;
static volatile int attempted;
static int ready, released, worker_failed;
static int worker_wait_error, worker_unlock_error, worker_final_errno;

static int fail_step(int step, int error)
{
    char text[] = "PTHREAD_LIBC_TEST: step 0 error 000\n";
    unsigned code = error < 0 ? -error : error;
    text[24] += step;
    text[32] += code / 100 % 10;
    text[33] += code / 10 % 10;
    text[34] += code % 10;
    __transputer_syscall(64, 1, (long)text, sizeof(text) - 1, 0, 0, 0);
    return 0;
}

static void *work(void *arg)
{
    int contention = pthread_mutex_trylock(&mutex);
    int error;
    attempted = 1;
    if (contention == 0)
        pthread_mutex_unlock(&mutex);
    if (contention != EBUSY || pthread_mutex_lock(&mutex))
        worker_failed |= 1;

    errno = 975;
#ifdef TP_TEST_SMP_BENCHMARKS
    if (__transputer_syscall(2000, (long)"/not-a-program", 0, 0, 0, 0, 0) != -EBUSY)
        worker_failed |= 32;
#endif
    if (close(-1) != -1 || errno != EBADF)
        worker_failed |= 2;
    ready = 1;
    if (pthread_cond_broadcast(&change))
        worker_failed |= 4;
    while (!released)
        if ((error = pthread_cond_wait(&change, &mutex))) {
            worker_failed |= 8;
            worker_wait_error = error;
        }
    worker_final_errno = errno;
    worker_unlock_error = pthread_mutex_unlock(&mutex);
    if (worker_final_errno != EBADF || worker_unlock_error)
        worker_failed |= 16;
    return arg;
}

static int test_threads(void)
{
    pthread_t worker;
    void *result;
    int value = 42;
    int error;

#ifdef TP_TEST_SMP_BENCHMARKS
    {
        char byte;
        int fd = open("/dev/null", O_RDWR | O_CLOEXEC);
        if (fd < 3 || fcntl(fd, F_GETFD) != FD_CLOEXEC ||
            read(fd, &byte, 1) != 0 || write(fd, "x", 1) != 1 || close(fd))
            return fail_step(0, errno);
    }
#endif
    errno = 831;
    if (pthread_mutex_init(&mutex, 0) || pthread_cond_init(&change, 0))
        return fail_step(0, 0);
    error = pthread_mutex_lock(&mutex);
    if (error)
        return fail_step(1, error);
    error = pthread_create(&worker, 0, work, &value);
    if (error)
        return fail_step(2, error);
    while (!attempted)
        __transputer_syscall(124, 0, 0, 0, 0, 0, 0);
    while (!ready)
        if ((error = pthread_cond_wait(&change, &mutex)))
            return fail_step(3, error);
    released = 1;
    error = pthread_cond_signal(&change);
    if (error)
        return fail_step(4, error);
    error = pthread_mutex_unlock(&mutex);
    if (error)
        return fail_step(5, error);
    error = pthread_join(worker, &result);
    if (error)
        return fail_step(6, error);
    if (worker_failed || result != &value) {
        if (worker_wait_error)
            fail_step(9, worker_wait_error);
        if (worker_unlock_error)
            fail_step(5, worker_unlock_error);
        if (worker_final_errno != EBADF)
            fail_step(8, worker_final_errno);
        return fail_step(7, worker_failed);
    }
    return errno == 831 ? 1 : fail_step(8, errno);
}

#ifdef TP_TEST_DESKTOP_INTEGRATION
static void sample_proc(const char *path)
{
    static char buffer[160];
    long fd = __transputer_syscall(1024, (long)path, 0, 0, 0, 0, 0);
    if (fd >= 0) {
        int count;
        while ((count = __transputer_syscall(63, fd, (long)buffer,
                                           sizeof(buffer), 0, 0, 0)) > 0)
            __transputer_syscall(64, 1, (long)buffer, count, 0, 0, 0);
        __transputer_syscall(57, fd, 0, 0, 0, 0, 0);
    }
}

static void sample_memory(void)
{
    sample_proc("/proc/meminfo");
#ifdef TP_TEST_SMP_CPUINFO
    sample_proc("/proc/cpuinfo");
#endif
}

static int spawn(const char *path)
{
    const char *argv[] = { path, 0 };
    const char *envp[] = { "HOME=/", 0 };
    return __transputer_syscall(2000, (long)path, (long)argv, (long)envp, 0, 0, 0);
}
#endif

#ifdef TP_TEST_SMP_BENCHMARKS
static int test_input_ownership(void)
{
    struct input_record { long sec, usec; unsigned short type, code; int value; } event;
    struct timespec begin, now;
    int fd, got_key = 0;
    const char *path = "/tmp/tp-input-kbd";
    static const char waiting[] = "INPUT_EVDEV_WAIT\n";
    static const char passed[] = "INPUT_EVDEV_PASS\n";
    static const char failed[] = "INPUT_EVDEV_FAIL\n";

    if (__transputer_syscall(1027, (long)path, 020600, (13 << 8) | 64, 0, 0, 0))
        goto fail;
    fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        goto fail;
    if (clock_gettime(CLOCK_MONOTONIC, &begin)) {
        close(fd);
        goto fail;
    }
    __transputer_syscall(64, 1, (long)waiting, sizeof(waiting)-1, 0, 0, 0);
    do {
        int count = read(fd, &event, sizeof(event));
        if (count == sizeof(event) && event.type == 1 && event.code == 30 && event.value == 1) {
            got_key = 1;
            break;
        }
        if (count < 0 && errno != EAGAIN && errno != EINTR)
            break;
        __transputer_syscall(124, 0, 0, 0, 0, 0, 0);
        if (clock_gettime(CLOCK_MONOTONIC, &now))
            break;
    } while (now.tv_sec - begin.tv_sec < 20);
    if (close(fd) || !got_key)
        goto fail;
    __transputer_syscall(64, 1, (long)passed, sizeof(passed)-1, 0, 0, 0);
    return 1;
fail:
    __transputer_syscall(64, 1, (long)failed, sizeof(failed)-1, 0, 0, 0);
    return 0;
}
#endif

int main(void)
{
    static const char pass[] = "PTHREAD_LIBC_TEST: PASS\n";
    static const char fail[] = "PTHREAD_LIBC_TEST: FAIL\n";
    int ok = test_threads();
    __transputer_syscall(64, 1, (long)(ok ? pass : fail), sizeof(pass) - 1, 0, 0, 0);
#ifdef TP_TEST_LIBC_STRESS
    if (ok) {
        static const char stress_pass[] = "LIBC_THREADS_TEST: PASS\n";
        static const char stress_fail[] = "LIBC_THREADS_TEST: FAIL\n";
        ok = test_threaded_libc() == 0;
        __transputer_syscall(64, 1, (long)(ok ? stress_pass : stress_fail),
                             sizeof(stress_pass) - 1, 0, 0, 0);
    }
#endif
#ifdef TP_TEST_EVENT_TIME
    if (ok) {
        static const char event_pass[] = "EVENT_TIME_TEST: PASS\n";
        static const char event_fail[] = "EVENT_TIME_TEST: FAIL\n";
        ok = test_protected_event_time() == 0;
        __transputer_syscall(64, 1, (long)(ok ? event_pass : event_fail),
                             sizeof(event_pass) - 1, 0, 0, 0);
    }
#endif
#ifdef TP_TEST_SMP_BENCHMARKS
    if (ok)
        ok = test_input_ownership();
#endif
#ifdef TP_TEST_DESKTOP_INTEGRATION
    if (ok) {
        __transputer_syscall(1030, (long)"/proc", 0755, 0, 0, 0, 0);
        __transputer_syscall(40, (long)"proc", (long)"/proc", (long)"proc", 0, 0, 0);
        sample_memory();
#ifdef TP_TEST_SMP_BENCHMARKS
        if (!spawn("/usr/bin/coremark2") && !spawn("/usr/bin/coremark6")) {
#else
        if (!spawn("/usr/bin/coremark")) {
#endif
            sample_memory();
            spawn("/usr/bin/fbwm");
            sample_memory();
        }
    }
#endif
    return !ok;
}
