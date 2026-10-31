/*
 * tfiles.c - TransputerOS File Manager
 *
 * A simple dual-pane-style file manager rendering directly to the 800x600
 * ARGB framebuffer at 0x81e00000 with INMOS-teal styled window chrome.
 *
 * Features:
 *   - Directory listing with file sizes and types
 *   - Keyboard navigation (arrows, Enter, Backspace)
 *   - File type icons (dir, file, executable, symlink)
 *   - Status bar with path, file count, selected file info
 *   - Delete files (d key), view files (v key)
 *
 * TCC constraints: no VLAs, functions under ~80 lines, int counters.
 */

#include <stdio.h>
#include <string.h>

/* Syscall interface */
extern long __transputer_syscall(long, long, long, long, long, long, long);

/* -------------------------------------------------------------------------
 * Syscall numbers (asm-generic/unistd.h)
 * ------------------------------------------------------------------------- */
#define __NR_open           1024
#define __NR_close          57
#define __NR_read           63
#define __NR_write          64
#define __NR_getdents64     61
#define __NR_stat           1038
#define __NR_chdir          49
#define __NR_getcwd         1029
#define __NR_unlinkat       35
#define __NR_exit_group     94
#define __NR_lseek          1057

#define O_RDONLY            0
#define O_DIRECTORY         0200000
#define AT_FDCWD            (-100)
#define AT_REMOVEDIR        0x200

#define S_IFMT   0170000
#define S_IFDIR  0040000
#define S_IFLNK  0120000
#define S_IFREG  0100000
#define S_IXUSR  0100
#define S_IXGRP  0010
#define S_IXOTH  0001

/* linux_dirent64 structure */
struct linux_dirent64 {
    unsigned long long d_ino;
    long long          d_off;
    unsigned short     d_reclen;
    unsigned char      d_type;
    char               d_name[256];
};

/* Minimal stat structure — only the fields we need */
struct tp_stat {
    unsigned long st_dev;
    unsigned long st_ino;
    unsigned long st_nlink;
    unsigned int  st_mode;
    unsigned int  st_uid;
    unsigned int  st_gid;
    unsigned int  __pad0;
    unsigned long st_rdev;
    long          st_size;
    long          st_blksize;
    long          st_blocks;
    long          st_atime_sec;
    long          st_atime_nsec;
    long          st_mtime_sec;
    long          st_mtime_nsec;
    long          st_ctime_sec;
    long          st_ctime_nsec;
};

/* -------------------------------------------------------------------------
 * Syscall wrappers
 * ------------------------------------------------------------------------- */
static int tp_open(const char *path, int flags)
{
    return (int)__transputer_syscall(__NR_open, (long)path, flags, 0, 0, 0, 0);
}

static int tp_close(int fd)
{
    return (int)__transputer_syscall(__NR_close, fd, 0, 0, 0, 0, 0);
}

static int tp_read(int fd, void *buf, int count)
{
    return (int)__transputer_syscall(__NR_read, fd, (long)buf, count, 0, 0, 0);
}

static int tp_getdents64(int fd, void *buf, int count)
{
    return (int)__transputer_syscall(__NR_getdents64, fd, (long)buf, count, 0, 0, 0);
}

static int tp_stat(const char *path, struct tp_stat *buf)
{
    return (int)__transputer_syscall(__NR_stat, (long)path, (long)buf, 0, 0, 0, 0);
}

static int tp_chdir(const char *path)
{
    return (int)__transputer_syscall(__NR_chdir, (long)path, 0, 0, 0, 0, 0);
}

static int tp_getcwd(char *buf, int size)
{
    return (int)__transputer_syscall(__NR_getcwd, (long)buf, size, 0, 0, 0, 0);
}

static int tp_unlinkat(int dirfd, const char *path, int flags)
{
    return (int)__transputer_syscall(__NR_unlinkat, dirfd, (long)path, flags, 0, 0, 0);
}

static int tp_lseek(int fd, long offset, int whence)
{
    return (int)__transputer_syscall(__NR_lseek, fd, offset, whence, 0, 0, 0);
}

static void tp_exit(int code)
{
    __transputer_syscall(__NR_exit_group, code, 0, 0, 0, 0, 0);
    for (;;) ;
}

/* -------------------------------------------------------------------------
 * Framebuffer hardware
 * ------------------------------------------------------------------------- */
#define FB_BASE         0x81E00000
#define FB_WIDTH        800
#define FB_HEIGHT       600

/* Keyboard UART */
#define KBD_DATA        0x11000000
#define KBD_STATUS      0x11000004
#define UART_RXAVAIL    0x02

/* Console UART (debug) */
#define UART_ADDR       0x10000000

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
#define COL_DIR_CLR     0xFF00BCD4  /* Cyan for directories */
#define COL_EXEC_CLR    0xFF4CAF50  /* Green for executables */
#define COL_LINK_CLR    0xFFFFAB40  /* Orange for symlinks */
#define COL_FILE_CLR    0xFFB2DFDB  /* Teal-white for regular files */
#define COL_SIZE_CLR    0xFF607D8B  /* Muted for size column */
#define COL_SCROLLBAR   0xFF37474F
#define COL_SCROLLTHUMB 0xFF00897B

/* -------------------------------------------------------------------------
 * Window layout
 * ------------------------------------------------------------------------- */
#define WIN_W           700
#define WIN_H           480
#define WIN_X           ((FB_WIDTH - WIN_W) / 2)
#define WIN_Y           ((FB_HEIGHT - WIN_H) / 2 - 10)
#define TITLE_H         22
#define BORDER          3
#define CLIENT_X        (WIN_X + BORDER)
#define CLIENT_Y        (WIN_Y + TITLE_H)
#define CLIENT_W        (WIN_W - BORDER * 2)
#define CLIENT_H        (WIN_H - TITLE_H - BORDER)

/* Path bar */
#define PATH_Y          (CLIENT_Y + 2)
#define PATH_H          14

/* Column header */
#define HDR_Y           (PATH_Y + PATH_H + 2)
#define HDR_H           12

/* File list */
#define LIST_Y          (HDR_Y + HDR_H + 2)
#define ROW_H           11
#define LIST_H          (CLIENT_H - (LIST_Y - CLIENT_Y) - 16)
#define VISIBLE_ROWS    (LIST_H / ROW_H)

/* Column positions (relative to CLIENT_X) */
#define COL_ICON_X      4
#define COL_NAME_X      16
#define COL_SIZE_X      (CLIENT_W - 120)
#define COL_TYPE_X      (CLIENT_W - 50)

/* Status bar */
#define STATUS_Y        (CLIENT_Y + CLIENT_H - 14)

/* Scrollbar */
#define SCROLL_X        (CLIENT_X + CLIENT_W - 10)
#define SCROLL_W        8

/* -------------------------------------------------------------------------
 * MMIO helpers
 * ------------------------------------------------------------------------- */
static unsigned int *fb;

static void mmio_w(unsigned int addr, unsigned int val)
{
    *(volatile unsigned int *)addr = val;
}

static void uart_putc(char c)
{
    *(volatile unsigned int *)UART_ADDR = (unsigned char)c;
}

static void uart_puts(const char *s)
{
    while (*s) uart_putc(*s++);
}

/* -------------------------------------------------------------------------
 * Pixel drawing
 * ------------------------------------------------------------------------- */
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

static void vline(int x, int y, int h, unsigned int c)
{
    int i;
    for (i = 0; i < h; i++)
        fb[(y + i) * FB_WIDTH + x] = c;
}

static void px(int x, int y, unsigned int c)
{
    if (x >= 0 && x < FB_WIDTH && y >= 0 && y < FB_HEIGHT)
        fb[y * FB_WIDTH + x] = c;
}

/* -------------------------------------------------------------------------
 * 5x7 mini font (same as ttop/tpkg-ui/doom)
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
    {0x00,0x00,0x00,0x00,0x00}, /* | (42) */
    {0x36,0x49,0x49,0x49,0x36}, /* % (43) */
    {0x00,0x44,0x7D,0x40,0x00}, /* ! (44) */
    {0x08,0x04,0x7E,0x04,0x08}, /* ^ (45) -- up arrow */
    {0x10,0x20,0x7E,0x20,0x10}, /* v (46) -- down arrow */
    {0x22,0x14,0x08,0x14,0x22}, /* * (47) */
    {0x3E,0x41,0x41,0x41,0x3E}, /* ( (48) */
    {0x3E,0x41,0x41,0x41,0x3E}, /* ) (49) */
    {0x7F,0x7F,0x7F,0x7F,0x7F}, /* block (50) */
    {0x00,0x00,0x4F,0x00,0x00}, /* , (51) */
    {0x36,0x36,0x36,0x36,0x36}, /* # (52) */
    {0x08,0x08,0x3E,0x08,0x08}, /* + (53) */
    {0x02,0x01,0x51,0x09,0x06}, /* ? (54) */
    {0x00,0x50,0x30,0x00,0x00}, /* ; (55) */
    {0x00,0x00,0x00,0x60,0x60}, /* _ (56) — low dot as underscore */
    {0x3E,0x41,0x5D,0x55,0x1E}, /* @ (57) */
    {0x08,0x14,0x22,0x41,0x00}, /* < (58) */
    {0x00,0x41,0x22,0x14,0x08}, /* > (59) */
    {0x00,0x05,0x03,0x00,0x00}, /* ~ (60) */
    {0x7F,0x41,0x41,0x41,0x7F}, /* [ (61) */
    {0x7F,0x41,0x41,0x41,0x7F}, /* ] (62) */
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
    /* lowercase 'v' already handled above */
    if (c == '*') return 47;
    if (c == '(') return 48;
    if (c == ')') return 49;
    if (c == ',') return 51;
    if (c == '#') return 52;
    if (c == '+') return 53;
    if (c == '?') return 54;
    if (c == ';') return 55;
    if (c == '_') return 56;
    if (c == '@') return 57;
    if (c == '<') return 58;
    if (c == '>') return 59;
    if (c == '~') return 60;
    if (c == '[') return 61;
    if (c == ']') return 62;
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
    int len = (int)strlen(s);
    int tx = cx + (w - len * 6) / 2;
    draws(tx, cy, s, fg, bg);
}

/* -------------------------------------------------------------------------
 * Keyboard
 * ------------------------------------------------------------------------- */
static int kbd_available(void)
{
    return (*(volatile unsigned int *)KBD_STATUS & UART_RXAVAIL) ? 1 : 0;
}

static int kbd_read(void)
{
    return *(volatile unsigned int *)KBD_DATA & 0xFF;
}

/* Read a key, handling VT100 escape sequences for arrows */
static int read_key(void)
{
    int c, c2, c3;
    int timeout;

    if (!kbd_available())
        return 0;

    c = kbd_read();
    if (c != 27)
        return c;

    /* ESC — check for [ sequence */
    timeout = 50000;
    while (!kbd_available() && timeout > 0)
        timeout--;
    if (timeout <= 0)
        return 27; /* bare ESC */

    c2 = kbd_read();
    if (c2 != '[')
        return 27;

    timeout = 50000;
    while (!kbd_available() && timeout > 0)
        timeout--;
    if (timeout <= 0)
        return 27;

    c3 = kbd_read();
    if (c3 == 'A') return 256; /* Up */
    if (c3 == 'B') return 257; /* Down */
    if (c3 == 'C') return 258; /* Right */
    if (c3 == 'D') return 259; /* Left */
    if (c3 == 'H') return 260; /* Home */
    if (c3 == 'F') return 261; /* End */
    if (c3 == '5') { kbd_read(); return 262; } /* PgUp */
    if (c3 == '6') { kbd_read(); return 263; } /* PgDn */
    return 27;
}

#define KEY_UP      256
#define KEY_DOWN    257
#define KEY_RIGHT   258
#define KEY_LEFT    259
#define KEY_HOME    260
#define KEY_END     261
#define KEY_PGUP    262
#define KEY_PGDN    263

/* -------------------------------------------------------------------------
 * File entry storage
 * ------------------------------------------------------------------------- */
#define MAX_ENTRIES     256
#define MAX_NAME        64

#define FTYPE_DIR       0
#define FTYPE_FILE      1
#define FTYPE_EXEC      2
#define FTYPE_LINK      3

typedef struct {
    char name[MAX_NAME];
    int  type;       /* FTYPE_* */
    long size;       /* bytes */
} fentry_t;

static fentry_t entries[MAX_ENTRIES];
static int nentries = 0;
static int selected = 0;
static int scroll = 0;
static char cwd[256];

/* Getdents buffer */
static char dents_buf[4096];

/* -------------------------------------------------------------------------
 * String helpers
 * ------------------------------------------------------------------------- */
static void int_to_str(long val, char *buf, int bufsz)
{
    char tmp[20];
    int i = 0, neg = 0, j;

    if (val < 0) { neg = 1; val = -val; }
    if (val == 0) { tmp[i++] = '0'; }
    else {
        while (val > 0 && i < 18) {
            tmp[i++] = '0' + (int)(val % 10);
            val /= 10;
        }
    }
    j = 0;
    if (neg && j < bufsz - 1) buf[j++] = '-';
    while (i > 0 && j < bufsz - 1)
        buf[j++] = tmp[--i];
    buf[j] = 0;
}

static void format_size(long sz, char *buf, int bufsz)
{
    if (sz < 0) {
        buf[0] = '-'; buf[1] = 0;
        return;
    }
    if (sz < 1024) {
        int_to_str(sz, buf, bufsz - 2);
        strcat(buf, " B");
    } else if (sz < 1024 * 1024) {
        int_to_str(sz / 1024, buf, bufsz - 2);
        strcat(buf, " K");
    } else {
        int_to_str(sz / (1024 * 1024), buf, bufsz - 2);
        strcat(buf, " M");
    }
}

/* -------------------------------------------------------------------------
 * Sort: directories first, then alphabetical
 * ------------------------------------------------------------------------- */
static int entry_cmp(fentry_t *a, fentry_t *b)
{
    /* ".." always first */
    if (strcmp(a->name, "..") == 0) return -1;
    if (strcmp(b->name, "..") == 0) return 1;
    /* dirs before files */
    if (a->type == FTYPE_DIR && b->type != FTYPE_DIR) return -1;
    if (a->type != FTYPE_DIR && b->type == FTYPE_DIR) return 1;
    return strcmp(a->name, b->name);
}

static void sort_entries(void)
{
    int i, j;
    fentry_t tmp;

    /* Simple insertion sort — MAX_ENTRIES is small */
    for (i = 1; i < nentries; i++) {
        tmp = entries[i];
        j = i - 1;
        while (j >= 0 && entry_cmp(&entries[j], &tmp) > 0) {
            entries[j + 1] = entries[j];
            j--;
        }
        entries[j + 1] = tmp;
    }
}

/* -------------------------------------------------------------------------
 * Read directory
 * ------------------------------------------------------------------------- */
static void build_path(char *out, int outsz, const char *dir, const char *name)
{
    int dl = (int)strlen(dir);
    int nl = (int)strlen(name);

    if (dl + 1 + nl >= outsz - 1) {
        out[0] = 0;
        return;
    }
    strcpy(out, dir);
    if (dl > 0 && dir[dl - 1] != '/') {
        out[dl] = '/';
        dl++;
    }
    strcpy(out + dl, name);
}

static void stat_entry(fentry_t *e, const char *fullpath)
{
    struct tp_stat st;
    int rc;

    e->size = 0;
    e->type = FTYPE_FILE;

    rc = tp_stat(fullpath, &st);
    if (rc < 0)
        return;

    e->size = st.st_size;

    if ((st.st_mode & S_IFMT) == S_IFDIR)
        e->type = FTYPE_DIR;
    else if ((st.st_mode & S_IFMT) == S_IFLNK)
        e->type = FTYPE_LINK;
    else if (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))
        e->type = FTYPE_EXEC;
}

static void read_directory(void)
{
    int fd, nread;
    char pathbuf[320];

    nentries = 0;
    selected = 0;
    scroll = 0;

    tp_getcwd(cwd, sizeof(cwd));

    fd = tp_open(cwd, O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        uart_puts("tfiles: open dir failed\r\n");
        return;
    }

    while (nentries < MAX_ENTRIES) {
        nread = tp_getdents64(fd, dents_buf, sizeof(dents_buf));
        if (nread <= 0)
            break;

        {
            int pos = 0;
            while (pos < nread && nentries < MAX_ENTRIES) {
                struct linux_dirent64 *d = (struct linux_dirent64 *)(dents_buf + pos);
                char *name = d->d_name;

                /* Skip "." */
                if (name[0] == '.' && name[1] == 0) {
                    pos += d->d_reclen;
                    continue;
                }

                /* Copy name (truncate if needed) */
                strncpy(entries[nentries].name, name, MAX_NAME - 1);
                entries[nentries].name[MAX_NAME - 1] = 0;

                /* Stat for size and type */
                build_path(pathbuf, sizeof(pathbuf), cwd, name);
                stat_entry(&entries[nentries], pathbuf);

                nentries++;
                pos += d->d_reclen;
            }
        }
    }

    tp_close(fd);
    sort_entries();
}

/* -------------------------------------------------------------------------
 * Fixed simplefb aperture init + compatibility flip
 * ------------------------------------------------------------------------- */
static void vga_init(void)
{
    fb = (unsigned int *)FB_BASE;
}

static void vga_flip(void)
{
}

/* -------------------------------------------------------------------------
 * Draw window chrome
 * ------------------------------------------------------------------------- */
static void draw_chrome(void)
{
    /* Desktop background */
    fill(0, 0, FB_WIDTH, FB_HEIGHT, COL_DESKTOP);

    /* Window border */
    fill(WIN_X, WIN_Y, WIN_W, WIN_H, COL_WINBG);
    hline(WIN_X, WIN_Y, WIN_W, COL_BORDER_LT);
    hline(WIN_X, WIN_Y + WIN_H - 1, WIN_W, COL_BORDER_DK);
    vline(WIN_X, WIN_Y, WIN_H, COL_BORDER_LT);
    vline(WIN_X + WIN_W - 1, WIN_Y, WIN_H, COL_BORDER_DK);

    /* Title bar */
    fill(WIN_X + 1, WIN_Y + 1, WIN_W - 2, TITLE_H - 1, COL_TITLEBAR);

    /* Close button */
    {
        int bx = WIN_X + WIN_W - 20;
        int by = WIN_Y + 3;
        fill(bx, by, 16, 16, 0xFF004040);
        draws(bx + 4, by + 5, "x", COL_TITLE_TEXT, 0xFF004040);
    }

    /* Title text */
    draws_c(WIN_X, WIN_Y + 7, WIN_W - 24, "tfiles - File Manager",
            COL_TITLE_TEXT, COL_TITLEBAR);
}

/* -------------------------------------------------------------------------
 * Draw path bar
 * ------------------------------------------------------------------------- */
static void draw_pathbar(void)
{
    int x = CLIENT_X + 4;
    char label[8];

    fill(CLIENT_X, PATH_Y, CLIENT_W, PATH_H, COL_HEADER);

    label[0] = '/'; label[1] = ' '; label[2] = 0;
    draws(x, PATH_Y + 3, label, COL_ACCENT, COL_HEADER);

    /* Truncate cwd if too long for display */
    {
        int maxch = (CLIENT_W - 20) / 6;
        char disp[128];
        int cwdlen = (int)strlen(cwd);

        if (cwdlen > maxch && maxch > 6) {
            strcpy(disp, "...");
            strncpy(disp + 3, cwd + cwdlen - (maxch - 3), maxch - 3);
            disp[maxch] = 0;
        } else {
            strncpy(disp, cwd, sizeof(disp) - 1);
            disp[sizeof(disp) - 1] = 0;
        }
        draws(x + 12, PATH_Y + 3, disp, COL_TITLE_TEXT, COL_HEADER);
    }
}

/* -------------------------------------------------------------------------
 * Draw column headers
 * ------------------------------------------------------------------------- */
static void draw_header(void)
{
    fill(CLIENT_X, HDR_Y, CLIENT_W, HDR_H, COL_BORDER_DK);
    draws(CLIENT_X + COL_NAME_X, HDR_Y + 2, "Name", COL_VERSION, COL_BORDER_DK);
    draws(CLIENT_X + COL_SIZE_X, HDR_Y + 2, "Size", COL_VERSION, COL_BORDER_DK);
    draws(CLIENT_X + COL_TYPE_X, HDR_Y + 2, "Type", COL_VERSION, COL_BORDER_DK);
}

/* -------------------------------------------------------------------------
 * Draw a single file entry row
 * ------------------------------------------------------------------------- */
static void draw_icon_dir(int x, int y, unsigned int bg)
{
    /* Tiny folder icon: 7x5 */
    fill(x, y + 1, 3, 1, COL_DIR_CLR);
    fill(x, y + 2, 7, 4, COL_DIR_CLR);
    px(x, y + 2, bg); /* notch */
}

static void draw_icon_file(int x, int y, unsigned int bg)
{
    fill(x + 1, y + 1, 5, 5, COL_FILE_CLR);
    px(x + 5, y + 1, bg);
    px(x + 4, y + 1, bg); /* folded corner */
}

static void draw_icon_exec(int x, int y, unsigned int bg)
{
    fill(x + 1, y + 1, 5, 5, COL_EXEC_CLR);
    draws(x + 1, y, "*", COL_EXEC_CLR, bg);
}

static void draw_icon_link(int x, int y, unsigned int bg)
{
    fill(x + 1, y + 1, 5, 5, COL_LINK_CLR);
    draws(x + 2, y, ">", COL_LINK_CLR, bg);
}

static void draw_entry_row(int idx, int row_y)
{
    fentry_t *e;
    unsigned int bg, fg;
    int is_sel;
    char sizebuf[16];
    const char *typelbl;
    int name_maxch;

    if (idx >= nentries) {
        fill(CLIENT_X, row_y, CLIENT_W - 12, ROW_H, COL_WINBG);
        return;
    }

    e = &entries[idx];
    is_sel = (idx == selected);
    bg = is_sel ? COL_SELECT : COL_WINBG;
    fg = is_sel ? COL_TITLE_TEXT : COL_WINTXT;

    /* Row background */
    fill(CLIENT_X, row_y, CLIENT_W - 12, ROW_H, bg);

    /* Icon */
    if (e->type == FTYPE_DIR)
        draw_icon_dir(CLIENT_X + COL_ICON_X, row_y + 1, bg);
    else if (e->type == FTYPE_EXEC)
        draw_icon_exec(CLIENT_X + COL_ICON_X, row_y + 1, bg);
    else if (e->type == FTYPE_LINK)
        draw_icon_link(CLIENT_X + COL_ICON_X, row_y + 1, bg);
    else
        draw_icon_file(CLIENT_X + COL_ICON_X, row_y + 1, bg);

    /* Name (truncate if too long) */
    name_maxch = (COL_SIZE_X - COL_NAME_X - 4) / 6;
    {
        char dispname[96];
        int nlen = (int)strlen(e->name);

        if (nlen > name_maxch && name_maxch > 4) {
            strncpy(dispname, e->name, name_maxch - 3);
            dispname[name_maxch - 3] = 0;
            strcat(dispname, "...");
        } else {
            strncpy(dispname, e->name, sizeof(dispname) - 1);
            dispname[sizeof(dispname) - 1] = 0;
        }

        if (e->type == FTYPE_DIR)
            draws(CLIENT_X + COL_NAME_X, row_y + 2, dispname, COL_DIR_CLR, bg);
        else if (e->type == FTYPE_EXEC)
            draws(CLIENT_X + COL_NAME_X, row_y + 2, dispname, COL_EXEC_CLR, bg);
        else if (e->type == FTYPE_LINK)
            draws(CLIENT_X + COL_NAME_X, row_y + 2, dispname, COL_LINK_CLR, bg);
        else
            draws(CLIENT_X + COL_NAME_X, row_y + 2, dispname, fg, bg);
    }

    /* Size */
    if (e->type == FTYPE_DIR) {
        draws(CLIENT_X + COL_SIZE_X, row_y + 2, "<dir>", COL_SIZE_CLR, bg);
    } else {
        format_size(e->size, sizebuf, sizeof(sizebuf));
        draws(CLIENT_X + COL_SIZE_X, row_y + 2, sizebuf, COL_SIZE_CLR, bg);
    }

    /* Type label */
    if (e->type == FTYPE_DIR) typelbl = "DIR";
    else if (e->type == FTYPE_EXEC) typelbl = "EXE";
    else if (e->type == FTYPE_LINK) typelbl = "LNK";
    else typelbl = "FILE";
    draws(CLIENT_X + COL_TYPE_X, row_y + 2, typelbl, COL_SIZE_CLR, bg);
}

/* -------------------------------------------------------------------------
 * Draw file list
 * ------------------------------------------------------------------------- */
static void draw_list(void)
{
    int i, row_y;

    for (i = 0; i < VISIBLE_ROWS; i++) {
        row_y = LIST_Y + i * ROW_H;
        draw_entry_row(scroll + i, row_y);
    }
}

/* -------------------------------------------------------------------------
 * Draw scrollbar
 * ------------------------------------------------------------------------- */
static void draw_scrollbar(void)
{
    int track_y = LIST_Y;
    int track_h = VISIBLE_ROWS * ROW_H;
    int thumb_h, thumb_y;

    fill(SCROLL_X, track_y, SCROLL_W, track_h, COL_BORDER_DK);

    if (nentries <= VISIBLE_ROWS) {
        thumb_h = track_h;
        thumb_y = track_y;
    } else {
        thumb_h = (track_h * VISIBLE_ROWS) / nentries;
        if (thumb_h < 8) thumb_h = 8;
        thumb_y = track_y + (scroll * (track_h - thumb_h)) / (nentries - VISIBLE_ROWS);
    }
    fill(SCROLL_X + 1, thumb_y, SCROLL_W - 2, thumb_h, COL_SCROLLTHUMB);
}

/* -------------------------------------------------------------------------
 * Draw status bar
 * ------------------------------------------------------------------------- */
static void draw_status(void)
{
    char buf[128];
    char numbuf[12];

    fill(CLIENT_X, STATUS_Y, CLIENT_W, 14, COL_STATUSBAR);

    /* File count */
    int_to_str(nentries, numbuf, sizeof(numbuf));
    strcpy(buf, numbuf);
    strcat(buf, " items");

    /* Selected file info */
    if (selected >= 0 && selected < nentries) {
        strcat(buf, "  |  ");
        {
            int space = sizeof(buf) - (int)strlen(buf) - 1;
            int nlen = (int)strlen(entries[selected].name);
            if (nlen < space) {
                strcat(buf, entries[selected].name);
            } else if (space > 4) {
                strncat(buf, entries[selected].name, space - 4);
                strcat(buf, "...");
            }
        }
    }

    draws(CLIENT_X + 4, STATUS_Y + 3, buf, COL_VERSION, COL_STATUSBAR);

    /* Key hints on right side */
    draws(CLIENT_X + CLIENT_W - 210, STATUS_Y + 3,
          "Enter:open  BS:up  d:del  v:view  q:quit",
          COL_SIZE_CLR, COL_STATUSBAR);
}

/* -------------------------------------------------------------------------
 * Full redraw
 * ------------------------------------------------------------------------- */
static void redraw_all(void)
{
    draw_chrome();
    draw_pathbar();
    draw_header();
    draw_list();
    draw_scrollbar();
    draw_status();
    vga_flip();
}

/* -------------------------------------------------------------------------
 * File viewer — display first ~30 lines of a file
 * ------------------------------------------------------------------------- */
static void view_file(const char *path, const char *name)
{
    int fd;
    char buf[2048];
    int nread, i, line, col;
    int vy, max_lines;

    fd = tp_open(path, O_RDONLY);
    if (fd < 0) return;

    nread = tp_read(fd, buf, sizeof(buf) - 1);
    tp_close(fd);

    if (nread <= 0) return;
    buf[nread] = 0;

    /* Draw viewer overlay */
    fill(CLIENT_X, CLIENT_Y, CLIENT_W, CLIENT_H, 0xFF0A1520);

    /* Header */
    fill(CLIENT_X, CLIENT_Y, CLIENT_W, 14, COL_HEADER);
    {
        char title[80];
        strcpy(title, "Viewing: ");
        strncat(title, name, sizeof(title) - 12);
        draws(CLIENT_X + 4, CLIENT_Y + 3, title, COL_TITLE_TEXT, COL_HEADER);
    }
    draws(CLIENT_X + CLIENT_W - 100, CLIENT_Y + 3,
          "Press any key", COL_ACCENT, COL_HEADER);

    /* Content */
    vy = CLIENT_Y + 18;
    max_lines = (CLIENT_H - 32) / 9;
    line = 0;
    col = 0;

    for (i = 0; i < nread && line < max_lines; i++) {
        char c = buf[i];
        if (c == '\n') {
            line++;
            col = 0;
            vy += 9;
        } else if (c == '\r') {
            /* skip */
        } else if (c >= 32 && c <= 126) {
            if (col < (CLIENT_W - 8) / 6) {
                drawch(CLIENT_X + 4 + col * 6, vy, c,
                       COL_WINTXT, 0xFF0A1520);
                col++;
            }
        } else {
            /* Non-printable: show dot */
            if (col < (CLIENT_W - 8) / 6) {
                drawch(CLIENT_X + 4 + col * 6, vy, '.',
                       COL_SIZE_CLR, 0xFF0A1520);
                col++;
            }
        }
    }

    if (line >= max_lines) {
        draws(CLIENT_X + 4, CLIENT_Y + CLIENT_H - 14,
              "... (truncated)", COL_SIZE_CLR, 0xFF0A1520);
    }

    vga_flip();

    /* Wait for keypress */
    while (!kbd_available()) ;
    kbd_read();
}

/* -------------------------------------------------------------------------
 * Navigate into directory
 * ------------------------------------------------------------------------- */
static void enter_directory(const char *name)
{
    if (tp_chdir(name) < 0) {
        uart_puts("tfiles: chdir failed\r\n");
        return;
    }
    read_directory();
}

/* -------------------------------------------------------------------------
 * Delete file/directory
 * ------------------------------------------------------------------------- */
static void delete_selected(void)
{
    fentry_t *e;
    char fullpath[320];
    int flags;

    if (selected < 0 || selected >= nentries)
        return;

    e = &entries[selected];

    /* Don't delete ".." */
    if (strcmp(e->name, "..") == 0)
        return;

    build_path(fullpath, sizeof(fullpath), cwd, e->name);

    flags = 0;
    if (e->type == FTYPE_DIR)
        flags = AT_REMOVEDIR;

    if (tp_unlinkat(AT_FDCWD, fullpath, flags) == 0) {
        read_directory();
        if (selected >= nentries && nentries > 0)
            selected = nentries - 1;
    }
}

/* -------------------------------------------------------------------------
 * Adjust scroll so selected item is visible
 * ------------------------------------------------------------------------- */
static void ensure_visible(void)
{
    if (selected < scroll)
        scroll = selected;
    if (selected >= scroll + VISIBLE_ROWS)
        scroll = selected - VISIBLE_ROWS + 1;
    if (scroll < 0)
        scroll = 0;
}

/* -------------------------------------------------------------------------
 * Main
 * ------------------------------------------------------------------------- */
int main(void)
{
    int running = 1;

    uart_puts("TFILES_START\r\n");

    vga_init();
    read_directory();
    redraw_all();

    while (running) {
        int key = read_key();

        if (key == 0) {
            /* No input — small delay */
            volatile int i;
            for (i = 0; i < 5000; i++) ;
            continue;
        }

        if (key == 'q' || key == 'Q' || key == 27) {
            running = 0;
            continue;
        }

        if (key == KEY_UP || key == 'k') {
            if (selected > 0) selected--;
            ensure_visible();
        }

        if (key == KEY_DOWN || key == 'j') {
            if (selected < nentries - 1) selected++;
            ensure_visible();
        }

        if (key == KEY_PGUP) {
            selected -= VISIBLE_ROWS;
            if (selected < 0) selected = 0;
            ensure_visible();
        }

        if (key == KEY_PGDN) {
            selected += VISIBLE_ROWS;
            if (selected >= nentries) selected = nentries - 1;
            if (selected < 0) selected = 0;
            ensure_visible();
        }

        if (key == KEY_HOME) {
            selected = 0;
            ensure_visible();
        }

        if (key == KEY_END) {
            selected = nentries - 1;
            if (selected < 0) selected = 0;
            ensure_visible();
        }

        if (key == '\r' || key == '\n' || key == KEY_RIGHT) {
            if (selected >= 0 && selected < nentries) {
                if (entries[selected].type == FTYPE_DIR)
                    enter_directory(entries[selected].name);
            }
        }

        if (key == 8 || key == 127 || key == KEY_LEFT) {
            /* Backspace / Left: go up */
            enter_directory("..");
        }

        if (key == 'd' || key == 'D') {
            delete_selected();
        }

        if (key == 'v' || key == 'V') {
            if (selected >= 0 && selected < nentries) {
                if (entries[selected].type != FTYPE_DIR) {
                    char fullpath[320];
                    build_path(fullpath, sizeof(fullpath),
                               cwd, entries[selected].name);
                    view_file(fullpath, entries[selected].name);
                }
            }
        }

        /* Refresh display */
        draw_list();
        draw_scrollbar();
        draw_status();
        vga_flip();
    }

    uart_puts("TFILES_DONE\r\n");
    tp_exit(0);
    return 0;
}
