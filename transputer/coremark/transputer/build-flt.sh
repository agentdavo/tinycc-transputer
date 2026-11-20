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
# Set COREMARK_SMP_TEST=1 COREMARK_THREADS=2 (or 6) to validate CPU affinity
# and per-thread POSIX timer delivery during the workload. This diagnostic
# mode adds overhead: require both CoreMark validation and COREMARK SMP PASS.
# OUT selects the object directory; FLT selects a distinct output image.
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
COREMARK_THREADS="${COREMARK_THREADS:-1}"
COREMARK_SMP_TEST="${COREMARK_SMP_TEST:-0}"
FLT="${FLT:-$INITRAMFS_DIR/coremark.flt}"
if [ "$COREMARK_SMP_TEST" = 1 ]; then
    case "$COREMARK_THREADS" in 2|3|4|5|6) ;; *)
        echo "COREMARK_SMP_TEST requires 2..6 threads" >&2; exit 1;; esac
elif [ "$COREMARK_SMP_TEST" != 0 ]; then
    echo "COREMARK_SMP_TEST must be 0 or 1" >&2; exit 1
fi
if [ "$COREMARK_THREADS" -gt 1 ]; then
    ITERATIONS="${ITERATIONS:-500}"
else
    ITERATIONS="${ITERATIONS:-200}"
fi
mkdir -p "$OUT" "$(dirname "$FLT")"

CFLAGS="-mt9000 -w -nostdlib -nostdinc -I$PORT -I$CM -I$TOP/include -I$UCLIBC/include"
DEFS="-DPERFORMANCE_RUN=1 -DITERATIONS=$ITERATIONS -DFLAGS_STR=\"\\\"transputer-tcc\\\"\""
DEFS="$DEFS -DCOREMARK_SMP_TEST=$COREMARK_SMP_TEST"
if [ "$COREMARK_SMP_TEST" = 1 ]; then
    CFLAGS="$CFLAGS -I$TOP/transputer/linux-7-headers/include -I$TOP/transputer/linux-7/include"
fi
thread_lib=()
if [ "$COREMARK_THREADS" -gt 1 ]; then
    : "${TP_PTHREAD_LIB:?set TP_PTHREAD_LIB to the TransputerOS static libpthread.a}"
    CFLAGS="$CFLAGS -I$TOP/transputer/pthread -D__TPOS_PTHREAD__"
    DEFS="$DEFS -DMULTITHREAD=$COREMARK_THREADS -DUSE_PTHREAD=1"
    thread_lib=("$TP_PTHREAD_LIB")
    TP_USE_T9K_SYSCALL=1
fi

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
"$TCC" -nostdlib -nostdinc "-DTP_USE_T9K_SYSCALL=${TP_USE_T9K_SYSCALL:-0}" \
    -c -o "$OUT/syscall_stub.o" "$TOP/transputer/tests/syscall_stub.c"

echo "=== link ==="
"$TCC" -nostdlib -nostdinc -o "$OUT/coremark.elf" \
    "$OUT/crt1.o" "$OUT/crt_bridge.o" \
    "$OUT/core_main.o" "$OUT/core_list_join.o" "$OUT/core_matrix.o" \
    "$OUT/core_state.o" "$OUT/core_util.o" \
    "$OUT/core_portme.o" "$OUT/ee_printf.o" "$OUT/cvt.o" "$OUT/math_stub.o" \
    "$OUT/transputer_ldtimer.o" \
    "$OUT/syscall_stub.o" "${thread_lib[@]}" "$UCLIBC/lib/libc.a"

echo "=== elf2flt ==="
"$ELF2FLT" "$OUT/coremark.elf" "$FLT"
ls -lh "$FLT"
echo "✓ coremark.flt built (rebuild kernel to stage it)"
