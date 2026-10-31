#!/bin/bash
# Build CoreMark as a spawnable bFLT (/usr/bin/coremark) for TransputerOS.
#
# This is the SPAWN variant: it links against the uClibc-ng crt1/crt_bridge +
# syscall stub (like busybox/tpsh) so init can tp_spawn it, and elf2flt converts
# it to bFLT. (The sibling build.sh builds the bare-metal netload variant that
# runs via test_boot.S — a different entry path.)
#
# Output: transputer/initramfs/coremark.flt (staged to /usr/bin/coremark by
# transputer/rootfs/stage.sh). Must be rebuilt with the fixed elf2flt
# (variable-length LDC_ABS relocations) or ~32 relocs are silently dropped.
set -e

TOP="$(cd "$(dirname "$0")/../../.." && pwd)"
# TCC, ELF2FLT, UCLIBC and INITRAMFS_DIR may name another tree's - e.g. the
# one the OS image is built in - so the program links against the same libc.
TCC="${TCC:-$TOP/transputer-tcc}"
ELF2FLT="${ELF2FLT:-$TOP/transputer-elf2flt}"
UCLIBC="${UCLIBC:-$TOP/transputer/uClibc-ng}"
CM="$TOP/transputer/coremark"
PORT="$CM/transputer"
INITRAMFS_DIR="${INITRAMFS_DIR:-$TOP/transputer/initramfs}"
OUT="${OUT:-/tmp/cm_spawn}"
# Under TransputerOS on T4 an iteration takes 0.10-0.14 s of host time - 100
# took 13.9 s spawned by init and 9.7 s from tpsh - so 200 is needed for the
# >=10 s CoreMark wants for a valid run, as build.sh's COREMARK_ITERATIONS
# default has; fewer can end "Errors detected" even when every CRC is right.
ITERATIONS="${ITERATIONS:-200}"
mkdir -p "$OUT"

CFLAGS="-mt9000 -w -nostdlib -nostdinc -I$PORT -I$CM -I$TOP/include -I$UCLIBC/include"
DEFS="-DPERFORMANCE_RUN=1 -DITERATIONS=$ITERATIONS -DFLAGS_STR=\"\\\"transputer-tcc\\\"\""

echo "=== compile CoreMark objects (ITERATIONS=$ITERATIONS) ==="
for src in core_main core_list_join core_matrix core_state core_util; do
    eval "$TCC $CFLAGS $DEFS -c -o \"$OUT/$src.o\" \"$CM/$src.c\""
done
for src in core_portme ee_printf cvt math_stub; do
    eval "$TCC $CFLAGS $DEFS -c -o \"$OUT/$src.o\" \"$PORT/$src.c\""
done
"$TCC" $CFLAGS -c -o "$OUT/transputer_ldtimer.o" "$PORT/transputer_ldtimer.S"

echo "=== compile crt/syscall (spawn chain) ==="
"$TCC" -nostdlib -nostdinc -c -o "$OUT/crt1.o" "$INITRAMFS_DIR/crt1.S"
"$TCC" -nostdlib -nostdinc -I"$TOP/include" -I"$UCLIBC/include" \
    -c -o "$OUT/crt_bridge.o" "$INITRAMFS_DIR/crt_bridge.c"
"$TCC" -nostdlib -nostdinc -c -o "$OUT/syscall_stub.o" "$TOP/transputer/tests/syscall_stub.c"

echo "=== link ==="
"$TCC" -nostdlib -nostdinc -o "$OUT/coremark.elf" \
    "$OUT/crt1.o" "$OUT/crt_bridge.o" \
    "$OUT/core_main.o" "$OUT/core_list_join.o" "$OUT/core_matrix.o" \
    "$OUT/core_state.o" "$OUT/core_util.o" \
    "$OUT/core_portme.o" "$OUT/ee_printf.o" "$OUT/cvt.o" "$OUT/math_stub.o" \
    "$OUT/transputer_ldtimer.o" \
    "$OUT/syscall_stub.o" "$UCLIBC/lib/libc.a"

echo "=== elf2flt ==="
"$ELF2FLT" "$OUT/coremark.elf" "$INITRAMFS_DIR/coremark.flt"
ls -lh "$INITRAMFS_DIR/coremark.flt"
echo "✓ coremark.flt built (rebuild kernel to stage it)"
