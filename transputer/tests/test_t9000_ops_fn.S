/* Raw-encoded probes for the T9000-only instructions implemented in the t4
 * emulator (fprem/fprange/fplg/fpabs/fpsqrt/fpadddbsn/fpchki32, cb/cbu/cs/csu,
 * devlw/devsw, ldprodid, wait/signal, settimeslice/intdis/intenb/timeslice,
 * fpstall/fpldall, stmove2dinit, swapqueue, nop/icl/fdca, ldflags/stflags).
 *
 * Every probe takes one pointer argument (arriving in workspace slot 1 after
 * CALL) and communicates through the pointed-to word array.
 */
.section .text,"ax"

.global t9_fprem_sn
t9_fprem_sn:
    /* io: b@0, a@4 -> rem@8   (FAreg := FBreg REM FAreg) */
    .byte 0x71              /* ldl 1 */
    .byte 0x28, 0xFE        /* fpldnlsn  (b) */
    .byte 0x71, 0x51        /* ldl 1; ldnlp 1 */
    .byte 0x28, 0xFE        /* fpldnlsn  (a) */
    .byte 0x2C, 0xFF        /* fprem */
    .byte 0x71, 0x52        /* ldl 1; ldnlp 2 */
    .byte 0x28, 0xF8        /* fpstnlsn */
    .byte 0x22, 0xF0        /* ret */

.global t9_fprange_sn
t9_fprange_sn:
    /* io: b@0, a@4 -> quot@8, rem@12 */
    .byte 0x71              /* ldl 1 */
    .byte 0x28, 0xFE        /* fpldnlsn  (b) */
    .byte 0x71, 0x51        /* ldl 1; ldnlp 1 */
    .byte 0x28, 0xFE        /* fpldnlsn  (a) */
    .byte 0x28, 0xFD        /* fprange   (FB=quot, FA=rem) */
    .byte 0x71, 0x53        /* ldl 1; ldnlp 3 */
    .byte 0x28, 0xF8        /* fpstnlsn  (rem; pops, quot -> FA) */
    .byte 0x71, 0x52        /* ldl 1; ldnlp 2 */
    .byte 0x28, 0xF8        /* fpstnlsn  (quot) */
    .byte 0x22, 0xF0        /* ret */

.global t9_fplg_sn
t9_fplg_sn:
    /* io: b@0, a@4 -> bool@8   (FBreg <> FAreg, NaN -> false) */
    .byte 0x71              /* ldl 1 */
    .byte 0x28, 0xFE        /* fpldnlsn */
    .byte 0x71, 0x51        /* ldl 1; ldnlp 1 */
    .byte 0x28, 0xFE        /* fpldnlsn */
    .byte 0x29, 0xFB        /* fplg */
    .byte 0x71              /* ldl 1  (A=io, B=bool) */
    .byte 0xE2              /* stnl 2 */
    .byte 0x22, 0xF0        /* ret */

.global t9_fpabs_sqrt_sn
t9_fpabs_sqrt_sn:
    /* io: x@0 -> abs@4, sqrt(abs)@8 */
    .byte 0x71              /* ldl 1 */
    .byte 0x28, 0xFE        /* fpldnlsn */
    .byte 0x2D, 0xFB        /* fpabs */
    .byte 0x71, 0x51        /* ldl 1; ldnlp 1 */
    .byte 0x28, 0xF8        /* fpstnlsn */
    .byte 0x71, 0x51        /* ldl 1; ldnlp 1 */
    .byte 0x28, 0xFE        /* fpldnlsn */
    .byte 0x2D, 0xF3        /* fpsqrt */
    .byte 0x71, 0x52        /* ldl 1; ldnlp 2 */
    .byte 0x28, 0xF8        /* fpstnlsn */
    .byte 0x22, 0xF0        /* ret */

.global t9_fpadddbsn
t9_fpadddbsn:
    /* io: b double@0, a double@8 -> float@16 */
    .byte 0x71              /* ldl 1 */
    .byte 0x28, 0xFA        /* fpldnldb  (b) */
    .byte 0x71, 0x52        /* ldl 1; ldnlp 2 */
    .byte 0x28, 0xFA        /* fpldnldb  (a) */
    .byte 0x2D, 0xFD        /* fpadddbsn */
    .byte 0x71, 0x54        /* ldl 1; ldnlp 4 */
    .byte 0x28, 0xF8        /* fpstnlsn */
    .byte 0x22, 0xF0        /* ret */

.global t9_fpchki32
t9_fpchki32:
    /* io: x float@0 -> errflag@4  (1 = no error) */
    .byte 0x22, 0xF9        /* testerr (clear stale error; discard result) */
    .byte 0x71              /* ldl 1  (drops testerr result to B) */
    .byte 0x28, 0xFE        /* fpldnlsn */
    .byte 0x2D, 0xFE        /* fpchki32 */
    .byte 0x22, 0xF9        /* testerr (A = no-error flag) */
    .byte 0x71              /* ldl 1 */
    .byte 0xE1              /* stnl 1 */
    .byte 0x22, 0xF0        /* ret */

.global t9_checkops
t9_checkops:
    /* io: v0@0 v1@4 v2@8 v3@12 -> flags@16..28
     * cb(v0), cbu(v1), cs(v2), csu(v3): each stores testerr result
     * (1 = value passed the check, 0 = IntegerError was signalled). */
    .byte 0x22, 0xF9        /* testerr (clear) */
    .byte 0x71, 0x30        /* ldl 1; ldnl 0 */
    .byte 0x2B, 0xFA        /* cb */
    .byte 0x22, 0xF9        /* testerr */
    .byte 0x71              /* ldl 1 */
    .byte 0xE4              /* stnl 4 */
    .byte 0x71, 0x31        /* ldl 1; ldnl 1 */
    .byte 0x2B, 0xFB        /* cbu */
    .byte 0x22, 0xF9        /* testerr */
    .byte 0x71              /* ldl 1 */
    .byte 0xE5              /* stnl 5 */
    .byte 0x71, 0x32        /* ldl 1; ldnl 2 */
    .byte 0x2F, 0xFA        /* cs */
    .byte 0x22, 0xF9        /* testerr */
    .byte 0x71              /* ldl 1 */
    .byte 0xE6              /* stnl 6 */
    .byte 0x71, 0x33        /* ldl 1; ldnl 3 */
    .byte 0x2F, 0xFB        /* csu */
    .byte 0x22, 0xF9        /* testerr */
    .byte 0x71              /* ldl 1 */
    .byte 0xE7              /* stnl 7 */
    .byte 0x22, 0xF0        /* ret */

.global t9_dev_roundtrip
t9_dev_roundtrip:
    /* io: val@0 -> copy@4 (devsw), readback@8 (devlw) */
    .byte 0x71, 0x30        /* ldl 1; ldnl 0   (val) */
    .byte 0x71, 0x51        /* ldl 1; ldnlp 1  (A=&io[1], B=val) */
    .byte 0x2F, 0xF5        /* devsw */
    .byte 0x71, 0x51        /* ldl 1; ldnlp 1 */
    .byte 0x2F, 0xF4        /* devlw */
    .byte 0x71              /* ldl 1 */
    .byte 0xE2              /* stnl 2 */
    .byte 0x22, 0xF0        /* ret */

.global t9_ldprodid
t9_ldprodid:
    /* io: -> id@0 */
    .byte 0x68, 0xFC        /* ldprodid */
    .byte 0x71              /* ldl 1 */
    .byte 0xE0              /* stnl 0 */
    .byte 0x22, 0xF0        /* ret */

.global t9_semaphore
t9_semaphore:
    /* io: sem structure {count@0, front@4, back@8} pre-set to
     * {0, NotProcess.p, NotProcess.p}.  signal -> count=1; wait -> count=0
     * without descheduling.  C side verifies count round-trip. */
    .byte 0x71              /* ldl 1 */
    .byte 0x60, 0xF4        /* signal */
    .byte 0x71              /* ldl 1 */
    .byte 0x60, 0xF5        /* wait  (count=1 -> decrement, keep running) */
    .byte 0x22, 0xF0        /* ret */

.global t9_slice_ops
t9_slice_ops:
    /* io: -> prev-enabled@0 (expect 1); leaves timeslicing enabled.
     * Also exercises intdis/intenb, timeslice, nop, icl, fdca. */
    .byte 0x40              /* ldc 0 */
    .byte 0x2B, 0xF0        /* settimeslice (disable; A = prev = 1) */
    .byte 0x71              /* ldl 1 */
    .byte 0xE0              /* stnl 0 */
    .byte 0x41              /* ldc 1 */
    .byte 0x2B, 0xF0        /* settimeslice (re-enable) */
    .byte 0x2C, 0xF4        /* intdis */
    .byte 0x2C, 0xF5        /* intenb */
    .byte 0x63, 0xF0        /* nop */
    .byte 0x62, 0xF3        /* icl */
    .byte 0x40              /* ldc 0 */
    .byte 0x62, 0xF0        /* fdca */
    .byte 0x60, 0xF3        /* timeslice (reschedule sole process) */
    .byte 0x22, 0xF0        /* ret */

.global t9_fpstate
t9_fpstate:
    /* io: buf@0..24 (7 words), fin@28 (float), din@32 (double)
     *  -> dout@40 (double), fout@48 (float)
     * Loads FA=din(db), FB=fin(sn); fpstall; clobbers FPU; fpldall;
     * stores the restored registers. */
    .byte 0x71, 0x57        /* ldl 1; ldnlp 7 */
    .byte 0x28, 0xFE        /* fpldnlsn  (fin) */
    .byte 0x71, 0x58        /* ldl 1; ldnlp 8 */
    .byte 0x28, 0xFA        /* fpldnldb  (din; FB=fin) */
    .byte 0x71              /* ldl 1  (buf) */
    .byte 0x60, 0xFF        /* fpstall */
    .byte 0x29, 0xFF        /* fpldzerosn (clobber FPU stack) */
    .byte 0x71              /* ldl 1 */
    .byte 0x60, 0xFE        /* fpldall */
    .byte 0x71, 0x5A        /* ldl 1; ldnlp 10 */
    .byte 0x28, 0xF4        /* fpstnldb  (restored FA = din) */
    .byte 0x71, 0x5C        /* ldl 1; ldnlp 12 */
    .byte 0x28, 0xF8        /* fpstnlsn  (restored FB = fin) */
    .byte 0x22, 0xF0        /* ret */

.global t9_stmove2dinit
t9_stmove2dinit:
    /* io: -> count@0, deltaD@4, deltaS@8   (5, 9, 7) */
    .byte 0x47              /* ldc 7  (source stride, ends in C) */
    .byte 0x49              /* ldc 9  (dest stride, ends in B) */
    .byte 0x45              /* ldc 5  (length, ends in A) */
    .byte 0x25, 0xFB        /* move2dinit */
    .byte 0x71              /* ldl 1 */
    .byte 0x61, 0xF0        /* stmove2dinit */
    .byte 0x22, 0xF0        /* ret */

.global t9_swapqueue
t9_swapqueue:
    /* io: -> oldfront@0.  Swap in an empty low-priority queue; the sole
     * running process is not on any queue, so the old front must be
     * NotProcess.p (MostNeg) and the swap is harmless. */
    .byte 0x24, 0xF2        /* mint  (back = NotProcess.p) */
    .byte 0x24, 0xF2        /* mint  (front = NotProcess.p) */
    .byte 0x41              /* ldc 1 (priority = low) */
    .byte 0x60, 0xF0        /* swapqueue */
    .byte 0x71              /* ldl 1  (A=io, B=oldfront) */
    .byte 0xE0              /* stnl 0 */
    .byte 0x22, 0xF0        /* ret */

.global t9_flags
t9_flags:
    /* io: -> flags-after-clear@0.  stflags(0) then ldflags: expect 0. */
    .byte 0x40              /* ldc 0 */
    .byte 0x2B, 0xF7        /* stflags */
    .byte 0x2B, 0xF6        /* ldflags */
    .byte 0x71              /* ldl 1 */
    .byte 0xE0              /* stnl 0 */
    .byte 0x22, 0xF0        /* ret */
