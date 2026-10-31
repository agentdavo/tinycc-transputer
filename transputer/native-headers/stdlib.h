#ifndef _STDLIB_H
#define _STDLIB_H

#define NULL ((void*)0)

typedef long ptrdiff_t;
typedef unsigned long size_t;

void *malloc(size_t size);
void free(void *ptr);
void *realloc(void *ptr, size_t size);
void exit(int status);
int atoi(const char *s);

#endif
