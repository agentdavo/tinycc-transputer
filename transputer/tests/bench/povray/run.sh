#!/usr/bin/env bash
# Full generic POV-Ray source, rebuilt with TinyCC, with packaged image oracles.
# This does not rebuild the historical ICC sixteen-node channel/process port.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
R=${TCC_DIR:-$ROOT}
T4=${T4_BIN:-$ROOT/transputer/t4/t4}
U=${UCLIBC:-$ROOT/transputer/uClibc-ng}
# UCLIBC_HEADERS may name a copy with dereferenced Windows symlink stubs.
H=${UCLIBC_HEADERS:-$U/include}
POV_RNG=${POV_RNG:-inmos}
case $POV_RNG in inmos|libc) ;; *) echo "invalid POV_RNG: $POV_RNG" >&2; exit 1;; esac
export POV_OUTPUT=${POV_OUTPUT:-row}
case $POV_OUTPUT in row|pixel) ;; *) echo "invalid POV_OUTPUT: $POV_OUTPUT" >&2; exit 1;; esac
WORK=${WORK:-$(mktemp -d)}
mkdir -p "$WORK"
WORK="$(cd "$WORK" && pwd)"
python3 "$HERE/prepare.py" "$WORK"
OBJ=$WORK/obj
mkdir -p "$OBJ"
FLAGS=(-mt800 -nostdinc -w -D__GNUC__=4 -D__GNUC_MINOR__=4 -D__GNUC_PATCHLEVEL__=0
    -I"$HERE" -I"$ROOT/include/transputer" -I"$ROOT/include" -I"$H"
    -I"$U/libc/sysdeps/linux/transputer" -I"$U/libc/sysdeps/linux/common"
    -I"$ROOT/transputer/linux-2.6.39/include"
    -I"$ROOT/transputer/linux-2.6.39/arch/transputer/include")
for file in "$WORK/source"/*.c; do
    name=$(basename "$file" .c)
    "$R/transputer-tcc" "${FLAGS[@]}" -c -o "$OBJ/$name.o" "$file" >"$OBJ/$name.log" 2>&1 || {
        cat "$OBJ/$name.log" >&2; exit 1;
    }
done
MATH=(s_sin s_cos s_floor s_ceil s_fabs s_scalbn s_copysign s_atan s_ldexp
      s_frexp s_modf e_pow e_exp e_log e_acos e_asin e_atan2 e_fmod
      e_rem_pio2 k_rem_pio2 k_sin k_cos)
for name in "${MATH[@]}"; do
    "$R/transputer-tcc" "${FLAGS[@]}" -I"$U/libm" -c -o "$OBJ/math_$name.o" \
        "$U/libm/$name.c" >"$OBJ/math_$name.log" 2>&1 || {
        cat "$OBJ/math_$name.log" >&2; exit 1;
    }
done
RNG=()
if [ "$POV_RNG" = inmos ]; then
    RNG=("$HERE/inmos-rand.c")
fi
T4_STRICT_UNRESOLVED_CALLS=1 "$R/transputer-tcc" "${FLAGS[@]}" -nostdlib \
    -o "$WORK/pov.elf" "$WORK/boot.S" "$HERE/runtime.c" "${RNG[@]}" "$OBJ"/*.o \
    "$U/lib/libc.a" "$R/transputer-libtcc1-t800.a" >"$WORK/link.log" 2>&1 || {
    cat "$WORK/link.log" >&2; exit 1;
}
[ ! -s "$WORK/link.log" ] || { cat "$WORK/link.log" >&2; exit 1; }
"$R/transputer-netload" "$WORK/pov.elf" "$WORK/pov.btl" >"$WORK/netload.log" 2>&1
{
    sha256sum "$R/transputer-tcc" "$T4" "$U/lib/libc.a" "$R/transputer-libtcc1-t800.a"
    sha256sum "$HERE"/* "$WORK/boot.S" "$WORK/source"/*.[ch]
    for name in "${MATH[@]}"; do sha256sum "$U/libm/$name.c"; done
} >"$WORK/inputs.sha256"
printf 'POV_RNG=%s\nPOV_OUTPUT=%s\n' "$POV_RNG" "$POV_OUTPUT" >"$WORK/settings.txt"
# The in-tree libc archive contains T9000 instructions.  The renderer and
# fdlibm above use the T800 subset, but the linked image requires -s9.
for scene in ${SCENES:-box basicvue glass}; do
    case $scene in *[!a-zA-Z0-9_-]*) echo "invalid scene: $scene" >&2; exit 1;; esac
    d=$WORK/$scene
    mkdir -p "$d"
    ln -sfn "$ROOT/transputer/t4/examples/povray/povscn/include" "$d/include"
    ln -sfn "$ROOT/transputer/t4/examples/povray/povscn/level1" "$d/scenes"
    printf '+linclude +lscenes -iscenes/%s.pov -w640 -h480 -v -f +d\n' "$scene" >"$d/povray.def"
    (cd "$d"; env -u DISPLAY T4_HEADLESS=1 \
        SPYNET="$ROOT/transputer/t4/examples/transputer-raytracer/bin/raytrace1.map" \
        timeout -k 3 "${TIMEOUT:-1800}" "$T4" -st -s9 -sn -1 -sm 26 -ss -se \
        -sb "$WORK/pov.btl" >run.log 2>&1)
    if grep -Eq '\[FAULT|\[ERROR|Invalid WPtr|undefined symbol|unresolved' "$d/run.log"; then
        cat "$d/run.log" >&2; exit 1;
    fi
    grep -q '\[EXIT\] quit=TRUE' "$d/run.log"
    python3 "$HERE/verify.py" "$ROOT/transputer/t4/examples/povray/pictures.zip" \
        "$d/image.ppm" "$scene"
done
