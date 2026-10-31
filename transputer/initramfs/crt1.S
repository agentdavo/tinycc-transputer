/*
 * crt1.S — Userspace entry for transputer bFLT.
 *
 * The kernel enters the image via GAJW/GCALL. GCALL only swaps IPtr/AReg; it
 * does not build a CALL frame.  Linux's flat loader enters with the standard
 * stack words [argc, argv, envp, ...].  Create a synthetic CALL frame with
 * ajw -4, save the GCALL return address at WPtr[0], and copy those three
 * words into slots 1..3 before calling _start_c(argc, argv, envp).
 */

.section .text.start,"ax"
.global _start

_start:
    /* Build the missing CALL frame required for a GCALL-entered routine. */
    ajw -4
    stl 0
    ldl 4
    stl 1
    ldl 5
    stl 2
    ldl 6
    stl 3

    /*
     * UART MMIO 0x10000000: ldc addr; ldc ch; rev; stnl 0.
     * ldc 0x10000000 = pfix 1 + 6×pfix 0 + ldc 0  (21 20…40).
     * Character ldc MUST be a 2-byte pfix/ldc, never the last byte of
     * the 9-byte address slot: 0x53 is ldnlp 3 and 0x43 is ldc 3, which
     * turned '<S>' / '<C>' into ldnlp 0x10000003; rev; stnl 0 — a wild
     * store (ISM stnl writes B at A+n).  dast of the old image:
     *   ldc 0x10000000; ldc 0x5c; rev; stnl 0; ldnlp 0x10000003; …
     */
    /* '<' 0x3c = pfix 3, ldc 12 */
    .byte 0x21, 0x20, 0x20, 0x20
    .byte 0x20, 0x20, 0x20, 0x40
    .byte 0x23, 0x4c
    .byte 0x20, 0xF0
    .byte 0xE0
    /* 'S' 0x53 = pfix 5, ldc 3 */
    .byte 0x21, 0x20, 0x20, 0x20
    .byte 0x20, 0x20, 0x20, 0x40
    .byte 0x25, 0x43
    .byte 0x20, 0xF0
    .byte 0xE0
    /* '>' 0x3e = pfix 3, ldc 14 */
    .byte 0x21, 0x20, 0x20, 0x20
    .byte 0x20, 0x20, 0x20, 0x40
    .byte 0x23, 0x4e
    .byte 0x20, 0xF0
    .byte 0xE0

    /*
     * UART <- '<C>' BEFORE loading args onto the eval stack.
     * MUST NOT sit between ldl and call — STNL consumes A/B.
     */
    .byte 0x21, 0x20, 0x20, 0x20
    .byte 0x20, 0x20, 0x20, 0x40
    .byte 0x23, 0x4c
    .byte 0x20, 0xF0
    .byte 0xE0
    /* 'C' 0x43 = pfix 4, ldc 3 */
    .byte 0x21, 0x20, 0x20, 0x20
    .byte 0x20, 0x20, 0x20, 0x40
    .byte 0x24, 0x43
    .byte 0x20, 0xF0
    .byte 0xE0
    .byte 0x21, 0x20, 0x20, 0x20
    .byte 0x20, 0x20, 0x20, 0x40
    .byte 0x23, 0x4e
    .byte 0x20, 0xF0
    .byte 0xE0

    ldl 3
    ldl 2
    ldl 1

    call _start_c

.global _halt
_halt:
    .byte 0x21, 0x20, 0x20, 0x20
    .byte 0x20, 0x20, 0x20, 0x40
    .byte 0x24, 0x48
    .byte 0x20, 0xF0
    .byte 0xE0
    .byte 0x21, 0xF5
