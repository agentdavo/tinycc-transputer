/* Constant-address lvalues must be loaded, never folded as address literals.
 * The volatile form models an MMIO register; ordinary fixed-address RAM must
 * obey the same value semantics. Run with test_boot.S and at least 2 MiB RAM.
 */
#define REG (*(volatile unsigned *)0x801f0000u)
#define RAM (*(unsigned *)0x801f0000u)
#define UART (*(volatile unsigned *)0x10000000u)
struct value { unsigned word; };
#define NOINLINE __attribute__((noinline))
#define OPS(tag, mem) \
NOINLINE int tag##_eq_r(struct value *p) { return p->word == (mem); } \
NOINLINE int tag##_eq_l(struct value *p) { return (mem) == p->word; } \
NOINLINE int tag##_ne_r(struct value *p) { return p->word != (mem); } \
NOINLINE int tag##_ne_l(struct value *p) { return (mem) != p->word; } \
NOINLINE unsigned tag##_add(unsigned x) { return x + (mem); } \
NOINLINE unsigned tag##_sub(unsigned x) { return x - (mem); } \
NOINLINE unsigned tag##_mul(unsigned x) { return x * (mem); } \
NOINLINE unsigned tag##_shl(unsigned x) { return x << (mem); }
OPS(reg, REG)
OPS(ram, RAM)

static unsigned failures;
static void check(int ok) { if (!ok) failures++; }
static void puts_uart(const char *s) { while (*s) UART = *s++; }

#define CHECKS(tag) \
    check(tag##_eq_r(&v)); check(tag##_eq_l(&v)); \
    check(!tag##_ne_r(&v)); check(!tag##_ne_l(&v)); \
    v.word = 9; \
    check(!tag##_eq_r(&v)); check(!tag##_eq_l(&v)); \
    check(tag##_ne_r(&v)); check(tag##_ne_l(&v)); \
    check(tag##_add(7) == 10); check(tag##_sub(7) == 4); \
    check(tag##_mul(7) == 21); check(tag##_shl(7) == 56)

void test_main(void)
{
    struct value v;
    REG = 3;
    v.word = 3;
    CHECKS(reg);
    v.word = 3;
    CHECKS(ram);
    puts_uart(failures ? "CONST-LVALUE-FAIL\n" : "CONST-LVALUE-PASS\n");
}
