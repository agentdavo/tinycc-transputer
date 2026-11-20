#!/usr/bin/env bash
# Frame times of the scene at several node counts. t4's timer is the host's
# clock, so other load on the host shows up in single frames: each count
# runs FRAMES frames and the fastest after the first is reported, with its
# shadow-map gather and node 0's band collection (ticks of 64 us).
#   NODE_COUNTS (default "1 2 4 8 16 32"), FRAMES (default 6), and the
#   run-scene.sh settings.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FRAMES=${FRAMES:-6}
if [[ -n ${WORK:-} ]]; then mkdir -p "$WORK"; base=$(cd "$WORK" && pwd); base=$(mktemp -d "$base/timing.XXXXXX"); else base=$(mktemp -d); fi
export BUILD=${BUILD:-$base/build}
bash "$HERE/build.sh" t4 "$BUILD" >"$base/build.log" 2>&1 || { cat "$base/build.log" >&2; exit 1; }
printf '%6s %10s %8s %8s %8s %8s\n' nodes ticks speedup render gather bands
one=
for nodes in ${NODE_COUNTS:-1 2 4 8 16 32}; do
    out=$(NODES="$nodes" FRAMES="$FRAMES" CAPTURE=0 WORK="$base" bash "$HERE/run-scene.sh")
    best=$(printf '%s\n' "$out" | awk '/^DENGINE FRAME/ && $3 != 0 {
        for (i = 1; i < NF; i++) v[$i] = $(i + 1)
        if (min == "" || v["ticks"] < min) { min = v["ticks"]; line = v["ticks"] " " v["render"] " " v["gather"] " " v["bands"] }
    } END { print line }')
    read -r ticks render gather bands <<<"$best"
    [[ -n $one ]] || one=$ticks
    printf '%6s %10s %8s %8s %8s %8s\n' "$nodes" "$ticks" "$(awk -v a="$one" -v b="$ticks" 'BEGIN { printf "%.2f", a / b }')" "$render" "$gather" "$bands"
done
