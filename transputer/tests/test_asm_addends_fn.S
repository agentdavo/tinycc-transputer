/* Entry and destination symbols intentionally differ from the branch targets. */
.text
.global asm_branches
asm_branches:
    ldc 0
    cj after_cj-1
    ldc 0
    ret
    .byte 0x01        /* j +1, over the ret */
after_cj:
    ret
    j before_call+1
before_call:
    ret
    call callee+1
    ret
callee:
    ret
    ldc 73
    ret
