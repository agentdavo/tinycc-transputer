/* Raw-encoded probes for pop (T425/T9000) and the register test
 * instructions testlds/teststs/testhardchan (INMOS register-test appendix).
 *
 * Every probe takes one pointer argument (arriving in workspace slot 1 after
 * CALL) and communicates through the pointed-to word array.
 */
.section .text,"ax"

.global rt_pop
rt_pop:
    /* io -> io[0] = A after pop, io[1] = B after pop */
    .byte 0x41              /* ldc 1 */
    .byte 0x42              /* ldc 2 */
    .byte 0x43              /* ldc 3   (A=3 B=2 C=1) */
    .byte 0x27, 0xF9        /* pop     (A=2 B=1) */
    .byte 0x71, 0xE0        /* ldl 1; stnl 0 */
    .byte 0x71, 0xE1        /* ldl 1; stnl 1 */
    .byte 0x22, 0xF0        /* ret */

.global rt_status
rt_status:
    /* io -> io[0] = StatusReg on entry,
     *       io[1] = testerr after teststs(S|Error),
     *       io[2] = StatusReg after that testerr */
    .byte 0x22, 0xF3        /* testlds */
    .byte 0x71, 0xE0        /* ldl 1; stnl 0 */
    .byte 0x71, 0x30        /* ldl 1; ldnl 0 */
    .byte 0x24, 0xF2        /* mint */
    .byte 0x24, 0xFB        /* or      (A = S | ErrorFlag) */
    .byte 0x22, 0xF6        /* teststs */
    .byte 0x22, 0xF9        /* testerr (false: Error set; clears it) */
    .byte 0x71, 0xE1        /* ldl 1; stnl 1 */
    .byte 0x22, 0xF3        /* testlds */
    .byte 0x71, 0xE2        /* ldl 1; stnl 2 */
    .byte 0x22, 0xF0        /* ret */

.global rt_hardchan
rt_hardchan:
    /* Shift 0x11, 0x22, 0x33 then three zeros through Link3Out's
     * DBuff/Ptr/Count stack; io[0..5] = the six values shifted out,
     * io[6] = CReg as it stood under the first testhardchan (B := C).
     * No process waits on Link3Out, so the link engine ignores Count.
     * resetch afterwards restores the channel's idle state. */
    .byte 0x25, 0x45        /* ldc 0x55 */
    .byte 0x21, 0x41        /* ldc 0x11 */
    .byte 0x24, 0xF2, 0x8C  /* mint; adc 12  (Link3Out) */
    .byte 0x22, 0xFD        /* testhardchan */
    .byte 0x71, 0xE0        /* ldl 1; stnl 0 */
    .byte 0x71, 0xE6        /* ldl 1; stnl 6 */

    .byte 0x22, 0x42        /* ldc 0x22 */
    .byte 0x24, 0xF2, 0x8C  /* mint; adc 12 */
    .byte 0x22, 0xFD        /* testhardchan */
    .byte 0x71, 0xE1        /* ldl 1; stnl 1 */

    .byte 0x23, 0x43        /* ldc 0x33 */
    .byte 0x24, 0xF2, 0x8C  /* mint; adc 12 */
    .byte 0x22, 0xFD        /* testhardchan */
    .byte 0x71, 0xE2        /* ldl 1; stnl 2 */

    .byte 0x40              /* ldc 0 */
    .byte 0x24, 0xF2, 0x8C  /* mint; adc 12 */
    .byte 0x22, 0xFD        /* testhardchan */
    .byte 0x71, 0xE3        /* ldl 1; stnl 3 */

    .byte 0x40              /* ldc 0 */
    .byte 0x24, 0xF2, 0x8C  /* mint; adc 12 */
    .byte 0x22, 0xFD        /* testhardchan */
    .byte 0x71, 0xE4        /* ldl 1; stnl 4 */

    .byte 0x40              /* ldc 0 */
    .byte 0x24, 0xF2, 0x8C  /* mint; adc 12 */
    .byte 0x22, 0xFD        /* testhardchan */
    .byte 0x71, 0xE5        /* ldl 1; stnl 5 */

    .byte 0x24, 0xF2, 0x8C  /* mint; adc 12 */
    .byte 0x21, 0xF2        /* resetch */
    .byte 0x22, 0xF0        /* ret */
