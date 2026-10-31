/* Target: -mt9000tvx */
#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void us(const char*s){while(*s)UART_DATA=(unsigned)(unsigned char)*s++;}
int test_main(void){
#if defined(__TVX__) && defined(__BF16__) && defined(__MXFP4__) && defined(__transputer__)
    us("FEAT: PASS\n");
#else
    us("FEAT: FAIL\n");
#endif
    /* C11 has neither format; nothing here should claim otherwise */
#ifdef __STDC_IEC_559__
    us("(iec559)\n");
#endif
    return 0;
}
