/* 32 bounded image bands assigned round-robin over a binary T800 link tree.
 * Each node forwards complete, checksummed descendant bands toward node 0. */
#include <conc.h>
#include "tinygl.h"
#ifndef NNODES
#define NNODES 4
#endif
#ifndef NODE
#define NODE 0
#endif
#ifndef FRAMES
#define FRAMES 4
#endif
#ifndef FPS
#define FPS 0
#endif
#ifndef CAPTURE
#define CAPTURE 1
#endif
#define TASKS 32
#define UART (*(volatile unsigned int *)0x10000000u)
#define REG(a) (*(volatile unsigned int *)(a))
#define FB_BASE 0x90000000u
#define FRAME_BYTES (320u*240u*4u)
void __in(int,Channel *,void *);
void __out(int,Channel *,const void *);
void __move2dinit(int,int,int);
void __move2dall(int,void *,const void *);
static unsigned int tile[TG_TILE_W*TG_TILE_H];
static int depth[TG_TILE_W*TG_TILE_H];
static unsigned int header[3]; /* frame, task, checksum */
#if NODE == 0
static unsigned int row[160];
static int seen[TASKS],bad;
static void puts_uart(const char *s) { while (*s) UART=(unsigned char)*s++; }
static void hex_uart(unsigned int v) { int k; for(k=28;k>=0;k-=4) UART="0123456789abcdef"[(v>>k)&15]; }
static void display_band(int task, unsigned int base, const unsigned int *data)
{
    int quadrant=task/8, band=task%8, lo=band*60/8, hi=(band+1)*60/8;
    int x,y, ox=(quadrant&1)*160, oy=(quadrant>>1)*120;
    for(y=lo;y<hi;y++) {
        for(x=0;x<80;x++) row[2*x]=row[2*x+1]=*data++;
        __move2dinit(2,320*4,0);
        __move2dall(sizeof(row),(void *)(base+((oy+y*2)*320+ox)*4),row);
    }
}
static void accept(unsigned int *h, unsigned int *data, unsigned int frame, unsigned int base)
{
    int task=(int)h[1], band, count;
    if (task<0 || task>=TASKS) { bad++; return; }
    band=task%8; count=((band+1)*60/8-band*60/8)*80;
    if (h[0]!=frame || h[2]!=tg_checksum(data,count) || seen[task]) bad++;
    seen[task]=1;
    display_band(task,base,data);
}
#endif
/* Number of assigned tasks in a binary subtree; independent of message order. */
static int subtree_tasks(int root)
{
    int i,j,total=0;
    for(i=root;i<NNODES;i++) {
        j=i;
        while(j>root) j=(j-1)/2;
        if(j==root && i<TASKS) total+=(TASKS-1-i)/NNODES+1;
    }
    return total;
}
static int band_words(int task)
{
    int b=task%8;
    return ((b+1)*60/8-b*60/8)*80;
}

int test_main(void)
{
    unsigned int frame;
#if NODE == 0
    unsigned int next=0;
    REG(0xA0000004u)=FB_BASE; REG(0xA0000008u)=320;
    REG(0xA000000cu)=240; REG(0xA0000010u)=1280; REG(0xA0000000u)=1;
#endif
    for(frame=0;frame<FRAMES;frame++) {
        int child, task;
#if NODE == 0
        unsigned int start=(unsigned int)Time(), elapsed;
        unsigned int base=FB_BASE+((frame&1u)?0u:FRAME_BYTES);
        for(task=0;task<TASKS;task++) seen[task]=0;
#else
        __in(sizeof(frame),LINKIN(0),&frame);
#endif
        for(child=1;child<=2;child++)
            if (NODE*2+child<NNODES) __out(sizeof(frame),LINKOUT(child),&frame);
        for(task=NODE;task<TASKS;task+=NNODES) {
            int band=task%8, lo=band*60/8, hi=(band+1)*60/8;
            unsigned int *data=tile+lo*80;
            cube_render_band(tile,depth,task/8,frame,lo,hi);
            header[0]=frame; header[1]=task; header[2]=tg_checksum(data,(hi-lo)*80);
#if NODE == 0
            accept(header,data,frame,base);
#else
            __out(sizeof(header),LINKOUT(0),header);
            __out((hi-lo)*80*4,LINKOUT(0),data);
#endif
        }
        for(child=1;child<=2;child++) {
            int root=NODE*2+child, n;
            if(root>=NNODES) continue;
            for(n=0;n<subtree_tasks(root);n++) {
                int count;
                __in(sizeof(header),LINKIN(child),header);
                task=(int)header[1];
                if(task<0 || task>=TASKS) return 1;
                count=band_words(task);
                __in(count*4,LINKIN(child),tile);
#if NODE == 0
                accept(header,tile,frame,base);
#else
                __out(sizeof(header),LINKOUT(0),header);
                __out(count*4,LINKOUT(0),tile);
#endif
            }
        }
#if NODE == 0
        for(task=0;task<TASKS;task++) if(!seen[task]) bad++;
        elapsed=(unsigned int)Time()-start;
        if (FPS && frame && TimeAfter(next,(unsigned int)Time())) ProcAfter((int)next);
        REG(0xA0000014u)=base;
        next=(unsigned int)Time()+(FPS ? (TICKS_PER_SEC_LOW+FPS-1)/FPS : 0);
        puts_uart("SCALE FRAME "); hex_uart(frame); UART=' '; hex_uart(elapsed); UART='\n';
#if CAPTURE
        REG(0xA0000018u)=1;
#endif
#endif
    }
#if NODE == 0
    puts_uart(bad ? "SCALE BAD\n" : "SCALE OK\n");
#endif
    for (;;) ProcWait(TICKS_PER_SEC_LOW);
}
