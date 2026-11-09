#ifndef DE_STDLIB_H
#define DE_STDLIB_H
#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 32767

#define malloc de_malloc
#define calloc de_calloc
#define realloc de_realloc
#define free de_free
#define atof de_atof
#define atoi de_atoi
#define atol de_atol
#define strtod de_strtod
#define strtof de_strtof
#define strtol de_strtol
#define strtoul de_strtoul
#define exit de_exit
#define abort de_abort
#define getenv de_getenv
#define setenv de_setenv
#define abs de_abs
#define labs de_labs
#define rand de_rand
#define srand de_srand
#define qsort de_qsort

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
double atof(const char *s);
int atoi(const char *s);
long atol(const char *s);
double strtod(const char *s, char **end);
float strtof(const char *s, char **end);
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
void exit(int status);
void abort(void);
char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int abs(int x);
long labs(long x);
int rand(void);
void srand(unsigned int seed);
void qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *));
#endif
