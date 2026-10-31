/* Second C file of test_link_relax, linked after test_link_relax_fn.S. */

int lr_even(int n);
int lr_main_add(int a, int b);

int lr_odd(int n)
{
	return n == 0 ? 0 : lr_even(n - 1);
}

int lr_helper_mix(int x)
{
	int i, s = 0;

	for (i = 0; i < x; i++) {
		if (i & 1)
			s = lr_main_add(s, i);
		else
			s = lr_main_add(s, -i);
	}
	return s;
}

/* Code between the asm and lr_far, so that calls to lr_far from the
 * other file relax to 3 bytes: that moves the asm by a number of bytes
 * that is not a multiple of 4, which its table would notice. */
#define S4(i) p[i] = i; p[i + 1] = i; p[i + 2] = i; p[i + 3] = i;
#define S16(i) S4(i) S4(i + 4) S4(i + 8) S4(i + 12)
int lr_filler(volatile int *p)
{
	S16(0) S16(16) S16(32) S16(48)
	return p[63];
}

int lr_far(int x)
{
	return x + 7;
}
