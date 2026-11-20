#!/bin/bash
# Build the secondary stub from the same early boot/atomic code as Linux.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:?usage: build-t4-secondary.sh OUTPUT_DIRECTORY}
mkdir -p "$out"
tcc=${TCC:-$root/transputer-tcc}
netload=${NETLOAD:-$root/transputer-netload}
arch=$root/transputer/linux-7/arch/transputer
"$tcc" -mt9000 -static -nostdlib -I"$arch/include" -I"$root/include" \
 -o "$out/secondary.elf" "$root/transputer/tests/t4_smp_secondary.S" \
 "$root/transputer/tests/t4_smp_secondary.c" "$arch/kernel/t4_smp_boot.c" \
 "$arch/kernel/t4_smp_entry.S"
"$netload" "$out/secondary.elf" "$out/secondary.btl"
