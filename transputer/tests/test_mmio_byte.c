/* test_mmio_byte.c - verify volatile MMIO byte stores remain direct stores
 *
 * Build/run:
 *   ./transputer-tcc -nostdlib -nostdinc -c -o /tmp/test_mmio_byte.o transputer/test_mmio_byte.c
 *   ./transputer-tcc -nostdlib -nostdinc -c -o /tmp/test_boot.o transputer/test_boot.S
 *   ./transputer-tcc -nostdlib -nostdinc -o /tmp/test_mmio_byte.elf /tmp/test_boot.o /tmp/test_mmio_byte.o
 *   ./transputer-netload /tmp/test_mmio_byte.elf /tmp/test_mmio_byte.btl
 *   BOOT_TIMEOUT=10 bash transputer/build.sh --boot /tmp/test_mmio_byte.btl
 */

typedef unsigned char u8;
typedef unsigned int u32;

#define UART_BYTE (*(volatile u8 *)0x10000000u)
#define UART_WORD (*(volatile u32 *)0x10000000u)

static void halt(void)
{
    *(volatile u32 *)0x80000020u = 0;
    while (1) {}
}

void test_main(void)
{
    UART_WORD = 'M';
    UART_WORD = 'M';
    UART_WORD = 'I';
    UART_WORD = 'O';
    UART_WORD = '\n';

    UART_BYTE = 'b';

    UART_WORD = 'W';
    UART_WORD = 'O';
    UART_WORD = 'R';
    UART_WORD = 'D';
    UART_WORD = '\n';
    halt();
}
