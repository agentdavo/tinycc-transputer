/*
 * Target: -mt9000
 * test_t9000_vcp_builtins.c - the T9000 configuration and VCP builtins of
 * include/t9000.h on one node, no cable needed.
 * Expected: SVCPB-OK\nH
 *
 * Configuration registers read back what stconf wrote; VLCB set-up through
 * t9_vlink() leaves the headers readhdr returns; swapbfr hands back the old
 * packet buffer; chantype tells a virtual channel from a memory one; and
 * ldchstatus shows a virtual input inactive, then active after setchmode.
 * Traffic over the links is tests/net/vlink.sh.
 */
#include <t9000.h>
#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c){UART_DATA=(unsigned)(unsigned char)c;}
static void us(const char*s){while(*s)up(*s++);}
static void uh(unsigned v){int i;for(i=28;i>=0;i-=4){int n=(v>>i)&0xF;up(n<10?'0'+n:'a'+n-10);}}

static unsigned pkt[2][8], spare[8];
static const unsigned char hdr0[2] = { 0x12, 0x34 }, hdr1[1] = { 7 };
static int fails;

static void check(const char *what, unsigned got, unsigned want)
{
    if (got == want)
        return;
    us(what); up(' '); uh(got); up('/'); uh(want); up('\n');
    fails++;
}

int test_main(void)
{
    unsigned char back[3];
    int chan_word;
    unsigned id = __ldconf(T9_CR_DEVICEID);

    check("deviceid", __ldconf(T9_CR_DEVICEID), id);
    t9_vcp_channels(2);
    check("mininvalid", __ldconf(T9_CR_MININVALIDCHAN), (unsigned)T9_VIN(2));
    check("bytemode", __ldconf(T9_CR_VCPLINKMODE(1)), T9_VLM_BYTEMODE);
    t9_link_virtual(1, 1);
    check("hdr2mode", __ldconf(T9_CR_VCPLINKMODE(1)), T9_VLM_HEADER2);
    __stconf(T9_CR_VCPLINKMAXHDR(1), 0xFF);
    check("maxhdr", __ldconf(T9_CR_VCPLINKMAXHDR(1)), 0xFF);

    t9_vlink(0, pkt[0], 1, hdr0, 2);
    t9_vlink(1, pkt[1], 1, hdr1, 1);
    back[0] = back[1] = back[2] = 0;
    __readhdr(T9_VIN(0), 2, back);
    check("hdr0", back[0] | back[1] << 8, 0x3412);
    __readhdr(T9_VIN(1), 1, back);
    check("hdr1", back[0], 7);

    check("swap", (unsigned)__swapbfr(T9_VIN(0), spare), (unsigned)pkt[0]);
    check("swapback", (unsigned)__swapbfr(T9_VIN(0), pkt[0]), (unsigned)spare);

    check("vtype", __chantype(T9_VIN(0)), 0);
    check("memtype", __chantype(&chan_word), 1);

    check("inactive", __ldchstatus(T9_VIN(1)), 0x80000000u);
    t9_vcp_start(2);
    check("active", __ldchstatus(T9_VIN(1)), 0x80000001u);
    check("outactive", __ldchstatus(T9_VOUT(1)), 0x80000001u);
    check("vcpstatus", __ldconf(T9_CR_VCPSTATUS), 0);

    us(fails ? "VCPB-BAD\n" : "VCPB-OK\n");
    return fails;
}
