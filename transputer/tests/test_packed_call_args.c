/* Expected: SPACKEDCALL_OK\nH */
typedef unsigned int u32;
struct __attribute__((packed)) Packed {
    char pad;
    u32 length;
    const char *source;
    char *destination;
};

static char source[8] = { 11, 22, 33, 44, 55, 66, 77, 88 };
static char destination[8];
static volatile u32 input = 5;
static struct Packed global;

__attribute__((noinline)) void *memcpy(void *dst, const void *src, u32 length)
{
    char *d = dst;
    const char *s = src;
    u32 i;
    for (i = 0; i < length; i++) d[i] = s[i];
    return dst;
}

__attribute__((noinline)) static u32 combine(u32 a, u32 b, u32 c)
{
    return a + 10 * b + 100 * c;
}
static u32 (*volatile indirect)(u32, u32, u32) = combine;
__attribute__((noinline)) static u32 combine5(u32 a, u32 b, u32 c, u32 d, u32 e)
{
    return a + 10 * b + 100 * c + 1000 * d + 10000 * e;
}
static u32 (*volatile indirect5)(u32, u32, u32, u32, u32) = combine5;

static int check(void)
{
    struct Packed local;
    volatile struct Packed *vp = &global;
    int i;

    local.pad = 99;
    local.length = input;
    local.source = source;
    local.destination = destination;
    for (i = 0; i < 8; i++) destination[i] = 0;
    if (memcpy(destination, source, local.length) != destination) return 0;
    for (i = 0; i < 8; i++)
        if (destination[i] != (i < 5 ? source[i] : 0)) return 0;

    global.pad = 77;
    global.length = input;
    global.source = source;
    global.destination = destination;
    for (i = 0; i < 8; i++) destination[i] = 0;
    if (memcpy(vp->destination, vp->source, vp->length) != destination) return 0;
    for (i = 0; i < 8; i++)
        if (destination[i] != (i < 5 ? source[i] : 0)) return 0;

    if (combine(local.length, 2, 3) != 325) return 0;
    if (combine(1, local.length, 3) != 351) return 0;
    if (combine(1, 2, local.length) != 521) return 0;
    if (indirect(1, 2, vp->length) != 521) return 0;
    if (combine5(1, 2, 3, 4, vp->length) != 54321) return 0;
    if (combine5(1, 2, 3, vp->length, 5) != 55321) return 0;
    if (indirect5(1, 2, 3, 4, local.length) != 54321) return 0;
    return 1;
}

#ifdef HOST_TEST
int main(void) { return !check(); }
#else
void test_main(void)
{
    const char *message = check() ? "PACKEDCALL_OK\n" : "PACKEDCALL_BAD\n";
    while (*message)
        *(volatile u32 *)0x10000000u = (unsigned char)*message++;
}
#endif
