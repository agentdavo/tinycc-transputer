/* printf integer conversions with flags, width and precision.  The suite's
 * printf ignored the width and the 0 flag for %u, %x, %X and %o, so a
 * differential test's "%08x" printed 0f112a7b as f112a7b.  Expected output
 * is host gcc/glibc's. */
int printf(const char *, ...);

int main(void)
{
    static const int iv[] = { 0, 7, -7, 42, -42, 2147483647, -2147483647 - 1 };
    static const unsigned uv[] = { 0, 7, 0x0f112a7bu, 0xffffffffu, 255 };
    static const long long lv[] = { 0, -1, 9007199254740993LL, -9223372036854775807LL - 1 };
    static const unsigned long long ulv[] = { 0, 0x00ae9ecf00000001ULL, 18446744073709551615ULL };
    int i;

    for (i = 0; i < (int)(sizeof iv / sizeof iv[0]); i++)
        printf("[%d|%5d|%-5d|%05d|%+d|% d|%.3d|%8.3d|%-+6d|%+06d]\n",
               iv[i], iv[i], iv[i], iv[i], iv[i], iv[i], iv[i], iv[i], iv[i], iv[i]);
    for (i = 0; i < (int)(sizeof uv / sizeof uv[0]); i++)
        printf("[%u|%08x|%8x|%-8X|%x%08x|%o|%06o|%.0u|%.5x|%3x]\n",
               uv[i], uv[i], uv[i], uv[i], uv[i], uv[i], uv[i], uv[i], uv[i], uv[i], uv[i]);
    for (i = 0; i < (int)(sizeof lv / sizeof lv[0]); i++)
        printf("[%lld|%22lld|%-22lld|%022lld]\n", lv[i], lv[i], lv[i], lv[i]);
    for (i = 0; i < (int)(sizeof ulv / sizeof ulv[0]); i++)
        printf("[%llu|%016llx|%llX|%-20llu|]\n", ulv[i], ulv[i], ulv[i], ulv[i]);
    printf("[%*d|%-*d|%.*u]\n", 6, 42, 6, 42, 4, 9u);
    return 0;
}
