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
    message = ok ? "THREECOPYOK\n" : "THREECOPYBAD\n";
    while (*message)
        *uart = *message++;
}
