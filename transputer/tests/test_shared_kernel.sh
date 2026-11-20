#!/bin/bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
out=${SHARED_KERNEL_TEST_OUT:-$(mktemp -d)}
mkdir -p "$out"
out=$(cd "$out" && pwd)
cd "$out"
tcc=${TCC:-$root/transputer-tcc}
netload=${NETLOAD:-$root/transputer-netload}
t4=${T4_BIN:-$root/transputer/t4/t4}
"$tcc" -nostdlib -static -I"$root/include" -o shared.elf \
    "$root/transputer/linux-7/arch/transputer/kernel/head.S" \
    "$root/transputer/tests/test_shared_kernel.c" \
    "$root/transputer/t4/tests/test_shared_board_move.S" > build.log 2>&1
"$netload" shared.elf shared.btl >> build.log 2>&1
: > shared.uart
status=0
env -u T4_NODE_BTL T4_DATA_LINKS=6 T4_SHARED_KERNEL=1 \
    T4_SHARED_BOARD=0:5,1:5,2:5,3:5,4:5,5:5 \
    T4_HEADLESS=1 SDL_VIDEODRIVER=dummy T4_UART_CONSOLE=0 \
    T4_UART_LOG="$out/shared.uart" SPYNET="$root/transputer/t4/tests/six_cpu_mesh.net" \
    timeout -k 3 20 "$t4" -s9 -st -sn -6 -sm 25 -sb shared.btl > shared.log 2>&1 || status=$?
case "$status" in 0|124|137|255) ;; *) exit "$status" ;; esac
grep -q SHARED-KERNEL-PASS shared.uart
! grep -q SHARED-KERNEL-FAIL shared.uart
for cpu in 1 2 3 4 5; do grep -q "SHARED_KERNEL.*CPU$cpu entry=" shared.log; done
echo SHARED-KERNEL-SIX-PASS
