#!/usr/bin/env bash
# Compare each distributed configuration against the one-node result.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
if [[ -n ${WORK:-} ]]; then mkdir -p "$WORK"; base=$(cd "$WORK" && pwd); base=$(mktemp -d "$base/scale-test.XXXXXX"); else base=$(mktemp -d); fi
reference=
for nodes in 1 2 4 8 16 32; do
    output=$(NODES="$nodes" FRAMES=2 CAPTURE=1 FPS=0 WORK="$base" bash "$HERE/run-scale.sh")
    echo "$output"
    path=$(printf '%s\n' "$output" | sed -n 's/^SCALE OK: .*: //p')
    [[ -n $path ]] || { echo 'No output path returned' >&2; exit 1; }
    if [[ -z $reference ]]; then reference=$path; fi
    for frame in 0 1; do
        if ! cmp -s "$reference/screenshot_$frame.bmp" "$path/screenshot_$frame.bmp"; then
            echo "Node count $nodes frame $frame differs from 1-node reference" >&2; exit 1
        fi
    done
done
echo "All six node counts matched the two one-node reference frames: $base"
