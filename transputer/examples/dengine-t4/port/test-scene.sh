#!/usr/bin/env bash
# Runs the scene at 1, 2, 4, 8, 16 and 32 nodes and requires every captured
# frame to match the one-node frame byte for byte; then checks the one-node
# frames against the host build of the same code.
#   NODE_COUNTS (default "1 2 4 8 16 32"), FRAMES (default 2), START_MS,
#   and the run-scene.sh settings, which every run shares.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FRAMES=${FRAMES:-2}
if [[ -n ${WORK:-} ]]; then mkdir -p "$WORK"; base=$(cd "$WORK" && pwd); base=$(mktemp -d "$base/scene-test.XXXXXX"); else base=$(mktemp -d); fi
export BUILD=${BUILD:-$base/build}
bash "$HERE/build.sh" t4 "$BUILD" >"$base/build-t4.log" 2>&1 || { cat "$base/build-t4.log" >&2; exit 1; }
reference=
for nodes in ${NODE_COUNTS:-1 2 4 8 16 32}; do
    output=$(NODES="$nodes" FRAMES="$FRAMES" CAPTURE=1 WORK="$base" bash "$HERE/run-scene.sh")
    echo "$output"
    path=$(printf '%s\n' "$output" | sed -n 's/^DENGINE OK: .*: //p')
    [[ -n $path ]] || { echo 'No output path returned' >&2; exit 1; }
    [[ -n $reference ]] || reference=$path
    for ((f = 0; f < FRAMES; f++)); do
        cmp -s "$reference/screenshot_$f.bmp" "$path/screenshot_$f.bmp" || { echo "$nodes nodes: frame $f differs from the first run" >&2; exit 1; }
    done
done
# The host build runs the same sources with the same software libm.
bash "$HERE/build.sh" host "$base/host" >"$base/build-host.log" 2>&1 || { cat "$base/build-host.log" >&2; exit 1; }
args=(--frames "$FRAMES" --start "${START_MS:-0}" --frame-ms "${FRAME_MS:-33}")
[[ ${LOW_QUALITY:-0} == 1 ]] && args+=(--low)
[[ ${SHADOWS:-1} == 0 ]] && args+=(--no-shadow)
[[ ${BUMP:-1} == 0 ]] && args+=(--no-bump)
[[ ${SPECULAR:-1} == 0 ]] && args+=(--no-spec)
[[ ${STATS:-0} == 1 ]] && args+=(--stats)
[[ ${CAMERA_PATH:-1} == 0 ]] && args+=(--no-camera)
(cd "$base/host" && ./dengine-host --rom rom.bin --out host "${args[@]}" >host.log)
for ((f = 0; f < FRAMES; f++)); do
    python3 "$HERE/compare_frames.py" "$base/host/host_$(printf %03d "$f").ppm" "$reference/screenshot_$f.bmp"
done
echo "All node counts matched the first run, which matched the host build: $base"
