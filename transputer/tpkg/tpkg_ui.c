/*
 * tpkg-ui — TransputerOS Package Manager GUI
 *
 * Renders in an INMOS-teal styled window on the 800x600 framebuffer.
 * Lists installed packages, shows info, provides install/remove actions.
 *
 * Window: 500x380, centered on desktop.
 * Uses direct framebuffer writes (same approach as DOOM video driver).
 *
 * Reads package database from /var/lib/tpkg/<pkg>/info.
 * Invokes "tpkg install/remove" for actual operations.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

/* ---------------------------------------------------------------------------
 * Framebuffer hardware
 * --------------------------------------------------------------------------- */
#define FB_BASE         0x81E00000
#define FB_WIDTH        800
#define FB_HEIGHT       600

/* Keyboard UART */
#define KBD_UART_DATA   0x11000000
#define KBD_UART_STATUS 0x11000004
#define UART_RXAVAIL    0x02

/* ---------------------------------------------------------------------------
 * INMOS color palette
 * --------------------------------------------------------------------------- */
#define COL_DESKTOP     0xFF0F1A2E
#define COL_TITLEBAR    0xFF00897B
#define COL_TITLE_TEXT  0xFFE0F2F1
#define COL_WINBG       0xFF1A2332
#define COL_WINTXT      0xFFB2DFDB
#define COL_ACCENT      0xFF00BCD4
#define COL_BORDER_LT   0xFF37474F
#define COL_BORDER_DK   0xFF0D1520
#define COL_SELECT      0xFF004D40
#define COL_HEADER      0xFF00695C
#define COL_SCROLLBAR   0xFF263238
#define COL_SCROLLTHUMB 0xFF00897B
#define COL_BTN_BG      0xFF00695C
#define COL_BTN_TEXT    0xFFE0F2F1
#define COL_VERSION     0xFF80CBC4
#define COL_STATUS      0xFF546E7A

/* ---------------------------------------------------------------------------
 * Window layout
 * --------------------------------------------------------------------------- */
#define WIN_W           500
#define WIN_H           380
#define WIN_X           ((FB_WIDTH - WIN_W) / 2)
#define WIN_Y           ((FB_HEIGHT - WIN_H) / 2)
#define TITLE_H         22
#define BORDER          3
#define CLIENT_X        (WIN_X + BORDER)
#define CLIENT_Y        (WIN_Y + TITLE_H)
#define CLIENT_W        (WIN_W - BORDER * 2)
#define CLIENT_H        (WIN_H - TITLE_H - BORDER)

/* List area */
#define LIST_X          (CLIENT_X + 4)
#define LIST_Y          (CLIENT_Y + 24)
#define LIST_W          (CLIENT_W - 8)
#define LIST_ROW_H      14
#define LIST_ROWS       ((CLIENT_H - 60) / LIST_ROW_H)

/* Status bar */
#define STATUS_Y        (CLIENT_Y + CLIENT_H - 16)

/* ---------------------------------------------------------------------------
 * MMIO helpers
 * --------------------------------------------------------------------------- */
static unsigned int *fb;

static void mmio_w(unsigned int addr, unsigned int val)
{
    *(volatile unsigned int *)addr = val;
}

static unsigned int mmio_r(unsigned int addr)
{
    return *(volatile unsigned int *)addr;
}

/* ---------------------------------------------------------------------------
 * Pixel drawing
 * --------------------------------------------------------------------------- */
static void px(int x, int y, unsigned int c)
{
    if (x >= 0 && x < FB_WIDTH && y >= 0 && y < FB_HEIGHT)
        fb[y * FB_WIDTH + x] = c;
}

static void fill(int x, int y, int w, int h, unsigned int c)
{
    int r, cx;
    for (r = 0; r < h; r++) {
        int off = (y + r) * FB_WIDTH + x;
        for (cx = 0; cx < w; cx++)
            fb[off + cx] = c;
    }
}

static void hline(int x, int y, int w, unsigned int c)
{
    int i, off = y * FB_WIDTH + x;
    for (i = 0; i < w; i++) fb[off + i] = c;
}

/* ---------------------------------------------------------------------------
 * 5x7 mini font (same as DOOM video driver)
 * --------------------------------------------------------------------------- */
static const unsigned char mfont[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* space */
    {0x7E,0x09,0x09,0x09,0x7E}, /* A */
    {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x41,0x3E}, {0x7F,0x49,0x49,0x49,0x41},
    {0x7F,0x09,0x09,0x09,0x01}, {0x3E,0x41,0x49,0x49,0x7A},
    {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40}, {0x7F,0x02,0x04,0x02,0x7F},
    {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E},
    {0x7F,0x09,0x19,0x29,0x46}, {0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x30,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63}, {0x07,0x08,0x70,0x08,0x07},
    {0x61,0x51,0x49,0x45,0x43},
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */
    {0x00,0x42,0x7F,0x40,0x00}, {0x42,0x61,0x51,0x49,0x46},
    {0x21,0x41,0x45,0x4B,0x31}, {0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x30},
    {0x01,0x71,0x09,0x05,0x03}, {0x36,0x49,0x49,0x49,0x36},
    {0x06,0x49,0x49,0x29,0x1E},
    {0x08,0x08,0x08,0x08,0x08}, /* - */
    {0x00,0x60,0x60,0x00,0x00}, /* . */
    {0x00,0x36,0x36,0x00,0x00}, /* : */
    {0x00,0x08,0x00,0x00,0x00}, /* / (approx) */
};

static int fidx(char c)
{
    if (c == ' ') return 0;
    if (c >= 'A' && c <= 'Z') return 1 + (c - 'A');
    if (c >= 'a' && c <= 'z') return 1 + (c - 'a');
    if (c >= '0' && c <= '9') return 27 + (c - '0');
    if (c == '-') return 37;
    if (c == '.') return 38;
    if (c == ':') return 39;
    if (c == '/') return 40;
    return 0;
}

static void drawch(int px_x, int py, char c, unsigned int fg, unsigned int bg)
{
    int idx = fidx(c);
    int col, row;
    if (idx < 0 || idx > 40) idx = 0;
    for (col = 0; col < 5; col++) {
        unsigned char bits = mfont[idx][col];
        for (row = 0; row < 7; row++)
            px(px_x + col, py + row, (bits & (1 << row)) ? fg : bg);
    }
    for (row = 0; row < 7; row++)
        px(px_x + 5, py + row, bg);
}

static void draws(int x, int y, const char *s, unsigned int fg, unsigned int bg)
{
    while (*s) {
        drawch(x, y, *s, fg, bg);
        x += 6;
        s++;
    }
}

/* Centered text */
static void draws_c(int x, int y, int w, const char *s, unsigned int fg, unsigned int bg)
{
    int len = strlen(s);
    int tx = x + (w - len * 6) / 2;
    draws(tx, y, s, fg, bg);
}

/* ---------------------------------------------------------------------------
 * Package database
 * --------------------------------------------------------------------------- */
#define MAX_PKG 32
#define MAX_NAME 32

typedef struct {
    char name[MAX_NAME];
    char version[16];
    char desc[80];
    char license[16];
    int  size;
} pkg_entry_t;

static pkg_entry_t packages[MAX_PKG];
static int npkg = 0;
static int selected = 0;
static int scroll = 0;

static const char *read_info_field(const char *path, const char *key)
{
    static char vbuf[128];
    FILE *f;
    char line[128];
    int klen = strlen(key);
    vbuf[0] = 0;
    f = fopen(path, "r");
    if (!f) return vbuf;
    while (fgets(line, sizeof(line), f)) {
        int len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = 0;
        if (strncmp(line, key, klen) == 0 && line[klen] == ':') {
            const char *v = line + klen + 1;
            while (*v == ' ') v++;
            strncpy(vbuf, v, 127);
            vbuf[127] = 0;
            break;
        }
    }
    fclose(f);
    return vbuf;
}

static void load_packages(void)
{
    char cmd[128];
    char line[MAX_NAME];
    char infopath[128];
    FILE *f;

    npkg = 0;
    snprintf(cmd, sizeof(cmd), "ls /var/lib/tpkg 2>/dev/null");
    f = popen(cmd, "r");
    if (!f) return;

    while (fgets(line, sizeof(line), f) && npkg < MAX_PKG) {
        int len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = 0;
        if (len == 0) continue;

        snprintf(infopath, sizeof(infopath), "/var/lib/tpkg/%s/info", line);
        if (access(infopath, 0) != 0) continue;

        strncpy(packages[npkg].name, line, MAX_NAME - 1);

        {
            const char *v = read_info_field(infopath, "Version");
            strncpy(packages[npkg].version, v, 15);
        }
        {
            const char *d = read_info_field(infopath, "Description");
            strncpy(packages[npkg].desc, d, 79);
        }
        {
            const char *l = read_info_field(infopath, "License");
            strncpy(packages[npkg].license, l, 15);
        }
        {
            const char *s = read_info_field(infopath, "Size");
            packages[npkg].size = atoi(s);
        }

        npkg++;
    }
    pclose(f);

    if (selected >= npkg) selected = npkg > 0 ? npkg - 1 : 0;
}

/* ---------------------------------------------------------------------------
 * UI rendering
 * --------------------------------------------------------------------------- */
static void flip(void) { }

static void draw_window(void)
{
    int x = WIN_X, y = WIN_Y, w = WIN_W, h = WIN_H;
    int i;

    /* Desktop background */
    fill(0, 0, FB_WIDTH, FB_HEIGHT, COL_DESKTOP);

    /* Window border — 3D raised */
    hline(x, y, w, COL_BORDER_LT);
    hline(x, y + 1, w, COL_BORDER_LT);
    hline(x, y + h - 1, w, COL_BORDER_DK);
    hline(x, y + h - 2, w, COL_BORDER_DK);
    for (i = 0; i < h; i++) {
        px(x, y + i, COL_BORDER_LT);
        px(x + 1, y + i, COL_BORDER_LT);
        px(x + w - 1, y + i, COL_BORDER_DK);
        px(x + w - 2, y + i, COL_BORDER_DK);
    }

    /* Titlebar */
    fill(x + BORDER, y + BORDER, WIN_W - BORDER * 2, TITLE_H - BORDER, COL_TITLEBAR);
    draws_c(x + BORDER, y + BORDER + 4, WIN_W - BORDER * 2,
            "TPKG - PACKAGE MANAGER", COL_TITLE_TEXT, COL_TITLEBAR);

    /* Client area background */
    fill(CLIENT_X, CLIENT_Y, CLIENT_W, CLIENT_H, COL_WINBG);

    /* Column headers */
    fill(LIST_X, CLIENT_Y + 4, LIST_W, 14, COL_HEADER);
    draws(LIST_X + 4, CLIENT_Y + 7, "PACKAGE", COL_TITLE_TEXT, COL_HEADER);
    draws(LIST_X + 140, CLIENT_Y + 7, "VERSION", COL_TITLE_TEXT, COL_HEADER);
    draws(LIST_X + 220, CLIENT_Y + 7, "DESCRIPTION", COL_TITLE_TEXT, COL_HEADER);

    /* Package list */
    for (i = 0; i < LIST_ROWS && (scroll + i) < npkg; i++) {
        int idx = scroll + i;
        int ry = LIST_Y + i * LIST_ROW_H;
        unsigned int bg = (idx == selected) ? COL_SELECT : COL_WINBG;
        unsigned int fg = (idx == selected) ? COL_ACCENT : COL_WINTXT;

        fill(LIST_X, ry, LIST_W, LIST_ROW_H, bg);
        draws(LIST_X + 4, ry + 3, packages[idx].name, fg, bg);
        draws(LIST_X + 140, ry + 3, packages[idx].version, COL_VERSION, bg);

        /* Truncate description to fit */
        {
            char desc_trunc[40];
            strncpy(desc_trunc, packages[idx].desc, 39);
            desc_trunc[39] = 0;
            draws(LIST_X + 220, ry + 3, desc_trunc, fg, bg);
        }
    }

    /* Empty state */
    if (npkg == 0) {
        draws_c(CLIENT_X, LIST_Y + 40, CLIENT_W,
                "NO PACKAGES INSTALLED", COL_STATUS, COL_WINBG);
        draws_c(CLIENT_X, LIST_Y + 56, CLIENT_W,
                "USE: TPKG INSTALL FILE.TPZ", COL_STATUS, COL_WINBG);
    }

    /* Status bar */
    fill(CLIENT_X, STATUS_Y, CLIENT_W, 14, COL_SCROLLBAR);
    {
        char status[80];
        snprintf(status, sizeof(status), "%d PACKAGES   UP/DOWN:SELECT  R:REMOVE  Q:QUIT", npkg);
        draws(CLIENT_X + 4, STATUS_Y + 3, status, COL_VERSION, COL_SCROLLBAR);
    }

    /* Selected package detail (below list, above status) */
    if (npkg > 0) {
        int dy = STATUS_Y - 22;
        fill(CLIENT_X + 4, dy, CLIENT_W - 8, 16, COL_WINBG);

        {
            char detail[120];
            snprintf(detail, sizeof(detail), "%s %s  %s  %dKB",
                packages[selected].name,
                packages[selected].version,
                packages[selected].license,
                packages[selected].size / 1024);
            draws(CLIENT_X + 8, dy + 4, detail, COL_ACCENT, COL_WINBG);
        }
    }
}

/* ---------------------------------------------------------------------------
 * Keyboard input (reads from UART1)
 * --------------------------------------------------------------------------- */
static int getkey(void)
{
    if (!(mmio_r(KBD_UART_STATUS) & UART_RXAVAIL))
        return 0;
    int ch = mmio_r(KBD_UART_DATA) & 0xFF;

    /* VT100 escape sequences */
    if (ch == 0x1B) {
        if (!(mmio_r(KBD_UART_STATUS) & UART_RXAVAIL)) return 0x1B;
        int c2 = mmio_r(KBD_UART_DATA) & 0xFF;
        if (c2 == '[') {
            if (!(mmio_r(KBD_UART_STATUS) & UART_RXAVAIL)) return 0;
            int c3 = mmio_r(KBD_UART_DATA) & 0xFF;
            if (c3 == 'A') return 'U';  /* Up */
            if (c3 == 'B') return 'D';  /* Down */
            return 0;
        }
        return 0x1B;
    }
    return ch;
}

/* ---------------------------------------------------------------------------
 * Main
 * --------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    int running = 1;

    fb = (unsigned int *)FB_BASE;

    load_packages();
    draw_window();
    flip();

    while (running) {
        int key = getkey();
        int redraw = 0;

        switch (key) {
        case 0:
            /* No input — small delay to avoid busy spin */
            { volatile int d; for (d = 0; d < 50000; d++); }
            break;

        case 'U': /* Up */
            if (selected > 0) {
                selected--;
                if (selected < scroll) scroll = selected;
                redraw = 1;
            }
            break;

        case 'D': /* Down */
            if (selected < npkg - 1) {
                selected++;
                if (selected >= scroll + LIST_ROWS)
                    scroll = selected - LIST_ROWS + 1;
                redraw = 1;
            }
            break;

        case 'r':
        case 'R':
            if (npkg > 0) {
                char cmd[128];
                snprintf(cmd, sizeof(cmd), "tpkg remove %s", packages[selected].name);
                system(cmd);
                load_packages();
                redraw = 1;
            }
            break;

        case 'q':
        case 'Q':
        case 0x1B: /* ESC */
            running = 0;
            break;

        default:
            break;
        }

        if (redraw) {
            draw_window();
            flip();
        }
    }

    /* Restore desktop */
    fill(0, 0, FB_WIDTH, FB_HEIGHT, COL_DESKTOP);
    flip();

    return 0;
}
