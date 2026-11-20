#!/usr/bin/env bash
# Build the opt-in static libpthread.a for protected T9000 bFLT programs.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
tcc=${TCC:-$root/transputer-tcc}
libc=${UCLIBC:-$root/transputer/uClibc-ng}
out=${1:?usage: build.sh OUTPUT_DIRECTORY}
mkdir -p "$out"

flags=(-mt9000 -nostdinc -I"$root/include/transputer" -I"$root/include"
       -I"$libc/include" -I"$root/transputer/linux-7-headers/include" -D__TPOS_PTHREAD__)
"$tcc" "${flags[@]}" -c "$here/pthread.c" -o "$out/pthread.o"
"$tcc" "${flags[@]}" -c "$here/clone.S" -o "$out/clone.o"
"$tcc" -ar crs "$out/libpthread.a" "$out/pthread.o" "$out/clone.o"
