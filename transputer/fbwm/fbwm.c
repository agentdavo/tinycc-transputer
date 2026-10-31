/*
 * fbwm.c - Transputer Framebuffer Window Manager / GUI Library
 *
 * Implementation optimized for the T800's MOVE2D instruction set.
 * All drawing targets the fixed Linux simplefb aperture directly.  The
 * hardware is an always-on scanout engine; the flip register is used only
 * as a completed-screen marker for display backends and capture tools.
 *
 * MOVE2D usage — note which of length/width goes to which instruction:
 *   __move2dinit(num_rows,  dst_stride, src_stride)   -- set up 2D params
 *   __move2dall (row_bytes, dst_addr,   src_addr)     -- copy all bytes
 *   __move2dnonzero(row_bytes, dst_addr, src_addr)    -- copy non-zero only
 *
 * The intrinsics map arg0->AReg, arg1->BReg, arg2->CReg, and the ISM
 * (§7.15.1, pp.386-387) puts *different* quantities in AReg for the two:
 *
 *   move2dinit  AReg = length (rows),  BReg = dst stride, CReg = src stride
 *   move2dall   AReg = width (bytes),  BReg = dst addr,   CReg = src addr
 *
 * This file previously had them the other way round - passing row_bytes as
 * the row count and the row count as the width - so every blit was
 * transposed.  It went unnoticed because the paths that use them
 * (fb_copy_rect, fb_scroll) are only reached on window drag and terminal
 * scroll, neither of which the headless boot exercises.
 *
 * The static __move2d* definitions below are dead code: transputer-tcc
 * recognises these names as builtins and emits opcodes 0x5b/0x5c directly,
 * so the C bodies are never called.  They are kept as documentation of the
 * semantics and must be kept consistent with the argument order above.
 */

#include "fbwm.h"
#include "fbwm_shm.h"

/* ========================================================================
 * T800 MOVE/MOVE2D intrinsic declarations
 * These are built-in to the transputer-tcc backend (transputer-gen.c).
 * ======================================================================== */

/* T800 MOVE instruction fallback — word-at-a-time copy.
 * Uses volatile to ensure writes generate STNL (not SB) so the
 * T4 emulator's vga_framebuffer shadow gets updated. */
static void __move(void *dest, const void *src, int length)
{
    volatile unsigned int *d = (volatile unsigned int *)dest;
    const volatile unsigned int *s = (const volatile unsigned int *)src;
    int words = length / 4;
    int i;
    for (i = 0; i < words; i++)
        d[i] = s[i];
}
/*
 * MOVE2D reference implementation (see the header note: these bodies are
 * shadowed by the compiler builtins and never actually run).
 */
static int m2d_length, m2d_dst_stride, m2d_src_stride;

static void __move2dinit(int num_rows, int dst_stride, int src_stride)
{
    m2d_length = num_rows;
    m2d_dst_stride = dst_stride;
    m2d_src_stride = src_stride;
}

static void __move2dall(int row_bytes, void *dst, const void *src)
{
    /* Use word-at-a-time copy (STNL) to ensure the T4 emulator's
     * vga_framebuffer shadow buffer gets updated via writeword_int. */
    volatile unsigned int *d = (volatile unsigned int *)dst;
    const volatile unsigned int *s = (const volatile unsigned int *)src;
    int words_per_row = row_bytes / 4;
    int row, i;

    for (row = 0; row < m2d_length; row++) {
        for (i = 0; i < words_per_row; i++)
            d[i] = s[i];
        d = (volatile unsigned int *)((unsigned char *)d + m2d_dst_stride);
        s = (const volatile unsigned int *)((const unsigned char *)s + m2d_src_stride);
    }
}

/* Not used yet, but declared for completeness */
static void __move2dnonzero(int row_bytes, void *dst, const void *src)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    int row, i;
    for (row = 0; row < m2d_length; row++) {
        for (i = 0; i < row_bytes; i++)
            if (s[i]) d[i] = s[i];
        d += m2d_dst_stride;
        s += m2d_src_stride;
    }
}

static void __move2dzero(int row_bytes, void *dst, const void *src)
{
    (void)row_bytes; (void)dst; (void)src;
}

/* ========================================================================
 * 8x16 Bitmap Font Data (ASCII 32-126)
 *
 * Each glyph is 16 bytes, one byte per row, MSB is leftmost pixel.
 * This gives a clean 8-pixel-wide by 16-pixel-tall character cell.
 * Based on the classic VGA/BIOS 8x16 font.
 * ======================================================================== */

static const unsigned char fbwm_font[FBWM_FONT_GLYPHS][FBWM_FONT_HEIGHT] = {
    /* 32 ' ' */  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 33 '!' */  {0x00,0x00,0x18,0x3C,0x3C,0x3C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
    /* 34 '"' */  {0x00,0x66,0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 35 '#' */  {0x00,0x00,0x00,0x6C,0x6C,0xFE,0x6C,0x6C,0xFE,0x6C,0x6C,0x00,0x00,0x00,0x00,0x00},
    /* 36 '$' */  {0x18,0x18,0x7C,0xC6,0xC2,0xC0,0x7C,0x06,0x06,0x86,0xC6,0x7C,0x18,0x18,0x00,0x00},
    /* 37 '%' */  {0x00,0x00,0x00,0x00,0xC2,0xC6,0x0C,0x18,0x30,0x60,0xC6,0x86,0x00,0x00,0x00,0x00},
    /* 38 '&' */  {0x00,0x00,0x38,0x6C,0x6C,0x38,0x76,0xDC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00},
    /* 39 ''' */  {0x00,0x30,0x30,0x30,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 40 '(' */  {0x00,0x00,0x0C,0x18,0x30,0x30,0x30,0x30,0x30,0x30,0x18,0x0C,0x00,0x00,0x00,0x00},
    /* 41 ')' */  {0x00,0x00,0x30,0x18,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x18,0x30,0x00,0x00,0x00,0x00},
    /* 42 '*' */  {0x00,0x00,0x00,0x00,0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 43 '+' */  {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 44 ',' */  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x30,0x00,0x00,0x00},
    /* 45 '-' */  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 46 '.' */  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
    /* 47 '/' */  {0x00,0x00,0x00,0x00,0x02,0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00,0x00,0x00,0x00},
    /* 48 '0' */  {0x00,0x00,0x38,0x6C,0xC6,0xC6,0xD6,0xD6,0xC6,0xC6,0x6C,0x38,0x00,0x00,0x00,0x00},
    /* 49 '1' */  {0x00,0x00,0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00},
    /* 50 '2' */  {0x00,0x00,0x7C,0xC6,0x06,0x0C,0x18,0x30,0x60,0xC0,0xC6,0xFE,0x00,0x00,0x00,0x00},
    /* 51 '3' */  {0x00,0x00,0x7C,0xC6,0x06,0x06,0x3C,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 52 '4' */  {0x00,0x00,0x0C,0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x0C,0x1E,0x00,0x00,0x00,0x00},
    /* 53 '5' */  {0x00,0x00,0xFE,0xC0,0xC0,0xC0,0xFC,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 54 '6' */  {0x00,0x00,0x38,0x60,0xC0,0xC0,0xFC,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 55 '7' */  {0x00,0x00,0xFE,0xC6,0x06,0x06,0x0C,0x18,0x30,0x30,0x30,0x30,0x00,0x00,0x00,0x00},
    /* 56 '8' */  {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 57 '9' */  {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7E,0x06,0x06,0x06,0x0C,0x78,0x00,0x00,0x00,0x00},
    /* 58 ':' */  {0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
    /* 59 ';' */  {0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x30,0x00,0x00,0x00,0x00},
    /* 60 '<' */  {0x00,0x00,0x00,0x06,0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x06,0x00,0x00,0x00,0x00},
    /* 61 '=' */  {0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 62 '>' */  {0x00,0x00,0x00,0x60,0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x60,0x00,0x00,0x00,0x00},
    /* 63 '?' */  {0x00,0x00,0x7C,0xC6,0xC6,0x0C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
    /* 64 '@' */  {0x00,0x00,0x00,0x7C,0xC6,0xC6,0xDE,0xDE,0xDE,0xDC,0xC0,0x7C,0x00,0x00,0x00,0x00},
    /* 65 'A' */  {0x00,0x00,0x10,0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
    /* 66 'B' */  {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x66,0x66,0x66,0x66,0xFC,0x00,0x00,0x00,0x00},
    /* 67 'C' */  {0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xC0,0xC0,0xC2,0x66,0x3C,0x00,0x00,0x00,0x00},
    /* 68 'D' */  {0x00,0x00,0xF8,0x6C,0x66,0x66,0x66,0x66,0x66,0x66,0x6C,0xF8,0x00,0x00,0x00,0x00},
    /* 69 'E' */  {0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00},
    /* 70 'F' */  {0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00},
    /* 71 'G' */  {0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xDE,0xC6,0xC6,0x66,0x3A,0x00,0x00,0x00,0x00},
    /* 72 'H' */  {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
    /* 73 'I' */  {0x00,0x00,0x3C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    /* 74 'J' */  {0x00,0x00,0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0xCC,0xCC,0xCC,0x78,0x00,0x00,0x00,0x00},
    /* 75 'K' */  {0x00,0x00,0xE6,0x66,0x66,0x6C,0x78,0x78,0x6C,0x66,0x66,0xE6,0x00,0x00,0x00,0x00},
    /* 76 'L' */  {0x00,0x00,0xF0,0x60,0x60,0x60,0x60,0x60,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00},
    /* 77 'M' */  {0x00,0x00,0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
    /* 78 'N' */  {0x00,0x00,0xC6,0xE6,0xF6,0xFE,0xDE,0xCE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
    /* 79 'O' */  {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 80 'P' */  {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x60,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00},
    /* 81 'Q' */  {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x0C,0x0E,0x00,0x00},
    /* 82 'R' */  {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x6C,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00},
    /* 83 'S' */  {0x00,0x00,0x7C,0xC6,0xC6,0x60,0x38,0x0C,0x06,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 84 'T' */  {0x00,0x00,0xFF,0xDB,0x99,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    /* 85 'U' */  {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /* 86 'V' */  {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00,0x00,0x00,0x00},
    /* 87 'W' */  {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xD6,0xD6,0xD6,0xFE,0xEE,0x6C,0x00,0x00,0x00,0x00},
    /* 88 'X' */  {0x00,0x00,0xC6,0xC6,0x6C,0x7C,0x38,0x38,0x7C,0x6C,0xC6,0xC6,0x00,0x00,0x00,0x00},
    /* 89 'Y' */  {0x00,0x00,0xC6,0xC6,0xC6,0x6C,0x38,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    /* 90 'Z' */  {0x00,0x00,0xFE,0xC6,0x86,0x0C,0x18,0x30,0x60,0xC2,0xC6,0xFE,0x00,0x00,0x00,0x00},
    /* 91 '[' */  {0x00,0x00,0x3C,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x3C,0x00,0x00,0x00,0x00},
    /* 92 '\' */  {0x00,0x00,0x00,0x80,0xC0,0xE0,0x70,0x38,0x1C,0x0E,0x06,0x02,0x00,0x00,0x00,0x00},
    /* 93 ']' */  {0x00,0x00,0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00,0x00,0x00,0x00},
    /* 94 '^' */  {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 95 '_' */  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00},
    /* 96 '`' */  {0x30,0x30,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 97 'a' */  {0x00,0x00,0x00,0x00,0x00,0x78,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00},
    /* 98 'b' */  {0x00,0x00,0xE0,0x60,0x60,0x78,0x6C,0x66,0x66,0x66,0x66,0x7C,0x00,0x00,0x00,0x00},
    /* 99 'c' */  {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xC0,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /*100 'd' */  {0x00,0x00,0x1C,0x0C,0x0C,0x3C,0x6C,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00},
    /*101 'e' */  {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xFE,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /*102 'f' */  {0x00,0x00,0x1C,0x36,0x32,0x30,0x78,0x30,0x30,0x30,0x30,0x78,0x00,0x00,0x00,0x00},
    /*103 'g' */  {0x00,0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0xCC,0x78,0x00},
    /*104 'h' */  {0x00,0x00,0xE0,0x60,0x60,0x6C,0x76,0x66,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00},
    /*105 'i' */  {0x00,0x00,0x18,0x18,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    /*106 'j' */  {0x00,0x00,0x06,0x06,0x00,0x0E,0x06,0x06,0x06,0x06,0x06,0x06,0x66,0x66,0x3C,0x00},
    /*107 'k' */  {0x00,0x00,0xE0,0x60,0x60,0x66,0x6C,0x78,0x78,0x6C,0x66,0xE6,0x00,0x00,0x00,0x00},
    /*108 'l' */  {0x00,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
    /*109 'm' */  {0x00,0x00,0x00,0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xD6,0xD6,0xC6,0x00,0x00,0x00,0x00},
    /*110 'n' */  {0x00,0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00},
    /*111 'o' */  {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /*112 'p' */  {0x00,0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00},
    /*113 'q' */  {0x00,0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0x0C,0x1E,0x00},
    /*114 'r' */  {0x00,0x00,0x00,0x00,0x00,0xDC,0x76,0x66,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00},
    /*115 's' */  {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00,0x00,0x00,0x00},
    /*116 't' */  {0x00,0x00,0x10,0x30,0x30,0xFC,0x30,0x30,0x30,0x30,0x36,0x1C,0x00,0x00,0x00,0x00},
    /*117 'u' */  {0x00,0x00,0x00,0x00,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00},
    /*118 'v' */  {0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00,0x00,0x00,0x00},
    /*119 'w' */  {0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xD6,0xD6,0xD6,0xFE,0x6C,0x00,0x00,0x00,0x00},
    /*120 'x' */  {0x00,0x00,0x00,0x00,0x00,0xC6,0x6C,0x38,0x38,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00},
    /*121 'y' */  {0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7E,0x06,0x0C,0xF8,0x00},
    /*122 'z' */  {0x00,0x00,0x00,0x00,0x00,0xFE,0xCC,0x18,0x30,0x60,0xC6,0xFE,0x00,0x00,0x00,0x00},
    /*123 '{' */  {0x00,0x00,0x0E,0x18,0x18,0x18,0x70,0x18,0x18,0x18,0x18,0x0E,0x00,0x00,0x00,0x00},
    /*124 '|' */  {0x00,0x00,0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00},
    /*125 '}' */  {0x00,0x00,0x70,0x18,0x18,0x18,0x0E,0x18,0x18,0x18,0x18,0x70,0x00,0x00,0x00,0x00},
    /*126 '~' */  {0x00,0x00,0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

/* ========================================================================
 * Window manager state
 * ======================================================================== */

static fbwm_window_t wm_windows[FBWM_MAX_WINDOWS];
static fbwm_window_t *wm_top;       /* front of z-order list */
static int wm_next_id;
static int wm_active_id;

/* Phase 2–4 compositor state (declared early for wm_init). */
static int dirty_full = 1;
static int dirty_x, dirty_y, dirty_w, dirty_h;
static int cascade_idx;
static char clock_text[16] = "00:00:00";
static int alttab_on;
static int alttab_sel;
static int alttab_ids[FBWM_MAX_WINDOWS];
static int alttab_n;
static int present_mode = FBWM_PRESENT_ATOMIC;
static unsigned int bench_composes;
static unsigned int bench_presents;
static unsigned int bench_pixels;

/* ========================================================================
 * Retained surface pool
 *
 * Fixed-capacity slots avoid malloc on the freestanding target.  Slot 1
 * is sized for a terminal client (~640x400); the rest fit DOOM 320x240
 * and general utility windows.
 * ======================================================================== */

#define FBWM_SURF_POOL_SLOTS 4

static unsigned int surf_pool_0[320 * 240];
static unsigned int surf_pool_1[640 * 400];
static unsigned int surf_pool_2[320 * 240];
static unsigned int surf_pool_3[400 * 300];

static unsigned int *const surf_pool_base[FBWM_SURF_POOL_SLOTS] = {
    surf_pool_0, surf_pool_1, surf_pool_2, surf_pool_3
};
static const int surf_pool_cap_w[FBWM_SURF_POOL_SLOTS] = {
    320, 640, 320, 400
};
static const int surf_pool_cap_h[FBWM_SURF_POOL_SLOTS] = {
    240, 400, 240, 300
};
static int surf_pool_used[FBWM_SURF_POOL_SLOTS];

typedef struct {
    int             win_id;     /* -1 if free */
    int             w, h;
    int             stride;     /* pixels per row */
    unsigned int   *pixels;
    int             pool_slot;  /* -1 if externally owned */
    int             dirty;
    int             dmg_x, dmg_y, dmg_w, dmg_h;
} fbwm_surface_t;

static fbwm_surface_t wm_surfaces[FBWM_MAX_SURFACES];

static void surface_reset_all(void);
static fbwm_surface_t *surface_for_window(int win_id);
static void surface_blit_to_client(fbwm_surface_t *surf,
                                   const fbwm_rect_t *client);

/* Forward decls for dirty tracking / clock used during create/move/draw. */
void wm_dirty_full(void);
void wm_dirty_window(int id);
const char *wm_get_clock_text(void);

/* ========================================================================
 * UART debug helpers
 * ======================================================================== */

void fbwm_uart_putc(char c)
{
    *(volatile int *)FBWM_UART_ADDR = c;
}

void fbwm_uart_puts(const char *s)
{
    while (*s) {
        fbwm_uart_putc(*s);
        s++;
    }
}

/* ========================================================================
 * Internal helpers
 * ======================================================================== */

static int fbwm_strlen(const char *s)
{
    int n = 0;
    while (*s++) n++;
    return n;
}

static void wg_reset_all(void);
static void wg_destroy_for_window(int win_id);

int wm_calc_layout(int x, int y, int w, int h, unsigned int flags,
                   fbwm_layout_t *layout)
{
    int border;

    if (!layout || w <= 0 || h <= 0) return 0;
    border = (flags & FBWM_WIN_BORDER) ? FBWM_BORDER_W : 0;

    layout->outer.x = x;
    layout->outer.y = y;
    layout->outer.w = w;
    layout->outer.h = h;

    layout->titlebar.x = 0;
    layout->titlebar.y = 0;
    layout->titlebar.w = 0;
    layout->titlebar.h = 0;
    if (flags & FBWM_WIN_TITLEBAR) {
        layout->titlebar.x = x + border;
        layout->titlebar.y = y + border;
        layout->titlebar.w = w - border * 2;
        layout->titlebar.h = FBWM_TITLEBAR_H;
    }

    layout->client.x = x + border;
    layout->client.y = y + border +
        ((flags & FBWM_WIN_TITLEBAR) ? FBWM_TITLEBAR_H : 0);
    layout->client.w = w - border * 2;
    layout->client.h = h - border * 2 -
        ((flags & FBWM_WIN_TITLEBAR) ? FBWM_TITLEBAR_H : 0);

    layout->close_button.x = 0;
    layout->close_button.y = 0;
    layout->close_button.w = 0;
    layout->close_button.h = 0;
    if ((flags & FBWM_WIN_TITLEBAR) && (flags & FBWM_WIN_CLOSEBTN)) {
        layout->close_button.x = layout->titlebar.x +
            layout->titlebar.w - FBWM_CLOSEBTN_W - TPOS_WIN_CLOSE_GAP;
        layout->close_button.y = layout->titlebar.y +
            TPOS_WIN_CLOSE_GAP;
        layout->close_button.w = FBWM_CLOSEBTN_W;
        layout->close_button.h = FBWM_CLOSEBTN_H;
    }

    return layout->client.w >= 0 && layout->client.h >= 0 &&
           layout->titlebar.w >= 0;
}

int wm_outer_for_client(int client_w, int client_h, unsigned int flags,
                        fbwm_rect_t *outer)
{
    int border;

    if (!outer || client_w <= 0 || client_h <= 0) return 0;
    border = (flags & FBWM_WIN_BORDER) ? FBWM_BORDER_W : 0;
    outer->x = 0;
    outer->y = 0;
    outer->w = client_w + border * 2;
    outer->h = client_h + border * 2 +
        ((flags & FBWM_WIN_TITLEBAR) ? FBWM_TITLEBAR_H : 0);
    return 1;
}

void wm_center_in_work_area(fbwm_rect_t *rect)
{
    if (!rect) return;
    rect->x = FBWM_WORK_X + (FBWM_WORK_W - rect->w) / 2;
    rect->y = FBWM_WORK_Y + (FBWM_WORK_H - rect->h) / 2;
}

static void wm_min_size(unsigned int flags, int *min_w, int *min_h)
{
    fbwm_rect_t outer;

    wm_outer_for_client(1, 1, flags, &outer);
    *min_w = outer.w;
    *min_h = outer.h;
    if ((flags & FBWM_WIN_TITLEBAR) && (flags & FBWM_WIN_CLOSEBTN) &&
        *min_w < FBWM_CLOSEBTN_W + TPOS_WIN_CLOSE_GAP * 2 +
                 ((flags & FBWM_WIN_BORDER) ? FBWM_BORDER_W * 2 : 0))
        *min_w = FBWM_CLOSEBTN_W + TPOS_WIN_CLOSE_GAP * 2 +
                 ((flags & FBWM_WIN_BORDER) ? FBWM_BORDER_W * 2 : 0);
}

/* Render at most max_chars characters.  This is used for chrome whose text
 * must not paint through adjacent controls. */
static void fb_puts_limited(int x, int y, const char *str, int max_chars,
                            fbwm_color_t fg, fbwm_color_t bg)
{
    while (*str && max_chars > 0) {
        fb_putchar(x, y, *str, fg, bg);
        x += FBWM_FONT_WIDTH;
        str++;
        max_chars--;
    }
}

/* Clamp a rectangle to the screen bounds, return 0 if fully clipped */
static int clip_rect(int *x, int *y, int *w, int *h)
{
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > FBWM_FB_WIDTH)  *w = FBWM_FB_WIDTH  - *x;
    if (*y + *h > FBWM_FB_HEIGHT) *h = FBWM_FB_HEIGHT - *y;
    if (*w <= 0 || *h <= 0) return 0;
    return 1;
}

/* Draw target: simplefb aperture or present scratch (Phase 4 atomic). */
static unsigned int bb_base = FBWM_FB_BASE;

static void fb_set_draw_base(unsigned int addr)
{
    bb_base = addr ? addr : FBWM_FB_BASE;
}

/* Get pointer into the current draw buffer at pixel (x, y) */
static volatile unsigned int *bb_pixel(int x, int y)
{
    return (volatile unsigned int *)(bb_base + (y * FBWM_FB_WIDTH + x) * FBWM_FB_BPP);
}

/* ========================================================================
 * Framebuffer core operations
 * ======================================================================== */

void fb_init(void)
{
    /* Firmware/RTL configures the fixed mode before Linux starts. */
}

/*
 * Clear the back buffer to a solid color.
 * Uses MOVE doubling: write 1 pixel, then __move to double repeatedly.
 */
void fb_clear(fbwm_color_t color)
{
    volatile unsigned int *bb = (volatile unsigned int *)bb_base;
    int total = FBWM_FB_WIDTH * FBWM_FB_HEIGHT;
    int filled, chunk;

    /* Seed first pixel */
    bb[0] = color;
    filled = 1;

    /* Double with MOVE until entire buffer is filled */
    while (filled < total) {
        chunk = filled;
        if (filled + chunk > total)
            chunk = total - filled;
        __move((void *)(bb_base + filled * FBWM_FB_BPP),
               (const void *)bb_base,
               chunk * FBWM_FB_BPP);
        filled += chunk;
    }
}

/*
 * Fill a rectangle in the back buffer.
 * Strategy: fill the first row using MOVE doubling, then __move each
 * subsequent row from the first row.
 */
void fb_fill_rect(int x, int y, int w, int h, fbwm_color_t color)
{
    volatile unsigned int *row0;
    int row_bytes, row;
    int base;
    int filled, chunk;

    if (!clip_rect(&x, &y, &w, &h)) return;

    /* Fill first row via MOVE doubling */
    base = y * FBWM_FB_WIDTH + x;
    row0 = (volatile unsigned int *)(bb_base + base * FBWM_FB_BPP);
    row0[0] = color;

    if (w > 1) {
        filled = 1;
        while (filled < w) {
            chunk = filled;
            if (filled + chunk > w) chunk = w - filled;
            __move((void *)(bb_base + (base + filled) * FBWM_FB_BPP),
                   (const void *)(bb_base + base * FBWM_FB_BPP),
                   chunk * FBWM_FB_BPP);
            filled += chunk;
        }
    }

    /* Copy first row to remaining rows */
    row_bytes = w * FBWM_FB_BPP;
    for (row = 1; row < h; row++) {
        __move((void *)(bb_base + ((y + row) * FBWM_FB_WIDTH + x) * FBWM_FB_BPP),
               (const void *)(bb_base + base * FBWM_FB_BPP),
               row_bytes);
    }
}

/*
 * Draw a 1-pixel outline rectangle.
 */
void fb_draw_rect(int x, int y, int w, int h, fbwm_color_t color)
{
    if (w <= 0 || h <= 0) return;
    fb_hline(x, y, w, color);
    fb_hline(x, y + h - 1, w, color);
    fb_vline(x, y, h, color);
    fb_vline(x + w - 1, y, h, color);
}

/*
 * Copy a rectangle within the back buffer using MOVE2D.
 * This is the key accelerated operation -- a single pair of
 * MOVE2DINIT + MOVE2DALL instructions handles the entire blit.
 */
void fb_copy_rect(int sx, int sy, int dx, int dy, int w, int h)
{
    int row_bytes;
    unsigned int src_addr, dst_addr;
    int trim;

    if (w <= 0 || h <= 0) return;

    /* Clip source and destination together so they continue to describe the
     * same pixels.  The old implementation could form MMIO addresses outside
     * the framebuffer before MOVE2D saw them. */
    if (sx < 0) { trim = -sx; sx = 0; dx += trim; w -= trim; }
    if (dx < 0) { trim = -dx; dx = 0; sx += trim; w -= trim; }
    if (sy < 0) { trim = -sy; sy = 0; dy += trim; h -= trim; }
    if (dy < 0) { trim = -dy; dy = 0; sy += trim; h -= trim; }
    if (sx + w > FBWM_FB_WIDTH)  w = FBWM_FB_WIDTH - sx;
    if (dx + w > FBWM_FB_WIDTH)  w = FBWM_FB_WIDTH - dx;
    if (sy + h > FBWM_FB_HEIGHT) h = FBWM_FB_HEIGHT - sy;
    if (dy + h > FBWM_FB_HEIGHT) h = FBWM_FB_HEIGHT - dy;
    if (w <= 0 || h <= 0) return;

    /* MOVE2D is not a memmove for a horizontally overlapping row. */
    if (sy == dy && dx > sx && dx < sx + w) {
        int row, col;
        for (row = 0; row < h; row++) {
            volatile unsigned int *src = bb_pixel(sx, sy + row);
            volatile unsigned int *dst = bb_pixel(dx, dy + row);
            for (col = w - 1; col >= 0; col--)
                dst[col] = src[col];
        }
        return;
    }

    row_bytes = w * FBWM_FB_BPP;
    src_addr = bb_base + (sy * FBWM_FB_WIDTH + sx) * FBWM_FB_BPP;
    dst_addr = bb_base + (dy * FBWM_FB_WIDTH + dx) * FBWM_FB_BPP;

    /*
     * For overlapping copies where dst is below src, we must copy
     * bottom-to-top. MOVE2D copies top-to-bottom, so adjust the
     * start pointers to the last row and use negative strides.
     */
    if (dy > sy && dy < sy + h) {
        /* Overlapping downward -- copy bottom-to-top */
        src_addr += (h - 1) * FBWM_FB_STRIDE;
        dst_addr += (h - 1) * FBWM_FB_STRIDE;
        __move2dinit(h, -(int)FBWM_FB_STRIDE, -(int)FBWM_FB_STRIDE);
    } else {
        __move2dinit(h, FBWM_FB_STRIDE, FBWM_FB_STRIDE);
    }
    __move2dall(row_bytes, (void *)dst_addr, (const void *)src_addr);
}

/*
 * Draw a horizontal line in the back buffer.
 */
void fb_hline(int x, int y, int w, fbwm_color_t color)
{
    fb_fill_rect(x, y, w, 1, color);
}

/*
 * Draw a vertical line in the back buffer.
 */
void fb_vline(int x, int y, int h, fbwm_color_t color)
{
    volatile unsigned int *p;
    int i;

    if (x < 0 || x >= FBWM_FB_WIDTH) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > FBWM_FB_HEIGHT) h = FBWM_FB_HEIGHT - y;
    if (h <= 0) return;

    p = bb_pixel(x, y);
    for (i = 0; i < h; i++) {
        *p = color;
        p += FBWM_FB_WIDTH;    /* advance one row (in pixels) */
    }
}

/*
 * Draw a Win95-style 3D raised border.
 */
void fb_draw_raised(int x, int y, int w, int h)
{
    /* Top and left highlight */
    fb_hline(x, y, w, FBWM_HILITE);
    fb_hline(x + 1, y + 1, w - 2, FBWM_HILITE);
    fb_vline(x, y, h, FBWM_HILITE);
    fb_vline(x + 1, y + 1, h - 2, FBWM_HILITE);

    /* Bottom and right dark shadow */
    fb_hline(x, y + h - 1, w, FBWM_DKSHADOW);
    fb_hline(x + 1, y + h - 2, w - 2, FBWM_SHADOW);
    fb_vline(x + w - 1, y, h, FBWM_DKSHADOW);
    fb_vline(x + w - 2, y + 1, h - 2, FBWM_SHADOW);
}

/*
 * Draw a Win95-style 3D sunken border.
 */
void fb_draw_sunken(int x, int y, int w, int h)
{
    /* Top and left shadow */
    fb_hline(x, y, w, FBWM_SHADOW);
    fb_hline(x + 1, y + 1, w - 2, FBWM_DKSHADOW);
    fb_vline(x, y, h, FBWM_SHADOW);
    fb_vline(x + 1, y + 1, h - 2, FBWM_DKSHADOW);

    /* Bottom and right highlight */
    fb_hline(x, y + h - 1, w, FBWM_HILITE);
    fb_hline(x + 1, y + h - 2, w - 2, FBWM_HILITE);
    fb_vline(x + w - 1, y, h, FBWM_HILITE);
    fb_vline(x + w - 2, y + 1, h - 2, FBWM_HILITE);
}

/*
 * Scroll the back buffer up by 'lines' pixel rows using MOVE2D.
 * The vacated bottom region is filled with black.
 */
void fb_scroll(int lines)
{
    int rows_to_copy;
    unsigned int src_addr, dst_addr;

    if (lines <= 0) return;
    if (lines >= FBWM_FB_HEIGHT) {
        fb_clear(FBWM_BLACK);
        return;
    }

    rows_to_copy = FBWM_FB_HEIGHT - lines;
    src_addr = bb_base + lines * FBWM_FB_STRIDE;
    dst_addr = bb_base;

    /* MOVE2D the upper portion */
    __move2dinit(rows_to_copy, FBWM_FB_STRIDE, FBWM_FB_STRIDE);
    __move2dall(FBWM_FB_STRIDE, (void *)dst_addr, (const void *)src_addr);

    /* Clear the vacated bottom rows */
    fb_fill_rect(0, rows_to_copy, FBWM_FB_WIDTH, lines, FBWM_BLACK);
}

/*
 * Simplefb is a single, always-visible buffer.  Retain the call boundary so
 * applications do not need separate T4 and hardware rendering paths.
 */
void fb_flip(void)
{
#ifndef FBWM_HOST
	*(volatile unsigned int *)0xA0000014 = 0;
#endif
}

/*
 * Screenshot capture belongs in the emulator/test harness, not the hardware
 * ABI exposed to Linux.
 */
void fb_screenshot(void)
{
}

/* ========================================================================
 * Text rendering (8x16 bitmap font)
 * ======================================================================== */

/*
 * Render a single character at pixel position (x, y) into the back buffer.
 * Each character is 8x16 pixels. Uses direct pixel writes (8 pixels per row
 * is small enough that MOVE overhead would not help).
 */
void fb_putchar(int x, int y, int ch, fbwm_color_t fg, fbwm_color_t bg)
{
    const unsigned char *glyph;
    volatile unsigned int *row_ptr;
    int row, col, first_col, last_col;
    unsigned char bits;
    int idx;

    if (ch < FBWM_FONT_FIRST || ch > FBWM_FONT_LAST)
        ch = ' ';
    idx = ch - FBWM_FONT_FIRST;
    glyph = fbwm_font[idx];

    first_col = x < 0 ? -x : 0;
    last_col = FBWM_FONT_WIDTH;
    if (x + last_col > FBWM_FB_WIDTH)
        last_col = FBWM_FB_WIDTH - x;
    if (first_col >= last_col) return;

    for (row = 0; row < FBWM_FONT_HEIGHT; row++) {
        int py = y + row;
        if (py < 0 || py >= FBWM_FB_HEIGHT) continue;
        bits = glyph[row];
        row_ptr = bb_pixel(x + first_col, py);
        for (col = first_col; col < last_col; col++)
            row_ptr[col - first_col] =
                (bits & (0x80 >> col)) ? fg : bg;
    }
}

/*
 * Render a null-terminated string at pixel position (x, y).
 */
void fb_puts(int x, int y, const char *str, fbwm_color_t fg, fbwm_color_t bg)
{
    while (*str) {
        if (x + FBWM_FONT_WIDTH > 0 && x < FBWM_FB_WIDTH) {
            fb_putchar(x, y, *str, fg, bg);
        }
        x += FBWM_FONT_WIDTH;
        str++;
    }
}

/*
 * Render a string centered within a given width at position (x, y).
 */
void fb_puts_centered(int x, int y, int w, const char *str,
                      fbwm_color_t fg, fbwm_color_t bg)
{
    int text_w = fb_text_width(str);
    int cx = x + (w - text_w) / 2;
    fb_puts(cx, y, str, fg, bg);
}

/*
 * Return the pixel width of a string.
 */
int fb_text_width(const char *str)
{
    return fbwm_strlen(str) * FBWM_FONT_WIDTH;
}

/* ========================================================================
 * Cursor rendering
 * ======================================================================== */

#define FBWM_CURSOR_W  12
#define FBWM_CURSOR_H  16

/* 1 = white fill, 2 = black border, 0 = transparent */
static const unsigned char cursor_bitmap[FBWM_CURSOR_H * FBWM_CURSOR_W] = {
    2,0,0,0,0,0,0,0,0,0,0,0,
    2,2,0,0,0,0,0,0,0,0,0,0,
    2,1,2,0,0,0,0,0,0,0,0,0,
    2,1,1,2,0,0,0,0,0,0,0,0,
    2,1,1,1,2,0,0,0,0,0,0,0,
    2,1,1,1,1,2,0,0,0,0,0,0,
    2,1,1,1,1,1,2,0,0,0,0,0,
    2,1,1,1,1,1,1,2,0,0,0,0,
    2,1,1,1,1,1,1,1,2,0,0,0,
    2,1,1,1,1,1,1,1,1,2,0,0,
    2,1,1,1,1,1,2,2,2,2,2,0,
    2,1,1,2,1,1,2,0,0,0,0,0,
    2,1,2,0,2,1,1,2,0,0,0,0,
    2,2,0,0,2,1,1,2,0,0,0,0,
    2,0,0,0,0,2,1,1,2,0,0,0,
    0,0,0,0,0,2,2,2,2,0,0,0,
};

static unsigned int cursor_save[FBWM_CURSOR_W * FBWM_CURSOR_H];

void fb_save_under_cursor(int x, int y)
{
    volatile unsigned int *bb = (volatile unsigned int *)bb_base;
    int row, col, idx = 0;

    for (row = 0; row < FBWM_CURSOR_H; row++) {
        int py = y + row;
        for (col = 0; col < FBWM_CURSOR_W; col++) {
            int px = x + col;
            if (py >= 0 && py < FBWM_FB_HEIGHT &&
                px >= 0 && px < FBWM_FB_WIDTH)
                cursor_save[idx] = bb[py * FBWM_FB_WIDTH + px];
            else
                cursor_save[idx] = 0;
            idx++;
        }
    }
}

void fb_restore_under_cursor(int x, int y)
{
    volatile unsigned int *bb = (volatile unsigned int *)bb_base;
    int row, col, idx = 0;

    for (row = 0; row < FBWM_CURSOR_H; row++) {
        int py = y + row;
        for (col = 0; col < FBWM_CURSOR_W; col++) {
            int px = x + col;
            if (py >= 0 && py < FBWM_FB_HEIGHT &&
                px >= 0 && px < FBWM_FB_WIDTH)
                bb[py * FBWM_FB_WIDTH + px] = cursor_save[idx];
            idx++;
        }
    }
}

void fb_draw_cursor(int x, int y)
{
    volatile unsigned int *bb = (volatile unsigned int *)bb_base;
    int row, col, idx = 0;

    for (row = 0; row < FBWM_CURSOR_H; row++) {
        int py = y + row;
        for (col = 0; col < FBWM_CURSOR_W; col++) {
            int px = x + col;
            unsigned char pix = cursor_bitmap[idx];
            if (pix && py >= 0 && py < FBWM_FB_HEIGHT &&
                px >= 0 && px < FBWM_FB_WIDTH)
                bb[py * FBWM_FB_WIDTH + px] =
                    pix == 1 ? FBWM_WHITE : FBWM_BLACK;
            idx++;
        }
    }
}

/* ========================================================================
 * Window manager
 * ======================================================================== */

void wm_init(void)
{
    int i;
    for (i = 0; i < FBWM_MAX_WINDOWS; i++) {
        wm_windows[i].id = -1;
        wm_windows[i].flags = 0;
        wm_windows[i].next = (void *)0;
    }
    wm_top = (void *)0;
    wm_next_id = 0;
    wm_active_id = -1;
    surface_reset_all();
    wg_reset_all();
    cascade_idx = 0;
    alttab_on = 0;
    wm_dirty_full();
    fb_set_draw_base(FBWM_FB_BASE);
}

int wm_create(int x, int y, int w, int h, const char *title, unsigned int flags)
{
    int i;
    int min_w, min_h;
    fbwm_window_t *win;

    wm_min_size(flags, &min_w, &min_h);
    if (w < min_w || h < min_h) return -1;

    /* Find a free slot */
    for (i = 0; i < FBWM_MAX_WINDOWS; i++) {
        if (wm_windows[i].id == -1) break;
    }
    if (i >= FBWM_MAX_WINDOWS) return -1;

    win = &wm_windows[i];
    win->id = wm_next_id++;
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->rest_x = x;
    win->rest_y = y;
    win->rest_w = w;
    win->rest_h = h;
    win->min_w = 0;
    win->min_h = 0;
    win->max_w = 0;
    win->max_h = 0;
    win->flags = flags;
    win->title = title;
    win->title_bg = FBWM_TITLEBAR;
    win->title_fg = FBWM_TITLETXT;
    win->body_bg = FBWM_WINBG;

    /* Push to front of z-order list */
    win->next = wm_top;
    wm_top = win;

    wm_active_id = win->id;
    wm_dirty_window(win->id);
    return win->id;
}

void wm_destroy(int id)
{
    fbwm_window_t **pp, *p;

    /* Remove from z-order list */
    pp = &wm_top;
    while (*pp) {
        if ((*pp)->id == id) {
            p = *pp;
            *pp = p->next;
            wm_surface_destroy(id);
            wg_destroy_for_window(id);
            p->id = -1;
            p->flags = 0;
            p->next = (void *)0;
            if (wm_active_id == id)
                wm_active_id = wm_top ? wm_top->id : -1;
            return;
        }
        pp = &((*pp)->next);
    }
}

void wm_clamp_to_work_area(int win_id, int *x, int *y)
{
    fbwm_window_t *win = wm_get(win_id);
    int min_vis_x = 48;
    int max_y;

    if (!win || !x || !y) return;

    /* Keep enough of the window on-screen to grab the titlebar again. */
    if (*x + win->w < min_vis_x)
        *x = min_vis_x - win->w;
    if (*x > FBWM_WORK_W - min_vis_x)
        *x = FBWM_WORK_W - min_vis_x;

    /* Titlebar must stay inside the work area (above the taskbar). */
    if (*y < FBWM_WORK_Y)
        *y = FBWM_WORK_Y;
    max_y = FBWM_WORK_Y + FBWM_WORK_H - FBWM_TITLEBAR_H -
            ((win->flags & FBWM_WIN_BORDER) ? FBWM_BORDER_W : 0);
    if (max_y < FBWM_WORK_Y)
        max_y = FBWM_WORK_Y;
    if (*y > max_y)
        *y = max_y;
}

void wm_move(int id, int x, int y)
{
    fbwm_window_t *win = wm_get(id);
    if (win) {
        wm_dirty_window(id);
        wm_clamp_to_work_area(id, &x, &y);
        win->x = x;
        win->y = y;
        wm_dirty_window(id);
    }
}

static void wm_apply_hints(fbwm_window_t *win, int *w, int *h)
{
    int min_w, min_h;
    wm_min_size(win->flags, &min_w, &min_h);
    if (win->min_w > min_w) min_w = win->min_w;
    if (win->min_h > min_h) min_h = win->min_h;
    if (*w < min_w) *w = min_w;
    if (*h < min_h) *h = min_h;
    if (win->max_w > 0 && *w > win->max_w) *w = win->max_w;
    if (win->max_h > 0 && *h > win->max_h) *h = win->max_h;
}

void wm_resize(int id, int w, int h)
{
    fbwm_window_t *win = wm_get(id);
    if (win) {
        wm_dirty_window(id);
        wm_apply_hints(win, &w, &h);
        win->w = w;
        win->h = h;
        wm_dirty_window(id);
    }
}

void wm_set_size_hints(int id, int min_w, int min_h, int max_w, int max_h)
{
    fbwm_window_t *win = wm_get(id);
    if (!win) return;
    win->min_w = min_w;
    win->min_h = min_h;
    win->max_w = max_w;
    win->max_h = max_h;
    /* Re-clamp current size. */
    {
        int w = win->w, h = win->h;
        wm_apply_hints(win, &w, &h);
        win->w = w;
        win->h = h;
    }
}

void wm_get_size_hints(int id, fbwm_size_hints_t *out)
{
    fbwm_window_t *win = wm_get(id);
    if (!out) return;
    if (!win) {
        out->min_w = out->min_h = out->max_w = out->max_h = 0;
        return;
    }
    out->min_w = win->min_w;
    out->min_h = win->min_h;
    out->max_w = win->max_w;
    out->max_h = win->max_h;
}

void wm_raise(int id)
{
    fbwm_window_t **pp, *win;

    /* Find and unlink */
    pp = &wm_top;
    while (*pp) {
        if ((*pp)->id == id) {
            win = *pp;
            *pp = win->next;
            /* Push to front */
            win->next = wm_top;
            wm_top = win;
            return;
        }
        pp = &((*pp)->next);
    }
}

void wm_set_active(int id)
{
    fbwm_window_t *win;

    if (id == -1) {
        wm_active_id = -1;
        return;
    }
    win = wm_get(id);
    if (win && (win->flags & FBWM_WIN_VISIBLE))
        wm_active_id = id;
}

fbwm_window_t *wm_get(int id)
{
    int i;
    for (i = 0; i < FBWM_MAX_WINDOWS; i++) {
        if (wm_windows[i].id == id) return &wm_windows[i];
    }
    return (void *)0;
}

/*
 * Get the client area rectangle (inside border and titlebar) for a window.
 */
void wm_client_rect(int id, fbwm_rect_t *r)
{
    fbwm_window_t *win = wm_get(id);
    fbwm_layout_t layout;
    if (!win) {
        r->x = r->y = r->w = r->h = 0;
        return;
    }
    if (!wm_calc_layout(win->x, win->y, win->w, win->h,
                        win->flags, &layout)) {
        r->x = r->y = r->w = r->h = 0;
        return;
    }
    *r = layout.client;
}

static void fb_draw_close_mark(const fbwm_rect_t *button,
                               fbwm_color_t color)
{
    int x0 = button->x + (button->w - TPOS_WIN_CLOSE_MARK) / 2;
    int y0 = button->y + (button->h - TPOS_WIN_CLOSE_MARK) / 2;
    int i;

    for (i = 0; i < TPOS_WIN_CLOSE_MARK; i++) {
        int x1 = x0 + i;
        int x2 = x0 + TPOS_WIN_CLOSE_MARK - 1 - i;
        int py = y0 + i;
        if (py >= 0 && py < FBWM_FB_HEIGHT) {
            if (x1 >= 0 && x1 < FBWM_FB_WIDTH)
                *bb_pixel(x1, py) = color;
            if (x2 >= 0 && x2 < FBWM_FB_WIDTH)
                *bb_pixel(x2, py) = color;
        }
    }
}

/*
 * Draw a single window into the back buffer.
 * Chrome first, then the retained client surface (if any).
 */
void wm_draw_window(fbwm_window_t *win)
{
    int is_active;
    fbwm_color_t tbg;
    fbwm_layout_t layout;
    fbwm_surface_t *surf;

    if (!(win->flags & FBWM_WIN_VISIBLE)) return;
    if (win->flags & FBWM_WIN_MINIMIZED) return;
    if (!wm_calc_layout(win->x, win->y, win->w, win->h,
                        win->flags, &layout)) return;

    is_active = (win->id == wm_active_id);

    /* Window body (solid fill under chrome; surface replaces client). */
    fb_fill_rect(win->x, win->y, win->w, win->h, win->body_bg);

    /* 3D border */
    if (win->flags & FBWM_WIN_BORDER) {
        fb_draw_raised(win->x, win->y, win->w, win->h);
    }

    /* Title bar */
    if (win->flags & FBWM_WIN_TITLEBAR) {
        int tx = layout.titlebar.x;
        int ty = layout.titlebar.y;
        int tw = layout.titlebar.w;

        /* Honour the per-window active colour; inactive chrome is shared. */
        tbg = is_active ? win->title_bg : FBWM_TITLE_INACT;

        fb_fill_rect(tx, ty, tw, FBWM_TITLEBAR_H, tbg);

        /* Title text */
        if (win->title) {
            int text_right = tx + tw - 4;
            int max_chars;
            if (win->flags & FBWM_WIN_CLOSEBTN)
                text_right -= FBWM_CLOSEBTN_W + 4;
            max_chars = (text_right - (tx + 4)) / FBWM_FONT_WIDTH;
            if (max_chars > 0)
                fb_puts_limited(tx + 4, ty + 3, win->title, max_chars,
                                win->title_fg, tbg);
        }

        /* Close button */
        if (win->flags & FBWM_WIN_CLOSEBTN) {
            fb_fill_rect(layout.close_button.x, layout.close_button.y,
                         layout.close_button.w, layout.close_button.h,
                         FBWM_TITLE_INACT);
            fb_draw_rect(layout.close_button.x, layout.close_button.y,
                         layout.close_button.w, layout.close_button.h,
                         FBWM_BORDER_CLR);
            fb_draw_close_mark(&layout.close_button, FBWM_TITLETXT);
        }
    }

    /* Retained application content (Phase 1 compositor). */
    surf = surface_for_window(win->id);
    if (surf && surf->pixels)
        surface_blit_to_client(surf, &layout.client);
}

/*
 * Draw all windows, back to front.
 * We walk the z-order list (which is front-to-back) in reverse by
 * collecting into a temporary array, then drawing back-to-front.
 */
void wm_draw_all(void)
{
    fbwm_window_t *stack[FBWM_MAX_WINDOWS];
    fbwm_window_t *p;
    int n = 0;
    int i;

    /* Collect front-to-back into array */
    p = wm_top;
    while (p && n < FBWM_MAX_WINDOWS) {
        stack[n++] = p;
        p = p->next;
    }

    /* Draw back-to-front */
    for (i = n - 1; i >= 0; i--) {
        wm_draw_window(stack[i]);
        if (stack[i]->flags & FBWM_WIN_VISIBLE)
            wg_draw_all(stack[i]->id);
    }
}

/*
 * Draw the desktop background.
 */
void wm_draw_desktop(fbwm_color_t color)
{
    fb_clear(color);
}

/*
 * Draw a taskbar at the bottom of the screen.
 * Shows a "Start" button and lists window titles.
 */
void wm_draw_taskbar(void)
{
    int taskbar_h = FBWM_TASKBAR_H;
    int ty = FBWM_FB_HEIGHT - taskbar_h;
    fbwm_window_t *p;
    int btn_x;

    /* Taskbar background */
    fb_fill_rect(0, ty, FBWM_FB_WIDTH, taskbar_h, FBWM_TASKBAR);
    /* Top edge highlight */
    fb_hline(0, ty, FBWM_FB_WIDTH, FBWM_BORDER_CLR);

    /* Start button */
    fb_fill_rect(TPOS_START_X, ty + TPOS_TASKBAR_PAD,
                 TPOS_START_W, taskbar_h - TPOS_TASKBAR_PAD * 2,
                 FBWM_TITLEBAR);
    fb_draw_rect(TPOS_START_X, ty + TPOS_TASKBAR_PAD,
                 TPOS_START_W, taskbar_h - TPOS_TASKBAR_PAD * 2,
                 FBWM_ACCENT);
    fb_puts(TPOS_START_X + 10, ty + 8, "Start",
            FBWM_WHITE, FBWM_TITLEBAR);

    /* Clock reservation is part of the fixed shell geometry. */
    fb_fill_rect(TPOS_CLOCK_X, ty + TPOS_TASKBAR_PAD,
                 TPOS_CLOCK_W, taskbar_h - TPOS_TASKBAR_PAD * 2,
                 FBWM_SHADOW);
    fb_draw_rect(TPOS_CLOCK_X, ty + TPOS_TASKBAR_PAD,
                 TPOS_CLOCK_W, taskbar_h - TPOS_TASKBAR_PAD * 2,
                 FBWM_BORDER_CLR);
    fb_puts(TPOS_CLOCK_X + 8, ty + 8, wm_get_clock_text(),
            FBWM_TASKTEXT, FBWM_SHADOW);
    fb_vline(TPOS_CLOCK_X - 10, ty + 6, taskbar_h - 12,
             FBWM_BORDER_CLR);

    /* Window buttons in taskbar (including minimized). */
    btn_x = TPOS_START_X + TPOS_START_W + 6;
    p = wm_top;
    while (p) {
        if ((p->flags & FBWM_WIN_VISIBLE) && p->title) {
            int tw = fb_text_width(p->title) + 12;
            int minimized = (p->flags & FBWM_WIN_MINIMIZED) != 0;
            if (tw < 80) tw = 80;
            if (btn_x + tw > TPOS_CLOCK_X - 14)
                tw = TPOS_CLOCK_X - 14 - btn_x;
            if (tw < 32) break;

            fb_fill_rect(btn_x, ty + TPOS_TASKBAR_PAD, tw,
                         taskbar_h - TPOS_TASKBAR_PAD * 2,
                         minimized ? FBWM_SHADOW :
                         (p->id == wm_active_id ? FBWM_HILITE : FBWM_WINBG));
            if (p->id == wm_active_id && !minimized) {
                fb_draw_rect(btn_x, ty + TPOS_TASKBAR_PAD, tw,
                             taskbar_h - TPOS_TASKBAR_PAD * 2,
                             FBWM_ACCENT);
            } else {
                fb_draw_rect(btn_x, ty + TPOS_TASKBAR_PAD, tw,
                             taskbar_h - TPOS_TASKBAR_PAD * 2,
                             FBWM_BORDER_CLR);
            }
            fb_puts_limited(btn_x + 6, ty + 8, p->title,
                            (tw - 12) / FBWM_FONT_WIDTH,
                            p->id == wm_active_id ? FBWM_WHITE : FBWM_TASKTEXT,
                            p->id == wm_active_id ? FBWM_HILITE : FBWM_WINBG);
            btn_x += tw + 2;
        }
        p = p->next;
    }
}

/* ========================================================================
 * Retained surfaces and compositor
 * ======================================================================== */

static void surface_reset_all(void)
{
    int i;
    for (i = 0; i < FBWM_MAX_SURFACES; i++) {
        wm_surfaces[i].win_id = -1;
        wm_surfaces[i].w = 0;
        wm_surfaces[i].h = 0;
        wm_surfaces[i].stride = 0;
        wm_surfaces[i].pixels = (void *)0;
        wm_surfaces[i].pool_slot = -1;
        wm_surfaces[i].dirty = 0;
    }
    for (i = 0; i < FBWM_SURF_POOL_SLOTS; i++)
        surf_pool_used[i] = 0;
}

static fbwm_surface_t *surface_for_window(int win_id)
{
    int i;
    for (i = 0; i < FBWM_MAX_SURFACES; i++) {
        if (wm_surfaces[i].win_id == win_id)
            return &wm_surfaces[i];
    }
    return (void *)0;
}

static fbwm_surface_t *surface_alloc_slot(void)
{
    int i;
    for (i = 0; i < FBWM_MAX_SURFACES; i++) {
        if (wm_surfaces[i].win_id == -1)
            return &wm_surfaces[i];
    }
    return (void *)0;
}

static int surface_pool_claim(int w, int h)
{
    int i;
    for (i = 0; i < FBWM_SURF_POOL_SLOTS; i++) {
        if (!surf_pool_used[i] &&
            surf_pool_cap_w[i] >= w &&
            surf_pool_cap_h[i] >= h) {
            surf_pool_used[i] = 1;
            return i;
        }
    }
    return -1;
}

static void surface_blit_to_client(fbwm_surface_t *surf,
                                   const fbwm_rect_t *client)
{
    int copy_w, copy_h;
    int row, col;
    volatile unsigned int *dst;
    const unsigned int *src;

    if (!surf || !surf->pixels || !client) return;
    if (client->w <= 0 || client->h <= 0) return;

    copy_w = surf->w < client->w ? surf->w : client->w;
    copy_h = surf->h < client->h ? surf->h : client->h;
    if (copy_w <= 0 || copy_h <= 0) return;

    /* Clip destination to the framebuffer. */
    {
        int dx = client->x;
        int dy = client->y;
        int sx0 = 0, sy0 = 0;

        if (dx < 0) { sx0 = -dx; copy_w += dx; dx = 0; }
        if (dy < 0) { sy0 = -dy; copy_h += dy; dy = 0; }
        if (dx + copy_w > FBWM_FB_WIDTH)
            copy_w = FBWM_FB_WIDTH - dx;
        if (dy + copy_h > FBWM_FB_HEIGHT)
            copy_h = FBWM_FB_HEIGHT - dy;
        if (copy_w <= 0 || copy_h <= 0) return;

        for (row = 0; row < copy_h; row++) {
            src = surf->pixels + (sy0 + row) * surf->stride + sx0;
            dst = bb_pixel(dx, dy + row);
            for (col = 0; col < copy_w; col++)
                dst[col] = src[col];
        }
    }
}

int wm_surface_create(int win_id)
{
    fbwm_window_t *win;
    fbwm_rect_t cr;
    fbwm_surface_t *surf;
    int slot;

    win = wm_get(win_id);
    if (!win) return -1;

    wm_client_rect(win_id, &cr);
    if (cr.w <= 0 || cr.h <= 0) return -1;

    /* Replace any existing surface. */
    wm_surface_destroy(win_id);

    surf = surface_alloc_slot();
    if (!surf) return -1;

    slot = surface_pool_claim(cr.w, cr.h);
    if (slot < 0) return -1;

    surf->win_id = win_id;
    surf->w = cr.w;
    surf->h = cr.h;
    surf->stride = cr.w;
    surf->pixels = surf_pool_base[slot];
    surf->pool_slot = slot;
    surf->dirty = 1;
    surf->dmg_x = 0;
    surf->dmg_y = 0;
    surf->dmg_w = cr.w;
    surf->dmg_h = cr.h;

    /* Clear to the window body colour so empty clients are not garbage. */
    wm_surface_clear(win_id, win->body_bg);
    return 0;
}

int wm_surface_attach(int win_id, unsigned int *pixels,
                      int w, int h, int stride)
{
    fbwm_surface_t *surf;

    if (!wm_get(win_id) || !pixels || w <= 0 || h <= 0 || stride < w)
        return -1;

    wm_surface_destroy(win_id);

    surf = surface_alloc_slot();
    if (!surf) return -1;

    surf->win_id = win_id;
    surf->w = w;
    surf->h = h;
    surf->stride = stride;
    surf->pixels = pixels;
    surf->pool_slot = -1;
    wm_surface_damage_all(win_id);
    return 0;
}

void wm_surface_destroy(int win_id)
{
    fbwm_surface_t *surf = surface_for_window(win_id);
    if (!surf) return;
    if (surf->pool_slot >= 0 && surf->pool_slot < FBWM_SURF_POOL_SLOTS)
        surf_pool_used[surf->pool_slot] = 0;
    surf->win_id = -1;
    surf->w = 0;
    surf->h = 0;
    surf->stride = 0;
    surf->pixels = (void *)0;
    surf->pool_slot = -1;
    surf->dirty = 0;
}

unsigned int *wm_surface_pixels(int win_id)
{
    fbwm_surface_t *surf = surface_for_window(win_id);
    return surf ? surf->pixels : (void *)0;
}

int wm_surface_stride(int win_id)
{
    fbwm_surface_t *surf = surface_for_window(win_id);
    return surf ? surf->stride : 0;
}

int wm_surface_width(int win_id)
{
    fbwm_surface_t *surf = surface_for_window(win_id);
    return surf ? surf->w : 0;
}

int wm_surface_height(int win_id)
{
    fbwm_surface_t *surf = surface_for_window(win_id);
    return surf ? surf->h : 0;
}

void wm_surface_damage(int win_id, int x, int y, int w, int h)
{
    fbwm_surface_t *surf = surface_for_window(win_id);
    int x2, y2;

    if (!surf || !surf->pixels || w <= 0 || h <= 0) return;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > surf->w) w = surf->w - x;
    if (y + h > surf->h) h = surf->h - y;
    if (w <= 0 || h <= 0) return;

    if (!surf->dirty) {
        surf->dmg_x = x;
        surf->dmg_y = y;
        surf->dmg_w = w;
        surf->dmg_h = h;
        surf->dirty = 1;
        return;
    }

    /* Union with existing damage. */
    x2 = surf->dmg_x + surf->dmg_w;
    y2 = surf->dmg_y + surf->dmg_h;
    if (x < surf->dmg_x) surf->dmg_x = x;
    if (y < surf->dmg_y) surf->dmg_y = y;
    if (x + w > x2) x2 = x + w;
    if (y + h > y2) y2 = y + h;
    surf->dmg_w = x2 - surf->dmg_x;
    surf->dmg_h = y2 - surf->dmg_y;
}

void wm_surface_damage_all(int win_id)
{
    fbwm_surface_t *surf = surface_for_window(win_id);
    if (!surf || !surf->pixels) return;
    surf->dirty = 1;
    surf->dmg_x = 0;
    surf->dmg_y = 0;
    surf->dmg_w = surf->w;
    surf->dmg_h = surf->h;
}

void wm_surface_clear(int win_id, fbwm_color_t color)
{
    fbwm_surface_t *surf = surface_for_window(win_id);
    int i, n;

    if (!surf || !surf->pixels) return;
    n = surf->stride * surf->h;
    for (i = 0; i < n; i++)
        surf->pixels[i] = color;
    wm_surface_damage_all(win_id);
}

void wm_surface_fill_rect(int win_id, int x, int y, int w, int h,
                          fbwm_color_t color)
{
    fbwm_surface_t *surf = surface_for_window(win_id);
    int row, col;

    if (!surf || !surf->pixels || w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > surf->w) w = surf->w - x;
    if (y + h > surf->h) h = surf->h - y;
    if (w <= 0 || h <= 0) return;

    for (row = 0; row < h; row++) {
        unsigned int *p = surf->pixels + (y + row) * surf->stride + x;
        for (col = 0; col < w; col++)
            p[col] = color;
    }
    wm_surface_damage(win_id, x, y, w, h);
}

void wm_surface_puts(int win_id, int x, int y, const char *str,
                     fbwm_color_t fg, fbwm_color_t bg)
{
    fbwm_surface_t *surf = surface_for_window(win_id);
    int start_x = x;

    if (!surf || !surf->pixels || !str) return;

    while (*str) {
        int ch = *str++;
        const unsigned char *glyph;
        int row, col, idx;

        if (ch < FBWM_FONT_FIRST || ch > FBWM_FONT_LAST)
            ch = ' ';
        idx = ch - FBWM_FONT_FIRST;
        glyph = fbwm_font[idx];

        for (row = 0; row < FBWM_FONT_HEIGHT; row++) {
            int py = y + row;
            unsigned char bits;
            if (py < 0 || py >= surf->h) continue;
            bits = glyph[row];
            for (col = 0; col < FBWM_FONT_WIDTH; col++) {
                int px = x + col;
                if (px < 0 || px >= surf->w) continue;
                surf->pixels[py * surf->stride + px] =
                    (bits & (0x80 >> col)) ? fg : bg;
            }
        }
        x += FBWM_FONT_WIDTH;
    }
    wm_surface_damage(win_id, start_x, y, x - start_x, FBWM_FONT_HEIGHT);
}

static fbwm_desktop_painter_t desktop_painter;

void fbwm_set_desktop_painter(fbwm_desktop_painter_t painter)
{
    desktop_painter = painter;
}

/* ---- Dirty region, cascade, clock, Alt-Tab, present (Phases 2–4) ---- */

void wm_dirty_full(void)
{
    dirty_full = 1;
    dirty_x = 0;
    dirty_y = 0;
    dirty_w = FBWM_FB_WIDTH;
    dirty_h = FBWM_FB_HEIGHT;
}

void wm_dirty_add(int x, int y, int w, int h)
{
    int x2, y2;
    if (w <= 0 || h <= 0) return;
    if (dirty_full) return;
    if (dirty_w <= 0 || dirty_h <= 0) {
        dirty_x = x; dirty_y = y; dirty_w = w; dirty_h = h;
        return;
    }
    x2 = dirty_x + dirty_w;
    y2 = dirty_y + dirty_h;
    if (x < dirty_x) dirty_x = x;
    if (y < dirty_y) dirty_y = y;
    if (x + w > x2) x2 = x + w;
    if (y + h > y2) y2 = y + h;
    dirty_w = x2 - dirty_x;
    dirty_h = y2 - dirty_y;
    if (dirty_w >= FBWM_FB_WIDTH && dirty_h >= FBWM_FB_HEIGHT)
        dirty_full = 1;
}

void wm_dirty_window(int id)
{
    fbwm_window_t *win = wm_get(id);
    if (!win) {
        wm_dirty_full();
        return;
    }
    wm_dirty_add(win->x, win->y, win->w, win->h);
}

int wm_dirty_pending(void)
{
    return dirty_full || (dirty_w > 0 && dirty_h > 0);
}

void wm_place_cascade(fbwm_rect_t *outer)
{
    int step = 28;
    int n;
    if (!outer) return;
    n = cascade_idx % 10;
    outer->x = FBWM_WORK_X + 36 + n * step;
    outer->y = FBWM_WORK_Y + 36 + n * step;
    if (outer->x + outer->w > FBWM_WORK_W)
        outer->x = FBWM_WORK_X + 20;
    if (outer->y + outer->h > FBWM_WORK_H)
        outer->y = FBWM_WORK_Y + 20;
    cascade_idx++;
}

void wm_set_clock_text(const char *text)
{
    int i;
    if (!text) text = "00:00:00";
    for (i = 0; i < 15 && text[i]; i++)
        clock_text[i] = text[i];
    clock_text[i] = 0;
    wm_dirty_add(TPOS_CLOCK_X, FBWM_WORK_H, TPOS_CLOCK_W, FBWM_TASKBAR_H);
}

const char *wm_get_clock_text(void)
{
    return clock_text;
}

static void alttab_collect(void)
{
    fbwm_window_t *p = wm_top;
    alttab_n = 0;
    while (p && alttab_n < FBWM_MAX_WINDOWS) {
        if ((p->flags & FBWM_WIN_VISIBLE) &&
            !(p->flags & FBWM_WIN_MINIMIZED))
            alttab_ids[alttab_n++] = p->id;
        p = p->next;
    }
}

void wm_alttab_begin(void)
{
    int i;
    alttab_collect();
    alttab_on = alttab_n > 0;
    alttab_sel = 0;
    for (i = 0; i < alttab_n; i++)
        if (alttab_ids[i] == wm_active_id) {
            alttab_sel = i;
            break;
        }
    if (alttab_on)
        alttab_sel = (alttab_sel + 1) % alttab_n;
    wm_dirty_full();
}

int wm_alttab_step(int dir)
{
    if (!alttab_on || alttab_n <= 0) return -1;
    if (dir >= 0)
        alttab_sel = (alttab_sel + 1) % alttab_n;
    else
        alttab_sel = (alttab_sel + alttab_n - 1) % alttab_n;
    wm_dirty_full();
    return alttab_ids[alttab_sel];
}

void wm_alttab_commit(void)
{
    if (alttab_on && alttab_n > 0) {
        int id = alttab_ids[alttab_sel];
        if (wm_is_minimized(id))
            wm_restore(id);
        wm_set_active(id);
        wm_raise(id);
    }
    alttab_on = 0;
    wm_dirty_full();
}

void wm_alttab_cancel(void)
{
    alttab_on = 0;
    wm_dirty_full();
}

int wm_alttab_is_active(void)
{
    return alttab_on;
}

void wm_draw_alttab_overlay(void)
{
    int box_w = 360, box_h, bx, by, i;
    if (!alttab_on || alttab_n <= 0) return;
    box_h = 24 + alttab_n * 22;
    if (box_h > FBWM_WORK_H - 40) box_h = FBWM_WORK_H - 40;
    bx = (FBWM_FB_WIDTH - box_w) / 2;
    by = (FBWM_WORK_H - box_h) / 2;
    fb_fill_rect(bx, by, box_w, box_h, FBWM_SHADOW);
    fb_draw_rect(bx, by, box_w, box_h, FBWM_ACCENT);
    fb_puts(bx + 12, by + 4, "Switch Window", FBWM_TITLETXT, FBWM_SHADOW);
    for (i = 0; i < alttab_n; i++) {
        fbwm_window_t *w = wm_get(alttab_ids[i]);
        int iy = by + 24 + i * 22;
        fbwm_color_t bg = (i == alttab_sel) ? FBWM_HILITE : FBWM_SHADOW;
        fbwm_color_t fg = (i == alttab_sel) ? FBWM_WHITE : FBWM_TASKTEXT;
        if (iy + 18 > by + box_h) break;
        fb_fill_rect(bx + 4, iy, box_w - 8, 20, bg);
        if (w && w->title)
            fb_puts(bx + 12, iy + 2, w->title, fg, bg);
    }
}

void wm_compose(fbwm_color_t desktop_color)
{
    bench_composes++;
    if (desktop_painter)
        desktop_painter();
    else
        wm_draw_desktop(desktop_color);
    wm_draw_all();
    wm_draw_taskbar();
    if (alttab_on)
        wm_draw_alttab_overlay();
    dirty_full = 0;
    dirty_w = dirty_h = 0;
}

void wm_compose_dirty(fbwm_color_t desktop_color)
{
    /* Full compose when dirty; region tracking still avoids no-op redraws. */
    if (!wm_dirty_pending())
        return;
    wm_compose(desktop_color);
}

void wm_present(void)
{
    fb_flip();
    bench_presents++;
}

void fbwm_set_present_mode(int mode)
{
    present_mode = (mode == FBWM_PRESENT_ATOMIC) ? FBWM_PRESENT_ATOMIC
                                                 : FBWM_PRESENT_DIRECT;
}

int fbwm_get_present_mode(void)
{
    return present_mode;
}

void fbwm_bench_reset(void)
{
    bench_composes = 0;
    bench_presents = 0;
    bench_pixels = 0;
}

unsigned int fbwm_bench_composes(void) { return bench_composes; }
unsigned int fbwm_bench_presents(void) { return bench_presents; }
unsigned int fbwm_bench_pixels_copied(void) { return bench_pixels; }

static void present_copy_scratch_to_fb(void)
{
    volatile unsigned int *src =
        (volatile unsigned int *)(unsigned long)TPOS_SCRATCH_BASE;
    volatile unsigned int *dst =
        (volatile unsigned int *)(unsigned long)FBWM_FB_BASE;
    int n = FBWM_FB_WIDTH * FBWM_FB_HEIGHT;
    int i;
    for (i = 0; i < n; i++)
        dst[i] = src[i];
    bench_pixels += (unsigned int)n;
}

void fbwm_present_scene(void)
{
    if (present_mode == FBWM_PRESENT_ATOMIC) {
        fb_set_draw_base(TPOS_SCRATCH_BASE);
        wm_compose_dirty(FBWM_DESKTOP);
        /* Cursor is drawn on the visible aperture after the atomic copy. */
        present_copy_scratch_to_fb();
        fb_set_draw_base(FBWM_FB_BASE);
        fb_save_under_cursor(fbwm_mouse_x, fbwm_mouse_y);
        fb_draw_cursor(fbwm_mouse_x, fbwm_mouse_y);
        fb_flip();
        fb_restore_under_cursor(fbwm_mouse_x, fbwm_mouse_y);
        bench_presents++;
    } else {
        fb_set_draw_base(FBWM_FB_BASE);
        wm_compose_dirty(FBWM_DESKTOP);
        fbwm_flip();
        bench_presents++;
    }
}

/*
 * Publish the current WM window stack and surface pixels into the
 * cross-process SHM bank so a spawned guest can recompose the scene.
 */
void wm_shm_publish_all(void)
{
    fbwm_window_t *stack[FBWM_MAX_WINDOWS];
    fbwm_window_t *p;
    int n = 0;
    int i;

    tpos_shm_init();
    tpos_shm_reset();
    tpos_shm_hdr()->desktop_color = FBWM_DESKTOP;
    tpos_shm_hdr()->active_id = wm_active_id;

    /* Collect front-to-back, publish in reverse so z ends front-first. */
    p = wm_top;
    while (p && n < FBWM_MAX_WINDOWS) {
        stack[n++] = p;
        p = p->next;
    }

    for (i = n - 1; i >= 0; i--) {
        fbwm_window_t *win = stack[i];
        fbwm_surface_t *surf = surface_for_window(win->id);
        fbwm_rect_t cr;
        int slot = -1;
        int sw = 0, sh = 0, ss = 0;

        if (!(win->flags & FBWM_WIN_VISIBLE))
            continue;
        if (win->flags & FBWM_WIN_MINIMIZED)
            continue;

        wm_client_rect(win->id, &cr);
        if (surf && surf->pixels && cr.w > 0 && cr.h > 0) {
            slot = tpos_shm_claim_slot(cr.w, cr.h);
            if (slot >= 0) {
                tpos_shm_blit_to_slot(slot, surf->pixels, surf->stride,
                                     surf->w, surf->h);
                sw = surf->w < tpos_shm_slot_cap_w(slot)
                         ? surf->w : tpos_shm_slot_cap_w(slot);
                sh = surf->h < tpos_shm_slot_cap_h(slot)
                         ? surf->h : tpos_shm_slot_cap_h(slot);
                ss = tpos_shm_slot_cap_w(slot);
            }
        }

        tpos_shm_publish_win(win->id, win->x, win->y, win->w, win->h,
                             win->flags, slot, sw, sh, ss,
                             win->title, 1);
    }
    tpos_shm_set_active(wm_active_id);
}

int wm_shm_prepare_guest(int win_id, int client_w, int client_h,
                         const char *title)
{
    fbwm_rect_t outer;
    int slot;

    tpos_shm_init();
    if (!wm_outer_for_client(client_w, client_h, FBWM_WIN_DEFAULT, &outer))
        return -1;
    wm_center_in_work_area(&outer);

    slot = tpos_shm_claim_slot(client_w, client_h);
    if (slot < 0) return -1;

    /* Clear guest surface. */
    {
        unsigned int *px = tpos_shm_slot_pixels(slot);
        int n = tpos_shm_slot_cap_w(slot) * tpos_shm_slot_cap_h(slot);
        int i;
        if (px)
            for (i = 0; i < n; i++)
                px[i] = FBWM_BLACK;
    }

    tpos_shm_publish_win(win_id, outer.x, outer.y, outer.w, outer.h,
                         FBWM_WIN_DEFAULT, slot, client_w, client_h,
                         tpos_shm_slot_cap_w(slot), title, 1);
    tpos_shm_set_active(win_id);
    return slot;
}

/* ========================================================================
 * Phase 1: Event system, hit-testing, window drag, focus management
 * ======================================================================== */

/* Global mouse state */
int fbwm_mouse_x = 400;
int fbwm_mouse_y = 300;
int fbwm_mouse_buttons = 0;

/* Drag / resize state */
int wm_dragging = -1;
int wm_drag_ox = 0;
int wm_drag_oy = 0;
int wm_resizing = -1;
int wm_resize_edge = 0;

/* Previous mouse buttons (for edge detection) */
static int prev_buttons = 0;

/*
 * Classify a border hit into one of eight resize edges/corners.
 */
static int resize_edge_at(fbwm_window_t *p, int x, int y)
{
    int on_w = x < p->x + FBWM_BORDER_W;
    int on_e = x >= p->x + p->w - FBWM_BORDER_W;
    int on_n = y < p->y + FBWM_BORDER_W;
    int on_s = y >= p->y + p->h - FBWM_BORDER_W;
    int corner = FBWM_BORDER_W + 6;

    if (on_n && on_w) return FBWM_HIT_RESIZE_NW;
    if (on_n && on_e) return FBWM_HIT_RESIZE_NE;
    if (on_s && on_w) return FBWM_HIT_RESIZE_SW;
    if (on_s && on_e) return FBWM_HIT_RESIZE_SE;
    /* Near-corner preference when only one axis is on the border strip. */
    if (on_n && x < p->x + corner) return FBWM_HIT_RESIZE_NW;
    if (on_n && x >= p->x + p->w - corner) return FBWM_HIT_RESIZE_NE;
    if (on_s && x < p->x + corner) return FBWM_HIT_RESIZE_SW;
    if (on_s && x >= p->x + p->w - corner) return FBWM_HIT_RESIZE_SE;
    if (on_w && y < p->y + corner) return FBWM_HIT_RESIZE_NW;
    if (on_w && y >= p->y + p->h - corner) return FBWM_HIT_RESIZE_SW;
    if (on_e && y < p->y + corner) return FBWM_HIT_RESIZE_NE;
    if (on_e && y >= p->y + p->h - corner) return FBWM_HIT_RESIZE_SE;
    if (on_n) return FBWM_HIT_RESIZE_N;
    if (on_s) return FBWM_HIT_RESIZE_S;
    if (on_w) return FBWM_HIT_RESIZE_W;
    if (on_e) return FBWM_HIT_RESIZE_E;
    return FBWM_HIT_BORDER;
}

/*
 * wm_hit_test_ex — window/region at (x,y) with resize edge detail.
 */
int wm_hit_test_ex(int x, int y, int *region)
{
    fbwm_window_t *p = wm_top;

    /* Taskbar strip */
    if (y >= FBWM_WORK_H) {
        *region = FBWM_HIT_TASKBAR;
        return -1;
    }

    while (p) {
        if (!(p->flags & FBWM_WIN_VISIBLE) ||
            (p->flags & FBWM_WIN_MINIMIZED)) {
            p = p->next;
            continue;
        }

        if (x >= p->x && x < p->x + p->w &&
            y >= p->y && y < p->y + p->h) {
            fbwm_layout_t layout;

            if (!wm_calc_layout(p->x, p->y, p->w, p->h,
                                p->flags, &layout)) {
                p = p->next;
                continue;
            }

            if ((p->flags & FBWM_WIN_CLOSEBTN) && (p->flags & FBWM_WIN_TITLEBAR)) {
                if (x >= layout.close_button.x &&
                    x < layout.close_button.x + layout.close_button.w &&
                    y >= layout.close_button.y &&
                    y < layout.close_button.y + layout.close_button.h) {
                    *region = FBWM_HIT_CLOSEBTN;
                    return p->id;
                }
            }

            if (p->flags & FBWM_WIN_BORDER) {
                if (x < p->x + FBWM_BORDER_W ||
                    x >= p->x + p->w - FBWM_BORDER_W ||
                    y < p->y + FBWM_BORDER_W ||
                    y >= p->y + p->h - FBWM_BORDER_W) {
                    *region = resize_edge_at(p, x, y);
                    return p->id;
                }
            }

            if (p->flags & FBWM_WIN_TITLEBAR) {
                if (x >= layout.titlebar.x &&
                    x < layout.titlebar.x + layout.titlebar.w &&
                    y >= layout.titlebar.y &&
                    y < layout.titlebar.y + layout.titlebar.h) {
                    *region = FBWM_HIT_TITLEBAR;
                    return p->id;
                }
            }

            *region = FBWM_HIT_CLIENT;
            return p->id;
        }
        p = p->next;
    }

    *region = FBWM_HIT_DESKTOP;
    return -1;
}

int wm_hit_test(int x, int y, int *region)
{
    int id = wm_hit_test_ex(x, y, region);
    if (*region >= FBWM_HIT_RESIZE_N && *region <= FBWM_HIT_RESIZE_SW)
        *region = FBWM_HIT_BORDER;
    if (*region == FBWM_HIT_TASKBAR)
        *region = FBWM_HIT_DESKTOP;
    return id;
}

void wm_minimize(int id)
{
    fbwm_window_t *win = wm_get(id);
    if (!win || (win->flags & FBWM_WIN_MINIMIZED)) return;
    win->rest_x = win->x;
    win->rest_y = win->y;
    win->rest_w = win->w;
    win->rest_h = win->h;
    win->flags |= FBWM_WIN_MINIMIZED;
    if (wm_active_id == id) {
        fbwm_window_t *p = wm_top;
        wm_active_id = -1;
        while (p) {
            if (!(p->flags & FBWM_WIN_MINIMIZED) &&
                (p->flags & FBWM_WIN_VISIBLE)) {
                wm_active_id = p->id;
                break;
            }
            p = p->next;
        }
    }
}

void wm_restore(int id)
{
    fbwm_window_t *win = wm_get(id);
    if (!win) return;
    if (win->flags & FBWM_WIN_MINIMIZED) {
        win->flags &= ~FBWM_WIN_MINIMIZED;
        win->x = win->rest_x;
        win->y = win->rest_y;
        win->w = win->rest_w;
        win->h = win->rest_h;
    } else if (win->flags & FBWM_WIN_MAXIMIZED) {
        win->flags &= ~FBWM_WIN_MAXIMIZED;
        win->x = win->rest_x;
        win->y = win->rest_y;
        win->w = win->rest_w;
        win->h = win->rest_h;
    }
    wm_set_active(id);
    wm_raise(id);
}

void wm_maximize(int id)
{
    fbwm_window_t *win = wm_get(id);
    if (!win) return;
    if (win->flags & FBWM_WIN_MAXIMIZED) {
        wm_restore(id);
        return;
    }
    if (!(win->flags & FBWM_WIN_MINIMIZED)) {
        win->rest_x = win->x;
        win->rest_y = win->y;
        win->rest_w = win->w;
        win->rest_h = win->h;
    }
    win->flags &= ~FBWM_WIN_MINIMIZED;
    win->flags |= FBWM_WIN_MAXIMIZED;
    win->x = FBWM_WORK_X;
    win->y = FBWM_WORK_Y;
    win->w = FBWM_WORK_W;
    win->h = FBWM_WORK_H;
    wm_set_active(id);
    wm_raise(id);
}

int wm_is_minimized(int id)
{
    fbwm_window_t *win = wm_get(id);
    return win && (win->flags & FBWM_WIN_MINIMIZED) ? 1 : 0;
}

int wm_taskbar_hit(int x, int y)
{
    int ty = FBWM_FB_HEIGHT - FBWM_TASKBAR_H;
    int btn_x;
    fbwm_window_t *p;

    if (y < ty) return -1;
    btn_x = TPOS_START_X + TPOS_START_W + 6;
    p = wm_top;
    while (p) {
        if ((p->flags & FBWM_WIN_VISIBLE) && p->title) {
            int tw = fb_text_width(p->title) + 12;
            if (tw < 80) tw = 80;
            if (btn_x + tw > TPOS_CLOCK_X - 14)
                tw = TPOS_CLOCK_X - 14 - btn_x;
            if (tw < 32) break;
            if (x >= btn_x && x < btn_x + tw)
                return p->id;
            btn_x += tw + 2;
        }
        p = p->next;
    }
    return -1;
}

int wm_focus_cycle(int dir)
{
    fbwm_window_t *stack[FBWM_MAX_WINDOWS];
    fbwm_window_t *p = wm_top;
    int n = 0, i, cur = -1;

    while (p && n < FBWM_MAX_WINDOWS) {
        if ((p->flags & FBWM_WIN_VISIBLE) &&
            !(p->flags & FBWM_WIN_MINIMIZED))
            stack[n++] = p;
        p = p->next;
    }
    if (n == 0) return -1;
    for (i = 0; i < n; i++)
        if (stack[i]->id == wm_active_id) cur = i;
    if (dir >= 0) {
        cur = (cur < 0) ? 0 : (cur + 1) % n;
    } else {
        cur = (cur < 0) ? 0 : (cur + n - 1) % n;
    }
    wm_set_active(stack[cur]->id);
    wm_raise(stack[cur]->id);
    return stack[cur]->id;
}

/* Apply an interactive resize for edge, given new pointer position. */
static void wm_apply_resize(int id, int edge, int mx, int my)
{
    fbwm_window_t *win = wm_get(id);
    int min_w, min_h;
    int x, y, w, h;
    int right, bottom;

    if (!win) return;
    wm_min_size(win->flags, &min_w, &min_h);
    if (win->min_w > min_w) min_w = win->min_w;
    if (win->min_h > min_h) min_h = win->min_h;
    x = win->x; y = win->y; w = win->w; h = win->h;
    right = x + w;
    bottom = y + h;

    if (edge == FBWM_HIT_RESIZE_E || edge == FBWM_HIT_RESIZE_NE ||
        edge == FBWM_HIT_RESIZE_SE)
        right = mx;
    if (edge == FBWM_HIT_RESIZE_W || edge == FBWM_HIT_RESIZE_NW ||
        edge == FBWM_HIT_RESIZE_SW)
        x = mx;
    if (edge == FBWM_HIT_RESIZE_S || edge == FBWM_HIT_RESIZE_SE ||
        edge == FBWM_HIT_RESIZE_SW)
        bottom = my;
    if (edge == FBWM_HIT_RESIZE_N || edge == FBWM_HIT_RESIZE_NE ||
        edge == FBWM_HIT_RESIZE_NW)
        y = my;

    w = right - x;
    h = bottom - y;
    if (w < min_w) {
        if (edge == FBWM_HIT_RESIZE_W || edge == FBWM_HIT_RESIZE_NW ||
            edge == FBWM_HIT_RESIZE_SW)
            x = right - min_w;
        w = min_w;
    }
    if (h < min_h) {
        if (edge == FBWM_HIT_RESIZE_N || edge == FBWM_HIT_RESIZE_NE ||
            edge == FBWM_HIT_RESIZE_NW)
            y = bottom - min_h;
        h = min_h;
    }
    if (win->max_w > 0 && w > win->max_w) w = win->max_w;
    if (win->max_h > 0 && h > win->max_h) h = win->max_h;
    if (y + h > FBWM_WORK_H)
        h = FBWM_WORK_H - y;
    if (h < min_h) h = min_h;
    wm_dirty_window(id);
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->flags &= ~FBWM_WIN_MAXIMIZED;
    wm_dirty_window(id);
}

/*
 * fbwm_flip — composited flip with cursor overlay.
 */
void fbwm_flip(void)
{
    fb_save_under_cursor(fbwm_mouse_x, fbwm_mouse_y);
    fb_draw_cursor(fbwm_mouse_x, fbwm_mouse_y);
    fb_flip();
    fb_restore_under_cursor(fbwm_mouse_x, fbwm_mouse_y);
}

/*
 * fbwm_redraw — full scene compose + present (respects present mode).
 */
void fbwm_redraw(void)
{
    wm_dirty_full();
    fbwm_present_scene();
}

/*
 * fbwm_poll_event — poll keyboard/mouse, handle drag, return event.
 * Returns event type (0 = no event).
 */
int fbwm_poll_event(fbwm_event_t *evt)
{
    int buttons, dx, dy;
    int moved = 0;
    int total_dx = 0, total_dy = 0;

    evt->type = FBWM_EVT_NONE;
    evt->key = 0;
    evt->win_id = -1;

    /* ---- Mouse ---- */
    while (fb_mouse_available()) {
        if (fb_mouse_read_packet(&buttons, &dx, &dy)) {
            fbwm_mouse_x += dx;
            fbwm_mouse_y += dy;
            total_dx += dx;
            total_dy += dy;
            /* Clamp to screen */
            if (fbwm_mouse_x < 0) fbwm_mouse_x = 0;
            if (fbwm_mouse_y < 0) fbwm_mouse_y = 0;
            if (fbwm_mouse_x >= FBWM_FB_WIDTH)  fbwm_mouse_x = FBWM_FB_WIDTH - 1;
            if (fbwm_mouse_y >= FBWM_FB_HEIGHT) fbwm_mouse_y = FBWM_FB_HEIGHT - 1;

            fbwm_mouse_buttons = buttons;
            moved = 1;
        }
    }

    /* Detect button press/release edges */
    {
        int pressed  = fbwm_mouse_buttons & ~prev_buttons;
        int released = prev_buttons & ~fbwm_mouse_buttons;

        /* Left button pressed */
        if (pressed & 1) {
            int region;
            int wid = wm_hit_test_ex(fbwm_mouse_x, fbwm_mouse_y, &region);
            int tb;

            /* Taskbar button activation / restore. */
            tb = wm_taskbar_hit(fbwm_mouse_x, fbwm_mouse_y);
            if (tb >= 0) {
                if (wm_is_minimized(tb))
                    wm_restore(tb);
                else if (tb == wm_active_id)
                    wm_minimize(tb);
                else {
                    wm_set_active(tb);
                    wm_raise(tb);
                }
                fbwm_redraw();
                evt->type = FBWM_EVT_WIN_FOCUS;
                evt->win_id = tb;
                prev_buttons = fbwm_mouse_buttons;
                return evt->type;
            }

            if (wid >= 0) {
                /* Focus + raise */
                if (wid != wm_active_id) {
                    wm_set_active(wid);
                    wm_raise(wid);
                    fbwm_redraw();
                    evt->type = FBWM_EVT_WIN_FOCUS;
                    evt->win_id = wid;
                    prev_buttons = fbwm_mouse_buttons;
                    return evt->type;
                }

                if (region == FBWM_HIT_CLOSEBTN) {
                    evt->type = FBWM_EVT_WIN_CLOSE;
                    evt->win_id = wid;
                    prev_buttons = fbwm_mouse_buttons;
                    return evt->type;
                }

                if (region == FBWM_HIT_TITLEBAR) {
                    fbwm_window_t *w = wm_get(wid);
                    if (w) {
                        wm_dragging = wid;
                        wm_drag_ox = fbwm_mouse_x - w->x;
                        wm_drag_oy = fbwm_mouse_y - w->y;
                    }
                }

                if (region >= FBWM_HIT_RESIZE_N &&
                    region <= FBWM_HIT_RESIZE_SW) {
                    wm_resizing = wid;
                    wm_resize_edge = region;
                }

                evt->type = FBWM_EVT_MOUSE_DOWN;
                evt->win_id = wid;
                evt->mx = fbwm_mouse_x;
                evt->my = fbwm_mouse_y;
                evt->mbuttons = fbwm_mouse_buttons;
            } else {
                evt->type = FBWM_EVT_MOUSE_DOWN;
                evt->win_id = -1;
                evt->mx = fbwm_mouse_x;
                evt->my = fbwm_mouse_y;
            }
            prev_buttons = fbwm_mouse_buttons;
            return evt->type;
        }

        /* Left button released */
        if (released & 1) {
            if (wm_dragging >= 0) {
                evt->type = FBWM_EVT_WIN_MOVE;
                evt->win_id = wm_dragging;
                wm_dragging = -1;
                prev_buttons = fbwm_mouse_buttons;
                return evt->type;
            }
            if (wm_resizing >= 0) {
                evt->type = FBWM_EVT_WIN_RESIZE;
                evt->win_id = wm_resizing;
                wm_resizing = -1;
                prev_buttons = fbwm_mouse_buttons;
                return evt->type;
            }
            evt->type = FBWM_EVT_MOUSE_UP;
            evt->mx = fbwm_mouse_x;
            evt->my = fbwm_mouse_y;
            evt->mbuttons = fbwm_mouse_buttons;
            prev_buttons = fbwm_mouse_buttons;
            return evt->type;
        }

        prev_buttons = fbwm_mouse_buttons;
    }

    /* Handle active drag */
    if (wm_dragging >= 0 && moved) {
        int nx = fbwm_mouse_x - wm_drag_ox;
        int ny = fbwm_mouse_y - wm_drag_oy;
        wm_move(wm_dragging, nx, ny);
        fbwm_redraw();
    }

    /* Handle active resize */
    if (wm_resizing >= 0 && moved) {
        wm_apply_resize(wm_resizing, wm_resize_edge,
                        fbwm_mouse_x, fbwm_mouse_y);
        fbwm_redraw();
    }

    /* Mouse motion event (no drag) */
    if (moved && wm_dragging < 0) {
        evt->type = FBWM_EVT_MOUSE_MOVE;
        evt->mx = fbwm_mouse_x;
        evt->my = fbwm_mouse_y;
        evt->mdx = total_dx;
        evt->mdy = total_dy;
        /* Just redraw cursor */
        fbwm_flip();
        return evt->type;
    }

    /* ---- Keyboard ---- */
    if (fb_kbd_available()) {
        int ch = fb_kbd_read();
        evt->type = FBWM_EVT_KEYDOWN;
        evt->key = ch;
        evt->win_id = wm_active_id;
        return evt->type;
    }

    return FBWM_EVT_NONE;
}

/*
 * fbwm_run — main event loop with callback.
 */
void fbwm_run(fbwm_event_handler_t handler, void *userdata)
{
    fbwm_event_t evt;

    for (;;) {
        int type = fbwm_poll_event(&evt);

        if (type != FBWM_EVT_NONE) {
            if (handler(&evt, userdata))
                return;
        } else {
            /* Small delay to avoid busy-spin */
            volatile int d;
            for (d = 0; d < 5000; d++) ;
        }
    }
}

/* ========================================================================
 * Phase 2: Widget system
 * ======================================================================== */

static fbwm_widget_t wg_widgets[FBWM_MAX_WIDGETS];
static int wg_next_id = 0;
static int wg_focus_id = -1;
static char wg_text_buf[FBWM_MAX_WIDGETS][FBWM_TEXTBOX_CAP];
static int wg_draw_order[FBWM_MAX_WIDGETS];

static void wg_reset_all(void)
{
    int i;
    wg_focus_id = -1;
    for (i = 0; i < FBWM_MAX_WIDGETS; i++) {
        wg_widgets[i].id = -1;
        wg_widgets[i].flags = 0;
    }
    wg_next_id = 0;
}

static void wg_destroy_for_window(int win_id)
{
    int i;
    for (i = 0; i < FBWM_MAX_WIDGETS; i++) {
        if (wg_widgets[i].id >= 0 && wg_widgets[i].win_id == win_id) {
            wg_widgets[i].id = -1;
            wg_widgets[i].flags = 0;
        }
    }
}

static int wg_list_count(const char *items)
{
    int count = 0;

    while (items && *items) {
        count++;
        while (*items && *items != '\n') items++;
        if (*items == '\n') items++;
    }
    return count;
}

static int wg_alloc(void)
{
    int i;
    for (i = 0; i < FBWM_MAX_WIDGETS; i++) {
        if (wg_widgets[i].id == -1)
            return i;
    }
    return -1;
}

int wg_label(int win, int x, int y, const char *text, fbwm_color_t fg)
{
    int i = wg_alloc();
    if (i < 0 || !wm_get(win) || !text) return -1;
    wg_widgets[i].id = wg_next_id++;
    wg_widgets[i].type = FBWM_W_LABEL;
    wg_widgets[i].win_id = win;
    wg_widgets[i].x = x; wg_widgets[i].y = y;
    wg_widgets[i].w = fbwm_strlen(text) * FBWM_FONT_WIDTH;
    wg_widgets[i].h = FBWM_FONT_HEIGHT;
    wg_widgets[i].text = text;
    wg_widgets[i].fg = fg;
    wg_widgets[i].bg = FBWM_WINBG;
    wg_widgets[i].flags = FBWM_WF_VISIBLE;
    wg_widgets[i].callback = 0;
    wg_widgets[i].userdata = 0;
    wg_widgets[i].value = 0;
    wg_widgets[i].scroll = 0;
    wg_widgets[i].max_visible = 0;
    return wg_widgets[i].id;
}

int wg_button(int win, int x, int y, int w, int h, const char *text,
              fbwm_widget_cb_t cb, void *ud)
{
    int i = wg_alloc();
    if (i < 0 || !wm_get(win) || w <= 0 || h <= 0) return -1;
    wg_widgets[i].id = wg_next_id++;
    wg_widgets[i].type = FBWM_W_BUTTON;
    wg_widgets[i].win_id = win;
    wg_widgets[i].x = x; wg_widgets[i].y = y;
    wg_widgets[i].w = w; wg_widgets[i].h = h;
    wg_widgets[i].text = text;
    wg_widgets[i].fg = FBWM_TITLETXT;
    wg_widgets[i].bg = FBWM_TITLEBAR;
    wg_widgets[i].flags = FBWM_WF_VISIBLE;
    wg_widgets[i].callback = cb;
    wg_widgets[i].userdata = ud;
    wg_widgets[i].value = 0;
    wg_widgets[i].scroll = 0;
    wg_widgets[i].max_visible = 0;
    return wg_widgets[i].id;
}

int wg_list(int win, int x, int y, int w, int h, const char *items,
            int visible_rows, fbwm_widget_cb_t cb, void *ud)
{
    int i = wg_alloc();
    int rows_by_height;
    if (i < 0 || !wm_get(win) || w <= 4 || h <= 4) return -1;
    rows_by_height = (h - 4) / (FBWM_FONT_HEIGHT + 2);
    if (visible_rows > rows_by_height) visible_rows = rows_by_height;
    if (visible_rows < 0) visible_rows = 0;
    wg_widgets[i].id = wg_next_id++;
    wg_widgets[i].type = FBWM_W_LIST;
    wg_widgets[i].win_id = win;
    wg_widgets[i].x = x; wg_widgets[i].y = y;
    wg_widgets[i].w = w; wg_widgets[i].h = h;
    wg_widgets[i].text = items;
    wg_widgets[i].value = 0;
    wg_widgets[i].scroll = 0;
    wg_widgets[i].max_visible = visible_rows;
    wg_widgets[i].fg = FBWM_WINTXT;
    wg_widgets[i].bg = FBWM_SHADOW;
    wg_widgets[i].flags = FBWM_WF_VISIBLE;
    wg_widgets[i].callback = cb;
    wg_widgets[i].userdata = ud;
    return wg_widgets[i].id;
}

int wg_textbox(int win, int x, int y, int w,
               fbwm_widget_cb_t cb, void *ud)
{
    int i = wg_alloc();
    if (i < 0 || !wm_get(win) || w <= 4) return -1;
    wg_text_buf[i][0] = 0;
    wg_widgets[i].id = wg_next_id++;
    wg_widgets[i].type = FBWM_W_TEXTBOX;
    wg_widgets[i].win_id = win;
    wg_widgets[i].x = x; wg_widgets[i].y = y;
    wg_widgets[i].w = w; wg_widgets[i].h = FBWM_FONT_HEIGHT + 6;
    wg_widgets[i].text = wg_text_buf[i];
    wg_widgets[i].value = 0; /* caret index */
    wg_widgets[i].scroll = 0;
    wg_widgets[i].max_visible = 0;
    wg_widgets[i].fg = FBWM_WINTXT;
    wg_widgets[i].bg = FBWM_SHADOW;
    wg_widgets[i].flags = FBWM_WF_VISIBLE;
    wg_widgets[i].callback = cb;
    wg_widgets[i].userdata = ud;
    wg_draw_order[i] = i;
    return wg_widgets[i].id;
}

int wg_progress(int win, int x, int y, int w, int h, int percent)
{
    int i = wg_alloc();
    if (i < 0 || !wm_get(win) || w <= 4 || h <= 4) return -1;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    wg_widgets[i].id = wg_next_id++;
    wg_widgets[i].type = FBWM_W_PROGRESS;
    wg_widgets[i].win_id = win;
    wg_widgets[i].x = x; wg_widgets[i].y = y;
    wg_widgets[i].w = w; wg_widgets[i].h = h;
    wg_widgets[i].value = percent;
    wg_widgets[i].fg = FBWM_TITLEBAR;
    wg_widgets[i].bg = FBWM_SHADOW;
    wg_widgets[i].flags = FBWM_WF_VISIBLE;
    wg_widgets[i].text = 0;
    wg_widgets[i].scroll = 0;
    wg_widgets[i].max_visible = 0;
    wg_widgets[i].callback = 0;
    wg_widgets[i].userdata = 0;
    return wg_widgets[i].id;
}

void wg_set_text(int id, const char *text)
{
    int i;
    for (i = 0; i < FBWM_MAX_WIDGETS; i++)
        if (wg_widgets[i].id == id &&
            (wg_widgets[i].flags & FBWM_WF_VISIBLE)) {
            if (wg_widgets[i].type == FBWM_W_TEXTBOX) {
                int n = 0;
                if (!text) text = "";
                while (text[n] && n < FBWM_TEXTBOX_CAP - 1) {
                    wg_text_buf[i][n] = text[n];
                    n++;
                }
                wg_text_buf[i][n] = 0;
                wg_widgets[i].text = wg_text_buf[i];
                wg_widgets[i].value = n; /* caret at end */
            } else {
                wg_widgets[i].text = text;
            }
            return;
        }
}

void wg_set_flags(int id, unsigned int flags)
{
    int i;
    for (i = 0; i < FBWM_MAX_WIDGETS; i++)
        if (wg_widgets[i].id == id) {
            wg_widgets[i].flags = flags;
            return;
        }
}

unsigned int wg_get_flags(int id)
{
    int i;
    for (i = 0; i < FBWM_MAX_WIDGETS; i++)
        if (wg_widgets[i].id == id)
            return wg_widgets[i].flags;
    return 0;
}

void wg_set_focus(int id)
{
    int i;
    for (i = 0; i < FBWM_MAX_WIDGETS; i++) {
        if (wg_widgets[i].id >= 0)
            wg_widgets[i].flags &= ~FBWM_WF_FOCUSED;
        if (wg_widgets[i].id == id)
            wg_widgets[i].flags |= FBWM_WF_FOCUSED;
    }
    wg_focus_id = id;
}

int wg_get_focus(void)
{
    return wg_focus_id;
}

void wg_set_hover(int win_id, int cx, int cy)
{
    int i, hit = wg_hit_test(win_id, cx, cy);
    for (i = 0; i < FBWM_MAX_WIDGETS; i++) {
        if (wg_widgets[i].win_id != win_id) continue;
        if (wg_widgets[i].id == hit)
            wg_widgets[i].flags |= FBWM_WF_HOVER;
        else
            wg_widgets[i].flags &= ~FBWM_WF_HOVER;
    }
}

void wg_set_pressed(int id, int pressed)
{
    int i;
    for (i = 0; i < FBWM_MAX_WIDGETS; i++) {
        if (wg_widgets[i].id == id) {
            if (pressed)
                wg_widgets[i].flags |= FBWM_WF_PRESSED;
            else
                wg_widgets[i].flags &= ~FBWM_WF_PRESSED;
            return;
        }
    }
}

void wg_raise(int id)
{
    int i, maxz = 0;
    for (i = 0; i < FBWM_MAX_WIDGETS; i++)
        if (wg_widgets[i].id >= 0 && wg_draw_order[i] > maxz)
            maxz = wg_draw_order[i];
    for (i = 0; i < FBWM_MAX_WIDGETS; i++)
        if (wg_widgets[i].id == id) {
            wg_draw_order[i] = maxz + 1;
            return;
        }
}

void wg_key(int key)
{
    int i;
    for (i = 0; i < FBWM_MAX_WIDGETS; i++) {
        fbwm_widget_t *wg;
        int len;
        if (wg_widgets[i].id != wg_focus_id) continue;
        wg = &wg_widgets[i];
        if (wg->type != FBWM_W_TEXTBOX) return;
        if (wg->flags & FBWM_WF_DISABLED) return;
        len = 0;
        while (wg_text_buf[i][len]) len++;
        if (key == 8 || key == 127) {
            if (len > 0) {
                wg_text_buf[i][len - 1] = 0;
                wg->value = len - 1;
            }
            return;
        }
        if (key == '\r' || key == '\n') {
            if (wg->callback)
                wg->callback(wg->id, wg->userdata);
            return;
        }
        if (key >= 32 && key < 127 && len < FBWM_TEXTBOX_CAP - 1) {
            wg_text_buf[i][len] = (char)key;
            wg_text_buf[i][len + 1] = 0;
            wg->value = len + 1;
            wg->text = wg_text_buf[i];
        }
        return;
    }
}

void wg_set_value(int id, int value)
{
    int i;
    for (i = 0; i < FBWM_MAX_WIDGETS; i++)
        if (wg_widgets[i].id == id &&
            (wg_widgets[i].flags & FBWM_WF_VISIBLE)) {
            if (wg_widgets[i].type == FBWM_W_PROGRESS) {
                if (value < 0) value = 0;
                if (value > 100) value = 100;
            } else if (wg_widgets[i].type == FBWM_W_LIST) {
                int count = wg_list_count(wg_widgets[i].text);
                if (value < 0) value = 0;
                if (count > 0 && value >= count) value = count - 1;
            }
            wg_widgets[i].value = value;
            return;
        }
}

int wg_get_value(int id)
{
    int i;
    for (i = 0; i < FBWM_MAX_WIDGETS; i++)
        if (wg_widgets[i].id == id &&
            (wg_widgets[i].flags & FBWM_WF_VISIBLE))
            return wg_widgets[i].value;
    return 0;
}

void wg_destroy(int id)
{
    int i;
    for (i = 0; i < FBWM_MAX_WIDGETS; i++)
        if (wg_widgets[i].id == id) {
            wg_widgets[i].id = -1;
            wg_widgets[i].flags = 0;
            return;
        }
}

/* Draw a single widget at absolute screen coordinates (client-clipped). */
static void wg_draw_one(fbwm_widget_t *wg, int ax, int ay,
                        const fbwm_rect_t *clip)
{
    int x = ax + wg->x;
    int y = ay + wg->y;
    int w = wg->w, h = wg->h;
    fbwm_color_t bg = wg->bg;
    fbwm_color_t fg = wg->fg;
    int disabled = (wg->flags & FBWM_WF_DISABLED) != 0;
    int hover = (wg->flags & FBWM_WF_HOVER) != 0;
    int pressed = (wg->flags & FBWM_WF_PRESSED) != 0;
    int focused = (wg->flags & FBWM_WF_FOCUSED) != 0;

    /* Clip widget outer box to parent client. */
    if (clip) {
        if (x >= clip->x + clip->w || y >= clip->y + clip->h) return;
        if (x + w <= clip->x || y + h <= clip->y) return;
    }

    if (disabled) {
        fg = FBWM_GRAY;
        bg = FBWM_DKGRAY;
    }

    switch (wg->type) {
    case FBWM_W_LABEL:
        if (wg->text)
            fb_puts(x, y, wg->text, fg, bg);
        break;

    case FBWM_W_BUTTON:
        if (pressed)
            bg = FBWM_HILITE;
        else if (hover)
            bg = FBWM_ACCENT;
        fb_fill_rect(x, y, w, h, bg);
        if (pressed)
            fb_draw_sunken(x, y, w, h);
        else
            fb_draw_raised(x, y, w, h);
        if (focused)
            fb_draw_rect(x + 2, y + 2, w - 4, h - 4, FBWM_WHITE);
        if (wg->text)
            fb_puts_centered(x, y + (h - FBWM_FONT_HEIGHT) / 2,
                             w, wg->text, fg, bg);
        break;

    case FBWM_W_TEXTBOX: {
        int max_chars = (w - 8) / FBWM_FONT_WIDTH;
        int caret;
        fb_fill_rect(x, y, w, h, bg);
        fb_draw_sunken(x, y, w, h);
        if (focused)
            fb_draw_rect(x + 1, y + 1, w - 2, h - 2, FBWM_ACCENT);
        if (wg->text && max_chars > 0)
            fb_puts_limited(x + 4, y + 3, wg->text, max_chars, fg, bg);
        /* Caret */
        if (focused && !disabled) {
            caret = wg->value;
            if (caret < 0) caret = 0;
            if (caret > max_chars) caret = max_chars;
            fb_vline(x + 4 + caret * FBWM_FONT_WIDTH, y + 3,
                     FBWM_FONT_HEIGHT, FBWM_ACCENT);
        }
        break;
    }

    case FBWM_W_PROGRESS: {
        int fill = (w - 4) * wg->value / 100;
        fb_fill_rect(x, y, w, h, bg);
        fb_draw_sunken(x, y, w, h);
        if (fill > 0)
            fb_fill_rect(x + 2, y + 2, fill, h - 4, fg);
        break;
    }

    case FBWM_W_LIST: {
        const char *p = wg->text;
        int row = 0;
        int skip = wg->scroll;
        int row_h = FBWM_FONT_HEIGHT + 2;

        fb_fill_rect(x, y, w, h, bg);
        fb_draw_sunken(x, y, w, h);
        if (focused)
            fb_draw_rect(x + 1, y + 1, w - 2, h - 2, FBWM_ACCENT);

        while (p && *p && row < wg->max_visible) {
            const char *nl;
            char line[64];
            int len = 0;
            const char *copy;
            nl = p;
            while (*nl && *nl != '\n') nl++;
            copy = p;
            while (copy < nl && len < 63)
                line[len++] = *copy++;
            line[len] = 0;
            if (*nl == '\n') nl++;

            if (skip > 0) {
                skip--;
            } else {
                int ry = y + 2 + row * row_h;
                int sel = (row + wg->scroll == wg->value);
                fbwm_color_t rbg = sel ? FBWM_HILITE : bg;
                fbwm_color_t rfg = sel ? FBWM_ACCENT : fg;
                if (ry + row_h <= y + h) {
                    fb_fill_rect(x + 2, ry, w - 4, row_h, rbg);
                    fb_puts_limited(x + 4, ry + 1, line,
                                    (w - 8) / FBWM_FONT_WIDTH, rfg, rbg);
                }
                row++;
            }
            p = nl;
        }
        break;
    }
    }
}

/* Draw all widgets belonging to a window (z-order + client clip). */
void wg_draw_all(int win_id)
{
    fbwm_rect_t cr;
    int order[FBWM_MAX_WIDGETS];
    int n = 0, i, j;

    wm_client_rect(win_id, &cr);

    for (i = 0; i < FBWM_MAX_WIDGETS; i++) {
        if ((wg_widgets[i].flags & FBWM_WF_VISIBLE) &&
            wg_widgets[i].win_id == win_id)
            order[n++] = i;
    }
    /* Insertion sort by draw order. */
    for (i = 1; i < n; i++) {
        int key = order[i];
        j = i - 1;
        while (j >= 0 && wg_draw_order[order[j]] > wg_draw_order[key]) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }
    for (i = 0; i < n; i++)
        wg_draw_one(&wg_widgets[order[i]], cr.x, cr.y, &cr);
}

/* Widget hit-test: which widget at client-relative (cx, cy)? */
int wg_hit_test(int win_id, int cx, int cy)
{
    int i;
    for (i = 0; i < FBWM_MAX_WIDGETS; i++) {
        fbwm_widget_t *wg = &wg_widgets[i];
        if ((wg->flags & FBWM_WF_VISIBLE) && wg->win_id == win_id) {
            if (cx >= wg->x && cx < wg->x + wg->w &&
                cy >= wg->y && cy < wg->y + wg->h) {
                return wg->id;
            }
        }
    }
    return -1;
}

/* Dispatch click to widget at (cx, cy) in window win_id */
void wg_dispatch_click(int win_id, int cx, int cy)
{
    int i;
    for (i = 0; i < FBWM_MAX_WIDGETS; i++) {
        fbwm_widget_t *wg = &wg_widgets[i];
        if ((wg->flags & FBWM_WF_VISIBLE) && wg->win_id == win_id &&
            !(wg->flags & FBWM_WF_DISABLED)) {
            if (cx >= wg->x && cx < wg->x + wg->w &&
                cy >= wg->y && cy < wg->y + wg->h) {
                if (wg->type == FBWM_W_TEXTBOX) {
                    wg_set_focus(wg->id);
                    if (wg->callback)
                        wg->callback(wg->id, wg->userdata);
                    return;
                }
                if (wg->type == FBWM_W_BUTTON) {
                    wg_set_focus(wg->id);
                    if (wg->callback)
                        wg->callback(wg->id, wg->userdata);
                    return;
                }
                if (wg->type == FBWM_W_LIST) {
                    int row_h = FBWM_FONT_HEIGHT + 2;
                    int rel_y = cy - wg->y - 2;
                    int clicked_row;
                    int item;
                    int count = wg_list_count(wg->text);
                    wg_set_focus(wg->id);
                    if (rel_y < 0 || cx < wg->x + 2 ||
                        cx >= wg->x + wg->w - 2)
                        return;
                    clicked_row = rel_y / row_h;
                    item = wg->scroll + clicked_row;
                    if (clicked_row < wg->max_visible && item < count) {
                        wg->value = item;
                        if (wg->callback)
                            wg->callback(wg->id, wg->userdata);
                    }
                }
                return;
            }
        }
    }
}

/* Scroll a list widget */
void wg_list_scroll(int id, int delta)
{
    int i;
    for (i = 0; i < FBWM_MAX_WIDGETS; i++) {
        if (wg_widgets[i].id == id && wg_widgets[i].type == FBWM_W_LIST &&
            (wg_widgets[i].flags & FBWM_WF_VISIBLE)) {
            int max_scroll = wg_list_count(wg_widgets[i].text) -
                             wg_widgets[i].max_visible;
            if (max_scroll < 0) max_scroll = 0;
            wg_widgets[i].scroll += delta;
            if (wg_widgets[i].scroll < 0) wg_widgets[i].scroll = 0;
            if (wg_widgets[i].scroll > max_scroll)
                wg_widgets[i].scroll = max_scroll;
            return;
        }
    }
}
