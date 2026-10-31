/* test_bytefield.c - isolate u8 field stores/loads through pointers */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

typedef unsigned char u8;
typedef signed short s16;

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void uart_hex(unsigned long v) {
    int i;
    for (i = 28; i >= 0; i -= 4)
        uart_putc("0123456789abcdef"[(v >> i) & 0xf]);
}
static void halt(void) {
    *(volatile unsigned long *)0x80000020 = 0;
    while (1) {}
}

struct small_spec {
    u8 type;
    u8 flags;
    u8 base;
    u8 qualifier;
    s16 field_width;
    s16 precision;
};

static void set_type_only(struct small_spec *spec, u8 v)
{
    spec->type = v;
}

static void set_two_fields(struct small_spec *spec)
{
    spec->type = 4;
    spec->base = 10;
}

static void set_all_small(struct small_spec *spec)
{
    spec->type = 4;
    spec->flags = 0x15;
    spec->base = 10;
    spec->qualifier = 0xff;
}

static u8 get_type(struct small_spec *spec)
{
    return spec->type;
}

static u8 get_base(struct small_spec *spec)
{
    return spec->base;
}

static void print_field_result(const char *tag, unsigned long calc, unsigned long expect)
{
    uart_puts(tag);
    uart_puts(" calc=");
    uart_hex(calc);
    uart_puts(" exp=");
    uart_hex(expect);
    uart_putc(calc == expect ? 'P' : 'F');
    uart_putc('\n');
}

void test_main(void)
{
    struct small_spec spec;

    uart_puts("BYTEFIELD\n");

    spec.type = 0;
    spec.flags = 0;
    spec.base = 0;
    spec.qualifier = 0;
    spec.field_width = 0;
    spec.precision = 0;

    set_type_only(&spec, 4);
    print_field_result("T1", (unsigned long)spec.type, 4);
    print_field_result("T1b", (unsigned long)get_type(&spec), 4);

    spec.type = 0;
    spec.base = 0;
    set_two_fields(&spec);
    print_field_result("T2", (unsigned long)spec.type, 4);
    print_field_result("T2b", (unsigned long)spec.base, 10);
    print_field_result("T2c", (unsigned long)get_base(&spec), 10);

    spec.type = 0;
    spec.flags = 0;
    spec.base = 0;
    spec.qualifier = 0;
    set_all_small(&spec);
    print_field_result("T3", (unsigned long)spec.type, 4);
    print_field_result("T3b", (unsigned long)spec.flags, 0x15);
    print_field_result("T3c", (unsigned long)spec.base, 10);
    print_field_result("T3d", (unsigned long)spec.qualifier, 0xff);

    uart_puts("DONE\n");
    halt();
}
