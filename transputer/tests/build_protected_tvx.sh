#!/bin/bash
# Add the protected TVX regression to a static-pthread/event-time init image.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
: "${TCC:?}" "${ELF2FLT:?}" "${UCLIBC:?}" "${LIBTCC1:?}" "${PTHREAD_TEST_DIR:?}" "${OUT:?}"
mkdir -p "$OUT"
cd "$root"
flags=(-mt9000 -nostdinc -I"$root/include/transputer" -I"$root/include"
       -I"$UCLIBC/include" -I"$UCLIBC/libc/sysdeps/linux/transputer"
       -I"$UCLIBC/libc/sysdeps/linux/common"
       -I"$root/transputer/linux-7-headers/include" -I"$root/transputer/linux-7/include")
export T4_STRICT_UNRESOLVED_CALLS=1
"$TCC" "${flags[@]}" -DTP_TEST_TVX -D__TPOS_PTHREAD__ -Itransputer/pthread \
    -c transputer/tests/protected_event_time.c -o "$OUT/event_time.o"
"$TCC" "${flags[@]}" -D__TPOS_PTHREAD__ -Itransputer/pthread \
    -c transputer/tests/protected_tvx.c -o "$OUT/tvx.o"
"$TCC" "${flags[@]}" -DTVX_BACKEND=tvx_software \
    -c transputer/tests/protected_tvx_ops.c -o "$OUT/tvx-soft.o"
"$TCC" "${flags[@]}" -mt9000tvx -DTVX_BACKEND=tvx_native \
    -c transputer/tests/protected_tvx_ops.c -o "$OUT/tvx-native.o"
"$TCC" -mt9000 -nostdinc -Ilib/transputer/tvx/include \
    -c lib/transputer/transputer-tvx.c -o "$OUT/tvx-runtime.o"
# The bundle supplies the tested startup, syscall bridge, libc stress test,
# and static_pthread main with EVENT_TIME and DESKTOP_INTEGRATION enabled.
"$TCC" -nostdlib -nostdinc -o "$OUT/probe.elf" \
    "$PTHREAD_TEST_DIR/"{crt1,crt_bridge,probe,syscall_stub,libc_stress}.o \
    "$OUT/event_time.o" "$OUT/"tvx{,-soft,-native,-runtime}.o \
    "$PTHREAD_TEST_DIR/libpthread/libpthread.a" "$UCLIBC/lib/libc.a" "$LIBTCC1"
"$ELF2FLT" "$OUT/probe.elf" "$OUT/init"
