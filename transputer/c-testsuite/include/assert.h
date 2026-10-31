#ifndef _ASSERT_H
#define _ASSERT_H

extern void abort(void);

#ifdef NDEBUG
#define assert(expr) ((void)0)
#else
#define assert(expr) \
    ((expr) ? (void)0 : abort())
#endif

#endif
