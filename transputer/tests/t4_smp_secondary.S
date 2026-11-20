/* Secondary bootstrap: private code, shared per-CPU workspace. */
.section .text.start,"ax"
.global _start

_start:
    /* Board port id selects a disjoint 16 KiB shared workspace.
     * top = 0x40010000 + id*0x4000 - 64; IDs 1..3 only. */
    .byte 0x24, 0x21, 0x20, 0x20, 0x20, 0x20, 0x21, 0x48
    .byte 0x30                  /* ldnl 0: board port */
    .byte 0x4e, 0x24, 0xf1      /* ldc 14; shl */
    .byte 0x24, 0x20, 0x20, 0x20, 0x2f, 0x2f, 0x2c, 0x40
    .byte 0x25, 0xf2            /* sum */
    .byte 0x23, 0xfc            /* gajw */

    /*
     * === Phase 2: Initialize processor state ===
     * The transputer REQUIRES these before function calls work properly.
     */

    /* testerr: test and clear error flag */
    .byte 0x22, 0xF9              /* pfix 2, opr 9 = opr 0x29 = testerr */

    /* clrhalterr: clear halt-on-error flag */
    .byte 0x25, 0xF7              /* pfix 5, opr 7 = opr 0x57 = clrhalterr */

    /* fptesterr: test and clear FPU error flag (REQUIRED for float ops) */
    .byte 0x29, 0xFC              /* pfix 9, opr C = opr 0x9C = fptesterr */

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
    .byte 0x21, 0xf5 /* stopp */
