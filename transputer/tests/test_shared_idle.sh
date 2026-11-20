#!/bin/bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
out=${SHARED_IDLE_TEST_OUT:-$(mktemp -d)}
mkdir -p "$out"
out=$(cd "$out" && pwd)
cd "$out"
tcc=${TCC:-$root/transputer-tcc}
netload=${NETLOAD:-$root/transputer-netload}
t4=${T4_BIN:-$root/transputer/t4/t4}
cc -std=gnu11 -Wall -Wextra -Werror -pthread -I"$root/transputer/t4" \
    "$root/transputer/t4/shared_board.c" \
    "$root/transputer/t4/tests/test_shared_board_idle_host.c" -o idle-host
./idle-host > idle-host.log 2>&1
cat idle-host.log
"$tcc" -nostdlib -static -I"$root/include" -o shared.elf \
    "$root/transputer/linux-7/arch/transputer/kernel/head.S" \
    "$root/transputer/tests/test_shared_idle.c" \
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
grep -q SHARED-IDLE-PASS shared.uart
! grep -q SHARED-IDLE-FAIL shared.uart
for cpu in 1 2 3 4 5; do grep -q "SHARED_KERNEL.*CPU$cpu entry=" shared.log; done
echo SHARED-IDLE-SIX-PASS
