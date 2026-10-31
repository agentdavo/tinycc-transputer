/*
 * conc.h - transputer links, ALT and process primitives for TinyCC.
 *
 * The shape of this interface follows the Logical Systems Transputer 'C'
 * Library (Version 91.1), because that spelling of the transputer
 * concurrency model is the one people already know.  What is underneath is
 * not the same: every operation that maps onto a single instruction is a
 * compiler builtin here, so ChanOutChar is one `outbyte`, not a call.
 *
 * Where the 1991 library and this one disagree, this one wins:
 *
 *   - The ALT wrappers are named Chan*Alt* rather than Proc*Alt*, and take
 *     an explicit count instead of relying on a NULL terminator, so a
 *     channel array can be built once and sliced.  The NULL-terminated
 *     spellings are kept as thin wrappers for ported code.
 *   - There is no virtual channel layer (no VChan/_vcmain).  Links are the
 *     four physical links; routing is the application's business.
 *   - Nothing here talks to a host I/O server.
 *
 * Requires -mtransputer (or -mt9000).  All ALT sequences must run to
 * completion inside one function: the enable/wait/disable instructions keep
 * their state in the workspace slots just below Wptr, and a call in the
 * middle of the sequence would land on them.
 */

#ifndef _CONC_H
#define _CONC_H

/* ------------------------------------------------------------------ */
/* Channels                                                            */
/* ------------------------------------------------------------------ */

typedef volatile int Channel;

/* The idle channel word.  MinInt on every transputer, and what `mint`
 * loads; spelled as a constant so it is usable in static initialisers. */
#define NOPROCESS   ((int)0x80000000)

/* A soft channel must hold NOPROCESS before first use:
 *      Channel c = NOPROCESS;
 * or, for one that is not statically initialised, ChanInit(&c). */
#define ChanInit(c) (*(c) = NOPROCESS)

/* The four hardware links.  Output channels sit at MinInt, inputs 16 bytes
 * above, one word per link (ISM 3.6). */
#define LINKOUT(n)  ((Channel *)(0x80000000 + ((n) << 2)))
#define LINKIN(n)   ((Channel *)(0x80000010 + ((n) << 2)))

#define LINK0OUT    LINKOUT(0)
#define LINK1OUT    LINKOUT(1)
#define LINK2OUT    LINKOUT(2)
#define LINK3OUT    LINKOUT(3)
#define LINK0IN     LINKIN(0)
#define LINK1IN     LINKIN(1)
#define LINK2IN     LINKIN(2)
#define LINK3IN     LINKIN(3)

#define NUM_LINKS   4

/* ------------------------------------------------------------------ */
/* Channel I/O - one instruction each                                  */
/* ------------------------------------------------------------------ */

/* Macros, not static inline functions: TinyCC does not inline, so as
 * functions each of these was a call, a frame and a return around its one
 * instruction - 13 instructions to input a word where occam spends 4
 * (transputer-raytracer/c/CODEGEN.md).  Each argument is evaluated once.
 *
 *   void ChanIn(Channel *c, void *buf, int n)
 *   void ChanOut(Channel *c, const void *buf, int n)
 *   void ChanOutChar(Channel *c, int b)
 *   void ChanOutInt(Channel *c, int w)
 *   int  ChanInChar(Channel *c)
 *   int  ChanInInt(Channel *c)
 *   int  ChanReset(Channel *c) - the word the channel held: NOPROCESS if it
 *        was idle, otherwise the descriptor of the process waiting on it.
 *        On a link channel this also resets that direction of the link
 *        hardware (and on a T9000 byte-stream link, restarts a link halted
 *        by an error).
 */
#define ChanIn(c, buf, n)    __in((n), (c), (buf))
#define ChanOut(c, buf, n)   __out((n), (c), (void *)(buf))
#define ChanOutChar(c, b)    __outbyte((c), (b))
#define ChanOutInt(c, w)     __outword((c), (w))
#define ChanInChar(c) \
    ({ unsigned char ChanIn_b_; __in(1, (c), &ChanIn_b_); (int)ChanIn_b_; })
#define ChanInInt(c) \
    ({ int ChanIn_w_; __in(4, (c), &ChanIn_w_); ChanIn_w_; })
#define ChanReset(c)         __resetch(c)

/* The technical note's Reinitialise (see "Aborting a transfer" below): reset
 * a channel that should be idle.  If a process was still waiting on it the
 * channel was in use - the process is left waiting for ever - and the Error
 * flag is set, which stops the processor if HaltOnError is set. */
static inline int ChanReinit(Channel *c)
{
    int w = __resetch(c);

    if (w != NOPROCESS)
        __seterr();
    return w;
}

/* ------------------------------------------------------------------ */
/* ALT - input multiplexing                                            */
/* ------------------------------------------------------------------ */

/*
 * These are the reason this header exists.  A process that serves several
 * links cannot poll them: a blocked `in` on the wrong link deadlocks the
 * pair.  ALT enables a set of input channels at once, blocks until one is
 * ready, and reports which - arbitration done by the scheduler, not by the
 * program.
 *
 * All four return a zero-based index into `chans`, and all of them only
 * select; the caller still performs the ChanIn on the channel that won.
 *
 *   ChanAlt        blocks until one of the channels is ready.
 *   ChanSkipAlt    returns -1 immediately if none is ready.
 *   ChanTimerAlt   returns -1 if the current-priority clock passes `time`
 *                  first.  `time` is an absolute timer value, so compare
 *                  against Time(), not a duration.
 *
 * A guard vector may be passed to ChanAltGuarded to disable individual
 * channels for one pass (guards[i] == 0 means "not this time"), which is
 * how you stop servicing a link that has no buffer space without
 * rebuilding the channel array.
 */

int ChanAlt(Channel **chans, int n);
int ChanSkipAlt(Channel **chans, int n);
int ChanTimerAlt(int time, Channel **chans, int n);
int ChanAltGuarded(Channel **chans, const int *guards, int n);

/* Two or three channels, straight-line: no channel or guard arrays, no
 * loops.  ChanAlt runs about 150 instructions to choose between two
 * channels; these about 30, against occam's 17 for an inline ALT.  A
 * guard g that is 0 leaves its channel out, as in ChanAltGuarded. */
#define ChanAlt2(c0, c1)          __tp_alt2(0, (c0), (c1))
#define ChanAlt3(c0, c1, c2)      __tp_alt3(0, 1, (c0), 1, (c1), 1, (c2))
#define ChanAltGuarded3(g0, c0, g1, c1, g2, c2) \
    __tp_alt3(0, (g0), (c0), (g1), (c1), (g2), (c2))
int __tp_alt2(int slot0, Channel *c0, Channel *c1);
int __tp_alt3(int slot0, int g0, Channel *c0, int g1, Channel *c1,
              int g2, Channel *c2);

/* NULL-terminated forms, for code written against the 1991 library. */
int ChanAltList(Channel **chans);
int ChanSkipAltList(Channel **chans);
int ChanTimerAltList(int time, Channel **chans);

/* ------------------------------------------------------------------ */
/* Aborting a transfer                                                 */
/* ------------------------------------------------------------------ */

/*
 * INMOS technical note 1, "Extraordinary use of transputer links"
 * (72-TCH-001), and TCWG 8.3.1.  A ChanIn or ChanOut on a link waits for
 * ever if the far end never communicates - it has crashed, or someone has
 * pulled the cable.  These abandon the transfer instead:
 *
 *   ChanInTimeFail / ChanOutTimeFail   the note's InputOrFail.t and
 *       OutputOrFail.t: give up when the current-priority clock passes
 *       `time`, an absolute timer value (Time() + ticks).
 *   ChanInChanFail / ChanOutChanFail   InputOrFail.c and OutputOrFail.c:
 *       give up when a 4-byte message (an int) arrives on `kill`.  The kill
 *       message is input only when it abandons the transfer: if the transfer
 *       completes first, the sender of the kill is still waiting to send it.
 *
 * All four return 0 if the message was transferred, 1 if it was abandoned
 * (the channel has then been reset with resetch, as ChanReset), and -1 if
 * the workspace is too small (nothing is done).  A transfer that completes
 * just as the deadline passes or the kill arrives still returns 0, but the
 * channel has then been reset as well.
 *
 * The transfer runs in a high-priority helper process in `ws` (at least
 * PROC_WS_MIN bytes, live until the call returns and free afterwards), and
 * the caller waits for it with an ALT that also has a timer guard or the
 * kill channel.  An ALT alone cannot do this:
 *
 *   - An ALT cannot rescue a stuck input.  A link input guard is ready as
 *     soon as the first byte has arrived; the ALT then commits to the input,
 *     and if the link fails before the last byte, the input waits for ever
 *     whatever timer guard the ALT had.  Only resetting the channel ends it.
 *
 * Resetting is not the end of it, because the other end of the link cannot
 * know what happened:
 *
 *   - Afterwards the link is only usable again once both ends have
 *     reinitialised both of its channels (ChanReinit on LINKnIN and
 *     LINKnOUT), and only after all activity on the link has stopped: a
 *     message still arriving, or a far end still trying to send, leaves
 *     part of a message in the link to be taken as the start of the next.
 *     Reinitialising a channel that is still in use sets the Error flag.
 *   - Failure can be one-sided.  An output is complete when the last byte
 *     has been acknowledged, an input when the last byte has arrived.  If
 *     the link fails in between, the input returns 0 and the output 1, so
 *     a message the sender resends may arrive twice: number them.
 *
 * The note's example 2 is a protocol that rides out a link being
 * unplugged; transputer/tests/net/xlink_recover.c implements it.  Each end
 * sends a tick at least every quarter second and treats a second without
 * input as a failure.  It then falls silent, and on a timer cycle agreed at
 * the first synchronisation, split into three phases, it reinitialises both
 * link channels at the start of the first phase and tries a timed input in
 * parallel with a timed output (each with this phase's end as the deadline)
 * in the second; the third is silence.  It repeats this until both the
 * input and the output succeed.  Phases longer than the difference between
 * the two ends' clocks keep each end's reset inside the other's silence.
 *
 * On a soft channel none of this arises: a transfer between two processes
 * is a memory copy, so it is never left half done.  These work on soft
 * channels too, but ChanTimerAlt is the cheaper way to wait there.
 */
int ChanInTimeFail(Channel *c, void *buf, int n, int time, void *ws, int wsize);
int ChanOutTimeFail(Channel *c, const void *buf, int n, int time, void *ws, int wsize);
int ChanInChanFail(Channel *c, void *buf, int n, Channel *kill, void *ws, int wsize);
int ChanOutChanFail(Channel *c, const void *buf, int n, Channel *kill, void *ws, int wsize);

/* ------------------------------------------------------------------ */
/* Time                                                                */
/* ------------------------------------------------------------------ */

/* Timer ticks are 1us at high priority and 64us at low - the two clocks
 * are separate, and a time captured at one priority is meaningless at the
 * other. */
#define TICKS_PER_SEC_HIGH  1000000
#define TICKS_PER_SEC_LOW   15625

static inline int Time(void)          { return __ldtimer(); }
static inline void SetTime(int t)     { __sttimer(t); }
static inline void ProcAfter(int t)   { __tin(t); }            /* until t */
static inline void ProcWait(int d)    { __tin(__ldtimer() + d); } /* for d */

/* Timer comparison must be done as a signed difference, never as `a < b`:
 * the tick counter wraps. */
#define TimeAfter(a, b)  (((int)((a) - (b))) > 0)

/* ------------------------------------------------------------------ */
/* Processes                                                           */
/* ------------------------------------------------------------------ */

/* A process descriptor is a workspace pointer with the priority in bit 0,
 * exactly as the hardware stores it. */
typedef int PDes;

#define PRI_HIGH 0
#define PRI_LOW  1

#define PDesWptr(p)  ((void *)((p) & ~1))
#define PDesPri(p)   ((p) & 1)

/* Minimum workspace for a forked process, in bytes: the scheduler's own
 * slots below Wptr, PSetup's start frame and PStop (about 80 words
 * between them) and room for a small frame.  Anything that calls printf or
 * similar needs far more, and so do VLAs - budget by measurement, not by
 * this. */
#define PROC_WS_MIN  512

/*
 * PSetup builds a workspace for `func` and returns its descriptor without
 * scheduling it; PRun starts it.  OR the descriptor with PRI_LOW to run at
 * low priority:
 *
 *      PRun(PSetup(ws, sizeof ws, server) | PRI_LOW);
 *
 * `func` takes no arguments: a process entered by runp never executes the
 * CALL that would spill A/B/C into the parameter slots, so arguments have
 * nowhere to arrive.  Hand it context over a channel once it is running.
 *
 * The workspace must outlive the process.  A process ends by calling
 * PStop or by returning from `func`, and its workspace may be reused once
 * it has: in practice once its last message has arrived, if all that
 * follows that message is PStop() or the end of `func` (neither contains a
 * timeslice point, so nothing else runs in between).  A process that ends
 * any other way - a bare __stopp(), or blocking forever - stays on the
 * record PSetup made, and its workspace must not then become another
 * process's stack.
 *
 * Variable-length arrays: a process PSetup started takes its VLAs from the
 * bottom of its own workspace, growing up toward its stack, so budget the
 * workspace for both.  Everything else - the main program, or a process
 * built by hand with __runp/__startp - shares one 16 KiB arena, so at most
 * one of those may have VLAs live at a time.  ProcVLAArena() gives the
 * calling process a buffer of its own instead (it fails, returning -1,
 * while that process has a VLA live).  Running out of arena stops the
 * process with the error flag set.
 */
PDes PSetup(void *ws, int wsize, void (*func)(void));
void __tp_proc_stop(void);
int ProcVLAArena(void *buf, int size);

static inline void PRun(PDes p)   { __runp(p); }
static inline void PStop(void)    { __tp_proc_stop(); }

static inline int  ProcGetPriority(void) { return __ldpri(); }

/*
 * Deliberately absent, and why:
 *
 *   PHalt / ProcReschedule  - both need the running process to name its own
 *       descriptor, and no instruction yields it.  gajw returns the old
 *       Wptr but only by replacing it, which is not something a C frame
 *       survives.  A process that wants to be resumable must instead block
 *       on a channel that another process writes.
 *
 *   SemP / SemV / PFork / PJoin - these need either an atomic section or
 *       the same self-descriptor.  Built on top of the primitives that do
 *       exist they would be racy under low-priority timeslicing, so they
 *       are left out rather than shipped subtly wrong.  A soft channel
 *       used as a token is the working substitute for a binary semaphore:
 *       one ChanIn to acquire, one ChanOut to release.  The T9000 has
 *       semaphore instructions: <t9000.h> has SemWait/SemSignal on them,
 *       and resources (a many-to-one server without an ALT).
 */

/* ------------------------------------------------------------------ */
/* Block moves and bit operations                                      */
/* ------------------------------------------------------------------ */

/* Rectangular copy: `rows` rows of `width` bytes, stepping sstride bytes
 * per source row and dstride per destination row.  One instruction pair on
 * T800 and later. */
static inline void Move2D(const void *src, void *dst, int width, int rows,
                          int sstride, int dstride)
{
    __move2dinit(rows, dstride, sstride);
    __move2dall(width, dst, (void *)src);
}

/* As Move2D, but copies only the non-zero (resp. zero) source bytes -
 * a sprite blit with colour 0 as the transparent key. */
static inline void Move2DNonZero(const void *src, void *dst, int width,
                                 int rows, int sstride, int dstride)
{
    __move2dinit(rows, dstride, sstride);
    __move2dnonzero(width, dst, (void *)src);
}

static inline void Move2DZero(const void *src, void *dst, int width,
                              int rows, int sstride, int dstride)
{
    __move2dinit(rows, dstride, sstride);
    __move2dzero(width, dst, (void *)src);
}

static inline int BitCnt(int w)            { return __bitcnt(w); }
static inline int BitRevWord(int w)        { return __bitrevword(w); }
static inline int BitRevNBits(int n, int w){ return __bitrevnbits(n, w); }

/* ------------------------------------------------------------------ */
/* Scheduler queue registers                                           */
/* ------------------------------------------------------------------ */

/* buf[0] receives the queue front pointer, buf[1] the back.  For writing a
 * scheduler or a debugger; ordinary code has no business here. */
static inline void GetHiPriQ(int *buf)  { __saveh(buf); }
static inline void GetLoPriQ(int *buf)  { __savel(buf); }

static inline void SetHiPriQ(void *front, void *back)
{
    __sthf(front);
    __sthb(back);
}

static inline void SetLoPriQ(void *front, void *back)
{
    __stlf(front);
    __stlb(back);
}

#endif /* _CONC_H */
