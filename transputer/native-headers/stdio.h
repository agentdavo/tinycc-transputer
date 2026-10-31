#ifndef _STDIO_H
#define _STDIO_H

#include <stdarg.h>

typedef struct FILE FILE;
extern FILE *stdin, *stdout, *stderr;

int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int sprintf(char *str, const char *format, ...);
int vprintf(const char *format, va_list ap);
int vfprintf(FILE *stream, const char *format, va_list ap);
int vsprintf(char *str, const char *format, va_list ap);

#endif
