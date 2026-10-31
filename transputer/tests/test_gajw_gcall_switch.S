/*
 * Kernel-style GAJW/GCALL switch smoke test.
 *
 * Expected: SKF
 */

.section .text.start,"ax"
.global _start

_start:
    /* Move workspace above code/data. */
    .byte 0x24, 0x20, 0x20, 0x20, 0xB0

    /* Clear processor error state and initialize queues/timer. */
    .byte 0x22, 0xF9
    .byte 0x25, 0xF7
    .byte 0x24, 0xF2
    .byte 0x21, 0xF8
    .byte 0x24, 0xF2
    .byte 0x21, 0xFC
    .byte 0x40
    .byte 0x25, 0xF4

    /* UART <- 'S' */
    .byte 0x21, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x40
    .byte 0x25, 0x43
    .byte 0xF0
    .byte 0xE0

    /* Allocate 8 words:
     *   Wptr[0..3]   synthetic call frame for the target workspace
     *   Wptr[4..5]   pseudo thread_struct { wptr, iptr }
     *   Wptr[6..7]   spare
     */
    .byte 0x60
    .byte 0xB8              /* ajw -8 */

    /* target_frame[0] = fail_return */
    .byte 0x22, 0x4E        /* ldc fail_return-(next inst after ldpi) = 0x2e */
    .byte 0x21, 0xFB        /* ldpi */
    .byte 0x14              /* ldlp 4 = &target_frame[0] */
    .byte 0xF0              /* rev */
    .byte 0xE0              /* stnl 0 */

    /* target_frame[1..3] = 0 */
    .byte 0x40              /* ldc 0 */
    .byte 0x15              /* ldlp 5 = &target_frame[1] */
    .byte 0xF0              /* rev */
    .byte 0xE0              /* stnl 0 */
    .byte 0x40
    .byte 0x16              /* ldlp 6 = &target_frame[2] */
    .byte 0xF0
    .byte 0xE0
    .byte 0x40
    .byte 0x17              /* ldlp 7 = &target_frame[3] */
    .byte 0xF0
    .byte 0xE0

    /* thread.wptr = &target_frame[0] */
    .byte 0x14              /* ldlp 4 */
    .byte 0x10              /* ldlp 0 = &thread.wptr */
    .byte 0xF0
    .byte 0xE0

    /* thread.iptr = target_entry */
    .byte 0x22, 0x45        /* ldc target_entry-(next inst after ldpi) = 0x25 */
    .byte 0x21, 0xFB        /* ldpi */
    .byte 0x11              /* ldlp 1 = &thread.iptr */
    .byte 0xF0
    .byte 0xE0

    /* UART <- 'K' before the switch sequence. */
    .byte 0x21, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x40
    .byte 0x24, 0x4B
    .byte 0xF0
    .byte 0xE0

    /* Kernel-style restore:
     *   A = thread.iptr
     *   B = thread.wptr
     *   gajw; rev; gcall
     */
    .byte 0x10              /* ldlp 0 = &thread.wptr */
    .byte 0x31              /* ldnl 1 = thread.iptr */
    .byte 0x10              /* ldlp 0 = &thread.wptr */
    .byte 0x30              /* ldnl 0 = thread.wptr */
    .byte 0x23, 0xFC        /* gajw */
    .byte 0xF0              /* rev */
    .byte 0xF6              /* gcall */

fail_return:
    /* If target_entry returns unexpectedly, emit 'R' and halt. */
    .byte 0x21, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x40
    .byte 0x25, 0x42
    .byte 0xF0
    .byte 0xE0
    .byte 0x21, 0xF5

target_entry:
    /* Reaching here proves the kernel restore sequence is viable. */
    .byte 0x21, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x40
    .byte 0x24, 0x46
    .byte 0xF0
    .byte 0xE0
    .byte 0x21, 0xF5
