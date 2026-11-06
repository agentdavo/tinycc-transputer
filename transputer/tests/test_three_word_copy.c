/* Expected: STHREECOPYOK\nH */
static void copy3(float *dst, const float *src)
{
    float local[3];
    int i;

    local[0] = src[2];
    local[1] = src[0];
    local[2] = src[1];
    for (i = 0; i < 3; i++)
        dst[i] = local[i];
}

/* A forward copy with overlap must retain its sequential behavior. */
static int overlap(void)
{
    int a[4] = { 1, 2, 3, 4 }, i;
    for (i = 0; i < 3; i++)
        a[i + 1] = a[i];
    return i == 3 && a[0] == 1 && a[1] == 1 && a[2] == 1 && a[3] == 1;
}

static int volatile_copy(void)
{
    volatile int src[3] = { 4, 5, 6 }, dst[3] = { 0, 0, 0 };
    int i;
    for (i = 0; i < 3; i++)
        dst[i] = src[i];
    return i == 3 && dst[0] == 4 && dst[1] == 5 && dst[2] == 6;
}

static int continue_copy(void)
{
    int src[3] = { 7, 8, 9 }, dst[3] = { 0, 0, 0 }, i;
    for (i = 0; i < 3; i++) {
        if (i == 1)
            continue;
        dst[i] = src[i];
    }
    return i == 3 && dst[0] == 7 && dst[1] == 0 && dst[2] == 9;
}

static int variable_copy(int n)
{
    int src[3] = { 10, 11, 12 }, dst[3] = { 0, 0, 0 }, i;
    for (i = 0; i < n; i++)
        dst[i] = src[i];
    return i == n && dst[0] == (n > 0 ? 10 : 0) &&
           dst[1] == (n > 1 ? 11 : 0) && dst[2] == (n > 2 ? 12 : 0);
}

static int break_copy(void)
{
    int src[3] = { 13, 14, 15 }, dst[3] = { 0, 0, 0 }, i;
    for (i = 0; i < 3; i++) {
        if (i == 2)
            break;
        dst[i] = src[i];
    }
    return i == 2 && dst[0] == 13 && dst[1] == 14 && dst[2] == 0;
}

void test_main(void)
{
    float src[3] = { 1.25f, -2.5f, 3.75f }, dst[3];
    volatile unsigned *uart = (volatile unsigned *)0x10000000u;
    const char *message;
    int ok;

    copy3(dst, src);
    ok = dst[0] == 3.75f && dst[1] == 1.25f && dst[2] == -2.5f;
    copy3(src, src);
    ok &= src[0] == 3.75f && src[1] == 1.25f && src[2] == -2.5f;
    ok &= overlap() && volatile_copy() && continue_copy();
    ok &= variable_copy(0) && variable_copy(2) && variable_copy(3);
    ok &= break_copy();
    message = ok ? "THREECOPYOK\n" : "THREECOPYBAD\n";
    while (*message)
        *uart = *message++;
}
