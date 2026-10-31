/* <stdint.h> for building the vendored TVX headers into the transputer
 * runtime (lib/transputer-tvx.c, built with -nostdinc: tcc would otherwise
 * find the host's /usr/include). */
#ifndef TP_TVX_STDINT_H
#define TP_TVX_STDINT_H
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;
typedef int intptr_t;
typedef unsigned int uintptr_t;
#endif
