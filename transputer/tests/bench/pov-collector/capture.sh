#!/usr/bin/env bash
# Build a private capture emulator and run the packaged 16-node image.
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)
T=$ROOT/transputer/t4
POV=$T/examples/povray
IMAGE=$POV/povsrc/build
WORK=${WORK:-$(mktemp -d)}
mkdir -p "$WORK"
WORK=$(cd "$WORK" && pwd)
src=$(mktemp -d)
trap 'rm -r "$src"' EXIT
cp "$T"/*.c "$T"/*.h "$T/Makefile.linux" "$src/"
python3 "$HERE/capture.py" instrument "$src/p.c" "$IMAGE/povray.map"
make -C "$src" -f Makefile.linux CC=cc T4_OPT=-O2 t4 >"$WORK/build.log" 2>&1 || { tail -50 "$WORK/build.log" >&2; exit 1; }
cp "$src/t4" "$WORK/t4-capture"
sha256sum "$IMAGE/povray.btl" "$IMAGE/povray.map" "$WORK/t4-capture" >"$WORK/inputs.sha256"
for scene in ${SCENES:-box basicvue glass}; do
    case $scene in box|basicvue|glass) ;; *) echo "unknown capture scene $scene" >&2; exit 1 ;; esac
    d=$WORK/$scene
    mkdir -p "$d"
    ln -sfn "$POV/povscn/include" "$d/include"
    ln -sfn "$POV/povscn/level1" "$d/scenes"
    printf '+linclude -lscenes -iscenes/%s.pov -w640 -h480 -v -f +d\n' "$scene" >"$d/povray.def"
    (cd "$d"; env -u DISPLAY -u T4_NODE_BTL T4_HEADLESS=1 SPYNET="$IMAGE/povray.map" \
        POV_COLLECTOR_CAPTURE="$d/raw.bin" timeout -k 3 "${TIMEOUT:-300}" "$WORK/t4-capture" \
        -st -s8 -sn -16 -sm 22 -ss -se -sb "$IMAGE/povray.btl" >run.log 2>&1)
    grep -Fq '[EXIT] quit=TRUE' "$d/run.log"
    python3 "$HERE/../povray/verify.py" "$POV/pictures.zip" "$d/picture.ppm" "$scene"
    python3 "$HERE/capture.py" normalize "$d/raw.bin" "$d/packets.bin" "$d/picture.ppm"
done
