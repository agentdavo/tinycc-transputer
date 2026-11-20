/* Integer-only UART reporting: no libc and no FP formatting in the oracle. */
#define DATA (*(volatile unsigned *)0x10000000u)
#define STATUS (*(volatile unsigned *)0x10000004u)
#ifndef CPU_NAME
#define CPU_NAME "unknown"
#endif
#ifndef TIMING_UNIT
#define TIMING_UNIT "ticks"
#endif
/* Slots 22/23 hold measured timestamps, never fixed expected values. */
struct testcase { const char *name; void (*run)(unsigned *); const unsigned *input; unsigned input_mask; const unsigned *want; unsigned mask; void (*handler)(void); };
#ifdef FPU_BF16
int bf16_test_main(void);
#endif
#ifdef FPU_BF16_TVX
int tvx_bf16_arithmetic_main(void);
#endif
#include "cases.h"
static void putc_uart(unsigned c) { while (!(STATUS & 1)) {} DATA = c; }
static void puts_uart(const char *p) { while (*p) putc_uart((unsigned char)*p++); }
static void hex(unsigned v) { int n; for(n=28;n>=0;n-=4) putc_uart("0123456789abcdef"[(v>>n)&15]); }
static unsigned identity_rods[4]={0x20000003u,0x60000003u,0xa0000003u,0xe0000003u};
int test_main(void)
{
    unsigned i,j,failed=0,io[24];
    puts_uart("\nFPU-SUPER " CPU_NAME " v2 UART tx=01 rx=02\n");
    for(i=0;i<sizeof(tests)/sizeof(tests[0]);i++) {
        unsigned bad=0,wi=0,ri=0;
        for(j=0;j<20;j++) io[j]=(tests[i].input_mask&(1u<<j))?tests[i].input[ri++]:0;
        io[21]=(unsigned)tests[i].handler;
        io[20]=(unsigned)identity_rods;
        puts_uart("BEGIN ");hex(i);putc_uart(' ');puts_uart(tests[i].name);putc_uart('\n');
        tests[i].run(io);
        for(j=0;j<24;j++) if(tests[i].mask & (1u<<j)) {
            unsigned want=tests[i].want[wi++];
            if(io[j]!=want) {
                bad=1;puts_uart("MISMATCH word=");hex(j);puts_uart(" got=");hex(io[j]);puts_uart(" want=");hex(want);putc_uart('\n');
            }
        }
        puts_uart(bad?"FAIL ":"PASS ");puts_uart(tests[i].name);puts_uart(" bits=");
        for(j=0;j<24;j++) if(tests[i].mask & (1u<<j)) { hex(io[j]);putc_uart(' '); }
        putc_uart('\n');
        puts_uart("TIME ");hex(i);puts_uart(" " TIMING_UNIT "=");hex(io[23]-io[22]);putc_uart('\n');
        failed+=bad;
    }
    puts_uart("FPU-SUPER ");puts_uart(failed?"FAIL":"PASS");puts_uart(" tests=");hex(i);puts_uart(" failed=");hex(failed);putc_uart('\n');
#ifdef FPU_BF16
    failed+=bf16_test_main()!=0;
#ifdef FPU_BF16_TVX
    failed+=tvx_bf16_arithmetic_main()!=0;
#endif
    puts_uart(failed?"FPU-ALL FAIL\n":"FPU-ALL PASS\n");
#endif
    return failed!=0;
}
