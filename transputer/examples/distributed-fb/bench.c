/* T4_EST_CYCLES=1 makes ldtimer report estimated T800 cycles. */
#include "tinygl.h"
#define UART (*(volatile unsigned int *)0x10000000u)
unsigned int __ldtimer(void);
static unsigned int tile[TG_TILE_W * TG_TILE_H];
static int depth[TG_TILE_W * TG_TILE_H];
static void put(const char *s) { while (*s) UART=(unsigned char)*s++; }
static void hex(unsigned int n) { int i; for (i=28;i>=0;i-=4) UART="0123456789abcdef"[(n>>i)&15]; }
int test_main(void)
{
    unsigned int begin, elapsed, hash=0; int frame, node;
    begin=__ldtimer();
    for (frame=0;frame<32;++frame)
        for (node=0;node<4;++node) {
            cube_render(tile,depth,node,frame);
            hash ^= tg_checksum(tile,TG_TILE_W*TG_TILE_H);
        }
    elapsed=__ldtimer()-begin;
    put("CUBEBENCH "); hex(elapsed); UART=' '; hex(hash); UART='\n';
    return 0;
}
