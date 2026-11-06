/* One complete 32-band bind-pose mesh frame, plus band checksums. */
#include "tinygl.h"
#define UART (*(volatile unsigned int *)0x10000000u)
unsigned int __ldtimer(void);
static unsigned int tile[TG_TILE_W*TG_TILE_H];
static int depth[TG_TILE_W*TG_TILE_H];
static void put(const char *s) { while(*s) UART=(unsigned char)*s++; }
static void hex(unsigned int n) { int i; for(i=28;i>=0;i-=4) UART="0123456789abcdef"[(n>>i)&15]; }
int test_main(void)
{
    unsigned int start=__ldtimer(),hash=0,elapsed; int task,lo,hi;
    for(task=0;task<32;task++) {
        lo=(task%8)*60/8; hi=(task%8+1)*60/8;
        cube_render_band(tile,depth,task/8,0,lo,hi);
        hash^=tg_checksum(tile+lo*80,(hi-lo)*80);
    }
    elapsed=__ldtimer()-start;
    put("HELLBENCH "); hex(elapsed); UART=' '; hex(hash); UART='\n';
    return 0;
}
