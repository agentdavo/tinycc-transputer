#!/usr/bin/env bash
# Builds the dEngine port.
#   build.sh host OUTDIR       host binary OUTDIR/dengine-host (gcc)
#   build.sh t4 OUTDIR         T800 objects and the ROM in OUTDIR/obj; node
#                              images are linked by run-scene.sh
# ROM_QUALITY=high|low|both selects the textures packed (default both).
# TCC_DIR selects the transputer toolchain (default: this checkout).
# DENGINE is the dEngine clone (default: transputer/examples/dEngine).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)
R=${TCC_DIR:-$ROOT}
DENGINE=${DENGINE:-$ROOT/transputer/examples/dEngine}
FB=$ROOT/transputer/examples/distributed-fb
mode=${1:?usage: build.sh host|t4 OUTDIR}
out=${2:?usage: build.sh host|t4 OUTDIR}
[[ -f $DENGINE/src/renderer_progr.c ]] || { echo 'dEngine clone missing: see transputer/examples/dengine-t4/README.md' >&2; exit 1; }
mkdir -p "$out"; out=$(cd "$out" && pwd)
# dEngine's own sources, unmodified; renderer_fixed.c is the ES 1.1 path.
ENGINE="ItextureLoader camera commands config dEngine entities filesystem font lexer material math matrix md5anim md5model obj quaternion renderer renderer_progr stats texture timer world"
PORT="libc romfs png pvrtc gles2 dengine_app"
python3 "$HERE/mkrom.py" "$DENGINE" "$out/rom.bin" --quality "${ROM_QUALITY:-both}" --list "$out/rom.txt" >"$out/rom.log" 2>&1 || { cat "$out/rom.log" >&2; exit 1; }

if [[ $mode == host ]]; then
    srcs=()
    for s in $ENGINE; do srcs+=("$DENGINE/src/$s.c"); done
    for s in $PORT host_main; do srcs+=("$HERE/$s.c"); done
    # Same software libm and libc as the T800, IEEE single and double with
    # no contraction, so host and T800 frames can be compared bit for bit.
    gcc -O2 -g -ffp-contract=off -fno-math-errno -fno-builtin -w \
        -Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wno-error=implicit-function-declaration \
        -Wno-error=implicit-int -Wno-error=return-mismatch \
        -I"$HERE/include" -I"$DENGINE/src" -o "$out/dengine-host" "${srcs[@]}" "$FB/glsl_fragment_compiler.c" -lm
    echo "host build: $out/dengine-host (ROM $out/rom.bin)"
    exit 0
fi

[[ $mode == t4 ]] || { echo 'mode must be host or t4' >&2; exit 1; }
obj=$out/obj; mkdir -p "$obj"
CFLAGS=(-mt800 -w -I"$HERE/include" -I"$R/include" -I"$DENGINE/src" ${T4_CFLAGS:-})
compile() { local src=$1 o=$2; shift 2; "$R/transputer-tcc" "${CFLAGS[@]}" "$@" -c -o "$o" "$src" >"$o.log" 2>&1 || { cat "$o.log" >&2; echo "compile failed: $src" >&2; exit 1; }; }
# The shader interpreter's dispatch is a jump table where the compiler has
# them (-mjump-tables); an older one compares.
JT=(); "$R/transputer-tcc" -mt800 -mjump-tables -E - </dev/null >/dev/null 2>&1 && JT=(-mjump-tables)
for s in $ENGINE; do compile "$DENGINE/src/$s.c" "$obj/$s.o"; done
for s in $PORT; do
    if [[ $s == gles2 ]]; then compile "$HERE/$s.c" "$obj/$s.o" "${JT[@]}"; else compile "$HERE/$s.c" "$obj/$s.o"; fi
done
compile "$FB/glsl_fragment_compiler.c" "$obj/glsl_fragment_compiler.o"
compile "$R/lib/transputer/transputer-conc.c" "$obj/conc.o"
"$R/transputer-tcc" -mt800 -c -o "$obj/boot.o" "$HERE/boot.S"
(cd "$out" && "$R/transputer-tcc" -mt800 -DROM_FILE='"rom.bin"' -c -o "$obj/rom.o" "$HERE/rom.S")
echo "t4 objects: $obj"
