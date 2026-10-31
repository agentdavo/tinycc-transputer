/* RTL microtest: kbd uart1 @0x11000000, mouse uart2 @0x12000000 (T4 map).
   Expects scripted RX: kbd sends 'K', mouse sends 'M'. UART0 echoes SKMH. */
#define UART0      (*(volatile unsigned long *)0x10000000)
#define KBD_DATA   (*(volatile unsigned long *)0x11000000)
#define KBD_ST     (*(volatile unsigned long *)0x11000004)
#define MOUSE_DATA (*(volatile unsigned long *)0x12000000)
#define MOUSE_ST   (*(volatile unsigned long *)0x12000004)
static void putc(int c) { UART0 = (unsigned long)c; }
int test_main(void) {
    unsigned long v;
    putc('S');
    while (!(KBD_ST & 2)) ;
    v = KBD_DATA & 0xff; putc((int)v);
    while (!(MOUSE_ST & 2)) ;
    v = MOUSE_DATA & 0xff; putc((int)v);
    putc('H');
    *(volatile unsigned long *)0x80000020 = 0;
    while (1) {}
    return 0;
}
