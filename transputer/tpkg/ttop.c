/*
 * ttop.c - TransputerOS System Monitor
 *
 * A "top"-like process monitor rendering directly to the 800x600
 * ARGB framebuffer at 0x81e00000 with INMOS-teal styled window chrome.
 *
 * Window: 600x400, centered on 800x600 desktop.
 * Uses the 5x7 mini bitmap font (same as DOOM video driver / tpkg-ui).
 * Auto-refreshes every ~2 seconds, reads keyboard from UART1.
 *
 * TCC constraints: no VLAs, functions under ~80 lines, int counters.
 */

#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Framebuffer hardware
 * ------------------------------------------------------------------------- */
#define FB_BASE         0x81E00000
#define FB_WIDTH        800
#define FB_HEIGHT       600

/* Keyboard UART */
#define KBD_UART_DATA   0x11000000
#define KBD_UART_STATUS 0x11000004
#define UART_RXAVAIL    0x02

/* T800 timer (read via LDTIMER inline) */
#define TIMER_HZ        20000000  /* 20 MHz */

/* -------------------------------------------------------------------------
 * INMOS color palette
 * ------------------------------------------------------------------------- */
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
#define COL_STATUSBAR   0xFF263238
#define COL_VERSION     0xFF80CBC4
#define COL_BAR_FILL    0xFF00897B
#define COL_BAR_EMPTY   0xFF0D1520
#define COL_BAR_BG      0xFF122030
#define COL_INFO_BG     0xFF00796B
#define COL_SEPARATOR   0xFF263238
#define COL_STATE_R     0xFF4CAF50
#define COL_STATE_S     0xFF607D8B
#define COL_STATE_Z     0xFFFF5722

/* -------------------------------------------------------------------------
 * Window layout
 * ------------------------------------------------------------------------- */
#define WIN_W           600
#define WIN_H           400
#define WIN_X           ((FB_WIDTH - WIN_W) / 2)
#define WIN_Y           ((FB_HEIGHT - WIN_H) / 2)
#define TITLE_H         22
#define BORDER          3
#define CLIENT_X        (WIN_X + BORDER)
#define CLIENT_Y        (WIN_Y + TITLE_H)
#define CLIENT_W        (WIN_W - BORDER * 2)
#define CLIENT_H        (WIN_H - TITLE_H - BORDER)

/* Info bar */
#define INFO_Y          (CLIENT_Y + 2)
#define INFO_H          14

/* CPU/MEM bars */
#define BAR_Y           (INFO_Y + INFO_H + 4)
#define BAR_H           12
#define BAR_X           (CLIENT_X + 60)
#define BAR_W           (CLIENT_W - 70)
#define BAR_GAP         16

/* Process list */
#define PROC_HDR_Y      (BAR_Y + BAR_H * 2 + BAR_GAP + 8)
#define PROC_LIST_Y     (PROC_HDR_Y + 14)
#define PROC_ROW_H      13
#define PROC_VISIBLE    16

/* Status bar */
#define STATUS_Y        (CLIENT_Y + CLIENT_H - 14)

/* -------------------------------------------------------------------------
 * MMIO helpers
 * ------------------------------------------------------------------------- */
static unsigned int *fb;

static void mmio_w(unsigned int addr, unsigned int val)
{
    *(volatile unsigned int *)addr = val;
}

static unsigned int mmio_r(unsigned int addr)
{
    return *(volatile unsigned int *)addr;
}

/* -------------------------------------------------------------------------
 * Read T800 low-priority timer
 * ------------------------------------------------------------------------- */
static unsigned int read_timer(void)
{
    /* LDTIMER: opcode 0x22 (OPR 2), but in C we read a memory-mapped
     * location that the T4 emulator provides, or use inline.
     * For the T4 emulator, LDTIMER is available as a built-in. */
    unsigned int t;
    __asm__ volatile (".byte 0x22" : "=a"(t));
    return t;
}

/* -------------------------------------------------------------------------
 * Pixel drawing
 * ------------------------------------------------------------------------- */
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

/* -------------------------------------------------------------------------
 * 5x7 mini font (same as DOOM video driver and tpkg-ui)
 * ------------------------------------------------------------------------- */
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
    {0x08,0x08,0x08,0x08,0x08}, /* - (37) */
    {0x00,0x60,0x60,0x00,0x00}, /* . (38) */
    {0x00,0x36,0x36,0x00,0x00}, /* : (39) */
    {0x20,0x10,0x08,0x04,0x02}, /* / (40) */
    {0x14,0x14,0x14,0x14,0x14}, /* = (41) */
    {0x00,0x00,0x00,0x00,0x00}, /* | (42) -- thin bar */
    {0x36,0x49,0x49,0x49,0x36}, /* % (43) */
    {0x00,0x44,0x7D,0x40,0x00}, /* ! (44) */
    {0x08,0x04,0x7E,0x04,0x08}, /* ^ (45) -- up arrow */
    {0x10,0x20,0x7E,0x20,0x10}, /* v (46) -- down arrow */
    {0x22,0x14,0x08,0x14,0x22}, /* * (47) */
    {0x3E,0x41,0x41,0x41,0x3E}, /* ( (48) = same as O */
    {0x3E,0x41,0x41,0x41,0x3E}, /* ) (49) = same as O */
    {0x7F,0x7F,0x7F,0x7F,0x7F}, /* block (50) -- solid fill */
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
    if (c == '=') return 41;
    if (c == '|') return 42;
    if (c == '%') return 43;
    if (c == '!') return 44;
    if (c == '^') return 45;
    if (c == 'v') return 46;
    if (c == '*') return 47;
    if (c == '(') return 48;
    if (c == ')') return 49;
    return 0;
}

static void drawch(int px_x, int py, char c, unsigned int fg, unsigned int bg)
{
    int idx = fidx(c);
    int col, row;
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

static void draws_c(int cx, int cy, int w, const char *s,
                     unsigned int fg, unsigned int bg)
{
    int len = strlen(s);
    int tx = cx + (w - len * 6) / 2;
    draws(tx, cy, s, fg, bg);
}

/* -------------------------------------------------------------------------
 * Simulated process table
 * ------------------------------------------------------------------------- */
#define MAX_PROC 16

typedef struct {
    int pid;
    char name[20];
    char state;      /* R = running, S = sleeping, Z = zombie */
    int  cpu_pct;    /* 0-100 */
    int  mem_kb;
} proc_t;

static proc_t procs[MAX_PROC];
static int nproc = 0;
static int selected = 0;
static int scroll = 0;

static void init_procs(void)
{
    /* Simulated process list matching TransputerOS boot */
    nproc = 0;

    procs[0].pid = 1;   procs[0].state = 'S'; procs[0].cpu_pct = 0;
    procs[0].mem_kb = 48; strcpy(procs[0].name, "init");

    procs[1].pid = 2;   procs[1].state = 'S'; procs[1].cpu_pct = 0;
    procs[1].mem_kb = 0;  strcpy(procs[1].name, "kthreadd");

    procs[2].pid = 3;   procs[2].state = 'S'; procs[2].cpu_pct = 1;
    procs[2].mem_kb = 0;  strcpy(procs[2].name, "ksoftirqd/0");

    procs[3].pid = 10;  procs[3].state = 'S'; procs[3].cpu_pct = 0;
    procs[3].mem_kb = 0;  strcpy(procs[3].name, "kworker/0:1");

    procs[4].pid = 42;  procs[4].state = 'S'; procs[4].cpu_pct = 2;
    procs[4].mem_kb = 384; strcpy(procs[4].name, "busybox");

    procs[5].pid = 43;  procs[5].state = 'S'; procs[5].cpu_pct = 3;
    procs[5].mem_kb = 128; strcpy(procs[5].name, "tpsh");

    procs[6].pid = 50;  procs[6].state = 'R'; procs[6].cpu_pct = 18;
    procs[6].mem_kb = 256; strcpy(procs[6].name, "fbwm");

    procs[7].pid = 55;  procs[7].state = 'R'; procs[7].cpu_pct = 42;
    procs[7].mem_kb = 1280; strcpy(procs[7].name, "doom");

    procs[8].pid = 60;  procs[8].state = 'R'; procs[8].cpu_pct = 8;
    procs[8].mem_kb = 64;  strcpy(procs[8].name, "ttop");

    nproc = 9;
}

/* -------------------------------------------------------------------------
 * System stats (simulated / timer-derived)
 * ------------------------------------------------------------------------- */
static int cpu_pct = 0;
static int mem_pct = 0;
static int mem_used_mb = 0;
static int mem_total_mb = 256;
static unsigned int uptime_sec = 0;
static unsigned int last_timer = 0;
static unsigned int timer_accum = 0;
static int refresh_counter = 0;

static void update_stats(void)
{
    unsigned int now = read_timer();
    unsigned int delta;
    int total_cpu;
    int i;

    /* Compute elapsed time */
    delta = now - last_timer;
    last_timer = now;
    timer_accum += delta;

    /* Convert timer ticks to seconds (20 MHz timer) */
    uptime_sec = timer_accum / TIMER_HZ;

    /* Sum CPU from all processes */
    total_cpu = 0;
    for (i = 0; i < nproc; i++)
        total_cpu += procs[i].cpu_pct;
    cpu_pct = total_cpu;
    if (cpu_pct > 100) cpu_pct = 100;

    /* Simulate memory usage from process table */
    mem_used_mb = 0;
    for (i = 0; i < nproc; i++)
        mem_used_mb += procs[i].mem_kb;
    mem_used_mb = mem_used_mb / 1024;
    /* Add kernel overhead (~48MB) */
    mem_used_mb += 48;
    mem_pct = (mem_used_mb * 100) / mem_total_mb;
    if (mem_pct > 100) mem_pct = 100;

    /* Simulate slight CPU variation each refresh */
    refresh_counter++;
    if (refresh_counter & 1) {
        procs[7].cpu_pct = 42 + (refresh_counter % 7);  /* doom */
        procs[6].cpu_pct = 18 + (refresh_counter % 3);  /* fbwm */
        procs[8].cpu_pct = 8  + (refresh_counter % 2);  /* ttop */
    } else {
        procs[7].cpu_pct = 42 - (refresh_counter % 5);
        procs[6].cpu_pct = 18 - (refresh_counter % 2);
        procs[8].cpu_pct = 8;
    }
}

/* -------------------------------------------------------------------------
 * Drawing: Window chrome
 * ------------------------------------------------------------------------- */
static void draw_desktop(void)
{
    fill(0, 0, FB_WIDTH, FB_HEIGHT, COL_DESKTOP);
}

static void draw_border(void)
{
    int x = WIN_X, y = WIN_Y, w = WIN_W, h = WIN_H;
    int i;

    /* Top edges (light) */
    hline(x, y, w, COL_BORDER_LT);
    hline(x, y + 1, w, COL_BORDER_LT);

    /* Bottom edges (dark) */
    hline(x, y + h - 1, w, COL_BORDER_DK);
    hline(x, y + h - 2, w, COL_BORDER_DK);

    /* Side edges */
    for (i = 0; i < h; i++) {
        px(x, y + i, COL_BORDER_LT);
        px(x + 1, y + i, COL_BORDER_LT);
        px(x + w - 1, y + i, COL_BORDER_DK);
        px(x + w - 2, y + i, COL_BORDER_DK);
    }
}

static void draw_titlebar(void)
{
    /* Teal titlebar */
    fill(WIN_X + BORDER, WIN_Y + BORDER,
         WIN_W - BORDER * 2, TITLE_H - BORDER, COL_TITLEBAR);
    draws_c(WIN_X + BORDER, WIN_Y + BORDER + 5,
            WIN_W - BORDER * 2,
            "TTOP - SYSTEM MONITOR", COL_TITLE_TEXT, COL_TITLEBAR);
}

/* -------------------------------------------------------------------------
 * Drawing: Info bar
 * ------------------------------------------------------------------------- */
static void draw_info_bar(void)
{
    fill(CLIENT_X, INFO_Y, CLIENT_W, INFO_H, COL_INFO_BG);
    draws_c(CLIENT_X, INFO_Y + 3, CLIENT_W,
            "T800 @ 20MHZ  |  256MB RAM  |  TRANSPUTEROS 0.1.0",
            COL_TITLE_TEXT, COL_INFO_BG);
}

/* -------------------------------------------------------------------------
 * Drawing: CPU and memory bars
 * ------------------------------------------------------------------------- */
static void draw_bar(int y, const char *label, int pct,
                     unsigned int fill_col)
{
    char buf[48];
    int fill_w;
    int i;

    /* Label */
    draws(CLIENT_X + 4, y + 2, label, COL_WINTXT, COL_WINBG);

    /* Bar background */
    fill(BAR_X, y, BAR_W, BAR_H, COL_BAR_EMPTY);

    /* Bar outline */
    hline(BAR_X, y, BAR_W, COL_SEPARATOR);
    hline(BAR_X, y + BAR_H - 1, BAR_W, COL_SEPARATOR);

    /* Filled portion */
    fill_w = (BAR_W - 2) * pct / 100;
    if (fill_w > 0)
        fill(BAR_X + 1, y + 1, fill_w, BAR_H - 2, fill_col);

    /* Percentage text (right-aligned on bar) */
    snprintf(buf, sizeof(buf), "%d%%", pct);
    draws(BAR_X + BAR_W + 6, y + 2, buf, COL_WINTXT, COL_WINBG);
}

static void draw_cpu_mem(void)
{
    char mem_label[24];

    /* Clear the bar area */
    fill(CLIENT_X, BAR_Y, CLIENT_W,
         BAR_H * 2 + BAR_GAP, COL_WINBG);

    draw_bar(BAR_Y, "CPU", cpu_pct, COL_BAR_FILL);

    snprintf(mem_label, sizeof(mem_label), "MEM");
    draw_bar(BAR_Y + BAR_H + 4, mem_label, mem_pct, COL_BAR_FILL);

    /* Memory detail */
    {
        char det[48];
        snprintf(det, sizeof(det), "%dMB / %dMB",
                 mem_used_mb, mem_total_mb);
        draws(CLIENT_X + 4, BAR_Y + BAR_H + 4 + BAR_H + 2,
              det, COL_VERSION, COL_WINBG);
    }
}

/* -------------------------------------------------------------------------
 * Drawing: Process list header
 * ------------------------------------------------------------------------- */
static void draw_proc_header(void)
{
    /* Separator line */
    hline(CLIENT_X, PROC_HDR_Y - 3, CLIENT_W, COL_SEPARATOR);

    /* Header background */
    fill(CLIENT_X, PROC_HDR_Y, CLIENT_W, 12, COL_HEADER);

    draws(CLIENT_X + 6,   PROC_HDR_Y + 2, "PID",   COL_TITLE_TEXT, COL_HEADER);
    draws(CLIENT_X + 46,  PROC_HDR_Y + 2, "STATE", COL_TITLE_TEXT, COL_HEADER);
    draws(CLIENT_X + 96,  PROC_HDR_Y + 2, "CPU%",  COL_TITLE_TEXT, COL_HEADER);
    draws(CLIENT_X + 140, PROC_HDR_Y + 2, "MEM",   COL_TITLE_TEXT, COL_HEADER);
    draws(CLIENT_X + 200, PROC_HDR_Y + 2, "NAME",  COL_TITLE_TEXT, COL_HEADER);
}

/* -------------------------------------------------------------------------
 * Drawing: Process list rows
 * ------------------------------------------------------------------------- */
static void draw_proc_row(int row_idx, int proc_idx)
{
    int ry = PROC_LIST_Y + row_idx * PROC_ROW_H;
    unsigned int bg, fg, state_col;
    char buf[8];
    proc_t *p;

    p = &procs[proc_idx];
    bg = (proc_idx == selected) ? COL_SELECT : COL_WINBG;
    fg = (proc_idx == selected) ? COL_ACCENT : COL_WINTXT;

    /* Row background */
    fill(CLIENT_X, ry, CLIENT_W, PROC_ROW_H, bg);

    /* Alternating subtle stripe for unselected rows */
    if (proc_idx != selected && (row_idx & 1))
        fill(CLIENT_X, ry, CLIENT_W, PROC_ROW_H, COL_BAR_BG);

    /* PID */
    snprintf(buf, sizeof(buf), "%d", p->pid);
    draws(CLIENT_X + 6, ry + 3, buf, fg,
          (proc_idx == selected) ? bg :
          ((row_idx & 1) ? COL_BAR_BG : bg));

    /* State with color coding */
    if (p->state == 'R') state_col = COL_STATE_R;
    else if (p->state == 'Z') state_col = COL_STATE_Z;
    else state_col = COL_STATE_S;

    buf[0] = p->state; buf[1] = 0;
    draws(CLIENT_X + 56, ry + 3, buf, state_col,
          (proc_idx == selected) ? bg :
          ((row_idx & 1) ? COL_BAR_BG : bg));

    /* CPU% */
    snprintf(buf, sizeof(buf), "%d", p->cpu_pct);
    {
        unsigned int rbg = (proc_idx == selected) ? bg :
                           ((row_idx & 1) ? COL_BAR_BG : bg);
        draws(CLIENT_X + 102, ry + 3, buf, fg, rbg);

        /* MEM (KB) */
        snprintf(buf, sizeof(buf), "%dK", p->mem_kb);
        draws(CLIENT_X + 142, ry + 3, buf, COL_VERSION, rbg);

        /* Name */
        draws(CLIENT_X + 200, ry + 3, p->name, fg, rbg);
    }
}

static void draw_proc_list(void)
{
    int i;
    int visible = PROC_VISIBLE;

    /* Clear list area */
    fill(CLIENT_X, PROC_LIST_Y, CLIENT_W,
         visible * PROC_ROW_H, COL_WINBG);

    for (i = 0; i < visible && (scroll + i) < nproc; i++)
        draw_proc_row(i, scroll + i);

    /* If fewer processes than visible rows, show empty space */
    if (nproc == 0) {
        draws_c(CLIENT_X, PROC_LIST_Y + 30, CLIENT_W,
                "NO PROCESSES", COL_VERSION, COL_WINBG);
    }
}

/* -------------------------------------------------------------------------
 * Drawing: Status bar
 * ------------------------------------------------------------------------- */
static void draw_status(void)
{
    unsigned int days, hours, mins;

    fill(CLIENT_X, STATUS_Y, CLIENT_W, 14, COL_STATUSBAR);

    /* Key help */
    draws(CLIENT_X + 6, STATUS_Y + 3,
          "Q:QUIT  UP/DOWN:SELECT  K:KILL  R:REFRESH",
          COL_VERSION, COL_STATUSBAR);

    /* Uptime (right side) */
    days = uptime_sec / 86400;
    hours = (uptime_sec % 86400) / 3600;
    mins = (uptime_sec % 3600) / 60;

    {
        char ubuf[32];
        snprintf(ubuf, sizeof(ubuf), "UP: %dD %dH %dM",
                 days, hours, mins);
        draws(CLIENT_X + CLIENT_W - strlen(ubuf) * 6 - 6,
              STATUS_Y + 3, ubuf, COL_WINTXT, COL_STATUSBAR);
    }
}

/* -------------------------------------------------------------------------
 * Full redraw
 * ------------------------------------------------------------------------- */
static void flip(void) { }

static void draw_all(void)
{
    draw_desktop();
    draw_border();
    draw_titlebar();
    draw_info_bar();
    draw_cpu_mem();
    draw_proc_header();
    draw_proc_list();
    draw_status();
}

/* -------------------------------------------------------------------------
 * Keyboard input (reads from UART1)
 * ------------------------------------------------------------------------- */
static int getkey(void)
{
    int ch, c2, c3;

    if (!(mmio_r(KBD_UART_STATUS) & UART_RXAVAIL))
        return 0;
    ch = mmio_r(KBD_UART_DATA) & 0xFF;

    /* VT100 escape sequences */
    if (ch == 0x1B) {
        if (!(mmio_r(KBD_UART_STATUS) & UART_RXAVAIL)) return 0x1B;
        c2 = mmio_r(KBD_UART_DATA) & 0xFF;
        if (c2 == '[') {
            if (!(mmio_r(KBD_UART_STATUS) & UART_RXAVAIL)) return 0;
            c3 = mmio_r(KBD_UART_DATA) & 0xFF;
            if (c3 == 'A') return 'U';  /* Up */
            if (c3 == 'B') return 'D';  /* Down */
            return 0;
        }
        return 0x1B;
    }
    return ch;
}

/* -------------------------------------------------------------------------
 * Handle input
 * ------------------------------------------------------------------------- */
static int handle_key(int key)
{
    switch (key) {
    case 'U': /* Up */
        if (selected > 0) {
            selected--;
            if (selected < scroll) scroll = selected;
        }
        return 1;

    case 'D': /* Down */
        if (selected < nproc - 1) {
            selected++;
            if (selected >= scroll + PROC_VISIBLE)
                scroll = selected - PROC_VISIBLE + 1;
        }
        return 1;

    case 'k':
    case 'K':
        /* "Kill" selected process (remove from list) */
        if (nproc > 1 && selected < nproc) {
            int i;
            for (i = selected; i < nproc - 1; i++)
                procs[i] = procs[i + 1];
            nproc--;
            if (selected >= nproc) selected = nproc - 1;
            if (selected < 0) selected = 0;
        }
        return 1;

    case 'r':
    case 'R':
        update_stats();
        return 1;

    case 'q':
    case 'Q':
    case 0x1B:
        return -1; /* quit */

    default:
        return 0;
    }
}

/* -------------------------------------------------------------------------
 * Main entry point
 * ------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    int running = 1;
    int auto_refresh = 0;

    (void)argc;
    (void)argv;

    fb = (unsigned int *)FB_BASE;

    /* Initialize state */
    last_timer = read_timer();
    timer_accum = 0;
    init_procs();
    update_stats();

    /* Initial draw */
    draw_all();
    flip();

    while (running) {
        int key = getkey();
        int redraw = 0;

        if (key == 0) {
            /* No input -- delay loop, then check auto-refresh */
            volatile int d;
            for (d = 0; d < 100000; d++);
            auto_refresh++;

            /* Auto-refresh roughly every 2 seconds */
            if (auto_refresh > 200) {
                auto_refresh = 0;
                update_stats();
                redraw = 1;
            }
        } else {
            int r = handle_key(key);
            if (r < 0) {
                running = 0;
            } else {
                redraw = 1;
                if (r) update_stats();
            }
        }

        if (redraw) {
            draw_all();
            flip();
        }
    }

    /* Restore desktop on exit */
    fill(0, 0, FB_WIDTH, FB_HEIGHT, COL_DESKTOP);
    flip();

    return 0;
}
