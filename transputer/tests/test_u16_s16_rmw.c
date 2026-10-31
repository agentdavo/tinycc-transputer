/*
 * Focused uint16/int16 regression suite for subword read-modify-write paths.
 *
 * These cases are meant to stress the kind of mask/shift/or sequences seen
 * in the live set_slob() trace, not just plain short assignment.
 *
 * Expected: SSU16RMW\nW0:0600B00C\nW1:FACE0007\nW2:0600B00C\nW3:FACE0007\nW4:FACEFFF9\nW5:FACEFFF9\nT0:P\nT1:P\nT2:P\nT3:P\nT4:P\nT5:P\nT6:P\nT7:P\nT8:P\nH
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

typedef unsigned short u16;
typedef signed short s16;
typedef unsigned int u32;

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

static void print_result(const char *tag, int pass)
{
    uart_puts(tag);
    uart_putc(':');
    uart_putc(pass ? 'P' : 'F');
    uart_putc('\n');
}

static void uart_hex32(u32 v)
{
    int i;
    for (i = 28; i >= 0; i -= 4)
        uart_putc("0123456789ABCDEF"[(v >> i) & 0xf]);
}

static void print_word(const char *tag, u32 v)
{
    uart_puts(tag);
    uart_putc(':');
    uart_hex32(v);
    uart_putc('\n');
}

static u32 pack_halfwords(u16 lo, u16 hi)
{
    return (u32)lo | ((u32)hi << 16);
}

static u32 store_hi_via_cast(u32 word, u16 hi)
{
    u32 tmp = word;
    ((u16 *)&tmp)[1] = hi;
    return tmp;
}

static u32 store_lo_via_cast(u32 word, u16 lo)
{
    u32 tmp = word;
    ((u16 *)&tmp)[0] = lo;
    return tmp;
}

static u32 store_hi_manual_rmw(u32 word, u16 hi)
{
    u32 masked = word & 0x0000ffffu;
    u32 shifted = ((u32)hi << 16) & 0xffff0000u;
    return masked | shifted;
}

static u32 store_lo_manual_rmw(u32 word, u16 lo)
{
    u32 masked = word & 0xffff0000u;
    u32 shifted = (u32)lo & 0x0000ffffu;
    return masked | shifted;
}

static u32 store_lo_s16_via_cast(u32 word, s16 lo)
{
    u32 tmp = word;
    ((s16 *)&tmp)[0] = lo;
    return tmp;
}

static u32 store_lo_s16_manual_rmw(u32 word, s16 lo)
{
    u32 masked = word & 0xffff0000u;
    u32 shifted = (u32)((u16)lo);
    return masked | shifted;
}

struct pair16 {
    s16 a;
    s16 b;
};

static void set_pair_fields(struct pair16 *p, s16 a, s16 b)
{
    p->a = a;
    p->b = b;
}

static void set_second_field_only(struct pair16 *p, s16 b)
{
    p->b = b;
}

static void set_first_field_only(struct pair16 *p, s16 a)
{
    p->a = a;
}

void test_main(void)
{
    u32 seed = 0xfaceb00cu;
    u32 w_hi_cast;
    u32 w_lo_cast;
    u32 w_hi_rmw;
    u32 w_lo_rmw;
    u32 w_lo_s16_cast;
    u32 w_lo_s16_rmw;
    struct pair16 p;
    volatile s16 *vp;

    uart_putc('S');
    uart_puts("U16RMW\n");

    w_hi_cast = store_hi_via_cast(seed, (u16)0x0600u);
    w_lo_cast = store_lo_via_cast(seed, (u16)0x0007u);
    w_hi_rmw = store_hi_manual_rmw(seed, (u16)0x0600u);
    w_lo_rmw = store_lo_manual_rmw(seed, (u16)0x0007u);
    w_lo_s16_cast = store_lo_s16_via_cast(seed, (s16)-7);
    w_lo_s16_rmw = store_lo_s16_manual_rmw(seed, (s16)-7);

    print_word("W0", w_hi_cast);
    print_word("W1", w_lo_cast);
    print_word("W2", w_hi_rmw);
    print_word("W3", w_lo_rmw);
    print_word("W4", w_lo_s16_cast);
    print_word("W5", w_lo_s16_rmw);

    print_result("T0", w_hi_cast == 0x0600b00cu);
    print_result("T1", w_lo_cast == 0xface0007u);
    print_result("T2", w_hi_rmw == 0x0600b00cu);
    print_result("T3", w_lo_rmw == 0xface0007u);

    p.a = (s16)0x55aa;
    p.b = (s16)0x33cc;
    set_pair_fields(&p, (s16)6, (s16)7);
    print_result("T4", p.a == 6 && p.b == 7);

    p.a = (s16)0x1234;
    p.b = (s16)0x5678;
    set_second_field_only(&p, (s16)7);
    print_result("T5", p.a == (s16)0x1234 && p.b == 7);

    p.a = (s16)0x1234;
    p.b = (s16)0x5678;
    vp = &p.a;
    vp[1] = (s16)7;
    set_first_field_only(&p, (s16)6);
    print_result("T6", p.a == 6 && p.b == 7);
    print_result("T7", w_lo_s16_cast == 0xfacefff9u);
    print_result("T8", w_lo_s16_rmw == 0xfacefff9u);
}
