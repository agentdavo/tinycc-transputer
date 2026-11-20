#!/usr/bin/env bash
# Run from any directory. Set WORK to retain output in a chosen directory.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
R=${TCC_DIR:-$ROOT}
T4=${T4_BIN:-$ROOT/transputer/t4/t4}
FRAMES=${FRAMES:-12}
FPS=${FPS:-20}
CAPTURE=${CAPTURE:-1}
CAMERA_DISTANCE=${CAMERA_DISTANCE:-1152}
FOCAL_LENGTH=${FOCAL_LENGTH:-110}
if ! [[ $FRAMES =~ ^[0-9]+$ ]] || (( 10#$FRAMES < 1 || 10#$FRAMES > 120 )); then
    echo 'FRAMES must be an integer from 1 to 120' >&2
    exit 1
fi
FRAMES=$((10#$FRAMES))
if ! [[ $FPS =~ ^[0-9]+$ ]] || (( 10#$FPS < 1 || 10#$FPS > 60 )); then
    echo 'FPS must be an integer from 1 to 60' >&2
    exit 1
fi
FPS=$((10#$FPS))
if [[ $CAPTURE != 0 && $CAPTURE != 1 ]]; then
    echo 'CAPTURE must be 0 or 1' >&2
    exit 1
fi
if ! [[ $CAMERA_DISTANCE =~ ^[0-9]+$ ]] || (( 10#$CAMERA_DISTANCE < 768 || 10#$CAMERA_DISTANCE > 4096 )); then
    echo 'CAMERA_DISTANCE must be an integer from 768 to 4096 (Q8 units)' >&2; exit 1
fi
CAMERA_DISTANCE=$((10#$CAMERA_DISTANCE))
if ! [[ $FOCAL_LENGTH =~ ^[0-9]+$ ]] || (( 10#$FOCAL_LENGTH < 30 || 10#$FOCAL_LENGTH > 200 )); then
    echo 'FOCAL_LENGTH must be an integer from 30 to 200 (pixels)' >&2; exit 1
fi
FOCAL_LENGTH=$((10#$FOCAL_LENGTH))
# Keep projected coordinates/area within the fixed-point rasterizer's 32-bit
# range, including the nearest rotated corner. Smaller values zoom out.
if (( FOCAL_LENGTH * 704 > 110 * (CAMERA_DISTANCE - 448) )); then
    echo 'Camera zoom is too high for the fixed-point rasterizer; increase CAMERA_DISTANCE or reduce FOCAL_LENGTH' >&2
    exit 1
fi
if [[ -n ${WORK:-} ]]; then
    mkdir -p "$WORK"
    WORK=$(cd "$WORK" && pwd)
    WORK=$(mktemp -d "$WORK/distfb.XXXXXX")
else
    WORK=$(mktemp -d)
fi
cd "$WORK"
{
    echo 'Connect HOST to processor 0 link 0'
    for n in 1 2 3; do
        echo "Connect processor 0 link $n to processor $n link 1"
    done
} > distfb.map
for n in 0 1 2 3; do
    "$R/transputer-tcc" -mt800 -static -nostdlib -I"$R/include" -DNODE="$n" -DFRAMES="$FRAMES" -DFPS="$FPS" -DCAPTURE="$CAPTURE" \
        -DCAMERA_DISTANCE="$CAMERA_DISTANCE" -DFOCAL_LENGTH="$FOCAL_LENGTH" \
        -o "n$n.elf" "$ROOT/transputer/tests/test_boot.S" "$HERE/demo.c" "$HERE/tinygl.c" "$HERE/gles2_subset.c" "$HERE/cube.c" \
        "$R/lib/transputer/transputer-conc.c" "$R/transputer-libtcc1-t800.a"
    "$R/transputer-netload" "n$n.elf" "n$n.btl" >"n$n.netload.log" 2>&1
done
export SPYNET="$WORK/distfb.map" T4_NODE_BTL="$WORK/n%d.btl"
display_args=()
if [[ ${LIVE:-0} != 1 ]]; then display_args=(-u DISPLAY); fi
env "${display_args[@]}" T4_UART_CONSOLE=0 T4_UART_LOG="$WORK/distfb.uart" \
    T4_PRESENT_ON_FLIP=1 T4_VSYNC="${T4_VSYNC:-1}" \
    "$T4" -s8 -sm 26 -ss -st -sn -4 -sb "$WORK/n0.btl" >distfb.out 2>&1 &
pid=$!
trap 'kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true' EXIT
for ((i=0; i<180 + 2 * FRAMES / FPS; i++)); do
    if grep -qaE 'DISTFB (OK|BAD)' distfb.uart 2>/dev/null; then break; fi
    if ! kill -0 "$pid" 2>/dev/null; then break; fi
    sleep 0.5
done
complete=1
if (( CAPTURE )); then
    for ((i=0; i<FRAMES; i++)); do
        if [[ ! -s screenshot_$i.bmp ]]; then complete=0; break; fi
    done
fi
if grep -qa 'DISTFB OK' distfb.uart 2>/dev/null && (( complete )); then
    echo "DISTFB OK: $FRAMES frames at up to $FPS fps in $WORK"
else
    echo "DISTFB failed; inspect $WORK/distfb.out and distfb.uart" >&2
    exit 1
fi
