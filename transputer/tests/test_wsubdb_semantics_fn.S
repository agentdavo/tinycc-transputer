.section .text,"ax"

.global wsubdb_probe_idx_base
wsubdb_probe_idx_base:
    /* Sequence: ldc idx; ldc base; wsubdb; ret
     * Before WSUBDB: AReg=base(100), BReg=idx(3)
     */
    .byte 0x43              /* ldc 3 */
    .byte 0x26, 0x44        /* ldc 100 */
    .byte 0x20, 0x28, 0xF1  /* opr wsubdb (0x81) */
    .byte 0x22, 0xF0        /* opr ret (0x20) */

.global wsubdb_probe_base_idx
wsubdb_probe_base_idx:
    /* Sequence: ldc base; ldc idx; wsubdb; ret
     * Before WSUBDB: AReg=idx(3), BReg=base(100)
     */
    .byte 0x26, 0x44        /* ldc 100 */
    .byte 0x43              /* ldc 3 */
    .byte 0x20, 0x28, 0xF1  /* opr wsubdb (0x81) */
    .byte 0x22, 0xF0        /* opr ret (0x20) */
