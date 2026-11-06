/* Frame remapping may move explicit call-staging slots, but the stacked
 * argument words are read implicitly by the callee and must stay in place.
 * Locals and an address-taken object also survive direct and indirect calls.
 * Expected: SSLOTCALL OK\nH
 */
#define UART (*(volatile unsigned int *)0x10000000u)
static volatile unsigned seed = 7;
static void put(const char *s) { while (*s) UART = (unsigned char)*s++; }

static unsigned __attribute__((noinline)) sum7(unsigned a, unsigned b,
    unsigned c, unsigned d, unsigned e, unsigned f, unsigned g)
{
    return a + 2*b + 3*c + 4*d + 5*e + 6*f + 7*g;
}

static unsigned (*volatile callee)(unsigned, unsigned, unsigned, unsigned,
                                    unsigned, unsigned, unsigned) = sum7;

static unsigned __attribute__((noinline)) exercise(unsigned n)
{
    unsigned x = n + 1, y = n + 2, z = n + 3;
    unsigned words[3];
    unsigned result, i;
    words[0] = n + 4; words[1] = n + 5; words[2] = n + 6;
    result = sum7(x, y, z, words[0], words[1], words[2], n);
    for (i = 0; i < 9; i++) {
        x += y; z ^= x; words[i % 3] += z;
    }
    return result + callee(x, y, z, words[0], words[1], words[2], n)
                  + x + y + z + words[0] + words[1] + words[2];
}

void test_main(void)
{
    unsigned n = seed, x = n + 1, y = n + 2, z = n + 3;
    unsigned a = n + 4, b = n + 5, c = n + 6, first, i;
    first = x + 2*y + 3*z + 4*a + 5*b + 6*c + 7*n;
    for (i = 0; i < 9; i++) {
        x += y; z ^= x;
        if (i % 3 == 0) a += z;
        else if (i % 3 == 1) b += z;
        else c += z;
    }
    first += x + 2*y + 3*z + 4*a + 5*b + 6*c + 7*n;
    first += x + y + z + a + b + c;
    put(exercise(n) == first ? "SLOTCALL OK\n" : "SLOTCALL FAIL\n");
}
