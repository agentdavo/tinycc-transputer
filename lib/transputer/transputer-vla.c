/*
 * transputer-vla.c - storage for variable-length arrays.
 *
 * Wptr is both the frame pointer and the stack pointer on a transputer:
 * every local is an ldl/stl at a fixed offset from it, so a VLA cannot be
 * carved off the workspace by moving Wptr - every later local access would
 * read the wrong word.  The compiler instead allocates VLAs from an arena
 * that belongs to the running process and calls these three functions:
 *
 *   __tp_vla_mark()             the arena's top, saved before a scope's
 *                               first VLA
 *   __tp_vla_alloc(size, align) a VLA; returns its base in A and the new
 *                               top in B
 *   __tp_vla_release(top)       back to a saved top: at the end of a scope,
 *                               on goto, break, continue and return
 *
 * which is the stack-pointer save/adjust/restore other targets do, on an
 * arena pointer instead of the stack pointer.  The arena grows upward.
 *
 * Whose arena: a process that PSetup started owns one at the bottom of its
 * workspace, growing up toward its stack (transputer-conc.c keeps the
 * records).  Anything else - the main program, or a process built by hand
 * with runp/startp - uses the main arena below.  A program that starts
 * processes some other way must not let two of them use VLAs at once.
 *
 * longjmp out of a VLA scope leaves its arrays allocated until a scope
 * that encloses the target of the jump ends.
 *
 * Running out stops the process with the error flag set rather than letting
 * an array overlap anything.
 */

#ifndef __TRANSPUTER__
#error "transputer-vla.c is for the transputer target"
#endif

#include "transputer-proc.h"

/* The main program's arena.  Small, because it is part of the image (BSS):
 * a program that needs more gives it a buffer with ProcVLAArena(). */
#define TP_VLA_MAIN_SIZE (16 * 1024)

/* Room left between the top of an arena that shares a workspace with the
 * stack and the Wptr of the allocation: the frames the caller is still to
 * push.  Only a sanity margin - the stack is not checked as it grows. */
#define TP_VLA_MARGIN 128

static char main_arena[TP_VLA_MAIN_SIZE];

/* Initialised, not zeroed: BSS is not cleared in every environment. */
static struct __tp_arena main_vla = {
    main_arena, main_arena, main_arena + TP_VLA_MAIN_SIZE
};

/* The calling process's arena.  Written out in each function rather than
 * called: a process's workspace can be small, and every frame costs some. */
#define ARENA_SELF(a)     do { if (!((a) = __tp_proc_arena())) (a) = &main_vla; } while (0)

static void vla_fail(void)
{
    __seterr();
    for (;;)
        __tp_proc_stop();
}

char *__tp_vla_mark(void)
{
    struct __tp_arena *a;

    ARENA_SELF(a);
    return a->top;
}

/* A long long comes back with its low word in A and its high word in B. */
unsigned long long __tp_vla_alloc(unsigned size, unsigned align)
{
    struct __tp_arena *a;
    char *w = (char *)&a;
    char *lim;
    unsigned base, top;
    union { unsigned long long v; struct { unsigned lo, hi; } p; } r;

    ARENA_SELF(a);
    lim = a->hi;
    if (align < 4)
        align = 4;
    base = ((unsigned)a->top + align - 1) & -align;
    top = base + ((size + 3) & ~3u);
    /* An arena inside the workspace must stay below the stack. */
    if (a->lo <= w && w < a->hi)
        lim = w - TP_VLA_MARGIN;
    if (top < base || top > (unsigned)lim || base < (unsigned)a->top)
        vla_fail();
    a->top = (char *)top;
    r.p.lo = base;
    r.p.hi = top;
    return r.v;
}

void __tp_vla_release(char *top)
{
    struct __tp_arena *a;

    ARENA_SELF(a);
    if (top < a->lo || top > a->hi)
        vla_fail();
    a->top = top;
}

/* Give the calling process `buf` for its VLAs instead of the arena it has.
 * Returns 0, or -1 (and changes nothing) while it has a VLA live. */
int ProcVLAArena(void *buf, int size)
{
    struct __tp_arena *a;
    char *lo = (char *)(((unsigned)buf + 3) & ~3u);
    char *hi = (char *)buf + size;

    ARENA_SELF(a);
    if (a->top != a->lo || !buf || hi < lo)
        return -1;
    a->lo = a->top = lo;
    a->hi = hi;
    return 0;
}
