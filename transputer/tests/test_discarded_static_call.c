/* Expected: SSCADONE\nH */
#define UART (*(volatile unsigned int *)0x10000000u)

struct item {
	unsigned int delayed : 1;
	unsigned int value;
};

static int consume(struct item *item, int flags);

static struct item *pick(struct item *item)
{
	if (item->delayed) {
		consume(item, 0x21);
		return 0;
	}
	return item;
}

static int consume(struct item *item, int flags)
{
	UART = 'C';
	item->delayed = 0;
	return flags;
}

void test_main(void)
{
	struct item item;

	item.delayed = 1;
	item.value = 7;
	UART = 'S';
	if (pick(&item) != 0 || item.delayed != 0)
		UART = 'F';
	else
		UART = 'A';
	UART = 'D';
	UART = 'O';
	UART = 'N';
	UART = 'E';
	UART = '\n';
}
