/* Expected: SINDEXEDFP_OK\nH */
/* The ordinary loop enables address reuse. Volatile casts and aliases must
 * retain every index read even though casts can remove the value qualifier. */
void plain(float *out, const float *divisor)
{
    float values[3]; int i;
    values[0] = 12; values[1] = 24; values[2] = 36;
    for (i = 0; i < 3; ++i) values[i] = values[i] / divisor[i];
    for (i = 0; i < 3; ++i) out[i] = values[i];
}

void volatile_index(float *out, const float *divisor)
{
    float values[3]; volatile int i;
    values[0] = 12; values[1] = 24; values[2] = 36;
    for (i = 0; i < 3; ++i) values[i] = values[i] / divisor[i];
    for (i = 0; i < 3; ++i) out[i] = values[i];
}

void volatile_cast(float *out, const float *divisor)
{
    float values[3]; volatile int i;
    values[0] = 12; values[1] = 24; values[2] = 36;
    for (i = 0; i < 3; ++i) values[(int)i] = values[(int)i] / divisor[i];
    for (i = 0; i < 3; ++i) out[i] = values[i];
}

void volatile_alias(float *out, const float *divisor)
{
    float values[3]; int i;
    values[0] = 12; values[1] = 24; values[2] = 36;
    for (i = 0; i < 3; ++i)
        values[(int)*(volatile int *)&i] = values[(int)*(volatile int *)&i] / divisor[i];
    for (i = 0; i < 3; ++i) out[i] = values[i];
}

void test_main(void)
{
    float out[3], divisor[3] = { 2, 3, 4 };
    volatile unsigned *uart = (volatile unsigned *)0x10000000u;
    const char *message;
    int ok;
    plain(out, divisor);
    ok = out[0] == 6 && out[1] == 8 && out[2] == 9;
    volatile_index(out, divisor);
    ok &= out[0] == 6 && out[1] == 8 && out[2] == 9;
    volatile_cast(out, divisor);
    ok &= out[0] == 6 && out[1] == 8 && out[2] == 9;
    volatile_alias(out, divisor);
    ok &= out[0] == 6 && out[1] == 8 && out[2] == 9;
    message = ok ? "INDEXEDFP_OK\n" : "INDEXEDFP_FAIL\n";
    while (*message) *uart = *message++;
}
