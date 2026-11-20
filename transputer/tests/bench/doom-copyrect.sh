#!/usr/bin/env bash
# Compare Doom's V_CopyRect loop and MOVE2D path with payload/canary checks.
# T4_BIN needs the MOVE2D transfer timing model from T4 commit 8177e21.
# TCC_DIR, T4_BIN, UCLIBC and WORK have the same meanings as in doom.sh.
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
R=$(cd "${TCC_DIR:-$ROOT}" && pwd)
T=$ROOT/transputer
T4=$(realpath "${T4_BIN:-$T/t4/t4}")
U=$(cd "${UCLIBC:-$T/uClibc-ng}" && pwd)
DS=$T/doom/linuxdoom-1.10
CPU=${1:-t800}
case $CPU in
    t9000) sim=-s9; lib=transputer-libtcc1.a ;;
    t805|t800) sim=-s8; lib=transputer-libtcc1-t800.a ;;
    t425|t414) sim=-s${CPU#t}; lib=transputer-libtcc1-$CPU.a ;;
    *) echo "unknown target $CPU" >&2; exit 1 ;;
esac
capable=1
if [ "$CPU" = t414 ]; then capable=0; fi
WORK=${WORK:-$(mktemp -d)}
mkdir -p "$WORK"
cd "$WORK"
flags=(-m"$CPU" -w -D__GNUC__=4 -D__GNUC_MINOR__=4 -D__GNUC_PATCHLEVEL__=0
       -DNORMALUNIX -DTRANSPUTER -include "$DS/doom_tp_config.h" -nostdinc
       -I"$R/include/transputer" -I"$R/include" -I"$U/include"
       -I"$U/libc/sysdeps/linux/transputer" -I"$U/libc/sysdeps/linux/common"
       -I"$T/linux-2.6.39/include" -I"$DS")
"$R/transputer-tcc" -m"$CPU" -DTEST_LARGE_WORKSPACE -c -o boot.o "$T/tests/test_boot.S"
for variant in rows move2d; do
    opt=0
    if [ "$variant" = move2d ]; then opt=1; fi
    "$R/transputer-tcc" "${flags[@]}" -DTPOS_HAVE_MOVE2D="$capable" -DDOOM_RECT_MOVE2D="$opt" \
        -c -o "$variant-video.o" "$DS/v_video.c"
    T4_STRICT_UNRESOLVED_CALLS=1 "$R/transputer-tcc" "${flags[@]}" \
        -static -nostdlib -o "$variant.elf" boot.o \
        "$HERE/bench_doom_copyrect.c" "$variant-video.o" "$DS/m_bbox.c" "$R/$lib" \
        2>"$variant.link" || { cat "$variant.link" >&2; exit 1; }
    [ ! -s "$variant.link" ] || { cat "$variant.link" >&2; exit 1; }
    "$R/transputer-netload" "$variant.elf" "$variant.btl" >"$variant.netload" 2>&1
    for mode in 1 2; do
        status=0
        uart=$variant-$mode.uart
        : >"$uart"
        env -u DISPLAY T4_EST_CYCLES=$mode T4_UART_CONSOLE=0 T4_UART_LOG="$uart" \
            timeout -k 5 120 "$T4" "$sim" -sm 24 -ss -sb "$variant.btl" \
            >"$variant-$mode.log" 2>&1 || status=$?
        [ "$status" -ne 124 ] && [ "$status" -ne 137 ]
        grep -q 'T800 cycle estimate enabled' "$variant-$mode.log"
    done
done
python3 - "$CPU" <<'PY'
import re
import sys
from pathlib import Path

phases = ('number', 'shortnum', 'key', 'face', 'eight', 'full')
result = {}
for variant in ('rows', 'move2d'):
    for mode in (1, 2):
        path = Path(f'{variant}-{mode}.uart')
        match = re.fullmatch(r'S((?:\w+ \d+ \d+\n){6})check 0\nH', path.read_text().replace('\0',''))
        if not match:
            sys.exit(f'{path}: incomplete run or framebuffer/dirty-box check failed')
        lines = [line.split() for line in match[1].splitlines()]
        if tuple(line[0] for line in lines) != phases:
            sys.exit(f'{path}: unexpected profile phase order')
        result[variant, mode] = [tuple(map(int, line[1:])) for line in lines]

if sys.argv[1] != 't414':
    cycles = result['move2d', 1]
    if cycles[phases.index('face')][0] <= cycles[phases.index('number')][0]:
        sys.exit('T4 does not report size-dependent MOVE2D costs for this fixture')

print(f'{sys.argv[1]} V_CopyRect, 256 calls per phase')
print('phase          row cycles   2D cycles    row insns    2D insns   framebuffer hash')
for i, name in enumerate(phases):
    values = [result[v, m][i] for m in (1, 2) for v in ('rows', 'move2d')]
    if len({digest for _, digest in values}) != 1:
        sys.exit(f'{name}: result hashes differ')
    print(f'{name:12}' + ''.join(f'{count:12}' for count, _ in values) + f'   {values[0][1]:08x}')
print('check 0')
PY
