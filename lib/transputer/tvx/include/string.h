/* <string.h> for the vendored TVX headers: no libc is linked into the
 * runtime, so the two functions they use are defined here. */
#ifndef TP_TVX_STRING_H
#define TP_TVX_STRING_H
#include <stddef.h>
static void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    while (n--)
        *p++ = *q++;
    return d;
}
static void *memset(void *d, int c, size_t n)
{
    unsigned char *p = d;
    while (n--)
        *p++ = (unsigned char)c;
    return d;
}
#endif
