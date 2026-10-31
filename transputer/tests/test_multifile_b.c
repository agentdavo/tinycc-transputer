/*
 * test_multifile_b.c - Helper 2 for multi-file relocation test
 */
#define UART_DATA (*(volatile unsigned char *)0x10000000)

/* Add some bulk: static data + helper functions to make the .o non-trivial */
static const char data_b[] = "This is data block b with padding to increase object size xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";

static int helper_b(int x)
{
	/* Touch the data to prevent optimization */
	volatile int sum = 0;
	int i;
	for (i = 0; i < 16; i++)
		sum += data_b[i];
	return x + (sum & 0);  /* sum & 0 = 0, so returns x unchanged */
}

int func_b(void)
{
	UART_DATA = 'B';
	int result = helper_b(2);
	UART_DATA = '0' + result;
	return result;
}
