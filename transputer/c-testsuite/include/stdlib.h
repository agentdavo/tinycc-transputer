#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

void exit(int status);
void abort(void);

int abs(int j);
long labs(long j);

int atoi(const char *nptr);
long atol(const char *nptr);

void qsort(void *base, size_t nmemb, size_t size,
            int (*compar)(const void *, const void *));

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#define RAND_MAX 2147483647

int rand(void);
void srand(unsigned int seed);

#endif
