/*
 * test_boot_main.S - Silent bootstrap for c-testsuite
 * Calls _start_c() which calls main() and handles the return value.
 * NO UART output from boot stub — only test program output appears.
 *
 * _start_c is defined in test_runtime.c
 */

.section .text.start,"ax"
.global _start

_start:
    /*
     * === Phase 1: Relocate workspace above code ===
     * After BTL load: IPtr = WPtr = MemStart (0x80000070)
     * AJW +0x4000 words = +64 KiB above boot address
     */
    .byte 0x24, 0x20, 0x20, 0x20, 0xB0  /* ajw 0x4000 */

    /*
     * === Phase 2: Initialize processor state ===
     */
    /* testerr */
    .byte 0x22, 0xF9
    /* clrhalterr */
    .byte 0x25, 0xF7
    /* mint; sthf */
    .byte 0x24, 0xF2
    .byte 0x21, 0xF8
    /* mint; stlf */
    .byte 0x24, 0xF2
    .byte 0x21, 0xFC
    /* ldc 0; sttimer */
    .byte 0x40
    .byte 0x25, 0xF4

    /*
     * === Phase 3: Build CALL frame and call _start_c ===
     * AJW -4: allocate 4 workspace slots for CALL frame
     */
    .byte 0x60                    /* nfix 0 */
    .byte 0xBC                    /* ajw C: operand = -4 */

    /* STL 3: CReg slot = 0 */
    .byte 0x40
    .byte 0xD3
    /* STL 2: BReg slot = 0 */
    .byte 0x40
    .byte 0xD2
    /* STL 1: AReg/param0 = 0 */
    .byte 0x40
    .byte 0xD1

    /* Load halt address and store as return address at WPtr[0] */
.global __return_label_addr
__return_label_addr:
    .byte 0x20, 0x20, 0x20, 0x20
    .byte 0x20, 0x20, 0x20
    .byte 0x40
    .byte 0xD0                    /* stl 0 */

    /* Load _start_c address */
.global __start_c_load_point
__start_c_load_point:
    .byte 0x20, 0x20, 0x20, 0x20
    .byte 0x20, 0x20, 0x20
    .byte 0x40

    /* GCALL: jump to _start_c */
    .byte 0xF6

halt:
.global halt
    /* stopp: halt processor */
    .byte 0x21, 0xF5
