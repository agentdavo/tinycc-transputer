/* Verify CALL argument delivery to the actual Linux SELTH wrapper. */
extern void t9k_asm_selth(unsigned long);
extern unsigned long t9k_asm_ldth(void);
static unsigned long descriptors[2][16] __attribute__((aligned(64)));
static void puts_uart(const char *s)
{
    while (*s) *(volatile unsigned *)0x10000000u = *s++;
}
void test_main(void)
{
    unsigned long old = t9k_asm_ldth();
    int ok;
    t9k_asm_selth((unsigned long)descriptors[0]);
    ok = t9k_asm_ldth() == (unsigned long)descriptors[0];
    t9k_asm_selth((unsigned long)descriptors[1]);
    ok = ok && t9k_asm_ldth() == (unsigned long)descriptors[1];
    t9k_asm_selth(old);
    ok = ok && t9k_asm_ldth() == old;
    puts_uart(ok ? "THDS-ABI-PASS\n" : "THDS-ABI-FAIL\n");
}
