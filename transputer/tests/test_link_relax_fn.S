/* Assembler code between the C files of test_link_relax: the linker must
 * move it as a whole and keep its alignment.  lr_asm_word reads a word
 * table placed after it, by distance (ldc; ldpi) and with ldnl, which
 * needs the table word-aligned. */
.section .text,"ax"

    .align 4
.global lr_asm_word
lr_asm_word:
    .byte 0x45              /* ldc 5: from L1 to the table */
    .byte 0x21, 0xFB        /* ldpi */
    /* L1: */
    .byte 0x30              /* ldnl 0 */
    .byte 0x22, 0xF0        /* ret */
    .byte 0x20, 0x20        /* to the next word */
    .byte 0x78, 0x56, 0x34, 0x12
