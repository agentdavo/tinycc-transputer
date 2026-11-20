#include <errno.h>
#include <limits.h>
#include "pthread.h"

#define NR_EXIT 93
#define NR_FUTEX 98
#define NR_MMAP2 222
#define NR_MUNMAP_OWNED 2002
#define NR_GET_TP 2003
#define NR_CMPXCHG 2005
#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define STACK_BYTES 32768
/* VM, FS, FILES, SIGHAND, THREAD, SYSVSEM, SETTLS, PARENT_SETTID, CHILD_CLEARTID */
#define THREAD_FLAGS 0x3d0f00

struct tp_pthread {
    int local_errno; /* Must be first for uClibc's __errno_location. */
    volatile unsigned tid;
    void *(*start)(void *);
    void *argument;
    void *result;
};

extern long __transputer_syscall(long, long, long, long, long, long, long);
extern long __tp_pthread_clone(const long *);
static struct tp_pthread initial;

static int futex(volatile unsigned *word, int command, unsigned value)
{
    return __transputer_syscall(NR_FUTEX, (long)word, command, value, 0, 0, 0);
}

static int exchange_if_equal(volatile unsigned *word, unsigned old, unsigned new)
{
    return __transputer_syscall(NR_CMPXCHG, (long)word, old, new, 0, 0, 0);
}

pthread_t pthread_self(void)
{
    pthread_t self = (pthread_t)__transputer_syscall(NR_GET_TP, 0, 0, 0, 0, 0, 0);
    return self ? self : &initial;
}

int pthread_equal(pthread_t left, pthread_t right)
{
    return left == right;
}

void __tp_pthread_start(void)
{
    pthread_t self = pthread_self();
    self->result = self->start(self->argument);
    __transputer_syscall(NR_EXIT, 0, 0, 0, 0, 0, 0);
    for (;;)
        ;
}

int pthread_create(pthread_t *out, const pthread_attr_t *attr,
                   void *(*start)(void *), void *argument)
{
    unsigned long memory;
    pthread_t thread;
    long args[6];
    long result;

    if (!out || !start)
        return EINVAL;
    if (attr)
        return ENOTSUP;
    memory = __transputer_syscall(NR_MMAP2, 0, STACK_BYTES, 3, 34, -1, 0);
    if (memory >= (unsigned long)-4095)
        return -(long)memory;
    thread = (pthread_t)memory;
    thread->local_errno = 0;
    thread->start = start;
    thread->argument = argument;
    args[0] = THREAD_FLAGS;
    args[1] = memory + STACK_BYTES - 16;
    args[2] = (long)&thread->tid;
    args[3] = args[2];
    args[4] = memory;
    args[5] = 0;
    result = __tp_pthread_clone(args);
    if (result < 0) {
        __transputer_syscall(NR_MUNMAP_OWNED, memory, STACK_BYTES, 0, 0, 0, 0);
        return -result;
    }
    *out = thread;
    return 0;
}

int pthread_join(pthread_t thread, void **result)
{
    unsigned tid;
    long rc;

    if (!thread || thread == &initial)
        return EINVAL;
    if (thread == pthread_self())
        return EDEADLK;
    while ((tid = thread->tid) != 0) {
        rc = futex(&thread->tid, FUTEX_WAIT, tid);
        if (rc < 0 && rc != -EAGAIN && rc != -EINTR)
            return -rc;
    }
    if (result)
        *result = thread->result;
    rc = __transputer_syscall(NR_MUNMAP_OWNED, (long)thread, STACK_BYTES, 0, 0, 0, 0);
    return rc < 0 ? -rc : 0;
}

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    if (attr)
        return ENOTSUP;
    mutex->locked = 0;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    return mutex->locked ? EBUSY : 0;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
    int rc = exchange_if_equal(&mutex->locked, 0, 1);
    return rc == 1 ? 0 : rc ? -rc : EBUSY;
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    int rc;
    while ((rc = pthread_mutex_trylock(mutex)) == EBUSY) {
        rc = futex(&mutex->locked, FUTEX_WAIT, 1);
        if (rc < 0 && rc != -EAGAIN && rc != -EINTR)
            return -rc;
    }
    return rc;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    int rc = exchange_if_equal(&mutex->locked, 1, 0);
    if (rc != 1)
        return rc ? -rc : EPERM;
    rc = futex(&mutex->locked, FUTEX_WAKE, 1);
    return rc < 0 ? -rc : 0;
}

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    if (attr)
        return ENOTSUP;
    cond->sequence = 0;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond)
{
    (void)cond;
    return 0;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    unsigned sequence = cond->sequence;
    int result = pthread_mutex_unlock(mutex);
    int locked;
    if (result)
        return result;
    result = futex(&cond->sequence, FUTEX_WAIT, sequence);
    locked = pthread_mutex_lock(mutex);
    if (locked)
        return locked;
    return result < 0 && result != -EAGAIN && result != -EINTR ? -result : 0;
}

static int cond_wake(pthread_cond_t *cond, int count)
{
    unsigned sequence;
    int rc;
    do {
        sequence = cond->sequence;
        rc = exchange_if_equal(&cond->sequence, sequence, sequence + 1);
    } while (rc == 0);
    if (rc < 0)
        return -rc;
    rc = futex(&cond->sequence, FUTEX_WAKE, count);
    return rc < 0 ? -rc : 0;
}

int pthread_cond_signal(pthread_cond_t *cond)
{
    return cond_wake(cond, 1);
}

int pthread_cond_broadcast(pthread_cond_t *cond)
{
    return cond_wake(cond, INT_MAX);
}
