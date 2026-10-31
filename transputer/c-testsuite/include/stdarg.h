#ifndef _STDARG_H
#define _STDARG_H

/* TCC built-in varargs support */
typedef __builtin_va_list va_list;
#define va_start __builtin_va_start
#define va_end __builtin_va_end
#define va_arg __builtin_va_arg
#define va_copy __builtin_va_copy

#endif
