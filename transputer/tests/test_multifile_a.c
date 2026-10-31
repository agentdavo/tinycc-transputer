/*
 * test_multifile_a.c - Helper 1 for multi-file relocation test
 */
#define UART_DATA (*(volatile unsigned char *)0x10000000)

/* Add some bulk: static data + helper functions to make the .o non-trivial */
static const char data_a[] = "This is data block a with padding to increase object size xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";

static int helper_a(int x)
{
	/* Touch the data to prevent optimization */
	volatile int sum = 0;
	int i;
	for (i = 0; i < 16; i++)
		sum += data_a[i];
	return x + (sum & 0);  /* sum & 0 = 0, so returns x unchanged */
}

int func_a(void)
{
	UART_DATA = 'A';
	int result = helper_a(1);
	UART_DATA = '0' + result;
	return result;
}
