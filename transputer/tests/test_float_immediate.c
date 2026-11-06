/* Expected: SFIMMOK\nH */
typedef union { float f; unsigned int u; } Bits;
static volatile float values[4];

void test_main(void)
{
    volatile unsigned int *uart = (volatile unsigned int *)0x10000000u;
    Bits b;
    const char *message;
    int ok = 1;

    values[0] = 0.0f;
    values[1] = -0.0f;
    values[2] = 1.5f;
    values[3] = -2.25f;
    b.f = values[0]; ok &= b.u == 0x00000000u;
    b.f = values[1]; ok &= b.u == 0x80000000u;
    b.f = values[2]; ok &= b.u == 0x3fc00000u;
    b.f = values[3]; ok &= b.u == 0xc0100000u;
    message = ok ? "FIMMOK\n" : "FIMMBAD\n";
    while (*message)
        *uart = *message++;
}
