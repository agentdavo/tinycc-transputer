/* Exercise partial-row revisits and out-of-order pixels, rather than just
 * the renderer's sequential happy path.  verify-display.py supplies bytes. */
#include <stdio.h>
void display_init(int, int);
void display_plot(int, int, char, char, char);
void display_finished(void);
void display_close(void);

int main(int argc, char **argv)
{
    const int ys[3] = {2, 0, 1}, xs[4] = {3, 1, 0, 2};
    int i, j, x, y;
    (void)argc;
    (void)argv;
    display_init(4, 3);
    for (i = 0; i < 3; ++i) {
        y = ys[i];
        for (j = 0; j < 4; ++j) {
            x = xs[j];
            display_plot(x, y, x * 37, y * 91, x + y * 4);
        }
    }
    display_plot(1, 0, 255, 128, 0);
    display_plot(2, 2, 1, 2, 3);
    display_finished();
    display_close();
    puts("DISPLAYTEST PASS");
    return 0;
}
