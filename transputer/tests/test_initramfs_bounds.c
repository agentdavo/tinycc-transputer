/* Runtime regression for Linux linker-script initramfs boundaries. */

#define UART_DATA (*(volatile unsigned long *)0x10000000)

extern const unsigned char __initramfs_start[];
extern const unsigned char __initramfs_end[];

static const unsigned char payload[]
    __attribute__((section(".init.ramfs"), used, aligned(1))) = "070701";

static void uart_putc(int c)
{
    UART_DATA = (unsigned long)c;
}

void test_main(void)
{
    int ok = 1;
    int i;

    if ((unsigned long)(__initramfs_end - __initramfs_start) !=
        sizeof(payload))
        ok = 0;
    for (i = 0; i < 6; i++) {
        if (__initramfs_start[i] != payload[i])
            ok = 0;
    }

    if (ok) {
        uart_putc('I'); uart_putc('R'); uart_putc('F');
        uart_putc('O'); uart_putc('K');
    } else {
        uart_putc('F'); uart_putc('A'); uart_putc('I'); uart_putc('L');
    }
    uart_putc('\n');
}
