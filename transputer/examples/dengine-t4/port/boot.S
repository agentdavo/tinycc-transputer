/*
 * boot.S - Bootstrap for the dEngine T4 port (from tests/test_boot.S)
 * Sets up workspace and calls test_main properly
 *
 * Transputer prefix encoding reminder:
 *   Fetch: Idata = byte & 0xF; OReg += Idata
 *   PFIX:  OReg <<= 4
 *   NFIX:  OReg = (~OReg) << 4
 *   OP n:  operand = OReg (already includes n); OReg = 0
 *
 * Key negative encoding:
 *   -1: NFIX 0 (0x60), OP F = operand 0xFFFFFFFF
 *   -4: NFIX 0 (0x60), OP C = operand 0xFFFFFFFC
 *
 * Proper transputer initialization (from Inmos boot protocol):
 *   1. MINT + GAJW to relocate workspace to top of on-chip RAM
 *   2. TESTERR, CLRHALTERR to clear error flags
 *   3. STHF, STLF to initialize process queue heads
 *   4. STTIMER to start the timer (REQUIRED for RET to work!)
 */

.section .text.start,"ax"
.global _start

_start:
    /*
     * === Phase 1: Workspace near the top of a 64 MiB node ===
     * t4 enters a BTL with WPtr at the end of the loaded image, not at
     * MemStart, so a relative AJW lands wherever the image ends: for this
     * multi-megabyte image tests/test_boot.S's AJW put the stack past the
     * end of RAM. Set it absolutely, 1 MiB below the top of RAM (t4 -sm 26:
     * 0x80000070-0x8400006F): mint; ldc #03F00070; sum; gajw.
     */
    .byte 0x24, 0xF2                                /* mint */
    .byte 0x23, 0x2F, 0x20, 0x20, 0x20, 0x27, 0x40  /* ldc #03F00070 */
    .byte 0x25, 0xF2                                /* sum */
    .byte 0x23, 0xFC                                /* gajw: WPtr = #83F00070 */

    /*
     * === Phase 2: Initialize processor state ===
     * The transputer REQUIRES these before function calls work properly.
     */

    /* testerr: test and clear error flag */
    .byte 0x22, 0xF9              /* pfix 2, opr 9 = opr 0x29 = testerr */

    /* clrhalterr: clear halt-on-error flag */
    .byte 0x25, 0xF7              /* pfix 5, opr 7 = opr 0x57 = clrhalterr */

#ifndef __SOFTFP__
    /* fptesterr: test and clear FPU error flag (REQUIRED for float ops).
       -mt425 targets the FPU-less T425, which has no such instruction. */
    .byte 0x29, 0xFC              /* pfix 9, opr C = opr 0x9C = fptesterr */
#endif

    /* mint: load MinInt (0x80000000) for queue init */
    .byte 0x24, 0xF2              /* pfix 4, opr 2 = opr 0x42 = mint */
    /* sthf: set high-priority front pointer */
    .byte 0x21, 0xF8              /* pfix 1, opr 8 = opr 0x18 = sthf */

    /* mint */
    .byte 0x24, 0xF2              /* mint */
    /* stlf: set low-priority front pointer */
    .byte 0x21, 0xFC              /* pfix 1, opr C = opr 0x1C = stlf */

    /* ldc 0; sttimer: start the timer at 0 */
    .byte 0x40                    /* ldc 0 */
    .byte 0x25, 0xF4              /* pfix 5, opr 4 = opr 0x54 = sttimer */

    /*
     * === Phase 3: Print 'S' marker to UART ===
     */
    .byte 0x21, 0x20, 0x20, 0x20  /* pfix 1, pfix 0, pfix 0, pfix 0 */
    .byte 0x20, 0x20, 0x20, 0x40  /* pfix 0, pfix 0, pfix 0, ldc 0 = ldc 0x10000000 */
    .byte 0x25, 0x43              /* pfix 5, ldc 3 = ldc 0x53 = 'S' */
    .byte 0x20, 0xF0              /* pfix 0, opr 0 = rev */
    .byte 0xE0                    /* stnl 0 */

    /*
     * === Phase 4: Build CALL frame and jump to test_main ===
     *
     * CALL saves to WPtr[-4..-1]: IPtr, AReg, BReg, CReg
     * Then WPtr -= 16. From callee: WPtr[0]=retaddr, [1..3]=params
     *
     * We do: AJW -4, then STL 0..3 to fill frame, then GCALL.
     */

    /* AJW -4: WPtr -= 16 bytes */
    .byte 0x60                    /* nfix 0 */
    .byte 0xBC                    /* ajw C: operand = -4 */

    /* STL 3: WPtr[3] = 0 (CReg slot) */
    .byte 0x40                    /* ldc 0 */
    .byte 0xD3                    /* stl 3 */

    /* STL 2: WPtr[2] = 0 (BReg slot) */
    .byte 0x40                    /* ldc 0 */
    .byte 0xD2                    /* stl 2 */

    /* STL 1: WPtr[1] = 0 (AReg/param0 slot) */
    .byte 0x40                    /* ldc 0 */
    .byte 0xD1                    /* stl 1 */

    /* Load halt address and store as return address at WPtr[0] */
.global __return_label_addr
__return_label_addr:
    .byte 0x20, 0x20, 0x20, 0x20  /* Linker patches with halt address */
    .byte 0x20, 0x20, 0x20
    .byte 0x40
    .byte 0xD0                    /* stl 0 */

    /* Load test_main address */
.global __test_main_load_point
__test_main_load_point:
    .byte 0x20, 0x20, 0x20, 0x20  /* Linker patches with test_main address */
    .byte 0x20, 0x20, 0x20
    .byte 0x40

    /* GCALL: IPtr = AReg (test_main), AReg = IPtr+1 */
    .byte 0xF6                    /* opr 6 = gcall */

    /* If test_main returns via RET, it goes to halt (stored at WPtr[0]) */
    /* If it returns via GCALL's AReg, we fall through here */

halt:
.global halt
    /* Print 'H' for clean halt */
    .byte 0x21, 0x20, 0x20, 0x20
    .byte 0x20, 0x20, 0x20, 0x40  /* ldc 0x10000000 */
    .byte 0x24, 0x48              /* ldc 0x48 = 'H' */
    .byte 0x20, 0xF0              /* rev */
    .byte 0xE0                    /* stnl 0 */
    /* stopp: halt processor */
    .byte 0x21, 0xF5              /* pfix 1, opr 5 = opr 0x15 = stopp */
