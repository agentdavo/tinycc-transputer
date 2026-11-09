/* Freestanding stdio for the dEngine T4 port: read-only files come from the
 * ROM image (romfs.c); stdout/stderr go to the platform console. The names
 * are mapped to de_* so a host build links this implementation, not libc's. */
#ifndef DE_STDIO_H
#define DE_STDIO_H
#include <stdarg.h>
#include <stddef.h>

typedef struct de_file FILE;
#define stdin de_stdin
#define stdout de_stdout
#define stderr de_stderr
extern FILE *de_stdin, *de_stdout, *de_stderr;

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define BUFSIZ 1024

#define fopen de_fopen
#define fclose de_fclose
#define fread de_fread
#define fwrite de_fwrite
#define fseek de_fseek
#define ftell de_ftell
#define rewind de_rewind
#define feof de_feof
#define ferror de_ferror
#define fflush de_fflush
#define fgetc de_fgetc
#define getc de_fgetc
#define ungetc de_ungetc
#define fgets de_fgets
#define fputc de_fputc
#define putc de_fputc
#define fputs de_fputs
#define puts de_puts
#define putchar de_putchar
#define printf de_printf
#define fprintf de_fprintf
#define sprintf de_sprintf
#define snprintf de_snprintf
#define vprintf de_vprintf
#define vfprintf de_vfprintf
#define vsprintf de_vsprintf
#define vsnprintf de_vsnprintf
#define scanf de_scanf
#define sscanf de_sscanf
#define fscanf de_fscanf
#define vsscanf de_vsscanf
#define vfscanf de_vfscanf

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *f);
size_t fread(void *ptr, size_t size, size_t count, FILE *f);
size_t fwrite(const void *ptr, size_t size, size_t count, FILE *f);
int fseek(FILE *f, long offset, int whence);
long ftell(FILE *f);
void rewind(FILE *f);
int feof(FILE *f);
int ferror(FILE *f);
int fflush(FILE *f);
int fgetc(FILE *f);
int ungetc(int c, FILE *f);
char *fgets(char *s, int n, FILE *f);
int fputc(int c, FILE *f);
int fputs(const char *s, FILE *f);
int puts(const char *s);
int putchar(int c);
int printf(const char *format, ...);
int fprintf(FILE *f, const char *format, ...);
int sprintf(char *s, const char *format, ...);
int snprintf(char *s, size_t n, const char *format, ...);
int vprintf(const char *format, va_list ap);
int vfprintf(FILE *f, const char *format, va_list ap);
int vsprintf(char *s, const char *format, va_list ap);
int vsnprintf(char *s, size_t n, const char *format, va_list ap);
int sscanf(const char *s, const char *format, ...);
int fscanf(FILE *f, const char *format, ...);
int vsscanf(const char *s, const char *format, va_list ap);
int vfscanf(FILE *f, const char *format, va_list ap);
#endif
