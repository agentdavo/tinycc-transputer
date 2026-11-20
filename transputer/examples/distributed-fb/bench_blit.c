/* Compare the scalar 2x tile blit with the demo's MOVE2D path. */
#define UART (*(volatile unsigned int *)0x10000000u)
#define FB ((volatile unsigned int *)0x90000000u)
unsigned int __ldtimer(void);
void __move2dinit(int,int,int);
void __move2dall(int,void *,const void *);
static unsigned int tile[80*60], row[160];
static void hex(unsigned int n) { int i; for(i=28;i>=0;i-=4) UART="0123456789abcdef"[(n>>i)&15]; }
static void put(const char *s) { while(*s) UART=(unsigned char)*s++; }
int test_main(void)
{
    unsigned int t, scalar, fast, bad=0; int i,x,y,sx,sy,k;
    for(i=0;i<80*60;i++) tile[i]=(unsigned int)i*7919u;
    t=__ldtimer();
    for(k=0;k<16;k++) for(y=0;y<60;y++) for(x=0;x<80;x++)
        for(sy=0;sy<2;sy++) for(sx=0;sx<2;sx++) FB[(y*2+sy)*320+x*2+sx]=tile[y*80+x];
    scalar=__ldtimer()-t;
    t=__ldtimer();
    for(k=0;k<16;k++) for(y=0;y<60;y++) {
        for(x=0;x<80;x++) row[2*x]=row[2*x+1]=tile[y*80+x];
        __move2dinit(2,320*4,0);
        __move2dall(sizeof(row),(void *)(0x90000000u+y*2*320*4),row);
    }
    fast=__ldtimer()-t;
    for(y=0;y<60;y++) for(x=0;x<80;x++) for(sy=0;sy<2;sy++) for(sx=0;sx<2;sx++)
        if(FB[(y*2+sy)*320+x*2+sx]!=tile[y*80+x]) ++bad;
    put("BLITBENCH "); hex(scalar); UART=' '; hex(fast); UART=' '; hex(bad); UART='\n';
    return 0;
}
