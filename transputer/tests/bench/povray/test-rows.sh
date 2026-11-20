#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
R=${TCC_DIR:-$ROOT}
T4=${T4_BIN:-$ROOT/transputer/t4/t4}
U=${UCLIBC:-$ROOT/transputer/uClibc-ng}
H=${UCLIBC_HEADERS:-$U/include}
WORK=${WORK:-$(mktemp -d)}
mkdir -p "$WORK"
WORK="$(cd "$WORK" && pwd)"
POV_OUTPUT=row python3 "$HERE/prepare.py" "$WORK"
python3 - "$HERE/runtime.c" "$WORK/io.c" <<'PY'
from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text()
begin=s.index('/* Use fdlibm')
end=s.index('static unsigned char sp[514];')
s=s[:begin]+s[end:]
s=s.replace('extern void main(int, char **), __uClibc_init(void);',
            'extern int main(int, char **); extern void __uClibc_init(void);')
s=s.replace('    main(2, args);\n    exit(0);', '    exit(main(2, args));')
Path(sys.argv[2]).write_text(s)
PY
FLAGS=(-mt800 -nostdinc -w -D__GNUC__=4 -D__GNUC_MINOR__=4 -D__GNUC_PATCHLEVEL__=0
    -I"$WORK/source" -I"$ROOT/include/transputer" -I"$ROOT/include" -I"$H"
    -I"$U/libc/sysdeps/linux/transputer" -I"$U/libc/sysdeps/linux/common"
    -I"$ROOT/transputer/linux-2.6.39/include")
case ${POV_RGB_PACK:-bytes} in
    bytes) ;;
    move2d) FLAGS+=(-DPOV_RGB_USE_MOVE2D=1);;
    *) echo "invalid POV_RGB_PACK" >&2; exit 1;;
esac
for name in rgb display pack; do
    SOURCES=("$HERE/test_rgb_rows.c")
    if [ "$name" = display ]; then SOURCES=("$HERE/test_display_rows.c" "$HERE/display-row.c"); fi
    if [ "$name" = pack ]; then SOURCES=("$HERE/bench_rgb_pack.c" "$HERE/pack-bytes.c" "$HERE/pack-move2d.c"); fi
    T4_STRICT_UNRESOLVED_CALLS=1 "$R/transputer-tcc" "${FLAGS[@]}" -nostdlib \
        -o "$WORK/$name.elf" "$WORK/boot.S" "$WORK/io.c" "${SOURCES[@]}" \
        "$U/lib/libc.a" "$R/transputer-libtcc1-t800.a" >"$WORK/$name-link.log" 2>&1
    [ ! -s "$WORK/$name-link.log" ] || { cat "$WORK/$name-link.log" >&2; exit 1; }
    "$R/transputer-netload" "$WORK/$name.elf" "$WORK/$name.btl" >"$WORK/$name-netload.log" 2>&1
done
python3 - "$ROOT/transputer/t4/examples/povray/pictures.zip" "$WORK" <<'PY'
from pathlib import Path
import sys,zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    for scene in ('box','basicvue','glass'):
        d=Path(sys.argv[2])/scene;d.mkdir(exist_ok=True)
        (d/'reference.ppm').write_bytes(z.read(f'ppm/level1/{scene}.ppm'))
PY
for scene in box basicvue glass; do
    d=$WORK/$scene
    (cd "$d"; env -u DISPLAY T4_HEADLESS=1 \
        SPYNET="$ROOT/transputer/t4/examples/transputer-raytracer/bin/raytrace1.map" \
        timeout -k 3 90 "$T4" -st -s9 -sn -1 -sm 26 -ss -se -sb "$WORK/rgb.btl" >run.log 2>&1)
    grep -q 'ROWTEST PASS' "$d/run.log"
    for phase in 0 1 2 3; do
        cmp "$d/reference.ppm" "$d/rows$phase.ppm"
    done
    echo "$scene: packed packet replay, four alignments and reordered rows pass"
done
mkdir -p "$WORK/display"
(cd "$WORK/display"; env -u DISPLAY T4_HEADLESS=1 \
    SPYNET="$ROOT/transputer/t4/examples/transputer-raytracer/bin/raytrace1.map" \
    timeout -k 3 30 "$T4" -st -s9 -sn -1 -sm 26 -ss -se -sb "$WORK/display.btl" >run.log 2>&1)
grep -q 'DISPLAYTEST PASS' "$WORK/display/run.log"
python3 "$HERE/verify-display.py" "$WORK/display/image.ppm"
mkdir -p "$WORK/pack"
(cd "$WORK/pack"; env -u DISPLAY T4_HEADLESS=1 T4_EST_CYCLES=1 \
    SPYNET="$ROOT/transputer/t4/examples/transputer-raytracer/bin/raytrace1.map" \
    timeout -k 3 30 "$T4" -st -s9 -sn -1 -sm 26 -ss -se -sb "$WORK/pack.btl" >run.log 2>&1)
grep -E '^pack (bytes|move2d|fail)' "$WORK/pack/run.log"
grep -q '^pack fail 0' "$WORK/pack/run.log"
CC=${CC:-gcc}
"$CC" -O2 -g -fsanitize=address,undefined -I"$WORK/source" "$HERE/test_row_failures.c" -o "$WORK/host-failures"
"$WORK/host-failures"
"$CC" -O2 -g -fsanitize=address,undefined -I"$WORK/source" "$HERE/test_display_rows.c" "$HERE/display-row.c" -o "$WORK/host-display"
mkdir -p "$WORK/host-callback"
(cd "$WORK/host-callback"; "$WORK/host-display")
python3 "$HERE/verify-display.py" "$WORK/host-callback/image.ppm"
