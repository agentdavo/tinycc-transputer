#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../../.." && pwd)
tp="$root/transputer"
tcc=${TCC:-$root/transputer-tcc}
elf2flt=${ELF2FLT:-$root/transputer-elf2flt}
libc=${UCLIBC:-$tp/uClibc-ng}
runtime=${LIBTCC1:-$root/transputer-libtcc1.a}
output=${1:-$here/night-relay.flt}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

if [ ! -x "$elf2flt" ]; then
    elf2flt="$work/transputer-elf2flt"
    cc -O2 "$tp/tools/transputer-elf2flt.c" -o "$elf2flt"
fi

includes=(-nostdinc "-I$root/include/transputer" "-I$root/include"
    "-I$libc/include" "-I$libc/libc/sysdeps/linux/transputer"
    "-I$libc/libc/sysdeps/linux/common" "-I$tp/linux-7-headers/include")
flags=(-mt9000 -w -D__GNUC__=4 -D__GNUC_MINOR__=4 -D__GNUC_PATCHLEVEL__=0)
if [ "${SPINE:-0}" = 1 ]; then
    flags+=(-DTPOS_SPINE_CHIP)
fi
for module in relay cabinet main; do
    "$tcc" "${flags[@]}" "${includes[@]}" -c "$here/$module.c" -o "$work/$module.o"
done
"$tcc" -nostdlib -nostdinc -c "$tp/initramfs/crt1.S" -o "$work/crt1.o"
"$tcc" -nostdlib "${includes[@]}" -c "$tp/initramfs/crt_bridge.c" -o "$work/crt_bridge.o"
"$tcc" -nostdlib "${includes[@]}" "-DTP_USE_T9K_SYSCALL=${TP_USE_T9K_SYSCALL:-0}" \
    -c "$tp/tests/syscall_stub.c" -o "$work/syscall.o"
T4_STRICT_UNRESOLVED_CALLS=1 "$tcc" -nostdlib -nostdinc -o "$work/night-relay.elf" \
    "$work/crt1.o" "$work/crt_bridge.o" "$work/main.o" "$work/relay.o" \
    "$work/cabinet.o" "$work/syscall.o" "$libc/lib/libc.a" "$runtime"
"$elf2flt" "$work/night-relay.elf" "$output"
