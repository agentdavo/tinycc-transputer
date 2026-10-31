/* Activations and decode hash shared by qwen38_ref.c (host) and the
 * on-target tests that check against its output.  Everything here is exact
 * in binary32, so host and target agree bit for bit.
 *
 * Pattern 0: small dyadic steps, (i % 31 - 15) / 128, as qall_ref uses.
 * Pattern 1: full 24-bit mantissas in [-2, 2) from an LCG, so every product
 *            and sum rounds. */
#define QW_HASH_INIT 2166136261u
static unsigned qw_hash_step(unsigned h, unsigned w)
{
    return (h ^ w) * 16777619u;            /* FNV-1a over 32-bit words */
}
static void qw_act(int pattern, float *a, int n)
{
    unsigned x = 0x2545F491u;
    int i;
    for (i = 0; i < n; i++) {
        if (pattern == 0) {
            a[i] = (float)((i % 31) - 15) * 0.0078125f;
        } else {
            x = x * 1664525u + 1013904223u;
            a[i] = (float)((int)(x >> 8) - 8388608) * (1.0f / 4194304.0f);
        }
    }
}
