/* Expected: SADDENDOK\nH */
int words[] = { 17, 42, 93 };
int result;
extern int asm_branches(void);

void test_main(void)
{
    const char *message;
    __asm__("ldc words+4\n ldnl 0\n ldc result\n stnl 0");
    message = result == 42 && asm_branches() == 73 ? "ADDENDOK\n" : "FAIL\n";
    while (*message)
        *(volatile int *)0x10000000 = *message++;
}
