/* One-row cache.  Normal rendering writes one RGB block per row.  Revisits
 * (for example preview/AA updates) reload the prior row before changing it,
 * so complete rows can arrive out of order without losing earlier pixels. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rgbrow.h"

static struct pov_rgb_writer output;
static unsigned char *row, *written;
static int row_y = -1, dirty;

static void flush_row(void)
{
    if (dirty) {
        if (!pov_rgb_write(&output, row, (unsigned)row_y))
            exit(1);
        written[row_y] = 1;
        dirty = 0;
    }
}

void display_init(int width, int height)
{
    FILE *file;
    if (width <= 0 || height <= 0)
        exit(1);
    file = fopen("image.ppm", "w+b");
    if (!file || !pov_rgb_begin(&output, file, width, height))
        exit(1);
    row = malloc((unsigned long)width * 3);
    written = calloc(height, 1);
    if (!row || !written)
        exit(1);
    row_y = -1;
    dirty = 0;
}

void display_plot(int x, int y, char r, char g, char b)
{
    unsigned char *pixel;
    unsigned long size = (unsigned long)output.width * 3;
    if (x < 0 || y < 0 || (unsigned)x >= output.width ||
        (unsigned)y >= output.height)
        exit(1);
    if (y != row_y) {
        flush_row();
        if (written[y]) {
            long offset = output.header + (long)((unsigned long)y * size);
            if (fseek(output.file, offset, SEEK_SET) ||
                fread(row, 1, size, output.file) != size)
                exit(1);
            output.position = offset + (long)size;
        } else {
            memset(row, 0, size);
        }
        row_y = y;
    }
    pixel = row + (unsigned long)x * 3;
    pixel[0] = (unsigned char)r;
    pixel[1] = (unsigned char)g;
    pixel[2] = (unsigned char)b;
    dirty = 1;
}

void display_finished(void)
{
    if (output.file) {
        flush_row();
        if (fclose(output.file))
            exit(1);
        output.file = NULL;
        free(row);
        free(written);
        row = written = NULL;
    }
}

void display_close(void) { display_finished(); }
