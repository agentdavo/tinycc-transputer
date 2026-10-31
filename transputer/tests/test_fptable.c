/*
 * test_fptable.c - Test function pointer table dispatch
 *
 * Reproduces the exact BusyBox applet_main[] pattern:
 * a const array of function pointers, indexed by integer,
 * called via table[i](argc, argv).
 *
 * This tests whether TCC generates correct R_TRANSPUTER_LDC_ABS
 * relocations for function pointers in static array initializers.
 *
 * Expected UART: SF0:cat
 * F1:echo
 * F2:false
 * DISPATCH:echo
 * OK
 * H
 */

#define UART_DATA (*(volatile unsigned char *)0x10000000)
static void uc(int c) { UART_DATA = (unsigned char)c; }
static void us(const char *s) { while (*s) uc(*s++); }

/* Applet functions (like busybox *_main) */
static int cat_fn(int argc, char **argv)
{
	us("cat\n");
	return 0;
}

static int echo_fn(int argc, char **argv)
{
	us("echo\n");
	return 0;
}

static int false_fn(int argc, char **argv)
{
	us("false\n");
	return 1;
}

/* Function pointer table (same pattern as busybox applet_main[]) */
static int (*const fn_table[])(int, char **) = {
	cat_fn,
	echo_fn,
	false_fn,
};

/* Applet name table (same pattern as busybox applet_names[]) */
static const char names[] = "cat\0echo\0false\0";

/* Linear search (same as busybox find_applet_by_name) */
static int find_by_name(const char *name)
{
	const char *p = names;
	int i = 0;
	int j;

	while (*p) {
		for (j = 0; p[j] == name[j]; j++) {
			if (p[j] == '\0')
				return i;
		}
		while (*p++ != '\0')
			;
		i++;
	}
	return -1;
}

void test_main(void)
{
	int i;
	int ret;

	/* Test 1: Direct calls through table by index */
	for (i = 0; i < 3; i++) {
		us("F");
		uc('0' + i);
		us(":");
		ret = fn_table[i](0, (char **)0);
	}

	/* Test 2: Dispatch by name (like busybox) */
	us("DISPATCH:");
	i = find_by_name("echo");
	if (i >= 0) {
		ret = fn_table[i](0, (char **)0);
	} else {
		us("NOT FOUND\n");
	}

	us("OK\n");
}
