/* POV-Ray reference compatibility only.  Recovered from the supplied ICC
 * image and checked against 55 state words and 128 returns from srand(0).
 * This object deliberately replaces rand/srand only in this fixture; it
 * does not change the installed libc or compiler runtime. */
typedef unsigned int pov_u32;
typedef char pov_require_32_bit_unsigned[(sizeof(pov_u32) == 4) ? 1 : -1];

static pov_u32 state[55];
static unsigned first, second;
static int initialized;

void srand(unsigned seed)
{
    pov_u32 value = seed;
    unsigned k;
    for (k = 0; k < 55; ++k) {
        state[k] = value + (value >> 16);
        value = value * 69069u + 0x66d619e1u;
    }
    first = 23;
    second = 54;
    initialized = 1;
}

int rand(void)
{
    pov_u32 value;
    /* The captured POV path explicitly seeds with zero.  Use C's specified
     * default seed for callers that have not seeded, without reading unset
     * indices.  This default is not part of the recovered image oracle. */
    if (!initialized)
        srand(1);
    value = state[second] + state[first];
    state[second] = value;
    /* Preserve the observed zero test and conditional order. */
    if (--first == 0) {
        first = 54;
        --second;
    } else if (--second == 0) {
        second = 54;
    }
    return (int)(value & 0x7fffffffu);
}
