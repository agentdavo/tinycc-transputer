#define UART (*(volatile unsigned long *)0x10000000)
static void putc(int c) { UART = (unsigned long)c; }

/* After CALL: A=return, B=arg0 — rev puts arg0 in A */
extern unsigned long asm_arg0_via_rev(unsigned long a);
__asm__(
".global asm_arg0_via_rev\n"
"asm_arg0_via_rev:\n"
"  .byte 0xF0\n"       /* rev */
"  .byte 0x22, 0xF0\n" /* ret */
);

/* Classic T4 ldl 1 form */
extern unsigned long asm_arg0_via_ldl1(unsigned long a);
__asm__(
".global asm_arg0_via_ldl1\n"
"asm_arg0_via_ldl1:\n"
"  .byte 0x71\n"
"  .byte 0x22, 0xF0\n"
);

int test_main(void) {
    unsigned long v;
    putc('S');
    v = asm_arg0_via_rev(0xA5A5A5A5UL);
    putc((v == 0xA5A5A5A5UL) ? 'R' : 'r'); /* R = rev path ok */
    v = asm_arg0_via_ldl1(0x5A5A5A5AUL);
    putc((v == 0x5A5A5A5AUL) ? 'L' : 'l'); /* L = ldl1 path ok */
    putc('H');
    *(volatile unsigned long *)0x80000020 = 0;
    while(1){}
    return 0;
}
