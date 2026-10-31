/*
 * test_multifile_e.c - Helper 5 for multi-file relocation test
 */
#define UART_DATA (*(volatile unsigned char *)0x10000000)

/* Add some bulk: static data + helper functions to make the .o non-trivial */
static const char data_e[] = "This is data block e with padding to increase object size xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";

static int helper_e(int x)
{
	/* Touch the data to prevent optimization */
	volatile int sum = 0;
	int i;
	for (i = 0; i < 16; i++)
		sum += data_e[i];
	return x + (sum & 0);  /* sum & 0 = 0, so returns x unchanged */
}

int func_e(void)
{
	UART_DATA = 'E';
	int result = helper_e(5);
	UART_DATA = '0' + result;
	return result;
}
