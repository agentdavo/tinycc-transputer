/* Expected: SFPAOK\nH */
static void scatter(float *dst, const float *src, const int *index)
{
    int i;
    for (i = 0; i < 4; i++)
        dst[index[i]] = src[i] - src[(i + 1) & 3];
}

static void gather(float *dst, const float *src, const int *index)
{
    int i;
    for (i = 0; i < 4; i++) {
        /* The second subscript itself reads through a computed address.
           It can need all three integer registers while dst[i] is live. */
        dst[i] = src[index[index[i]]] + src[i];
    }
}

void test_main(void)
{
    float src[4] = { 1, 4, -2, 8 }, tmp[4], result[4];
    int index[4] = { 2, 0, 3, 1 };
    volatile unsigned *uart = (volatile unsigned *)0x10000000u;
    const char *msg;

    scatter(tmp, src, index);
    gather(result, tmp, index);
    msg = tmp[0] == 6 && tmp[1] == 7 && tmp[2] == -3 && tmp[3] == -10 &&
          result[0] == -4 && result[1] == 4 &&
          result[2] == 4 && result[3] == -4 ? "FPAOK\n" : "FPABAD\n";
    while (*msg)
        *uart = *msg++;
}
