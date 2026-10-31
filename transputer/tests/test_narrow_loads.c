/*
 * Byte and halfword loads, signed and unsigned, from every addressing
 * path (local, pointer, global, struct member at an odd offset, call
 * argument), with the top bit set so a missing or extra extension shows.
 *
 * Expected: SNLOADOK\nH
 *
 * lb zero-extends and a halfword built from two lb is already 0..0xFFFF,
 * so the `ldc #ff; and` / `ldc #ffff; and` that followed them were dead;
 * the T805 sign extension now skips its mask on those paths too.
 */
typedef unsigned int u32;

#define UART (*(volatile unsigned int *)0x10000000u)

static void uc(int c) { UART = (unsigned char)c; }
static void us(const char *s) { while (*s) uc(*s++); }
static void uh(u32 v) { int i; for (i = 28; i >= 0; i -= 4) uc("0123456789abcdef"[(v >> i) & 15]); }

static int bad;

static void eq(const char *what, u32 got, u32 want)
{
	if (got == want)
		return;
	bad++;
	us("\nF ");
	us(what);
	us(" g=");
	uh(got);
}

struct mix {
	char pad;
	unsigned char uc;	/* offset 1 */
	signed char sc;		/* offset 2 */
	char pad2;
	unsigned short us;	/* offset 4 */
	short ss;		/* offset 6 */
} __attribute__((packed));

struct odd {
	char pad;
	unsigned short us;	/* offset 1 */
	short ss;		/* offset 3 */
} __attribute__((packed));

static struct mix gm = { 0x11, 0xfe, -2, 0x22, 0xfffe, -3 };
static struct odd go = { 0x33, 0xfffd, -4 };
static unsigned char gub = 0x80;
static signed char gsb = -128;
static unsigned short gus = 0x8000;
static short gss = -32768;

static u32 id(u32 v) { return v; }
static int sid(int v) { return v; }
static u32 take2(u32 a, int b) { return a + (u32)b; }

int test_main(void)
{
	struct mix m = gm, *pm = &gm;
	struct odd *po = &go;
	unsigned char lub = 0xff;
	signed char lsb = -1;
	unsigned short lus = 0xffff;
	short lss = -1;
	unsigned char buf[4] = { 0x81, 0x82, 0x83, 0x84 };
	unsigned char *pb = buf;

	/* expressions: load() */
	eq("local uchar", lub + 0u, 0xff);
	eq("local schar", (u32)(lsb + 0), 0xffffffff);
	eq("local ushort", lus + 0u, 0xffff);
	eq("local short", (u32)(lss + 0), 0xffffffff);
	eq("ptr uchar", pm->uc + 0u, 0xfe);
	eq("ptr schar", (u32)(pm->sc + 0), 0xfffffffe);
	eq("ptr ushort", pm->us + 0u, 0xfffe);
	eq("ptr short", (u32)(pm->ss + 0), 0xfffffffd);
	eq("odd ushort", po->us + 0u, 0xfffd);
	eq("odd short", (u32)(po->ss + 0), 0xfffffffc);
	eq("global uchar", gub + 0u, 0x80);
	eq("global schar", (u32)(gsb + 0), 0xffffff80);
	eq("global ushort", gus + 0u, 0x8000);
	eq("global short", (u32)(gss + 0), 0xffff8000);
	eq("local struct uc", m.uc + 0u, 0xfe);
	eq("local struct ss", (u32)(m.ss + 0), 0xfffffffd);
	eq("buf[3]", pb[3] + 0u, 0x84);
	eq("sum", pb[0] + pb[1] + 0u, 0x81 + 0x82);
	/* call arguments: tp_push_sv */
	eq("arg uchar", id(pm->uc), 0xfe);
	eq("arg schar", (u32)sid(pm->sc), 0xfffffffe);
	eq("arg ushort", id(po->us), 0xfffd);
	eq("arg short", (u32)sid(po->ss), 0xfffffffc);
	eq("arg local uchar", id(lub), 0xff);
	eq("arg local short", (u32)sid(lss), 0xffffffff);
	eq("arg pair", take2(pm->us, pm->sc), 0xfffe - 2);
	us(bad ? "\nFAIL\n" : "NLOADOK\n");
	return 0;
}
