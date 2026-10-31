/*
 * tman.c - TransputerOS Task Manager
 *
 * A full-screen (800x600) task manager rendering directly to the ARGB
 * framebuffer with INMOS-teal chrome, in the fbwm/ttop visual family —
 * but with REAL data: processes enumerated from /proc via raw
 * getdents64, CPU%% and memory from /proc/stat and /proc/meminfo
 * deltas, per-process CPU from utime+stime deltas, and 64-sample
 * history sparklines for CPU and memory.
 *
 * Keys: Q quit | up/down select | S cycle sort (CPU/MEM/PID) | SPACE refresh
 *
 * Live refresh: ~500 ms (TIMER_HZ/2 on the low-priority timer).  Reads
 * real machine data from /proc — not synthetic placeholders.
 *
 * Raw getdents64 note: the kernel emits CLASSIC linux_dirent64 layout
 * (name at byte offset 19).  TCC pads the C struct so d_name lands at
 * 20 — so dirents are parsed here with explicit byte offsets, never
 * through a struct or readdir().
 *
 * TCC constraints honoured: no VLAs, modest functions, int counters.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

/* ------------------------------------------------------------------
 * Hardware
 * ------------------------------------------------------------------ */
#define FB_BASE         0x81E00000
#define FB_WIDTH        800
#define FB_HEIGHT       600

#define VGA_FLIP        0xA0000014

#define KBD_UART_DATA   0x11000000
#define KBD_UART_STATUS 0x11000004
#define UART_RXAVAIL    0x02

#define TIMER_HZ        15625       /* low-priority timer ticks/sec */
#define REFRESH_TICKS   (TIMER_HZ / 2)  /* ~500 ms live refresh */
#define JIFFY_HZ        100         /* kernel HZ for utime/stime */

/* ------------------------------------------------------------------
 * INMOS palette (fbwm / ttop family)
 * ------------------------------------------------------------------ */
#define COL_DESKTOP     0xFF0F1A2E
#define COL_TITLEBAR    0xFF00897B
#define COL_TITLE_TEXT  0xFFE0F2F1
#define COL_PANEL       0xFF1A2332
#define COL_PANEL_EDGE  0xFF37474F
#define COL_TEXT        0xFFB2DFDB
#define COL_TEXT_DIM    0xFF607D8B
#define COL_ACCENT      0xFF00BCD4
#define COL_HEADER      0xFF00695C
#define COL_ROW_A       0xFF16202F
#define COL_ROW_B       0xFF121B29
#define COL_SELECT      0xFF004D40
#define COL_STATUSBAR   0xFF263238
#define COL_GAUGE_BG    0xFF0D1520
#define COL_GAUGE_OK    0xFF00897B
#define COL_GAUGE_HOT   0xFFFF7043
#define COL_SPARK       0xFF00BCD4
#define COL_SPARK_DIM   0xFF00695C
#define COL_STATE_R     0xFF4CAF50
#define COL_STATE_S     0xFF607D8B
#define COL_STATE_D     0xFFFFC107
#define COL_STATE_Z     0xFFFF5722

/* ------------------------------------------------------------------
 * Layout
 * ------------------------------------------------------------------ */
#define TITLE_H         24
#define TILE_Y          32
#define TILE_H          72
#define TILE_W          256
#define TILE_GAP        8
#define GRAPH_Y         (TILE_Y + TILE_H + 8)
#define GRAPH_H         86
#define GRAPH_W         392
#define TABLE_HDR_Y     (GRAPH_Y + GRAPH_H + 10)
#define TABLE_Y         (TABLE_HDR_Y + 16)
#define ROW_H           15
#define STATUS_H        20
#define STATUS_Y        (FB_HEIGHT - STATUS_H)
#define VISIBLE_ROWS    ((STATUS_Y - 4 - TABLE_Y) / ROW_H)

/* Table columns (x positions) */
#define C_PID           12
#define C_STATE         60
#define C_CPU           110
#define C_CPUBAR        150
#define C_THR           220
#define C_MEM           270
#define C_TIME          350
#define C_NAME          420

#define HIST_LEN        64
#define MAX_PROCS       64

/* ------------------------------------------------------------------
 * Syscall gateway (for raw getdents64)
 * ------------------------------------------------------------------ */
extern long __transputer_syscall(long number, long a1, long a2, long a3,
                                 long a4, long a5, long a6);
#define NR_GETDENTS64 61

unsigned int __ldtimer(void);

/* ------------------------------------------------------------------
 * Framebuffer primitives
 * ------------------------------------------------------------------ */
static unsigned int *fb = (unsigned int *)FB_BASE;

static void px(int x, int y, unsigned int c)
{
    if (x >= 0 && x < FB_WIDTH && y >= 0 && y < FB_HEIGHT)
        fb[y * FB_WIDTH + x] = c;
}

static void fill(int x, int y, int w, int h, unsigned int c)
{
    int r, i;
    for (r = 0; r < h; r++) {
        int off = (y + r) * FB_WIDTH + x;
        for (i = 0; i < w; i++)
            fb[off + i] = c;
    }
}

static void rect(int x, int y, int w, int h, unsigned int c)
{
    int i;
    for (i = 0; i < w; i++) { px(x + i, y, c); px(x + i, y + h - 1, c); }
    for (i = 0; i < h; i++) { px(x, y + i, c); px(x + w - 1, y + i, c); }
}

static void present(void)
{
    *(volatile unsigned int *)VGA_FLIP = FB_BASE;
}

/* ------------------------------------------------------------------
 * 5x7 mini font (ttop/tpkg-ui family)
 * ------------------------------------------------------------------ */
static const unsigned char mfont[][5] = {
    {0x00,0x00,0x00,0x00,0x00},                         /* space */
    {0x7E,0x09,0x09,0x09,0x7E}, {0x7F,0x49,0x49,0x49,0x36},
    {0x3E,0x41,0x41,0x41,0x22}, {0x7F,0x41,0x41,0x41,0x3E},
    {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F},
    {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01},
    {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x04,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F},
    {0x3E,0x41,0x41,0x41,0x3E}, {0x7F,0x09,0x09,0x09,0x06},
    {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01},
    {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F},
    {0x3F,0x40,0x30,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43},
    {0x3E,0x51,0x49,0x45,0x3E},                         /* 0 */
    {0x00,0x42,0x7F,0x40,0x00}, {0x42,0x61,0x51,0x49,0x46},
    {0x21,0x41,0x45,0x4B,0x31}, {0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x30},
    {0x01,0x71,0x09,0x05,0x03}, {0x36,0x49,0x49,0x49,0x36},
    {0x06,0x49,0x49,0x29,0x1E},
    {0x08,0x08,0x08,0x08,0x08},                         /* - */
    {0x00,0x60,0x60,0x00,0x00},                         /* . */
    {0x00,0x36,0x36,0x00,0x00},                         /* : */
    {0x20,0x10,0x08,0x04,0x02},                         /* / */
    {0x14,0x14,0x14,0x14,0x14},                         /* = */
    {0x23,0x13,0x08,0x64,0x62},                         /* % */
    {0x08,0x04,0x7E,0x04,0x08},                         /* ^ */
    {0x10,0x20,0x7E,0x20,0x10},                         /* v */
    {0x00,0x00,0x5F,0x00,0x00},                         /* ! */
    {0x02,0x01,0x51,0x09,0x06},                         /* ? */
    {0x00,0x08,0x1C,0x3E,0x00},                         /* < (arrow) */
    {0x00,0x3E,0x1C,0x08,0x00},                         /* > (arrow) */
    {0x0C,0x1E,0x1E,0x0C,0x00},                         /* @ = dot */
};

static int fidx(char c)
{
    if (c >= 'A' && c <= 'Z') return 1 + (c - 'A');
    if (c >= 'a' && c <= 'z') return 1 + (c - 'a');
    if (c >= '0' && c <= '9') return 27 + (c - '0');
    if (c == '-') return 37;
    if (c == '.') return 38;
    if (c == ':') return 39;
    if (c == '/') return 40;
    if (c == '=') return 41;
    if (c == '%') return 42;
    if (c == '^') return 43;
    if (c == 'v') return 44;   /* only as arrow via draws() callers */
    if (c == '!') return 45;
    if (c == '?') return 46;
    if (c == '<') return 47;
    if (c == '>') return 48;
    if (c == '@') return 49;
    return 0;
}

static void drawch(int x, int y, char c, unsigned int fg, unsigned int bg)
{
    int idx = fidx(c);
    int col, row;
    for (col = 0; col < 5; col++) {
        unsigned char bits = mfont[idx][col];
        for (row = 0; row < 7; row++)
            px(x + col, y + row, (bits & (1 << row)) ? fg : bg);
    }
    for (row = 0; row < 7; row++)
        px(x + 5, y + row, bg);
}

static void draws(int x, int y, const char *s, unsigned int fg, unsigned int bg)
{
    while (*s) { drawch(x, y, *s, fg, bg); x += 6; s++; }
}

/* 2x-scaled text for tile numbers and the title */
static void drawch2(int x, int y, char c, unsigned int fg, unsigned int bg)
{
    int idx = fidx(c);
    int col, row;
    for (col = 0; col < 5; col++) {
        unsigned char bits = mfont[idx][col];
        for (row = 0; row < 7; row++) {
            unsigned int c2 = (bits & (1 << row)) ? fg : bg;
            fill(x + col * 2, y + row * 2, 2, 2, c2);
        }
    }
    fill(x + 10, y, 2, 14, bg);
}

static void draws2(int x, int y, const char *s, unsigned int fg, unsigned int bg)
{
    while (*s) { drawch2(x, y, *s, fg, bg); x += 12; s++; }
}

/* ------------------------------------------------------------------
 * Small file reader (for /proc)
 * ------------------------------------------------------------------ */
static int read_file(const char *path, char *buf, int cap)
{
    int fd = open(path, O_RDONLY);
    int n, total = 0;
    if (fd < 0)
        return -1;
    while (total < cap - 1) {
        n = read(fd, buf + total, cap - 1 - total);
        if (n <= 0)
            break;
        total += n;
    }
    close(fd);
    buf[total] = 0;
    return total;
}

/* Skip to field N (space separated), fields counted from 0 at p. */
static const char *skip_fields(const char *p, int n)
{
    while (n > 0 && *p) {
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        n--;
    }
    return p;
}

static long field_num(const char *p, int n)
{
    return atol(skip_fields(p, n));
}

/* ------------------------------------------------------------------
 * System stats
 * ------------------------------------------------------------------ */
typedef struct {
    long cpu_total, cpu_idle;          /* /proc/stat jiffies         */
    int  cpu_pct;                      /* derived, 0-100             */
    long mem_total_kb, mem_avail_kb;   /* /proc/meminfo              */
    int  mem_pct;
    long uptime_s;
    char loadavg[24];
} sysstat_t;

static sysstat_t cur, prev;

static void read_sysstat(void)
{
    static char buf[1024];
    prev = cur;

    if (read_file("/proc/stat", buf, sizeof buf) > 0) {
        /* "cpu  user nice system idle ..." */
        const char *p = skip_fields(buf, 1);
        long user = atol(p);
        long nice = field_num(p, 1);
        long sys  = field_num(p, 2);
        long idle = field_num(p, 3);
        cur.cpu_total = user + nice + sys + idle;
        cur.cpu_idle  = idle;
        if (cur.cpu_total > prev.cpu_total) {
            long dt = cur.cpu_total - prev.cpu_total;
            long di = cur.cpu_idle - prev.cpu_idle;
            cur.cpu_pct = (int)((dt - di) * 100 / dt);
            if (cur.cpu_pct < 0) cur.cpu_pct = 0;
            if (cur.cpu_pct > 100) cur.cpu_pct = 100;
        }
    }

    if (read_file("/proc/meminfo", buf, sizeof buf) > 0) {
        char *t = strstr(buf, "MemTotal:");
        char *f = strstr(buf, "MemFree:");
        char *a = strstr(buf, "MemAvailable:");
        if (t) cur.mem_total_kb = atol(t + 9);
        if (a) cur.mem_avail_kb = atol(a + 13);
        else if (f) cur.mem_avail_kb = atol(f + 8);
        if (cur.mem_total_kb > 0)
            cur.mem_pct = (int)((cur.mem_total_kb - cur.mem_avail_kb) * 100
                                / cur.mem_total_kb);
    }

    if (read_file("/proc/uptime", buf, sizeof buf) > 0)
        cur.uptime_s = atol(buf);

    if (read_file("/proc/loadavg", buf, sizeof buf) > 0) {
        int i = 0;
        const char *p = buf;
        /* keep "a.aa b.bb c.cc" */
        while (*p && *p != '\n' && i < 14)
            cur.loadavg[i++] = *p++;
        cur.loadavg[i] = 0;
    } else {
        strcpy(cur.loadavg, "-");
    }
}

/* ------------------------------------------------------------------
 * Process table from /proc
 * ------------------------------------------------------------------ */
typedef struct {
    int  pid;
    char name[18];
    char state;
    long jiffies;       /* utime+stime  */
    long prev_jiffies;
    int  cpu_pct;
    int  threads;       /* from /proc/<pid>/status Threads: */
    long mem_kb;        /* rss pages * 4 */
} proc_t;

static proc_t procs[MAX_PROCS];
static int nprocs;

static proc_t prev_procs[MAX_PROCS];
static int nprev;

static long prev_find(int pid)
{
    int i;
    for (i = 0; i < nprev; i++)
        if (prev_procs[i].pid == pid)
            return prev_procs[i].jiffies;
    return -1;
}

/* Parse /proc/<pid>/stat: "pid (comm) S ppid ... utime stime ... num_threads
 * ... rss" — comm may contain spaces; find the LAST ')'.
 * Field indices after state: utime=11 stime=12 num_threads=17 rss=21. */
static int read_proc_one(int pid, proc_t *p)
{
    static char buf[512];
    char path[40];
    const char *q;
    char *close_paren;
    int i;
    long thr;

    sprintf(path, "/proc/%d/stat", pid);
    if (read_file(path, buf, sizeof buf) <= 0)
        return 0;

    p->pid = pid;
    p->threads = 1;

    close_paren = strrchr(buf, ')');
    if (!close_paren)
        return 0;

    /* comm between '(' and last ')' */
    q = strchr(buf, '(');
    if (q) {
        int len = 0;
        q++;
        while (q + len < close_paren && len < 17)
            len++;
        for (i = 0; i < len; i++)
            p->name[i] = q[i];
        p->name[len] = 0;
    } else {
        strcpy(p->name, "?");
    }

    q = close_paren + 2;           /* "…) S rest" -> at state char  */
    p->state = *q;

    p->jiffies = field_num(q, 11) + field_num(q, 12);
    thr = field_num(q, 17);
    p->threads = (thr > 0 && thr < 10000) ? (int)thr : 1;
    p->mem_kb  = field_num(q, 21) * 4;   /* pages -> KB */
    return 1;
}

static void read_procs(void)
{
    static char dents[4096];
    int fd, n, off, i;
    int from_dents = 0;

    memcpy(prev_procs, procs, sizeof(procs));
    nprev = nprocs;
    nprocs = 0;

    /*
     * Preferred path: getdents64 on /proc now goes through iterate_dir
     * so PID directories from proc_pid_readdir are included.  Classic
     * dirent layout: reclen @+16, name @+19.
     */
    fd = open("/proc", O_RDONLY);
    if (fd >= 0) {
        for (;;) {
            n = (int)__transputer_syscall(NR_GETDENTS64, fd,
                                          (long)dents, sizeof dents, 0, 0, 0);
            if (n <= 0)
                break;
            off = 0;
            while (off < n && nprocs < MAX_PROCS) {
                int reclen = (unsigned char)dents[off + 16] |
                             ((unsigned char)dents[off + 17] << 8);
                const char *name = dents + off + 19;
                if (name[0] >= '1' && name[0] <= '9') {
                    int pid = atoi(name);
                    if (pid > 0 && read_proc_one(pid, &procs[nprocs])) {
                        nprocs++;
                        from_dents = 1;
                    }
                }
                if (reclen <= 0)
                    break;
                off += reclen;
            }
        }
        close(fd);
    }

    /*
     * Always merge a direct PID probe.  Covers kernels where getdents
     * still omits proc entries, and guarantees at least init (pid 1)
     * shows up so the PID column is never blank.
     */
    {
        int pid;
        for (pid = 1; pid <= 128 && nprocs < MAX_PROCS; pid++) {
            int have = 0, j;
            for (j = 0; j < nprocs; j++) {
                if (procs[j].pid == pid) {
                    have = 1;
                    break;
                }
            }
            if (!have && read_proc_one(pid, &procs[nprocs]))
                nprocs++;
        }
        (void)from_dents;
    }

    /* per-process CPU% from jiffy deltas */
    {
        long dt = cur.cpu_total - prev.cpu_total;
        for (i = 0; i < nprocs; i++) {
            long pj = prev_find(procs[i].pid);
            procs[i].prev_jiffies = pj;
            procs[i].cpu_pct = 0;
            if (pj >= 0 && dt > 0) {
                long dp = procs[i].jiffies - pj;
                if (dp > 0) {
                    procs[i].cpu_pct = (int)(dp * 100 / dt);
                    if (procs[i].cpu_pct > 100)
                        procs[i].cpu_pct = 100;
                }
            }
        }
    }
}

/* sort modes: 0 = CPU desc, 1 = MEM desc, 2 = PID asc */
static int sort_mode;

static int proc_before(const proc_t *a, const proc_t *b)
{
    if (sort_mode == 0) {
        if (a->cpu_pct != b->cpu_pct) return a->cpu_pct > b->cpu_pct;
        return a->pid < b->pid;
    }
    if (sort_mode == 1) {
        if (a->mem_kb != b->mem_kb) return a->mem_kb > b->mem_kb;
        return a->pid < b->pid;
    }
    return a->pid < b->pid;
}

static void sort_procs(void)
{
    int i, j;
    for (i = 1; i < nprocs; i++) {
        proc_t key = procs[i];
        j = i - 1;
        while (j >= 0 && !proc_before(&procs[j], &key)) {
            procs[j + 1] = procs[j];
            j--;
        }
        procs[j + 1] = key;
    }
}

/* ------------------------------------------------------------------
 * History rings
 * ------------------------------------------------------------------ */
static int cpu_hist[HIST_LEN];
static int mem_hist[HIST_LEN];
static int hist_pos;

static void hist_push(void)
{
    cpu_hist[hist_pos] = cur.cpu_pct;
    mem_hist[hist_pos] = cur.mem_pct;
    hist_pos = (hist_pos + 1) % HIST_LEN;
}

/* ------------------------------------------------------------------
 * Widgets
 * ------------------------------------------------------------------ */
static void draw_gauge(int x, int y, int w, int h, int pct)
{
    int fillw = w * pct / 100;
    unsigned int c = (pct >= 85) ? COL_GAUGE_HOT : COL_GAUGE_OK;
    fill(x, y, w, h, COL_GAUGE_BG);
    if (fillw > 0)
        fill(x, y, fillw, h, c);
    rect(x, y, w, h, COL_PANEL_EDGE);
}

static void draw_tile(int x, const char *label, const char *big,
                      const char *sub, int pct)
{
    fill(x, TILE_Y, TILE_W, TILE_H, COL_PANEL);
    rect(x, TILE_Y, TILE_W, TILE_H, COL_PANEL_EDGE);
    fill(x, TILE_Y, TILE_W, 3, COL_ACCENT);
    draws(x + 10, TILE_Y + 10, label, COL_TEXT_DIM, COL_PANEL);
    draws2(x + 10, TILE_Y + 22, big, COL_TITLE_TEXT, COL_PANEL);
    draws(x + 10, TILE_Y + 42, sub, COL_TEXT, COL_PANEL);
    if (pct >= 0)
        draw_gauge(x + 10, TILE_Y + TILE_H - 16, TILE_W - 20, 8, pct);
}

static void draw_graph(int x, const char *title, const int *hist)
{
    int i, gx = x + 8, gy = GRAPH_Y + 20;
    int gw = GRAPH_W - 16, gh = GRAPH_H - 30;
    int barw = gw / HIST_LEN;

    fill(x, GRAPH_Y, GRAPH_W, GRAPH_H, COL_PANEL);
    rect(x, GRAPH_Y, GRAPH_W, GRAPH_H, COL_PANEL_EDGE);
    draws(x + 8, GRAPH_Y + 7, title, COL_TEXT_DIM, COL_PANEL);

    fill(gx, gy, gw, gh, COL_GAUGE_BG);
    for (i = 0; i < HIST_LEN; i++) {
        int v = hist[(hist_pos + i) % HIST_LEN];
        int bh = v * gh / 100;
        unsigned int c = (i >= HIST_LEN - 2) ? COL_SPARK : COL_SPARK_DIM;
        if (bh > 0)
            fill(gx + i * barw, gy + gh - bh, barw - 1, bh, c);
    }
    /* 50% reference line */
    for (i = 0; i < gw; i += 6)
        px(gx + i, gy + gh / 2, COL_TEXT_DIM);
}

static void fmt_time(char *out, long secs)
{
    long h = secs / 3600, m = (secs / 60) % 60, s = secs % 60;
    if (h > 0)
        sprintf(out, "%ld:%02ld:%02ld", h, m, s);
    else
        sprintf(out, "%ld:%02ld", m, s);
}

static unsigned int state_color(char st)
{
    if (st == 'R') return COL_STATE_R;
    if (st == 'Z') return COL_STATE_Z;
    if (st == 'D') return COL_STATE_D;
    return COL_STATE_S;
}

/* ------------------------------------------------------------------
 * Screen
 * ------------------------------------------------------------------ */
static int selected;
static int scroll_top;

static void draw_chrome(void)
{
    fill(0, 0, FB_WIDTH, FB_HEIGHT, COL_DESKTOP);
    fill(0, 0, FB_WIDTH, TITLE_H, COL_TITLEBAR);
    draws2(12, 5, "TASK MANAGER", COL_TITLE_TEXT, COL_TITLEBAR);
    draws(FB_WIDTH - 180, 9, "TRANSPUTEROS 0.1.0", COL_TITLE_TEXT,
          COL_TITLEBAR);

    /* table header */
    fill(0, TABLE_HDR_Y, FB_WIDTH, 14, COL_HEADER);
    draws(C_PID,   TABLE_HDR_Y + 3, "PID",    COL_TITLE_TEXT, COL_HEADER);
    draws(C_STATE, TABLE_HDR_Y + 3, "ST",     COL_TITLE_TEXT, COL_HEADER);
    draws(C_CPU,   TABLE_HDR_Y + 3, "CPU%",   COL_TITLE_TEXT, COL_HEADER);
    draws(C_THR,   TABLE_HDR_Y + 3, "THR",    COL_TITLE_TEXT, COL_HEADER);
    draws(C_MEM,   TABLE_HDR_Y + 3, "MEM KB", COL_TITLE_TEXT, COL_HEADER);
    draws(C_TIME,  TABLE_HDR_Y + 3, "TIME",   COL_TITLE_TEXT, COL_HEADER);
    draws(C_NAME,  TABLE_HDR_Y + 3, "NAME",   COL_TITLE_TEXT, COL_HEADER);
}

static void draw_tiles(void)
{
    char big[24], sub[40];
    int run = 0, thr = 0, i;

    sprintf(big, "%d%%", cur.cpu_pct);
    sprintf(sub, "T806 / 200MHZ");
    draw_tile(12, "CPU", big, sub, cur.cpu_pct);

    sprintf(big, "%ld MB", (cur.mem_total_kb - cur.mem_avail_kb) / 1024);
    sprintf(sub, "OF %ld MB USED", cur.mem_total_kb / 1024);
    draw_tile(12 + TILE_W + TILE_GAP, "MEMORY", big, sub, cur.mem_pct);

    for (i = 0; i < nprocs; i++) {
        if (procs[i].state == 'R')
            run++;
        thr += procs[i].threads > 0 ? procs[i].threads : 1;
    }
    sprintf(big, "%d", nprocs);
    sprintf(sub, "%d RUN  %d THR", run, thr);
    draw_tile(12 + (TILE_W + TILE_GAP) * 2, "PROCESSES", big, sub, -1);
}

static void draw_row(int idx, int y)
{
    proc_t *p = &procs[idx];
    unsigned int bg = (idx == selected) ? COL_SELECT
                    : ((idx & 1) ? COL_ROW_B : COL_ROW_A);
    char t[32];
    char st[2];

    fill(0, y, FB_WIDTH, ROW_H, bg);

    /* PID in accent colour so the column is obvious in captures. */
    sprintf(t, "%d", p->pid);
    draws(C_PID, y + 4, t, COL_ACCENT, bg);

    st[0] = p->state; st[1] = 0;
    fill(C_STATE, y + 3, 9, 9, state_color(p->state));
    draws(C_STATE + 13, y + 4, st, COL_TEXT, bg);

    sprintf(t, "%d", p->cpu_pct);
    draws(C_CPU, y + 4, t, COL_TEXT, bg);
    draw_gauge(C_CPUBAR, y + 4, 56, 7, p->cpu_pct);

    sprintf(t, "%d", p->threads > 0 ? p->threads : 1);
    draws(C_THR, y + 4, t, COL_TEXT, bg);

    sprintf(t, "%ld", p->mem_kb);
    draws(C_MEM, y + 4, t, COL_TEXT, bg);

    fmt_time(t, p->jiffies / JIFFY_HZ);
    draws(C_TIME, y + 4, t, COL_TEXT, bg);

    draws(C_NAME, y + 4, p->name,
          (idx == selected) ? COL_ACCENT : COL_TEXT, bg);
}

static void draw_table(void)
{
    int i, y = TABLE_Y;
    int shown = 0;

    if (nprocs <= 0) {
        fill(0, TABLE_Y, FB_WIDTH, STATUS_Y - TABLE_Y, COL_DESKTOP);
        draws(C_PID, TABLE_Y + 8,
              "NO PROCESSES — /proc empty (open or getdents failed)",
              COL_GAUGE_HOT, COL_DESKTOP);
        return;
    }

    if (selected >= nprocs) selected = nprocs ? nprocs - 1 : 0;
    if (selected < scroll_top) scroll_top = selected;
    if (selected >= scroll_top + VISIBLE_ROWS)
        scroll_top = selected - VISIBLE_ROWS + 1;

    for (i = scroll_top; i < nprocs && shown < VISIBLE_ROWS; i++, shown++) {
        draw_row(i, y);
        y += ROW_H;
    }
    /* clear leftover rows */
    if (y < STATUS_Y)
        fill(0, y, FB_WIDTH, STATUS_Y - y, COL_DESKTOP);
}

static void draw_status(void)
{
    static const char *sortname[3] = { "CPU", "MEM", "PID" };
    char t[96], up[24];

    fill(0, STATUS_Y, FB_WIDTH, STATUS_H, COL_STATUSBAR);
    sprintf(t, "Q QUIT  ^/v  S SORT=%s  SPACE  LIVE 500ms /proc",
            sortname[sort_mode]);
    draws(12, STATUS_Y + 6, t, COL_TEXT, COL_STATUSBAR);

    fmt_time(up, cur.uptime_s);
    sprintf(t, "LOAD %s  UP %s", cur.loadavg, up);
    draws(FB_WIDTH - 12 - (int)strlen(t) * 6, STATUS_Y + 6, t,
          COL_ACCENT, COL_STATUSBAR);
}

static void redraw_dynamic(void)
{
    draw_tiles();
    draw_graph(12, "CPU HISTORY", cpu_hist);
    draw_graph(12 + GRAPH_W + 8, "MEMORY HISTORY", mem_hist);
    draw_table();
    draw_status();
    present();
}

/* ------------------------------------------------------------------
 * Keyboard (VT100 from UART1)
 * ------------------------------------------------------------------ */
static int key_available(void)
{
    return (*(volatile unsigned int *)KBD_UART_STATUS) & UART_RXAVAIL;
}

static int key_read(void)
{
    return (int)(*(volatile unsigned int *)KBD_UART_DATA) & 0xFF;
}

/* returns: 0 none, 'q','s',' ', 'U' up, 'D' down */
static int poll_key(void)
{
    int c;
    if (!key_available())
        return 0;
    c = key_read();
    if (c == 0x1B) {                    /* ESC [ A/B */
        if (key_available() && key_read() == '[') {
            if (key_available()) {
                c = key_read();
                if (c == 'A') return 'U';
                if (c == 'B') return 'D';
            }
        }
        return 0;
    }
    if (c == 'q' || c == 'Q') return 'q';
    if (c == 's' || c == 'S') return 's';
    if (c == ' ') return ' ';
    if (c == 'k' || c == 0x0B) return 'U';   /* vi-ish fallback */
    if (c == 'j' || c == 0x0A) return 'D';
    return 0;
}

/* ------------------------------------------------------------------
 * Main
 * ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    unsigned int last, now;
    int running = 1;
    int demo_frames = 0;   /* 0 = interactive; >0 auto-quit after N presents */
    int frames_done = 0;
    int ai;

    for (ai = 1; ai < argc; ai++) {
        if (argv[ai][0] == '-' && argv[ai][1] == 'd') {
            /* -demo or -demo=N */
            demo_frames = 30;
            if (argv[ai][5] == '=' && argv[ai][6]) {
                int v = 0, p = 6;
                while (argv[ai][p] >= '0' && argv[ai][p] <= '9') {
                    v = v * 10 + (argv[ai][p] - '0');
                    p++;
                }
                if (v > 0) demo_frames = v;
            } else if (ai + 1 < argc && argv[ai + 1][0] >= '0') {
                int v = 0;
                const char *p = argv[ai + 1];
                while (*p >= '0' && *p <= '9') {
                    v = v * 10 + (*p - '0');
                    p++;
                }
                if (v > 0) demo_frames = v;
                ai++;
            }
        }
    }

    read_sysstat();     /* prime deltas */
    read_procs();

    draw_chrome();
    read_sysstat();
    read_procs();
    sort_procs();
    hist_push();
    redraw_dynamic();
    frames_done = 1;

    last = __ldtimer();

    while (running) {
        int k = poll_key();
        int refresh = 0;

        if (demo_frames > 0 && frames_done >= demo_frames)
            running = 0;

        if (k == 'q') {
            running = 0;
        } else if (k == 'U' && selected > 0) {
            selected--;
            draw_table();
            present();
        } else if (k == 'D' && selected < nprocs - 1) {
            selected++;
            draw_table();
            present();
        } else if (k == 's') {
            sort_mode = (sort_mode + 1) % 3;
            sort_procs();
            selected = 0;
            scroll_top = 0;
            refresh = 1;
        } else if (k == ' ') {
            refresh = 1;
        }

        now = __ldtimer();
        if (demo_frames > 0) {
            /* Demo: refresh every loop so we hit the frame budget quickly. */
            refresh = 1;
        } else if ((unsigned int)(now - last) >= (unsigned int)REFRESH_TICKS) {
            /* Interactive: re-read /proc every ~500 ms for live data. */
            last = now;
            refresh = 1;
        }

        if (refresh) {
            read_sysstat();
            read_procs();
            sort_procs();
            hist_push();
            redraw_dynamic();
            frames_done++;
        }
    }

    /* Demo mode keeps the last painted frame so the compositor can capture
     * it as an underlay and open DOOM as a window on top.  Interactive mode
     * still restores a solid desktop for a clean return to FBWM. */
    if (demo_frames <= 0) {
        fill(0, 0, FB_WIDTH, FB_HEIGHT, COL_DESKTOP);
        present();
    } else {
        present();
    }
    return 0;
}
