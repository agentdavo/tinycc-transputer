#ifndef TPKG_TPZ_H
#define TPKG_TPZ_H

#include <stdio.h>

#define TPZ_PATH 256

struct tpz_member {
    char path[TPZ_PATH];
    unsigned long size;
    unsigned mode;
    int directory;
};

FILE *tpz_open(const char *filename);
/* One member, clean end, or invalid archive. */
int tpz_next(FILE *archive, struct tpz_member *member);
int tpz_copy(FILE *archive, FILE *out, unsigned long size);
int tpz_relative_path(char *path, const char *name);

#endif
