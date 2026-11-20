#!/bin/bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${SMP_TEST_OUT:-$(mktemp -d)}
mkdir -p "$out"
out=$(cd "$out" && pwd)
cd "$out"
tcc=${TCC:-$root/transputer-tcc}
netload=${NETLOAD:-$root/transputer-netload}
t4=${T4_BIN:-$root/transputer/t4/t4}
fixtures=$root/transputer/tests/smp-locks
printf '%s\n' 'Connect HOST to processor 0 link 0' 'Connect processor 0 link 3 to processor 1 link 2' 'Connect processor 1 link 3 to processor 2 link 2' 'Connect processor 2 link 3 to processor 3 link 2' > "$out/network.map"
arch=$root/transputer/linux-7/arch/transputer
"$tcc" -DCONFIG_SMP -static -nostdlib -I"$fixtures/include" -I"$arch/include" -I"$root/include" -o "$out/locks.elf" "$root/transputer/t4/tests/test_boot.S" "$fixtures/test.c" "$arch/kernel/smp_locks.c" "$root/transputer/t4/tests/test_shared_board_move.S" > "$out/locks-build.log" 2>&1
"$netload" "$out/locks.elf" "$out/locks.btl" >> "$out/locks-build.log" 2>&1
status=0
env T4_HEADLESS=1 SDL_VIDEODRIVER=dummy T4_UART_CONSOLE=0 T4_UART_LOG="$out/locks.uart" SPYNET="$out/network.map" T4_NODE_BTL="$out/locks.btl" T4_SHARED_BOARD=0:1,1:1,2:1,3:1 timeout -k 3 20 "$t4" -s9 -st -sn -4 -sm 25 -ss -sb "$out/locks.btl" > "$out/locks.log" 2>&1 || status=$?
case "$status" in 0|124|137|255) ;; *) exit "$status";; esac
grep -q LOCK-PASS "$out/locks.uart"
! grep -Eq 'LOCK-(BUG|FAIL)' "$out/locks.uart"
echo LOCK-PASS
