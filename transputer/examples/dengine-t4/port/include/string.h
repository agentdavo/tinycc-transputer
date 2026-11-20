#ifndef DE_STRING_H
#define DE_STRING_H
#include <stddef.h>
/* Standard names: TinyCC inlines memcpy as `move` where it can, and a host
 * build may use its C library's string functions, whose results are fixed
 * by the standard. libc.c defines them for the transputer. */
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memchr(const void *s, int c, size_t n);
size_t strlen(const char *s);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strcat(char *dst, const char *src);
char *strncat(char *dst, const char *src, size_t n);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
int strcasecmp(const char *a, const char *b);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
#define strdup de_strdup
char *strdup(const char *s);
#endif
