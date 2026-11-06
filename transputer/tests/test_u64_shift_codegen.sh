#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TCC="${TCC_BIN:-$ROOT/transputer-tcc}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

for cpu in t414 t800 t9000; do
    "$TCC" -m"$cpu" -c "$ROOT/lib/transputer/transputer-runtime.c" -o "$WORK/runtime.o"
    python3 "$ROOT/transputer/scripts/tpdis.py" "$WORK/runtime.o" __ashldi3 > "$WORK/left"
    python3 "$ROOT/transputer/scripts/tpdis.py" "$WORK/runtime.o" __lshrdi3 > "$WORK/right"
    grep -q 'lshl' "$WORK/left"
    grep -q 'lshr' "$WORK/right"
    readelf -r "$WORK/runtime.o" | awk '
        /^Relocation section/ { text = $3 == "\047.rel.text\047" }
        text { print }
    ' > "$WORK/relocs"
    if grep -Eq '__ashldi3|__lshrdi3' "$WORK/relocs"; then
        echo "$cpu shift helper calls itself" >&2
        exit 1
    fi
done
echo 'Shift helpers use native long shifts without recursive calls.'
