#!/bin/bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
out=${PERCPU_TEST_OUT:-$(mktemp -d)}
mkdir -p "$out"
out=$(cd "$out" && pwd)
cd "$out"
tcc=${TCC:-$root/transputer-tcc}
netload=${NETLOAD:-$root/transputer-netload}
t4=${T4_BIN:-$root/transputer/t4/t4}
"$tcc" -c "$root/transputer/tests/test_percpu_zero.c" -o "$out/percpu-zero.o"
python3 - "$out/percpu-zero.o" <<'PY'
import struct,sys
from pathlib import Path
p=Path(sys.argv[1]); b=bytearray(p.read_bytes())
shoff=struct.unpack_from('<I',b,32)[0]; entsize,count,strings=struct.unpack_from('<HHH',b,46)
stroff=struct.unpack_from('<I',b,shoff+strings*entsize+16)[0]
found=0
for i in range(count):
 at=shoff+i*entsize; no=struct.unpack_from('<I',b,at)[0]
 name=b[stroff+no:].split(b'\0',1)[0]
 if name==b'.data..percpu..zero':
  struct.pack_into('<I',b,at+4,8); found+=1
assert found==1
p.write_bytes(b)
PY
"$tcc" -nostdlib -static -I"$root/include" -o "$out/percpu.elf" "$root/transputer/linux-7/arch/transputer/kernel/head.S" "$root/transputer/tests/test_percpu_layout.c" "$out/percpu-zero.o" > "$out/percpu-build.log" 2>&1
"$netload" "$out/percpu.elf" "$out/percpu.btl" >> "$out/percpu-build.log" 2>&1
status=0
T4_HEADLESS=1 SDL_VIDEODRIVER=dummy T4_UART_CONSOLE=0 T4_UART_LOG="$out/percpu.uart" timeout -k 3 5 "$t4" -s9 -sm 25 -ss -sb "$out/percpu.btl" > "$out/percpu-run.log" 2>&1 || status=$?
grep -q PERCPU-LAYOUT-PASS "$out/percpu.uart"

case "$status" in 0|124|255) ;; *) exit "$status";; esac
! grep -q PERCPU-LAYOUT-FAIL "$out/percpu.uart"
grep -q '^H' "$out/percpu.uart"
echo PERCPU-LAYOUT-PASS
