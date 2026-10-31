/* Minimal stdarg.h for TinyCC self-hosting */
#ifndef _STDARG_H
#define _STDARG_H

typedef char *va_list;

#define va_start(ap, last) ((ap) = (va_list)&(last) + sizeof(last))
#define va_end(ap) ((void)0)
#define va_arg(ap, type) (*((type *)((ap) += sizeof(type)) - 1))
#define va_copy(dest, src) ((dest) = (src))

#endif