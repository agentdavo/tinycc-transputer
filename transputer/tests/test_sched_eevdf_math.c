/* Regression coverage for Linux EEVDF weighted-vruntime arithmetic.
 * Expected: SEEVDFMATH\nT01P\nT02P\nT03P\nT04P\nT05P\nT06P\nT07P\nT08P\nT09P\nEEVDFMATHDONE\nH
 */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

typedef long long s64;
typedef unsigned long long u64;

static void uart_putc(int c) { UART_DATA = (unsigned long)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void uart_hex(unsigned long v)
{
	int shift;
	for (shift = 28; shift >= 0; shift -= 4)
		uart_putc("0123456789abcdef"[(v >> shift) & 15]);
}

static void check(int n, u64 got, unsigned long hi, unsigned long lo)
{
	unsigned long got_hi = (unsigned long)(got >> 32);
	unsigned long got_lo = (unsigned long)got;

	uart_putc('T');
	uart_putc('0' + n / 10);
	uart_putc('0' + n % 10);
	if (got_hi == hi && got_lo == lo) {
		uart_putc('P');
	} else {
		uart_puts("F got=");
		uart_hex(got_hi);
		uart_putc(':');
		uart_hex(got_lo);
	}
	uart_putc('\n');
}

static s64 __attribute__((noinline)) weighted_key(s64 key,
						   unsigned long weight)
{
	return key * weight;
}

static u64 __attribute__((noinline)) update_sum(u64 sum, u64 weight,
						 s64 delta)
{
	return sum - weight * delta;
}

static u64 __attribute__((noinline)) mixed_product(u64 weight, s64 delta)
{
	return weight * delta;
}

struct accumulator {
	s64 sum;
	u64 weight;
	u64 zero;
};

static void __attribute__((noinline)) update_accumulator(struct accumulator *a,
							  s64 delta)
{
	a->sum -= a->weight * delta;
	a->zero += delta;
}

void test_main(void)
{
	volatile s64 key;
	volatile unsigned long weight;
	s64 sum;
	struct accumulator a;

	uart_puts("EEVDFMATH\n");
	key = -0x595036LL;
	weight = 0x400;
	sum = weighted_key(key, weight);
	check(1, (u64)sum, 0xfffffffeUL, 0x9abf2800UL);

	key = 0x12345678LL;
	weight = 0x800;
	sum = weighted_key(key, weight);
	check(2, (u64)sum, 0x00000091UL, 0xa2b3c000UL);

	key = -0x12345678LL;
	sum += weighted_key(key, weight);
	check(3, (u64)sum, 0x00000000UL, 0x00000000UL);
	check(4, (u64)((u64)0x800 * (s64)-0x17eac),
	      0xffffffffUL, 0xf40aa000UL);
	check(5, update_sum(0x6627e000ULL, 0x800ULL, -0x17eacLL),
	      0x00000000UL, 0x721d4000UL);
	check(6, mixed_product(0x800ULL, -0x1daa55LL),
	      0xffffffffUL, 0x12ad5800UL);
	check(7, update_sum(0, 0x800ULL, -0x1daa55LL),
	      0x00000000UL, 0xed52a800UL);
	a.sum = 0;
	a.weight = 0x800;
	a.zero = 0x2000000;
	update_accumulator(&a, -0xd801);
	check(8, (u64)a.sum, 0x00000000UL, 0x06c00800UL);
	check(9, a.zero, 0x00000000UL, 0x01ff27ffUL);
	uart_puts("EEVDFMATHDONE\n");
}
