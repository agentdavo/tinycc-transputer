/*
 * transputer_ldtimer — read T800 low-priority timer.
 * LDTIMER = PFIX 2 (0x22), OPR 2 (0xF2)
 * Returns timer value in AReg.
 */
.global transputer_ldtimer
transputer_ldtimer:
    .byte 0x22, 0xF2        /* ldtimer */
    .byte 0x22, 0xF0        /* ret */
