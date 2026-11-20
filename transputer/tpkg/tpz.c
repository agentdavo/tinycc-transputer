/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "tpz.h"
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/zlib.h>
#include "inflate.h"
#include "infutil.h"

static struct inflate_workspace workspace;
static unsigned char inbuf[4096], outbuf[4096];
static uint32_t crc_table[256];

static uint32_t crc32(uint32_t crc, const unsigned char *data, size_t size)
{
    while (size--)
        crc = (crc >> 8) ^ crc_table[(crc ^ *data++) & 255];
    return crc;
}

static void init_crc(void)
{
    unsigned i, j;
    for (i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (j = 0; j < 8; j++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
        crc_table[i] = crc;
    }
}

static int skip_header(FILE *in)
{
    unsigned char header[10];
    unsigned flags;
    int ch;
    if (fread(header, 1, sizeof(header), in) != sizeof(header) ||
        header[0] != 31 || header[1] != 139 || header[2] != 8 || (header[3] & 224))
        return -1;
    flags = header[3];
    if (flags & 4) {
        unsigned char size[2];
        if (fread(size, 1, 2, in) != 2 ||
            fseek(in, size[0] + (size[1] << 8), SEEK_CUR))
            return -1;
    }
    for (flags &= 24; flags; flags &= flags - 1) {
        do {
            ch = fgetc(in);
        } while (ch > 0);
        if (ch == EOF)
            return -1;
    }
    return (header[3] & 2) && fseek(in, 2, SEEK_CUR) ? -1 : 0;
}

static uint32_t little32(const unsigned char *p)
{
    return p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int check_trailer(FILE *in, z_stream *stream, uint32_t crc)
{
    unsigned char trailer[8];
    unsigned n = stream->avail_in;
    if (n > sizeof(trailer))
        return -1;
    memcpy(trailer, stream->next_in, n);
    if (fread(trailer + n, 1, sizeof(trailer) - n, in) != sizeof(trailer) - n ||
        little32(trailer) != (crc ^ 0xffffffffu) ||
        little32(trailer + 4) != (uint32_t)stream->total_out ||
        fgetc(in) != EOF || ferror(in))
        return -1;
    return 0;
}

static int unpack_gzip(FILE *in, FILE *out)
{
    z_stream stream = {0};
    uint32_t crc = 0xffffffffu;
    int rc;
    if (skip_header(in))
        return -1;
    init_crc();
    stream.workspace = &workspace;
    if (zlib_inflateInit2(&stream, -MAX_WBITS) != Z_OK)
        return -1;
    do {
        size_t n;
        if (!stream.avail_in) {
            stream.next_in = inbuf;
            stream.avail_in = fread(inbuf, 1, sizeof(inbuf), in);
            if (!stream.avail_in)
                return -1;
        }
        stream.next_out = outbuf;
        stream.avail_out = sizeof(outbuf);
        rc = zlib_inflate(&stream, Z_NO_FLUSH);
        n = sizeof(outbuf) - stream.avail_out;
        if ((rc != Z_OK && rc != Z_STREAM_END) ||
            fwrite(outbuf, 1, n, out) != n)
            return -1;
        crc = crc32(crc, outbuf, n);
    } while (rc != Z_STREAM_END);
    return check_trailer(in, &stream, crc);
}

FILE *tpz_open(const char *filename)
{
    /* Keep syscall paths below the protected stack ceiling. The
     * guest pathname reader validates a full PATH_MAX span. */
    static char path[TPZ_PATH];
    static char temporary[sizeof("/tmp/tpkg-XXXXXX")];
    FILE *archive, *source;
    int fd;
    if (strlen(filename) >= sizeof(path))
        return NULL;
    strcpy(path, filename);
    strcpy(temporary, "/tmp/tpkg-XXXXXX");
    source = fopen(path, "rb");
    if (!source)
        return NULL;
    fd = mkstemp(temporary);
    archive = fd >= 0 ? fdopen(fd, "w+b") : NULL;
    if (fd >= 0) {
        unlinkat(AT_FDCWD, temporary, 0);
        if (!archive)
            close(fd);
    }
    if (archive && (unpack_gzip(source, archive) || fflush(archive) ||
                    fseek(archive, 0, SEEK_SET))) {
        fclose(archive);
        archive = NULL;
    }
    fclose(source);
    return archive;
}

static int octal(const char *text, unsigned n, unsigned long *value)
{
    unsigned long v = 0;
    while (n && *text == ' ') {
        n--;
        text++;
    }
    while (n && *text >= '0' && *text <= '7') {
        if (v > (0x7fffffffUL >> 3))
            return -1;
        v = (v << 3) + (*text++ - '0');
        n--;
    }
    for (; n; n--, text++)
        if (*text && *text != ' ')
            return -1;
    *value = v;
    return 0;
}

int tpz_relative_path(char *path, const char *name)
{
    const char *p;
    size_t length;
    while (name[0] == '.' && name[1] == '/')
        name += 2;
    length = strlen(name);
    if (length && name[length - 1] == '/')
        length--;
    if (!length || length >= TPZ_PATH)
        return -1;
    memcpy(path, name, length);
    path[length] = 0;
    for (p = path; ; ) {
        size_t n = strcspn(p, "/\n\r\t");
        if (!n || (n == 1 && p[0] == '.') ||
            (n == 2 && p[0] == '.' && p[1] == '.') ||
            (p[n] && p[n] != '/'))
            return -1;
        if (!p[n])
            return 0;
        p += n + 1;
    }
}

static int next_member(FILE *archive, struct tpz_member *member, int long_name)
{
    char header[512], name[TPZ_PATH + 1], *prefix;
    unsigned long size, mode, stored_sum;
    unsigned i, sum = 0;
    if (fread(header, 1, sizeof(header), archive) != sizeof(header))
        return -1;
    for (i = 0; i < sizeof(header); i++)
        sum += (unsigned char)header[i];
    if (!sum)
        return 0;
    if (octal(header + 148, 8, &stored_sum) ||
        octal(header + 124, 12, &size) || octal(header + 100, 8, &mode))
        return -1;
    for (i = 148; i < 156; i++)
        sum += ' ' - (unsigned char)header[i];
    if (sum != stored_sum)
        return -1;

    if (header[156] == 'L') {
        if (long_name || !size || size > sizeof(name) ||
            fread(name, 1, size, archive) != size || name[size - 1] ||
            fseek(archive, (-size) & 511, SEEK_CUR) ||
            next_member(archive, member, 1) != 1 ||
            tpz_relative_path(member->path, name))
            return -1;
        return 1;
    }
    member->directory = header[156] == '5';
    if (header[156] && header[156] != '0' && !member->directory)
        return -1;
    prefix = !memcmp(header + 257, "ustar\0", 6) ? header + 345 : "";
    if (*prefix)
        snprintf(name, sizeof(name), "%.155s/%.100s", prefix, header);
    else
        snprintf(name, sizeof(name), "%.100s", header);
    member->mode = mode & 0777;
    member->size = size;
    if (member->directory && size)
        return -1;
    if (member->directory && (!strcmp(name, ".") || !strcmp(name, "./"))) {
        member->path[0] = 0;
        return 1;
    }
    return tpz_relative_path(member->path, name) ? -1 : 1;
}

int tpz_next(FILE *archive, struct tpz_member *member)
{
    return next_member(archive, member, 0);
}

int tpz_copy(FILE *archive, FILE *out, unsigned long size)
{
    unsigned long padding = (-size) & 511;
    while (size) {
        size_t n = size < sizeof(outbuf) ? size : sizeof(outbuf);
        if (fread(outbuf, 1, n, archive) != n || (out && fwrite(outbuf, 1, n, out) != n))
            return -1;
        size -= n;
    }
    return padding && fread(outbuf, 1, padding, archive) != padding ? -1 : 0;
}
