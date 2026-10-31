/*
 * Broad C subword regression suite for int8/uint8/int16/uint16 behavior.
 *
 * This is meant to catch backend bugs around:
 * - narrow local stores
 * - pointer-cast subword stores
 * - array indexing on byte/halfword elements
 * - packed struct field loads/stores
 * - signed vs unsigned subword loads
 *
 * Expected: SSSUBWORD\nT00:P\nT01:P\nT02:P\nT03:P\nT04:P\nT05:P\nT06:P\nT07:P\nT08:P\nT09:P\nT10:P\nT11:P\nT12:P\nT13:P\nT14:P\nT15:P\nT16:P\nT17:P\nT18:P\nT19:P\nT20:P\nT21:P\nH
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

typedef signed char s8;
typedef unsigned char u8;
typedef signed short s16;
typedef unsigned short u16;
typedef signed int s32;
typedef unsigned int u32;

static void uart_putc(int c)
{
    UART_DATA = (unsigned long)c;
}

static void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

static void print_result(const char *tag, int pass)
{
    uart_puts(tag);
    uart_putc(':');
    uart_putc(pass ? 'P' : 'F');
    uart_putc('\n');
}

static u32 store_lo_u16_cast(u32 word, u16 v)
{
    u32 tmp = word;
    ((u16 *)&tmp)[0] = v;
    return tmp;
}

static u32 store_hi_u16_cast(u32 word, u16 v)
{
    u32 tmp = word;
    ((u16 *)&tmp)[1] = v;
    return tmp;
}

static u32 store_lo_s16_cast(u32 word, s16 v)
{
    u32 tmp = word;
    ((s16 *)&tmp)[0] = v;
    return tmp;
}

static u32 store_b0_u8_cast(u32 word, u8 v)
{
    u32 tmp = word;
    ((u8 *)&tmp)[0] = v;
    return tmp;
}

static u32 store_b1_u8_cast(u32 word, u8 v)
{
    u32 tmp = word;
    ((u8 *)&tmp)[1] = v;
    return tmp;
}

static u32 store_b3_u8_cast(u32 word, u8 v)
{
    u32 tmp = word;
    ((u8 *)&tmp)[3] = v;
    return tmp;
}

static u32 rmw_lo_u16(u32 word, u16 v)
{
    return (word & 0xffff0000u) | ((u32)v & 0x0000ffffu);
}

static u32 rmw_hi_u16(u32 word, u16 v)
{
    return (word & 0x0000ffffu) | (((u32)v << 16) & 0xffff0000u);
}

static u32 rmw_b0_u8(u32 word, u8 v)
{
    return (word & 0xffffff00u) | (u32)v;
}

static u32 rmw_b1_u8(u32 word, u8 v)
{
    return (word & 0xffff00ffu) | (((u32)v << 8) & 0x0000ff00u);
}

static u32 rmw_b3_u8(u32 word, u8 v)
{
    return (word & 0x00ffffffu) | (((u32)v << 24) & 0xff000000u);
}

struct pair16 {
    s16 a;
    s16 b;
};

struct bytes4 {
    u8 a;
    u8 b;
    u8 c;
    u8 d;
};

struct mixed_packed {
    u8 a;
    u16 b;
    u8 c;
} __attribute__((packed));

static void set_pair(struct pair16 *p, s16 a, s16 b)
{
    p->a = a;
    p->b = b;
}

static void set_pair_second(struct pair16 *p, s16 b)
{
    p->b = b;
}

static void set_bytes(struct bytes4 *p, u8 a, u8 b, u8 c, u8 d)
{
    p->a = a;
    p->b = b;
    p->c = c;
    p->d = d;
}

static s32 widen_s8(s8 v)
{
    return v;
}

static s32 widen_s16(s16 v)
{
    return v;
}

static u32 widen_u8(u8 v)
{
    return v;
}

static u32 widen_u16(u16 v)
{
    return v;
}

void test_main(void)
{
    u32 seed = 0xfaceb00cu;
    u32 tmp;
    s8 sbuf[4];
    u8 ubuf[4];
    s16 shbuf[4];
    u16 uhbuf[4];
    struct pair16 pair;
    struct bytes4 bytes;
    struct mixed_packed mix;
    volatile u8 *vub;
    volatile s16 *vsh;

    uart_putc('S');
    uart_puts("SUBWORD\n");

    tmp = store_lo_u16_cast(seed, (u16)0x0007u);
    print_result("T00", tmp == 0xface0007u);

    tmp = rmw_lo_u16(seed, (u16)0x0007u);
    print_result("T01", tmp == 0xface0007u);

    tmp = store_hi_u16_cast(seed, (u16)0x0600u);
    print_result("T02", tmp == 0x0600b00cu);

    tmp = rmw_hi_u16(seed, (u16)0x0600u);
    print_result("T03", tmp == 0x0600b00cu);

    tmp = store_lo_s16_cast(seed, (s16)-7);
    print_result("T04", tmp == 0xfacefff9u);

    tmp = store_b0_u8_cast(seed, (u8)0x11u);
    print_result("T05", tmp == 0xfaceb011u);

    tmp = rmw_b0_u8(seed, (u8)0x11u);
    print_result("T06", tmp == 0xfaceb011u);

    tmp = store_b1_u8_cast(seed, (u8)0x22u);
    print_result("T07", tmp == 0xface220cu);

    tmp = rmw_b1_u8(seed, (u8)0x22u);
    print_result("T08", tmp == 0xface220cu);

    tmp = store_b3_u8_cast(seed, (u8)0x44u);
    print_result("T09", tmp == 0x44ceb00cu);

    tmp = rmw_b3_u8(seed, (u8)0x44u);
    print_result("T10", tmp == 0x44ceb00cu);

    sbuf[0] = (s8)-5;
    sbuf[1] = (s8)9;
    print_result("T11", widen_s8(sbuf[0]) == -5 && widen_s8(sbuf[1]) == 9);

    ubuf[0] = (u8)0xfeu;
    ubuf[1] = (u8)0x7fu;
    print_result("T12", widen_u8(ubuf[0]) == 0xfeu && widen_u8(ubuf[1]) == 0x7fu);

    shbuf[0] = (s16)-123;
    shbuf[1] = (s16)456;
    print_result("T13", widen_s16(shbuf[0]) == -123 && widen_s16(shbuf[1]) == 456);

    uhbuf[0] = (u16)0xfff0u;
    uhbuf[1] = (u16)0x1234u;
    print_result("T14", widen_u16(uhbuf[0]) == 0xfff0u && widen_u16(uhbuf[1]) == 0x1234u);

    pair.a = (s16)0x1111;
    pair.b = (s16)0x2222;
    set_pair(&pair, (s16)6, (s16)7);
    print_result("T15", pair.a == 6 && pair.b == 7);

    pair.a = (s16)0x1234;
    pair.b = (s16)0x5678;
    set_pair_second(&pair, (s16)-9);
    print_result("T16", pair.a == (s16)0x1234 && pair.b == (s16)-9);

    set_bytes(&bytes, (u8)1u, (u8)2u, (u8)3u, (u8)4u);
    print_result("T17", bytes.a == 1u && bytes.b == 2u && bytes.c == 3u && bytes.d == 4u);

    mix.a = (u8)0x12u;
    mix.b = (u16)0x3456u;
    mix.c = (u8)0x78u;
    print_result("T18", mix.a == 0x12u && mix.b == 0x3456u && mix.c == 0x78u);

    ubuf[0] = (u8)0xaau;
    ubuf[1] = (u8)0xbbu;
    ubuf[2] = (u8)0xccu;
    ubuf[3] = (u8)0xddu;
    vub = &ubuf[0];
    vub[1] = (u8)0x33u;
    vub[3] = (u8)0x55u;
    print_result("T19", ubuf[0] == 0xaau && ubuf[1] == 0x33u &&
                        ubuf[2] == 0xccu && ubuf[3] == 0x55u);

    shbuf[0] = (s16)0x1111;
    shbuf[1] = (s16)0x2222;
    shbuf[2] = (s16)0x3333;
    vsh = &shbuf[0];
    vsh[1] = (s16)-7;
    vsh[2] = (s16)0x4444;
    print_result("T20", shbuf[0] == (s16)0x1111 &&
                        shbuf[1] == (s16)-7 &&
                        shbuf[2] == (s16)0x4444);

    tmp = 0x11223344u;
    ((u8 *)&tmp)[0] = 0xa5u;
    ((u8 *)&tmp)[2] = 0x5au;
    print_result("T21", tmp == 0x115a33a5u);
}
