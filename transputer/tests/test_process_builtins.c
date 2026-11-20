/* Expected: SPROCOK\nH */
static int child_ws[128];
static volatile int join_record[2] = { 0, 2 };
static int *volatile parent_local;
static volatile int byte_count = -9;

void test_main(void)
{
    int local;
    volatile unsigned *uart = (volatile unsigned *)0x10000000u;
    const char *message;

    _Static_assert(__builtin_types_compatible_p(typeof(__tin(0)), void),
                   "tin only waits");
    _Static_assert(__builtin_types_compatible_p(typeof(__altwt()), void),
                   "altwt only waits");
    _Static_assert(__builtin_types_compatible_p(typeof(__taltwt()), void),
                   "taltwt only waits");

    /* Zero entry offset starts the child at the same next instruction as
       the parent. Its workspace, hence the address of local, differs. */
    parent_local = &local;
    __startp(child_ws + 64, 0);
    if (&local != parent_local)
        __endp(join_record);

    while (join_record[1] == 2)
        __tin(__ldtimer() + 100);
    message = join_record[1] == 1 && __wcnt(byte_count) == -3
              && __wcnt(11) == 2 ? "PROCOK\n" : "FAIL\n";
    while (*message)
        *uart = *message++;
}
