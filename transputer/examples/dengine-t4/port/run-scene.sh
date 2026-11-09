#!/usr/bin/env bash
# Runs dEngine's original techDemo scene on NODES emulated T800s.
#   NODES=1|2|4|8|16|32  FRAMES=1..120  CAPTURE=0|1  START_MS  FRAME_MS
#   BAND_ROWS (rows per band, divides 480)  LOW_QUALITY=0|1 (PVRTC set)
#   SHADOWS BUMP SPECULAR STATS CAMERA_PATH (0|1, default 1 1 1 0 1)
#   TIMEOUT (seconds, default 3600)  WORK (parent of the run directory)
#   TCC_DIR / T4_BIN select the toolchain and emulator.
# Prints the run directory; screenshots are screenshot_N.bmp there.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)
R=${TCC_DIR:-$ROOT}; T4=${T4_BIN:-$ROOT/transputer/t4/t4}
NODES=${NODES:-1}; FRAMES=${FRAMES:-2}; CAPTURE=${CAPTURE:-1}
case "$NODES" in 1|2|4|8|16|32) ;; *) echo 'NODES must be 1, 2, 4, 8, 16 or 32' >&2; exit 1;; esac
[[ $FRAMES =~ ^[0-9]+$ ]] && (( 10#$FRAMES >= 1 && 10#$FRAMES <= 120 )) || { echo 'FRAMES must be 1-120' >&2; exit 1; }
if [[ -n ${WORK:-} ]]; then mkdir -p "$WORK"; WORK=$(cd "$WORK" && pwd); WORK=$(mktemp -d "$WORK/scene.XXXXXX"); else WORK=$(mktemp -d); fi
cd "$WORK"
# Objects are built once per toolchain and ROM selection, then reused.
BUILD=${BUILD:-$WORK/build}
if [[ ! -f $BUILD/obj/rom.o || -n ${REBUILD:-} ]]; then bash "$HERE/build.sh" t4 "$BUILD" >build.log 2>&1 || { cat build.log >&2; exit 1; }; fi
DEFS=(-DNNODES="$NODES" -DFRAMES="$FRAMES" -DCAPTURE="$CAPTURE" -DSTART_MS="${START_MS:-0}" -DFRAME_MS="${FRAME_MS:-33}"
      -DBAND_ROWS="${BAND_ROWS:-5}" -DLOW_QUALITY="${LOW_QUALITY:-0}" -DSHADOWS="${SHADOWS:-1}" -DBUMP="${BUMP:-1}"
      -DSPECULAR="${SPECULAR:-1}" -DSTATS="${STATS:-0}" -DCAMERA_PATH="${CAMERA_PATH:-1}")
{
    echo 'Connect HOST to processor 0 link 0'
    for ((n = 1; n < NODES; n++)); do
        echo "Connect processor $(((n - 1) / 2)) link $(((n - 1) % 2 + 1)) to processor $n link 0"
    done
} >scene.map
objs=()
for o in boot ItextureLoader camera commands config dEngine entities filesystem font lexer material math matrix md5anim md5model obj quaternion renderer renderer_progr stats texture timer world libc romfs png pvrtc gles2 dengine_app glsl_fragment_compiler conc rom; do
    objs+=("$BUILD/obj/$o.o")
done
for ((n = 0; n < NODES; n++)); do
    "$R/transputer-tcc" -mt800 -w -I"$HERE/include" -I"$R/include" "${DEFS[@]}" -DNODE="$n" -c -o "node$n.o" "$HERE/node.c" >"n$n.compile.log" 2>&1 || { cat "n$n.compile.log" >&2; exit 1; }
    "$R/transputer-tcc" -mt800 -static -nostdlib -o "n$n.elf" "${objs[0]}" "node$n.o" "${objs[@]:1}" \
        "$R/transputer-libtcc1-t800.a" >"n$n.link.log" 2>&1 || { cat "n$n.link.log" >&2; exit 1; }
    "$R/transputer-netload" "n$n.elf" "n$n.btl" >"n$n.netload.log" 2>&1 || { cat "n$n.netload.log" >&2; exit 1; }
done
export SPYNET="$WORK/scene.map" T4_NODE_BTL="$WORK/n%d.btl"
env -u DISPLAY -u T4_EST_CYCLES T4_UART_CONSOLE=0 T4_UART_LOG="$WORK/scene.uart" T4_PRESENT_ON_FLIP=1 \
    timeout -k 5 "${TIMEOUT:-3600}" "$T4" -s8 -sm 26 -ss -st -sn "-$NODES" -sb "$WORK/n0.btl" 2>&1 | tail -c 1000000 >scene.out &
pid=$!
# $! is the pipeline's tail; the emulator is found by its unique image path.
stop_t4() { pkill -f -- "-sb $WORK/n0.btl" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; }
trap stop_t4 EXIT
while kill -0 "$pid" 2>/dev/null; do
    if grep -qaE 'DENGINE (OK|BAD|FATAL|EXIT)' scene.uart 2>/dev/null; then break; fi
    sleep 1
done
if ! grep -qa 'DENGINE OK' scene.uart 2>/dev/null; then
    grep -aE 'DENGINE (BAD|FATAL|EXIT)' scene.uart >&2 || true
    echo "DENGINE failed: inspect $WORK/scene.uart and scene.out" >&2; exit 1
fi
if (( CAPTURE )); then
    for ((i = 0; i < FRAMES; i++)); do [[ -s screenshot_$i.bmp ]] || { echo "missing screenshot $i in $WORK" >&2; exit 1; }; done
fi
grep -a 'DENGINE' scene.uart
echo "DENGINE OK: $NODES nodes, $FRAMES frames: $WORK"
