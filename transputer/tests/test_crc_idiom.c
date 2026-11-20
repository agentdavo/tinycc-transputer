/* Expected: SCRCIDIOMOK\nH */
/* Flags: -mcrc-idiom */
/*
 * test_crc_idiom.c - -mcrc-idiom (tps_crc_lower, transputer-fnopt.h):
 * bit-serial reflected CRC loops become bitrevword; crcbyte; bitrevword.
 *
 * Each loop below is checked against the same CRC computed by a reference
 * that reads a volatile (a function naming a volatile is never rewritten),
 * over every data byte and 64 checksums.  The first six lower (CoreMark's
 * crcu8 form, CRC-16/ARC with the byte xored in first, reflected CCITT
 * #8408 with a ternary, CRC-8/MAXIM, CRC-32, a second shift order); the
 * near misses must not, and must still be right: 7 rounds, a shift left, a
 * signed (arithmetic) shift, a test of bit 1.  Which ones lowered is
 * visible with TP_NARROW_STATS=1 ([CRC] lines) - six here at -mt9000.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c) { UART_DATA = (unsigned)(unsigned char)c; }
static void us(const char *s) { while (*s) up(*s++); }
static void uh(unsigned v)
{
    int i;
    for (i = 28; i >= 0; i -= 4) {
        int n = (v >> i) & 0xF;
        up(n < 10 ? '0' + n : 'a' + n - 10);
    }
}

/* CoreMark's crcu8, written out again */
static u16 cm8(u8 data, u16 crc)
{
    u8 i = 0, x16 = 0, carry = 0;
    for (i = 0; i < 8; i++) {
        x16 = (u8)((data & 1) ^ ((u8)crc & 1));
        data >>= 1;
        if (x16 == 1) {
            crc ^= 0x4002;
            carry = 1;
        } else
            carry = 0;
        crc >>= 1;
        if (carry)
            crc |= 0x8000;
        else
            crc &= 0x7fff;
    }
    return crc;
}

/* CRC-16/ARC: the byte in first, then eight shifts */
static u16 arc(u16 crc, u8 d)
{
    int i;
    crc ^= d;
    for (i = 0; i < 8; i++) {
        if (crc & 1)
            crc = (crc >> 1) ^ 0xA001;
        else
            crc = crc >> 1;
    }
    return crc;
}

/* reflected CCITT, a ternary, data shifted alongside */
static u16 ccitt(u16 crc, u8 d)
{
    int i;
    for (i = 8; i > 0; i--) {
        crc = ((crc ^ d) & 1) ? (u16)((crc >> 1) ^ 0x8408) : (u16)(crc >> 1);
        d >>= 1;
    }
    return crc;
}

/* CRC-8/MAXIM (#31 reflected: #8C) */
static u8 maxim(u8 crc, u8 d)
{
    int i;
    for (i = 0; i < 8; i++) {
        u8 mix = (crc ^ d) & 1;
        crc >>= 1;
        if (mix)
            crc ^= 0x8C;
        d >>= 1;
    }
    return crc;
}

/* CRC-32 */
static u32 c32(u32 crc, u8 d)
{
    unsigned k;
    crc ^= d;
    for (k = 0; k < 8; k++)
        crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    return crc;
}

/* xor after the shift, test taken before it */
static u16 late(u16 crc, u8 d)
{
    int i;
    for (i = 0; i < 8; i++) {
        int b = (crc ^ d) & 1;
        crc >>= 1;
        d >>= 1;
        if (b)
            crc ^= 0xA001;
    }
    return crc;
}

/* ---- near misses: computed by the loop, never lowered ---- */
static u16 seven(u16 crc, u8 d)
{
    int i;
    for (i = 0; i < 7; i++) {
        if ((crc ^ d) & 1)
            crc = (crc >> 1) ^ 0xA001;
        else
            crc >>= 1;
        d >>= 1;
    }
    return crc;
}

static u16 left(u16 crc, u8 d)
{
    int i;
    for (i = 0; i < 8; i++) {
        if ((crc ^ d) & 1)
            crc = (u16)((crc << 1) ^ 0xA001);
        else
            crc = (u16)(crc << 1);
        d >>= 1;
    }
    return crc;
}

static short sgn(short crc, u8 d)
{
    int i;
    for (i = 0; i < 8; i++) {
        if ((crc ^ d) & 1)
            crc = (short)((crc >> 1) ^ 0x2001);
        else
            crc = (short)(crc >> 1);
        d >>= 1;
    }
    return crc;
}

static u16 bit1(u16 crc, u8 d)
{
    int i;
    for (i = 0; i < 8; i++) {
        if ((crc ^ d) & 2)
            crc = (crc >> 1) ^ 0xA001;
        else
            crc >>= 1;
        d >>= 1;
    }
    return crc;
}

/* ---- the reference: one generic loop, kept out of reach by a volatile.
 * kind: 0 reflected (P, n rounds), 1 shift left, 2 arithmetic, 3 bit 1 */
static u32 ref(u32 crc, u32 d, u32 P, int n, int kind, u32 mask)
{
    volatile int guard = 0;
    int i;
    for (i = 0; i < n + guard; i++) {
        u32 b = kind == 3 ? (crc ^ d) & 2 : (crc ^ d) & 1;
        if (kind == 1)
            crc = (crc << 1) & mask;
        else if (kind == 2)
            crc = (u32)((int)(short)crc >> 1) & mask;
        else
            crc = (crc >> 1) & mask;
        if (b)
            crc ^= P;
        crc &= mask;
        d >>= 1;
    }
    return crc;
}

static int bad;
static void chk(const char *what, u32 d, u32 c, u32 got, u32 want)
{
    if (got != want && bad++ < 8) {
        us("\nFAIL ");
        us(what);
        up(' '); uh(d); up(' '); uh(c); up(' '); uh(got); up(' '); uh(want);
    }
}

static u32 seed = 12345;
static u32 rnd(void)
{
    seed = seed * 1103515245u + 12345u;
    return seed >> 8;
}

int test_main(void)
{
    u32 d, k;

    for (k = 0; k < 64; k++) {
        u32 c = rnd(), c16 = c & 0xffff;
        if (k == 0) c = c16 = 0;
        if (k == 1) { c = 0xffffffffu; c16 = 0xffff; }
        for (d = 0; d < 256; d++) {
            chk("cm8", d, c16, cm8((u8)d, (u16)c16), ref(c16, d, 0xA001, 8, 0, 0xffff));
            chk("arc", d, c16, arc((u16)c16, (u8)d), ref(c16, d, 0xA001, 8, 0, 0xffff));
            chk("ccitt", d, c16, ccitt((u16)c16, (u8)d), ref(c16, d, 0x8408, 8, 0, 0xffff));
            chk("maxim", d, c & 0xff, maxim((u8)c, (u8)d), ref(c & 0xff, d, 0x8C, 8, 0, 0xff));
            chk("c32", d, c, c32(c, (u8)d), ref(c, d, 0xEDB88320u, 8, 0, 0xffffffffu));
            chk("late", d, c16, late((u16)c16, (u8)d), ref(c16, d, 0xA001, 8, 0, 0xffff));
            chk("seven", d, c16, seven((u16)c16, (u8)d), ref(c16, d, 0xA001, 7, 0, 0xffff));
            chk("left", d, c16, left((u16)c16, (u8)d), ref(c16, d, 0xA001, 8, 1, 0xffff));
            chk("sgn", d, c16, (u16)sgn((short)c16, (u8)d), ref(c16, d, 0x2001, 8, 2, 0xffff));
            chk("bit1", d, c16, bit1((u16)c16, (u8)d), ref(c16, d, 0xA001, 8, 3, 0xffff));
        }
    }
    us(bad ? "\nCRCIDIOMFAIL\n" : "CRCIDIOMOK\n");
    return 0;
}
