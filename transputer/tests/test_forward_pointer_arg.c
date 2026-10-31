/*
 * A pointer parameter forwarded through an always_inline helper must be
 * passed by value, not as the address of its incoming workspace slot.
 *
 * Expected: SPF123C456OK\nH
 */

struct lock_word {
    int count;
    int owner;
};

static void uart_putc(int c)
{
    *(volatile int *)0x10000000 = c;
}

__attribute__((noinline))
static int slowpath(struct lock_word *lock, int state)
{
    int old = lock->count;
    if (old == 0)
        lock->count = state;
    return old * 100 + lock->count;
}

static inline int trylock(struct lock_word *lock)
{
    if (lock->count == 0) {
        lock->count = 1;
        return 1;
    }
    return 0;
}

/* Keep both calls out of line.  The backend must not reuse its cached
 * A-register tag for "lock" after reject() has clobbered the call registers. */
__attribute__((noinline))
static int reject(struct lock_word *lock)
{
    return lock->count == 0;
}

__attribute__((noinline))
static int call_then_forward(struct lock_word *lock)
{
    if (!reject(lock))
        return slowpath(lock, 8);
    return -1;
}

__attribute__((always_inline))
static inline int forward(struct lock_word *lock, int state)
{
    if (!trylock(lock))
        return slowpath(lock, state);
    return lock->count;
}

__attribute__((noinline))
static int wrapper(struct lock_word *lock)
{
    return forward(lock, 7);
}

void test_main(void)
{
    struct lock_word lock;
    int r1;
    int r2;
    int r3;
    int r4;
    int r5;
    int r6;

    lock.count = 5;
    lock.owner = 99;
    r1 = wrapper(&lock) == 505;
    r2 = lock.count == 5;
    r3 = lock.owner == 99;

    r4 = call_then_forward(&lock) == 505;
    r5 = lock.count == 5;
    r6 = lock.owner == 99;

    uart_putc('P');
    uart_putc('F');
    uart_putc(r1 ? '1' : 'x');
    uart_putc(r2 ? '2' : 'x');
    uart_putc(r3 ? '3' : 'x');
    uart_putc('C');
    uart_putc(r4 ? '4' : 'x');
    uart_putc(r5 ? '5' : 'x');
    uart_putc(r6 ? '6' : 'x');
    uart_putc(r1 && r2 && r3 && r4 && r5 && r6 ? 'O' : 'F');
    uart_putc('K');
    uart_putc('\n');
}
