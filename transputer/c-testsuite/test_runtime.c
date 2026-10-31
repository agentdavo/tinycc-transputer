/*
 * Minimal freestanding C runtime for transputer c-testsuite
 * All output via UART MMIO at 0x10000000
 */

typedef unsigned long size_t;
typedef __builtin_va_list va_list;
#define va_start __builtin_va_start
#define va_end __builtin_va_end
#define va_arg __builtin_va_arg

#define UART_ADDR 0x10000000u
#define NULL ((void *)0)

/* ---- Entry point (called from boot stub) ---- */

extern int main(void);

void _start_c(void)
{
	int ret = main();
	if (ret != 0) {
		/* Non-zero return: signal failure via UART */
		*(volatile unsigned int *)UART_ADDR = 'F';
		*(volatile unsigned int *)UART_ADDR = 'A';
		*(volatile unsigned int *)UART_ADDR = 'I';
		*(volatile unsigned int *)UART_ADDR = 'L';
	}
	/* stopp: halt the transputer */
	__asm__(".byte 0x21, 0xf5");
	while (1);
}

/* ---- UART output ---- */

static void uart_putc(int c)
{
	*(volatile unsigned int *)UART_ADDR = (unsigned int)c;
}

int putchar(int c)
{
	uart_putc(c);
	return c;
}

int puts(const char *s)
{
	while (*s)
		uart_putc(*s++);
	uart_putc('\n');
	return 0;
}

int fputs(const char *s, void *stream)
{
	while (*s)
		uart_putc(*s++);
	return 0;
}

int fputc(int c, void *stream)
{
	uart_putc(c);
	return c;
}

/* ---- String functions ---- */

size_t strlen(const char *s)
{
	const char *p = s;
	while (*p)
		p++;
	return p - s;
}

char *strcpy(char *dest, const char *src)
{
	char *d = dest;
	while ((*d++ = *src++))
		;
	return dest;
}

char *strncpy(char *dest, const char *src, size_t n)
{
	size_t i;
	for (i = 0; i < n && src[i]; i++)
		dest[i] = src[i];
	for (; i < n; i++)
		dest[i] = '\0';
	return dest;
}

char *strcat(char *dest, const char *src)
{
	char *d = dest;
	while (*d)
		d++;
	while ((*d++ = *src++))
		;
	return dest;
}

char *strncat(char *dest, const char *src, size_t n)
{
	char *d = dest;
	size_t i;
	while (*d)
		d++;
	for (i = 0; i < n && src[i]; i++)
		d[i] = src[i];
	d[i] = '\0';
	return dest;
}

int strcmp(const char *s1, const char *s2)
{
	while (*s1 && *s1 == *s2) {
		s1++;
		s2++;
	}
	return (unsigned char)*s1 - (unsigned char)*s2;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++) {
		if (s1[i] != s2[i])
			return (unsigned char)s1[i] - (unsigned char)s2[i];
		if (s1[i] == '\0')
			return 0;
	}
	return 0;
}

char *strchr(const char *s, int c)
{
	while (*s) {
		if (*s == (char)c)
			return (char *)s;
		s++;
	}
	return (c == '\0') ? (char *)s : NULL;
}

char *strrchr(const char *s, int c)
{
	const char *last = NULL;
	while (*s) {
		if (*s == (char)c)
			last = s;
		s++;
	}
	if (c == '\0')
		return (char *)s;
	return (char *)last;
}

char *strstr(const char *haystack, const char *needle)
{
	size_t nlen;
	if (!*needle)
		return (char *)haystack;
	nlen = strlen(needle);
	while (*haystack) {
		if (strncmp(haystack, needle, nlen) == 0)
			return (char *)haystack;
		haystack++;
	}
	return NULL;
}

/* ---- Memory functions ---- */

void *memset(void *s, int c, size_t n)
{
	unsigned char *p = (unsigned char *)s;
	while (n--)
		*p++ = (unsigned char)c;
	return s;
}

void *memcpy(void *dest, const void *src, size_t n)
{
	unsigned char *d = (unsigned char *)dest;
	const unsigned char *s = (const unsigned char *)src;
	while (n--)
		*d++ = *s++;
	return dest;
}

void *memmove(void *dest, const void *src, size_t n)
{
	unsigned char *d = (unsigned char *)dest;
	const unsigned char *s = (const unsigned char *)src;
	if (d < s) {
		while (n--)
			*d++ = *s++;
	} else {
		d += n;
		s += n;
		while (n--)
			*--d = *--s;
	}
	return dest;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
	const unsigned char *a = (const unsigned char *)s1;
	const unsigned char *b = (const unsigned char *)s2;
	while (n--) {
		if (*a != *b)
			return *a - *b;
		a++;
		b++;
	}
	return 0;
}

/* ---- Bump allocator ---- */

static char heap[1024 * 64];  /* 64 KiB heap */
static size_t heap_pos = 0;

void *malloc(size_t size)
{
	void *p;
	/* Align to 4 bytes */
	size = (size + 3) & ~3;
	if (heap_pos + size > sizeof(heap))
		return NULL;
	p = &heap[heap_pos];
	heap_pos += size;
	return p;
}

void *calloc(size_t nmemb, size_t size)
{
	size_t total = nmemb * size;
	void *p = malloc(total);
	if (p)
		memset(p, 0, total);
	return p;
}

void *realloc(void *ptr, size_t size)
{
	/* Simple: always allocate new, can't free */
	void *p = malloc(size);
	if (p && ptr)
		memcpy(p, ptr, size);  /* may overread, but tests are small */
	return p;
}

void free(void *ptr)
{
	/* Bump allocator: no-op */
	(void)ptr;
}

/* ---- Math ---- */

int abs(int j)
{
	return j < 0 ? -j : j;
}

long labs(long j)
{
	return j < 0 ? -j : j;
}

int atoi(const char *nptr)
{
	int result = 0;
	int sign = 1;
	while (*nptr == ' ' || *nptr == '\t')
		nptr++;
	if (*nptr == '-') {
		sign = -1;
		nptr++;
	} else if (*nptr == '+') {
		nptr++;
	}
	while (*nptr >= '0' && *nptr <= '9') {
		result = result * 10 + (*nptr - '0');
		nptr++;
	}
	return sign * result;
}

long atol(const char *nptr)
{
	return (long)atoi(nptr);
}

/* ---- Simple qsort (insertion sort) ---- */

void qsort(void *base, size_t nmemb, size_t size,
            int (*compar)(const void *, const void *))
{
	size_t i, j;
	char *arr = (char *)base;
	char tmp[256];  /* max element size */
	if (size > sizeof(tmp))
		return;
	for (i = 1; i < nmemb; i++) {
		memcpy(tmp, arr + i * size, size);
		j = i;
		while (j > 0 && compar(arr + (j - 1) * size, tmp) > 0) {
			memcpy(arr + j * size, arr + (j - 1) * size, size);
			j--;
		}
		memcpy(arr + j * size, tmp, size);
	}
}

/* ---- Random ---- */

static unsigned int rand_state = 1;

int rand(void)
{
	rand_state = rand_state * 1103515245 + 12345;
	return (int)((rand_state >> 16) & 0x7FFF);
}

void srand(unsigned int seed)
{
	rand_state = seed;
}

/* ---- errno ---- */

int errno;

/* ---- printf implementation ---- */

static void print_char(char c, char **buf, size_t *pos, size_t limit)
{
	if (buf) {
		if (*pos < limit - 1)
			(*buf)[*pos] = c;
	} else {
		uart_putc(c);
	}
	(*pos)++;
}

static void print_string(const char *s, char **buf, size_t *pos, size_t limit)
{
	if (!s)
		s = "(null)";
	while (*s)
		print_char(*s++, buf, pos, limit);
}

/* An integer conversion with C99 padding: `precision` (-1 = none) is the
 * minimum digit count, the 0 flag pads with zeros after the sign when there
 * is no precision, otherwise the width pads with spaces - on the right for
 * the - flag.  `sign` is '-', '+', ' ' or 0.  Values that fit 32 bits use
 * 32-bit arithmetic, so only real 64-bit values need __udivdi3. */
static void print_num(unsigned long long mag, int sign, int base, int upper,
		      int width, int precision, int pad_zero, int left_align,
		      char **buf, size_t *pos, size_t limit)
{
	const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	char tmp[24];
	int nd = 0, zeros, pad;

	if (mag >> 32) {
		do { tmp[nd++] = digits[(int)(mag % base)]; mag /= base; } while (mag);
	} else if (mag || precision != 0) {
		unsigned long m = (unsigned long)mag;
		do { tmp[nd++] = digits[(int)(m % base)]; m /= base; } while (m);
	}
	zeros = precision > nd ? precision - nd : 0;
	pad = width - ((sign != 0) + zeros + nd);
	if (pad_zero && !left_align && precision < 0 && pad > 0) {
		zeros += pad;
		pad = 0;
	}
	if (!left_align)
		while (pad-- > 0) print_char(' ', buf, pos, limit);
	if (sign)
		print_char((char)sign, buf, pos, limit);
	while (zeros-- > 0)
		print_char('0', buf, pos, limit);
	while (nd > 0)
		print_char(tmp[--nd], buf, pos, limit);
	if (left_align)
		while (pad-- > 0) print_char(' ', buf, pos, limit);
}

static int do_printf(char **buf, size_t limit, const char *fmt, va_list ap)
{
	size_t pos = 0;
	int is_long;
	int width;
	int precision;
	int have_precision;
	int pad_zero;
	int left_align;
	int plus_sign, space_sign;

	while (*fmt) {
		if (*fmt != '%') {
			print_char(*fmt++, buf, &pos, limit);
			continue;
		}
		fmt++;  /* skip % */

		/* flags */
		left_align = 0;
		pad_zero = 0;
		plus_sign = space_sign = 0;
		while (*fmt == '-' || *fmt == '0' || *fmt == '+' || *fmt == ' ' || *fmt == '#') {
			if (*fmt == '-') left_align = 1;
			if (*fmt == '0') pad_zero = 1;
			if (*fmt == '+') plus_sign = 1;
			if (*fmt == ' ') space_sign = 1;
			fmt++;
		}

		/* width */
		width = 0;
		if (*fmt == '*') {
			width = va_arg(ap, int);
			fmt++;
		} else {
			while (*fmt >= '0' && *fmt <= '9') {
				width = width * 10 + (*fmt - '0');
				fmt++;
			}
		}

		/* precision */
		precision = -1;
		have_precision = 0;
		if (*fmt == '.') {
			fmt++;
			have_precision = 1;
			precision = 0;
			if (*fmt == '*') {
				precision = va_arg(ap, int);
				fmt++;
			} else {
				while (*fmt >= '0' && *fmt <= '9') {
					precision = precision * 10 + (*fmt - '0');
					fmt++;
				}
			}
		}

		/* length */
		is_long = 0;
		if (*fmt == 'l') {
			is_long = 1;
			fmt++;
			if (*fmt == 'l') {
				is_long = 2;  /* long long — 64-bit */
				fmt++;
			}
		} else if (*fmt == 'L') {
			is_long = 2; /* long double */
			fmt++;
		} else if (*fmt == 'h') {
			fmt++;
			if (*fmt == 'h')
				fmt++;
		} else if (*fmt == 'z') {
			is_long = 1;
			fmt++;
		}

		/* conversion */
		switch (*fmt) {
		case 'd':
		case 'i': {
			long long val = is_long == 2 ? va_arg(ap, long long)
				      : is_long ? (long long)va_arg(ap, long)
				      : (long long)va_arg(ap, int);
			unsigned long long mag = val < 0 ? (unsigned long long)(-(val + 1)) + 1
						       : (unsigned long long)val;
			int sign = val < 0 ? '-' : plus_sign ? '+' : space_sign ? ' ' : 0;
			print_num(mag, sign, 10, 0, width, have_precision ? precision : -1,
				  pad_zero, left_align, buf, &pos, limit);
			break;
		}
		case 'u':
		case 'x':
		case 'X':
		case 'o': {
			unsigned long long val = is_long == 2 ? va_arg(ap, unsigned long long)
					       : is_long ? (unsigned long long)va_arg(ap, unsigned long)
					       : (unsigned long long)va_arg(ap, unsigned int);
			print_num(val, 0, *fmt == 'u' ? 10 : *fmt == 'o' ? 8 : 16, *fmt == 'X',
				  width, have_precision ? precision : -1, pad_zero, left_align,
				  buf, &pos, limit);
			break;
		}
		case 'p': {
			unsigned long val = (unsigned long)va_arg(ap, void *);
			print_string("0x", buf, &pos, limit);
			print_num(val, 0, 16, 0, 0, -1, 0, 0, buf, &pos, limit);
			break;
		}
		case 's': {
			const char *s = va_arg(ap, const char *);
			int slen;
			if (!s) s = "(null)";
			slen = (int)strlen(s);
			if (have_precision && precision >= 0 && precision < slen)
				slen = precision;
			if (width > 0 && !left_align) {
				int pad = width - slen;
				while (pad-- > 0) print_char(' ', buf, &pos, limit);
			}
			{ int k; for (k = 0; k < slen; k++) print_char(s[k], buf, &pos, limit); }
			if (width > 0 && left_align) {
				int pad = width - slen;
				while (pad-- > 0) print_char(' ', buf, &pos, limit);
			}
			break;
		}
		case 'c': {
			int c = va_arg(ap, int);
			print_char((char)c, buf, &pos, limit);
			break;
		}
		case '%':
			print_char('%', buf, &pos, limit);
			break;
		case 'n': {
			int *n = va_arg(ap, int *);
			if (n) *n = (int)pos;
			break;
		}
		case 'f':
		case 'F': {
			double val = va_arg(ap, double);
			int prec = have_precision ? precision : 6;
			int neg = 0;
			unsigned long ipart;
			double frac;
			double round = 0.5;
			int i;
			char numbuf[40];
			int npos = 0;

			if (val < 0) { neg = 1; val = -val; }
			for (i = 0; i < prec; i++)
				round /= 10.0;
			val += round;
			ipart = (unsigned long)val;
			frac = val - (double)ipart;

			/* integer part */
			if (neg) numbuf[npos++] = '-';
			{
				char digs[20];
				int nd = 0;
				unsigned long v = ipart;
				do { digs[nd++] = '0' + (int)(v % 10); v /= 10; } while (v);
				while (nd > 0) numbuf[npos++] = digs[--nd];
			}
			if (prec > 0) {
				numbuf[npos++] = '.';
				for (i = 0; i < prec && npos < (int)sizeof(numbuf) - 1; i++) {
					frac *= 10.0;
					int d = (int)frac;
					numbuf[npos++] = '0' + d;
					frac -= (double)d;
				}
			}
			numbuf[npos] = '\0';
			/* width padding */
			if (width > 0 && !left_align) {
				int pad = width - npos;
				char padch = pad_zero ? '0' : ' ';
				while (pad-- > 0) print_char(padch, buf, &pos, limit);
			}
			{ int k; for (k = 0; k < npos; k++) print_char(numbuf[k], buf, &pos, limit); }
			if (width > 0 && left_align) {
				int pad = width - npos;
				while (pad-- > 0) print_char(' ', buf, &pos, limit);
			}
			break;
		}
		case 'e':
		case 'E':
		case 'g':
		case 'G': {
			/* Consume the argument even if we can't format it properly */
			(void)va_arg(ap, double);
			print_string("<?float?>", buf, &pos, limit);
			break;
		}
		case '\0':
			goto done;
		default:
			print_char('%', buf, &pos, limit);
			print_char(*fmt, buf, &pos, limit);
			break;
		}
		fmt++;
	}
done:
	if (buf && pos < limit)
		(*buf)[pos] = '\0';
	return (int)pos;
}

int printf(const char *fmt, ...)
{
	va_list ap;
	int ret;
	va_start(ap, fmt);
	ret = do_printf(NULL, 0, fmt, ap);
	va_end(ap);
	return ret;
}

int vprintf(const char *fmt, va_list ap)
{
	return do_printf(NULL, 0, fmt, ap);
}

int sprintf(char *str, const char *fmt, ...)
{
	va_list ap;
	int ret;
	va_start(ap, fmt);
	ret = do_printf(&str, (size_t)-1, fmt, ap);
	va_end(ap);
	return ret;
}

int snprintf(char *str, size_t size, const char *fmt, ...)
{
	va_list ap;
	int ret;
	va_start(ap, fmt);
	ret = do_printf(&str, size, fmt, ap);
	va_end(ap);
	return ret;
}

int vsprintf(char *str, const char *fmt, va_list ap)
{
	return do_printf(&str, (size_t)-1, fmt, ap);
}

int vsnprintf(char *str, size_t size, const char *fmt, va_list ap)
{
	return do_printf(&str, size, fmt, ap);
}

int fprintf(void *stream, const char *fmt, ...)
{
	va_list ap;
	int ret;
	va_start(ap, fmt);
	ret = do_printf(NULL, 0, fmt, ap);
	va_end(ap);
	return ret;
}

int sscanf(const char *str, const char *fmt, ...)
{
	/* Minimal stub — just enough for basic %d */
	va_list ap;
	int count = 0;
	va_start(ap, fmt);
	while (*fmt && *str) {
		if (*fmt == '%') {
			fmt++;
			if (*fmt == 'd') {
				int *p = va_arg(ap, int *);
				int val = 0;
				int neg = 0;
				while (*str == ' ') str++;
				if (*str == '-') { neg = 1; str++; }
				while (*str >= '0' && *str <= '9') {
					val = val * 10 + (*str - '0');
					str++;
				}
				*p = neg ? -val : val;
				count++;
				fmt++;
			} else {
				break;
			}
		} else if (*fmt == *str) {
			fmt++;
			str++;
		} else {
			break;
		}
	}
	va_end(ap);
	return count;
}

/* ---- Process control ---- */

void exit(int status)
{
	if (status != 0) {
		uart_putc('F');
		uart_putc('A');
		uart_putc('I');
		uart_putc('L');
	}
	/* stopp: halt the transputer */
	__asm__(".byte 0x21, 0xf5");
	while (1);
}

void abort(void)
{
	uart_putc('A');
	uart_putc('B');
	uart_putc('O');
	uart_putc('R');
	uart_putc('T');
	__asm__(".byte 0x21, 0xf5");
	while (1);
}

/* ---- FILE stubs ---- */

void *stdin = (void *)0;
void *stdout = (void *)1;
void *stderr = (void *)2;
