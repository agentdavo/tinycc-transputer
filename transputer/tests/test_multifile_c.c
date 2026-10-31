/*
 * test_multifile_c.c - Helper 3 for multi-file relocation test
 */
#define UART_DATA (*(volatile unsigned char *)0x10000000)

/* Add some bulk: static data + helper functions to make the .o non-trivial */
static const char data_c[] = "This is data block c with padding to increase object size xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";

static int helper_c(int x)
{
	/* Touch the data to prevent optimization */
	volatile int sum = 0;
	int i;
	for (i = 0; i < 16; i++)
		sum += data_c[i];
	return x + (sum & 0);  /* sum & 0 = 0, so returns x unchanged */
}

int func_c(void)
{
	UART_DATA = 'C';
	int result = helper_c(3);
	UART_DATA = '0' + result;
	return result;
}
