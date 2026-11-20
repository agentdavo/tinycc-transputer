#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
tcc="${TCC_BIN:-$root/transputer-tcc}"
netload="${NETLOAD:-$root/transputer-netload}"
t4="${T4_BIN:-$root/transputer/t4/t4}"
out="${TEST_OUT:-$(mktemp -d)}"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
"$tcc" -DCONFIG_TRANSPUTER_SPINE_CHIP -DCONFIG_TRANSPUTER_T4_SHARED_KERNEL -static -nostdlib -I"$root/include" -o "$out/test.elf" \
    "$root/transputer/tests/test_boot.S" \
    "$root/transputer/tests/test_thds_call_abi.c" \
    "$root/transputer/linux-7/arch/transputer/kernel/thds_selth.S"
"$netload" "$out/test.elf" "$out/test.btl"
: > "$out/test.uart"
status=0
env T4_HEADLESS=1 SDL_VIDEODRIVER=dummy T4_UART_CONSOLE=0 \
    T4_UART_LOG="$out/test.uart" timeout -k 2 5 "$t4" \
    -s9 -sm 25 -ss -sb "$out/test.btl" > "$out/test.log" 2>&1 || status=$?
case "$status" in 0|124|137|255) ;; *) exit "$status" ;; esac
grep -q THDS-ABI-PASS "$out/test.uart"
! grep -q THDS-ABI-FAIL "$out/test.uart"
echo "T4 shared-kernel THDS CALL ABI: PASS ($out)"
