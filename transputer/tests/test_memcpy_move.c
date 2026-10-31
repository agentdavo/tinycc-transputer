#define U (*(volatile unsigned int *)0x10000000)
static void up(int c){U=(unsigned)(unsigned char)c;}
static void us(const char*s){while(*s)up(*s++);}
static void ud(unsigned v){char b[12];int n=0;if(!v){up(48);return;}while(v){b[n++]=48+v%10;v/=10;}while(n)up(b[--n]);}
static int bad=0,ck=0;
static void eq(const char*w,int i,long g,long e){ck++;if(g!=e){bad++;us("\nF ");us(w);up(58);ud((unsigned)i);us(" g=");ud((unsigned)g);us(" e=");ud((unsigned)e);}}
void *memcpy(void *d, const void *s, unsigned long n);
static unsigned char SRC[80], DST[80];
static void fillsrc(void){int i;for(i=0;i<80;i++)SRC[i]=(unsigned char)(i+1);}
static void clr(void){int i;for(i=0;i<80;i++)DST[i]=0xEE;}
int test_main(void){
    int i,n,off;
    fillsrc();
    /* constant sizes, including 0 and non-multiples of 4 */
    { static const int sizes[9]={0,1,2,3,4,5,7,16,64};
      for(i=0;i<9;i++){ int k; clr(); n=sizes[i];
        if(n==0) memcpy(DST,SRC,0); else if(n==1) memcpy(DST,SRC,1);
        else if(n==2) memcpy(DST,SRC,2); else if(n==3) memcpy(DST,SRC,3);
        else if(n==4) memcpy(DST,SRC,4); else if(n==5) memcpy(DST,SRC,5);
        else if(n==7) memcpy(DST,SRC,7); else if(n==16) memcpy(DST,SRC,16);
        else memcpy(DST,SRC,64);
        for(k=0;k<n;k++)  eq("cst",i*100+k,DST[k],SRC[k]);
        for(k=n;k<80;k++) eq("cstg",i*100+k,DST[k],0xEE); } }
    /* variable size */
    for(n=0;n<=40;n++){ int k; clr(); memcpy(DST,SRC,(unsigned long)n);
        for(k=0;k<n;k++)  eq("var",n*100+k,DST[k],SRC[k]);
        for(k=n;k<80;k++) eq("varg",n*100+k,DST[k],0xEE); }
    /* unaligned src and dest at every combination */
    for(off=0;off<4;off++){ int so,k; for(so=0;so<4;so++){ clr();
        memcpy(DST+off,SRC+so,17UL);
        for(k=0;k<17;k++) eq("un",off*400+so*20+k,DST[off+k],SRC[so+k]);
        if(off>0) eq("ung",off*400+so,DST[off-1],0xEE);
        eq("ung2",off*400+so,DST[off+17],0xEE); } }
    /* return value must be dest */
    { void *r; clr(); r=memcpy(DST,SRC,8UL); eq("ret",0,(long)(r==(void*)DST),1); }
    /* non-overlapping forward copy within one array */
    { int k; fillsrc(); memcpy(SRC+40,SRC,20UL);
      for(k=0;k<20;k++) eq("self",k,SRC[40+k],k+1); }
    us("\nchecks=");ud((unsigned)ck);us(" failures=");ud((unsigned)bad);
    us(bad?"\nMEMCPY: FAIL\n":"\nMEMCPY: PASS\n"); return bad; }
