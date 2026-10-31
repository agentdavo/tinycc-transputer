/*
 * test_multifile_d.c - Helper 4 for multi-file relocation test
 */
#define UART_DATA (*(volatile unsigned char *)0x10000000)

/* Add some bulk: static data + helper functions to make the .o non-trivial */
static const char data_d[] = "This is data block d with padding to increase object size xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";

static int helper_d(int x)
{
	/* Touch the data to prevent optimization */
	volatile int sum = 0;
	int i;
	for (i = 0; i < 16; i++)
		sum += data_d[i];
	return x + (sum & 0);  /* sum & 0 = 0, so returns x unchanged */
}

int func_d(void)
{
	UART_DATA = 'D';
	int result = helper_d(4);
	UART_DATA = '0' + result;
	return result;
}
