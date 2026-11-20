#!/usr/bin/env bash
# The historical BTL expects 16 nodes and exactly 640x480 scanlines.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
SRC="$ROOT/transputer/t4/examples/povray/povsrc/build"
T4=${T4_BIN:-$ROOT/transputer/t4/t4}
TIMEOUT=${TIMEOUT:-180}
SCENE=${SCENE:-$HERE/scene.pov}
LEVEL=${LEVEL:-}
SCENE_NAME=${SCENE_NAME:-}
if ! [[ $TIMEOUT =~ ^[0-9]+$ ]] || (( 10#$TIMEOUT < 1 )); then
    echo 'TIMEOUT must be a positive number of seconds' >&2; exit 1
fi
if [[ -n ${WORK:-} ]]; then
    mkdir -p "$WORK"; WORK=$(cd "$WORK" && pwd); WORK=$(mktemp -d "$WORK/povray.XXXXXX")
else
    WORK=$(mktemp -d)
fi
if [[ -n $LEVEL || -n $SCENE_NAME ]]; then
    if [[ ! $LEVEL =~ ^level[123]$ || ! $SCENE_NAME =~ ^[a-zA-Z0-9_.-]+\.pov$ ]]; then
        echo 'Use LEVEL=level1, level2, or level3 and SCENE_NAME=<name>.pov' >&2; exit 1
    fi
    SCENES="$ROOT/transputer/t4/examples/povray/povscn"
    if [[ ! -f $SCENES/$LEVEL/$SCENE_NAME ]]; then
        echo "Scene not found: $SCENES/$LEVEL/$SCENE_NAME" >&2; exit 1
    fi
    cp -R "$SCENES/include" "$WORK/inc"
    cp -R "$SCENES/$LEVEL" "$WORK/scene"
    printf '+linc/ +lscene/ +iscene/%s +w640 +h480 -v -f +d\n' "$SCENE_NAME" > "$WORK/povray.def"
else
    cp "$SCENE" "$WORK/scene.pov"
    printf '%s\n' '+iscene.pov +w640 +h480 -v -f +d' > "$WORK/povray.def"
fi
cd "$WORK"
export SPYNET="$SRC/povray.map" T4_UART_CONSOLE=0 T4_UART_LOG="$WORK/uart.log"
unset DISPLAY T4_NODE_BTL
# timeout signals only its own emulator process; no global pkill or shared link cleanup.
status=0
TIMEFORMAT=$'wall_seconds=%3R\nuser_seconds=%3U\nsystem_seconds=%3S'
{ time timeout -k 5 "$TIMEOUT" "$T4" -s8 -sm 22 -ss -st -sn -16 -sb "$SRC/povray.btl" >povray.log 2>&1; } 2>timing.txt || status=$?
printf 'exit=%s\n' "$status" >result.txt
cat timing.txt >>result.txt
if (( status != 0 )); then
    echo "POV-Ray failed or timed out (exit $status); inspect $WORK/povray.log" >&2; exit 1
fi
# PPM is P6, with a fixed 15-byte header and 640*480*3 bytes of pixel data.
if [[ ! -f picture.ppm ]] || [[ $(wc -c <picture.ppm) -ne 921615 ]] || [[ $(head -n 3 picture.ppm) != $'P6\n640 480\n255' ]]; then
    echo "POV-Ray produced an incomplete or invalid PPM; inspect $WORK" >&2; exit 1
fi
sha256sum picture.ppm >sha256.txt
echo "POV-RAY OK: $WORK/picture.ppm"
cat timing.txt sha256.txt
