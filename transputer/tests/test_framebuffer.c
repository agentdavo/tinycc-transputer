/*
 * test_framebuffer.c - Bare-metal framebuffer test for T4 emulator
 *
 * Uses transputer MOVE/MOVE2D instructions for fast framebuffer fills.
 * Writes directly to the VGA framebuffer at 0x90000000 and configures
 * the VGA controller at 0xA0000000.
 *
 * Run with DISPLAY set so T4 opens an X11 window:
 *   DISPLAY=:0 timeout 10 transputer/t4/t4 -si -se -sb /tmp/test_fb.btl
 *
 * UART oracle: SFBOKDRAWH
 */

#define UART_ADDR     0x10000000

/* Framebuffer geometry */
#define FB_BASE       0x90000000
#define FB_WIDTH      640
#define FB_HEIGHT     480
#define FB_BPP        4
#define FB_STRIDE     (FB_WIDTH * FB_BPP)
#define FB_TOTAL      (FB_WIDTH * FB_HEIGHT)

/* VGA Controller Registers */
#define VGA_CONTROL     0xA0000000
#define VGA_FB_BASE_REG 0xA0000004
#define VGA_REG_WIDTH   0xA0000008
#define VGA_REG_HEIGHT  0xA000000C
#define VGA_STRIDE_REG  0xA0000010

/* Colors (ARGB) */
#define COLOR_BLACK   0xFF000000
#define COLOR_WHITE   0xFFFFFFFF
#define COLOR_RED     0xFFFF0000
#define COLOR_GREEN   0xFF00FF00
#define COLOR_BLUE    0xFF0000FF
#define COLOR_YELLOW  0xFFFFFF00
#define COLOR_CYAN    0xFF00FFFF
#define COLOR_GRAY    0xFF808080
#define COLOR_DKBLUE  0xFF000080
#define COLOR_DKGRAY  0xFF404040
#define COLOR_LTGRAY  0xFFC0C0C0
#define COLOR_TEAL    0xFF008080

static inline void uart_putc(char c)
{
    *(volatile int *)UART_ADDR = c;
}

static inline void uart_puts(const char *s)
{
    while (*s) {
        uart_putc(*s);
        s++;
    }
}

/* Transputer MOVE builtins - declared as intrinsics */
void __move(void *dest, const void *src, int length);
void __move2dinit(int width, int dest_stride, int src_stride);
void __move2dall(int length, void *dest, const void *src);

/*
 * Initialize the VGA controller.
 */
static void vga_init(void)
{
    *(volatile unsigned int *)VGA_FB_BASE_REG = FB_BASE;
    *(volatile unsigned int *)VGA_REG_WIDTH   = FB_WIDTH;
    *(volatile unsigned int *)VGA_REG_HEIGHT  = FB_HEIGHT;
    *(volatile unsigned int *)VGA_STRIDE_REG  = FB_STRIDE;
    *(volatile unsigned int *)VGA_CONTROL     = 0x01;
}

/*
 * Fast framebuffer clear using MOVE doubling.
 * Writes one pixel, then doubles with MOVE: 1→2→4→...→total.
 */
static void fb_clear(unsigned int color)
{
    volatile unsigned int *fb = (volatile unsigned int *)FB_BASE;
    int filled, chunk;

    /* Seed first pixel */
    fb[0] = color;
    filled = 1;

    /* Double with MOVE until we've filled everything */
    while (filled < FB_TOTAL) {
        chunk = filled;
        if (filled + chunk > FB_TOTAL)
            chunk = FB_TOTAL - filled;
        __move((void *)(FB_BASE + filled * 4),
               (const void *)FB_BASE,
               chunk * 4);
        filled += chunk;
    }
}

/*
 * Fast horizontal line fill using MOVE.
 * Seeds one pixel, then MOVE-doubles to fill the row.
 */
static void fb_hline(int x, int y, int w, unsigned int color)
{
    volatile unsigned int *fb = (volatile unsigned int *)FB_BASE;
    int base, filled, chunk;

    if (y < 0 || y >= FB_HEIGHT || w <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > FB_WIDTH) w = FB_WIDTH - x;
    if (w <= 0) return;

    base = y * FB_WIDTH + x;
    fb[base] = color;

    if (w == 1) return;

    filled = 1;
    while (filled < w) {
        chunk = filled;
        if (filled + chunk > w)
            chunk = w - filled;
        __move((void *)(FB_BASE + (base + filled) * 4),
               (const void *)(FB_BASE + base * 4),
               chunk * 4);
        filled += chunk;
    }
}

/*
 * Fast rectangle fill using MOVE.
 * Fills first row with fb_hline, then MOVE-copies to remaining rows.
 */
static void fb_fillrect(int x, int y, int w, int h, unsigned int color)
{
    int row;
    int row_bytes;

    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > FB_WIDTH) w = FB_WIDTH - x;
    if (y + h > FB_HEIGHT) h = FB_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    /* Fill the first row */
    fb_hline(x, y, w, color);

    /* Copy to remaining rows using MOVE */
    row_bytes = w * 4;
    for (row = 1; row < h; row++) {
        __move((void *)(FB_BASE + ((y + row) * FB_WIDTH + x) * 4),
               (const void *)(FB_BASE + (y * FB_WIDTH + x) * 4),
               row_bytes);
    }
}

/*
 * Single pixel (for font rendering — small count, no MOVE needed).
 */
static inline void fb_putpixel(int x, int y, unsigned int color)
{
    if (x >= 0 && x < FB_WIDTH && y >= 0 && y < FB_HEIGHT) {
        volatile unsigned int *fb = (volatile unsigned int *)FB_BASE;
        fb[y * FB_WIDTH + x] = color;
    }
}

/*
 * Minimal 5x7 font. Each glyph is 7 bytes, MSB = leftmost pixel.
 * Only uppercase + digits + basic punctuation.
 */
static const unsigned char font5x7[][7] = {
    /* ' ' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* '!' */ {0x20,0x20,0x20,0x20,0x00,0x20,0x00},
    /* '"' */ {0x50,0x50,0x00,0x00,0x00,0x00,0x00},
    /* '#' */ {0x50,0xF8,0x50,0xF8,0x50,0x00,0x00},
    /* '$' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* '%' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* '&' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* '\''*/ {0x20,0x20,0x00,0x00,0x00,0x00,0x00},
    /* '(' */ {0x10,0x20,0x20,0x20,0x20,0x10,0x00},
    /* ')' */ {0x40,0x20,0x20,0x20,0x20,0x40,0x00},
    /* '*' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* '+' */ {0x00,0x20,0x70,0x20,0x00,0x00,0x00},
    /* ',' */ {0x00,0x00,0x00,0x00,0x20,0x20,0x40},
    /* '-' */ {0x00,0x00,0x70,0x00,0x00,0x00,0x00},
    /* '.' */ {0x00,0x00,0x00,0x00,0x00,0x20,0x00},
    /* '/' */ {0x08,0x10,0x20,0x40,0x80,0x00,0x00},
    /* '0' */ {0x70,0x88,0x98,0xA8,0xC8,0x70,0x00},
    /* '1' */ {0x20,0x60,0x20,0x20,0x20,0x70,0x00},
    /* '2' */ {0x70,0x88,0x08,0x30,0x40,0xF8,0x00},
    /* '3' */ {0x70,0x88,0x30,0x08,0x88,0x70,0x00},
    /* '4' */ {0x10,0x30,0x50,0x90,0xF8,0x10,0x00},
    /* '5' */ {0xF8,0x80,0xF0,0x08,0x88,0x70,0x00},
    /* '6' */ {0x30,0x40,0xF0,0x88,0x88,0x70,0x00},
    /* '7' */ {0xF8,0x08,0x10,0x20,0x40,0x40,0x00},
    /* '8' */ {0x70,0x88,0x70,0x88,0x88,0x70,0x00},
    /* '9' */ {0x70,0x88,0x78,0x08,0x10,0x60,0x00},
    /* ':' */ {0x00,0x20,0x00,0x20,0x00,0x00,0x00},
    /* ';' */ {0x00,0x20,0x00,0x20,0x20,0x40,0x00},
    /* '<' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* '=' */ {0x00,0x70,0x00,0x70,0x00,0x00,0x00},
    /* '>' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* '?' */ {0x70,0x88,0x10,0x20,0x00,0x20,0x00},
    /* '@' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* A */   {0x70,0x88,0x88,0xF8,0x88,0x88,0x00},
    /* B */   {0xF0,0x88,0xF0,0x88,0x88,0xF0,0x00},
    /* C */   {0x70,0x88,0x80,0x80,0x88,0x70,0x00},
    /* D */   {0xE0,0x90,0x88,0x88,0x90,0xE0,0x00},
    /* E */   {0xF8,0x80,0xF0,0x80,0x80,0xF8,0x00},
    /* F */   {0xF8,0x80,0xF0,0x80,0x80,0x80,0x00},
    /* G */   {0x70,0x88,0x80,0xB8,0x88,0x70,0x00},
    /* H */   {0x88,0x88,0xF8,0x88,0x88,0x88,0x00},
    /* I */   {0x70,0x20,0x20,0x20,0x20,0x70,0x00},
    /* J */   {0x08,0x08,0x08,0x08,0x88,0x70,0x00},
    /* K */   {0x88,0x90,0xA0,0xC0,0xA0,0x90,0x00},
    /* L */   {0x80,0x80,0x80,0x80,0x80,0xF8,0x00},
    /* M */   {0x88,0xD8,0xA8,0x88,0x88,0x88,0x00},
    /* N */   {0x88,0xC8,0xA8,0x98,0x88,0x88,0x00},
    /* O */   {0x70,0x88,0x88,0x88,0x88,0x70,0x00},
    /* P */   {0xF0,0x88,0xF0,0x80,0x80,0x80,0x00},
    /* Q */   {0x70,0x88,0x88,0xA8,0x90,0x68,0x00},
    /* R */   {0xF0,0x88,0xF0,0xA0,0x90,0x88,0x00},
    /* S */   {0x70,0x80,0x70,0x08,0x88,0x70,0x00},
    /* T */   {0xF8,0x20,0x20,0x20,0x20,0x20,0x00},
    /* U */   {0x88,0x88,0x88,0x88,0x88,0x70,0x00},
    /* V */   {0x88,0x88,0x88,0x50,0x50,0x20,0x00},
    /* W */   {0x88,0x88,0x88,0xA8,0xD8,0x88,0x00},
    /* X */   {0x88,0x50,0x20,0x50,0x88,0x00,0x00},
    /* Y */   {0x88,0x50,0x20,0x20,0x20,0x20,0x00},
    /* Z */   {0xF8,0x10,0x20,0x40,0x80,0xF8,0x00},
};

/*
 * Draw a character at pixel position, 2x scale for readability.
 * Font glyphs are 8 pixels wide x 7 rows.
 */
static void fb_drawchar(int px, int py, char c, unsigned int fg, unsigned int bg)
{
    int idx, row, col;
    const unsigned char *glyph;

    if (c >= 'a' && c <= 'z')
        c = c - 'a' + 'A';
    if (c < 32 || c > 90)
        c = ' ';
    idx = c - 32;

    glyph = font5x7[idx];
    for (row = 0; row < 7; row++) {
        unsigned char bits = glyph[row];
        for (col = 0; col < 8; col++) {
            unsigned int color = (bits & (0x80 >> col)) ? fg : bg;
            /* 2x scale */
            fb_putpixel(px + col * 2,     py + row * 2,     color);
            fb_putpixel(px + col * 2 + 1, py + row * 2,     color);
            fb_putpixel(px + col * 2,     py + row * 2 + 1, color);
            fb_putpixel(px + col * 2 + 1, py + row * 2 + 1, color);
        }
    }
}

/*
 * Draw a string at pixel position.
 * Character cell is 8 pixels * 2x scale = 16 pixels + 2 pixel gap = 18 per char.
 */
static void fb_drawtext(int px, int py, const char *text,
                         unsigned int fg, unsigned int bg)
{
    int i;
    for (i = 0; text[i]; i++) {
        fb_drawchar(px + i * 18, py, text[i], fg, bg);
    }
}

/*
 * Draw a window with title bar.
 */
static void draw_window(int x, int y, int w, int h, const char *title)
{
    /* Shadow */
    fb_fillrect(x + 4, y + 4, w, h, COLOR_DKGRAY);
    /* Body */
    fb_fillrect(x, y, w, h, COLOR_LTGRAY);
    /* 3D border */
    fb_fillrect(x, y, w, 2, COLOR_WHITE);
    fb_fillrect(x, y, 2, h, COLOR_WHITE);
    fb_fillrect(x + w - 2, y, 2, h, COLOR_DKGRAY);
    fb_fillrect(x, y + h - 2, w, 2, COLOR_DKGRAY);
    /* Title bar */
    fb_fillrect(x + 2, y + 2, w - 4, 24, COLOR_DKBLUE);
    fb_drawtext(x + 8, y + 6, title, COLOR_WHITE, COLOR_DKBLUE);
    /* Close button */
    fb_fillrect(x + w - 26, y + 4, 22, 20, COLOR_RED);
    fb_drawchar(x + w - 20, y + 8, 'X', COLOR_WHITE, COLOR_RED);
}

/*
 * Draw a 3D button.
 */
static void draw_button(int x, int y, int w, int h, const char *text)
{
    int text_len, text_x, text_y;

    fb_fillrect(x, y, w, h, COLOR_LTGRAY);
    fb_fillrect(x, y, w, 2, COLOR_WHITE);
    fb_fillrect(x, y, 2, h, COLOR_WHITE);
    fb_fillrect(x + w - 2, y, 2, h, COLOR_DKGRAY);
    fb_fillrect(x, y + h - 2, w, 2, COLOR_DKGRAY);

    text_len = 0;
    while (text[text_len]) text_len++;
    text_x = x + (w - text_len * 18) / 2;
    text_y = y + (h - 14) / 2;
    fb_drawtext(text_x, text_y, text, COLOR_BLACK, COLOR_LTGRAY);
}

/*
 * Draw color test bars.
 */
static void draw_color_bars(void)
{
    int bar_w = FB_WIDTH / 8;
    fb_fillrect(0 * bar_w, 0, bar_w, 30, COLOR_RED);
    fb_fillrect(1 * bar_w, 0, bar_w, 30, COLOR_GREEN);
    fb_fillrect(2 * bar_w, 0, bar_w, 30, COLOR_BLUE);
    fb_fillrect(3 * bar_w, 0, bar_w, 30, COLOR_YELLOW);
    fb_fillrect(4 * bar_w, 0, bar_w, 30, COLOR_CYAN);
    fb_fillrect(5 * bar_w, 0, bar_w, 30, 0xFFFF00FF);
    fb_fillrect(6 * bar_w, 0, bar_w, 30, COLOR_WHITE);
    fb_fillrect(7 * bar_w, 0, bar_w, 30, COLOR_BLACK);
}

/*
 * Draw taskbar.
 */
static void draw_taskbar(void)
{
    fb_fillrect(0, FB_HEIGHT - 32, FB_WIDTH, 32, COLOR_LTGRAY);
    fb_fillrect(0, FB_HEIGHT - 32, FB_WIDTH, 2, COLOR_WHITE);
    draw_button(4, FB_HEIGHT - 28, 80, 24, "TC");
    fb_drawtext(100, FB_HEIGHT - 24, "TINYCORE TRANSPUTER T805",
                COLOR_BLACK, COLOR_LTGRAY);
}

void test_main(void)
{
    uart_puts("FB");

    /* Initialize VGA controller */
    vga_init();
    uart_puts("OK");

    /* Clear to teal desktop */
    fb_clear(COLOR_TEAL);

    /* Color bars */
    draw_color_bars();

    /* Main window */
    draw_window(40, 50, 400, 280, "TRANSPUTER GUI");

    fb_drawtext(60, 100, "TINYCORE LINUX ON T805",
                COLOR_BLACK, COLOR_LTGRAY);
    fb_drawtext(60, 130, "FRAMEBUFFER 640X480",
                COLOR_DKBLUE, COLOR_LTGRAY);
    fb_drawtext(60, 160, "SELF-HOSTING DEV ENV",
                COLOR_DKBLUE, COLOR_LTGRAY);

    /* Buttons */
    draw_button(60, 200, 120, 36, "COMPILE");
    draw_button(200, 200, 120, 36, "RUN");
    draw_button(60, 250, 120, 36, "LOAD TCZ");
    draw_button(200, 250, 120, 36, "EXIT");

    /* Status window */
    draw_window(300, 180, 300, 160, "STATUS");
    fb_drawtext(320, 220, "KERNEL 2.6.39",
                COLOR_BLACK, COLOR_LTGRAY);
    fb_drawtext(320, 250, "COMPILER TINYCC",
                COLOR_BLACK, COLOR_LTGRAY);
    fb_drawtext(320, 280, "GUI NATIVE FB",
                COLOR_BLACK, COLOR_LTGRAY);

    /* Taskbar */
    draw_taskbar();

    uart_puts("DRAW");
}
