/* Real stdio failures, not a mock of the helper's implementation. Linux host. */
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include "rgbrow.h"

int main(void)
{
    struct pov_rgb_writer out;
    unsigned char rgb[3] = {1, 2, 3};
    FILE *file = tmpfile();
    int fd, pipefd[2];
    if (!file || pov_rgb_begin(&out, file, 0, 1) ||
        pov_rgb_begin(&out, file, 1, 0) ||
        pov_rgb_begin(&out, file, UINT_MAX, UINT_MAX) || ftell(file) != 0)
        return 1;
    fclose(file);
    file = fopen("/dev/full", "wb");
    if (!file || setvbuf(file, NULL, _IONBF, 0) || pov_rgb_begin(&out, file, 1, 1))
        return 2;
    fclose(file);
    file = tmpfile();
    if (!file || setvbuf(file, NULL, _IONBF, 0) || !pov_rgb_begin(&out, file, 1, 1))
        return 3;
    fd = open("/dev/full", O_WRONLY);
    if (fd < 0 || dup2(fd, fileno(file)) < 0 || pov_rgb_write(&out, rgb, 0))
        return 4;
    close(fd);
    fclose(file);
    if (pipe(pipefd))
        return 5;
    file = fdopen(pipefd[1], "wb");
    if (!file || setvbuf(file, NULL, _IONBF, 0) ||
        !pov_rgb_begin(&out, file, 1, 2) || pov_rgb_write(&out, rgb, 1))
        return 6;
    fclose(file);
    close(pipefd[0]);
    puts("row dimensions, header/write/seek failures pass");
    return 0;
}
