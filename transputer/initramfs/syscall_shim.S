/*
 * syscall_shim.S — tiny userspace-to-kernel call trampoline probes.
 *
 * This avoids the 7-argument __transputer_syscall() C call ABI while we prove
 * the raw cross-boundary CALL/RET path itself.
 */

.section .text,"ax"

.global __tp_const1_shim
__tp_const1_shim:
    .byte 0x41
    .byte 0x22, 0xF0

.global __tp_getpid_shim
__tp_getpid_shim:
    /* Build a synthetic CALL frame for a far GCALL into the kernel helper. */
    ajw -4
    ldc 0
    stl 3
    ldc 0
    stl 2
    ldc 0
    stl 1
    ldc __transputer_getpid_entry
    gcall

    .byte 0x22, 0xF0

.global __tp_ioctl_shim
__tp_ioctl_shim:
    /*
     * Called as a normal 3-arg C function. After reserving a fresh frame,
     * the original args live at WPtr[5..7]; copy them into the synthetic
     * GCALL-entered callee slots 1..3.
     */
    ajw -4
    stl 0
    ldl 5
    stl 1
    ldl 6
    stl 2
    ldl 7
    stl 3
    ldc __transputer_ioctl_entry
    gcall

    .byte 0x22, 0xF0
