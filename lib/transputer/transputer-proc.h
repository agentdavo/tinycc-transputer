/*
 * transputer-proc.h - what transputer-conc.c and transputer-vla.c share.
 *
 * A VLA arena: storage for the variable-length arrays of one process.  It
 * grows upward from `lo`; `top` is the first free byte and `hi` the end.
 * When the arena is the bottom of the process's own workspace, the stack
 * grows down toward it from above, and the allocator also keeps `top`
 * below the current Wptr.
 */

#ifndef TRANSPUTER_PROC_H
#define TRANSPUTER_PROC_H

struct __tp_arena {
    char *top;
    char *lo;
    char *hi;
};

/* The arena of the calling process if PSetup started it, else 0. */
struct __tp_arena *__tp_proc_arena(void);

/* PStop(): forget the calling process's record, then stop it. */
void __tp_proc_stop(void);

#endif
