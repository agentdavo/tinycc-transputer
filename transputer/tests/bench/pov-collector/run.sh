#!/usr/bin/env bash
# Build the original muxdemux.c with the TinyCC channel/runtime ABI, then
# profile both RGB packers on the same captured FRead→Cache row traffic.
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)
T=$ROOT/transputer/t4
POV=$T/examples/povray
SOURCE=$POV/povsrc/machine/transp
R=$(cd "${TCC_DIR:-$ROOT}" && pwd)
U=${UCLIBC:-$ROOT/transputer/uClibc-ng}
H=${UCLIBC_HEADERS:-$U/include}
CAPTURE=$(realpath "${1:?give the capture.sh output directory}")
EMU=$(realpath "${T4_BIN:-$CAPTURE/t4-capture}")
WORK=${WORK:-$(mktemp -d)}
mkdir -p "$WORK/compat"
WORK=$(cd "$WORK" && pwd)

printf '%s\n' '#include <conc.h>' >"$WORK/compat/channel.h"
printf '%s\n' 'void *get_param(int);' >"$WORK/compat/misc.h"
cat >"$WORK/compat/process.h" <<'EOF'
/* ipack.h uses BIG_ENDIAN as a switch. uClibc defines both endian constants. */
#undef BIG_ENDIAN
#undef LITTLE_ENDIAN
#define ProcAltList ChanAltList
void collector_start(void);
void collector_stop(void);
#define START_TIME collector_start();
#define STOP_TIME collector_stop();
#define TIME_ELAPSED 0.0;
EOF
# The source tests these hooks before including the legacy headers.
python3 - "$HERE/../povray/runtime.c" "$WORK/io.c" "$ROOT/transputer/tests/test_boot.S" "$WORK/boot.S" <<'PY'
from pathlib import Path
import sys
runtime, io, boot, out = map(Path, sys.argv[1:])
s = runtime.read_text()
s = s[:s.index('/* Use fdlibm')] + s[s.index('static unsigned char sp[514];'):]
s = s.replace('extern void main(int, char **), __uClibc_init(void);',
              'extern int main(int, char **); extern void __uClibc_init(void);')
s = s.replace('    main(2, args);\n    exit(0);', '    exit(main(2, args));')
io.write_text(s)
s = boot.read_text()
old = '0x24, 0x20, 0x20, 0x20, 0xB0'
assert s.count(old) == 1
out.write_text(s.replace(old, '0x22, 0x20, 0x20, 0x20, 0x20, 0xB0'))
PY
flags=(-mt800 -nostdinc -w -D__GNUC__=4 -D__GNUC_MINOR__=4 -D__GNUC_PATCHLEVEL__=0
       -I"$WORK/compat" -I"$ROOT/include/transputer" -I"$ROOT/include" -I"$H"
       -I"$U/libc/sysdeps/linux/transputer" -I"$U/libc/sysdeps/linux/common"
       -I"$ROOT/transputer/linux-2.6.39/include")
for pack in bytes move2d; do
    d=$WORK/$pack
    mkdir -p "$d"
    mode=()
    if [ "$pack" = move2d ]; then mode=(-DPOV_RGB_USE_MOVE2D=1); fi
    "$R/transputer-tcc" "${flags[@]}" -Dmain=collector_main -DFILE_SUPPORT -funsigned-char \
        -include "$WORK/compat/process.h" "${mode[@]}" -c -o "$d/muxdemux.o" "$SOURCE/muxdemux.c"
    T4_STRICT_UNRESOLVED_CALLS=1 "$R/transputer-tcc" "${flags[@]}" -nostdlib \
        -o "$d/collector.elf" "$WORK/boot.S" "$WORK/io.c" "$HERE/driver.c" "$d/muxdemux.o" \
        "$ROOT/lib/transputer/transputer-conc.c" "$U/lib/libc.a" "$R/transputer-libtcc1-t800.a" \
        >"$d/link.log" 2>&1 || { cat "$d/link.log" >&2; exit 1; }
    [ ! -s "$d/link.log" ] || { cat "$d/link.log" >&2; exit 1; }
    "$R/transputer-netload" "$d/collector.elf" "$d/collector.btl" >"$d/netload.log" 2>&1
done
sha256sum "$R/transputer-tcc" "$EMU" "$SOURCE/muxdemux.c" "$SOURCE/rgbrow.h" "$HERE/driver.c" >"$WORK/inputs.sha256"
for scene in ${SCENES:-box basicvue glass}; do
    case $scene in box|basicvue|glass) ;; *) echo "unknown capture scene $scene" >&2; exit 1 ;; esac
    for pack in bytes move2d; do
        for mode in 1 2; do
            d=$WORK/$pack/$scene-$mode
            mkdir -p "$d"
            ln -sfn "$CAPTURE/$scene/packets.bin" "$d/packets.bin"
            (cd "$d"; env -u DISPLAY -u T4_NODE_BTL -u POV_COLLECTOR_CAPTURE \
                T4_HEADLESS=1 T4_EST_CYCLES=$mode \
                SPYNET="$T/examples/transputer-raytracer/bin/raytrace1.map" \
                timeout -k 3 120 "$EMU" -st -s9 -sn -1 -sm 26 -ss -se \
                -sb "$WORK/$pack/collector.btl" >run.log 2>&1)
            grep -Fq '[EXIT] quit=TRUE' "$d/run.log"
            grep -Fq 'T800 cycle estimate enabled' "$d/run.log"
            grep -Eq '^COLLECTOR elapsed [1-9][0-9]*$' "$d/run.log"
            cmp "$CAPTURE/$scene/picture.ppm" "$d/picture.ppm"
        done
    done
    move_cycles=$(sed -n 's/^COLLECTOR elapsed //p' "$WORK/move2d/$scene-1/run.log")
    # The nominal transfer alone costs 25 cycles per three-byte row.
    if (( move_cycles < 25 * 640 * 480 )); then
        echo 'T4 cycle count omits MOVE2D transfer costs' >&2
        exit 1
    fi
    printf '%s\n' "$scene, exact collector images"
    for pack in bytes move2d; do
        printf '%s  cycles %s  instructions %s\n' "$pack" \
            "$(sed -n 's/^COLLECTOR elapsed //p' "$WORK/$pack/$scene-1/run.log")" \
            "$(sed -n 's/^COLLECTOR elapsed //p' "$WORK/$pack/$scene-2/run.log")"
    done
done
