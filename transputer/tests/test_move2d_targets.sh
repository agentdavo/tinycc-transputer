#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TCC="${TCC_BIN:-$ROOT/transputer-tcc}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

for op in init all nonzero zero; do
    printf 'void f(void) { __move2d%s(0, 0, 0); }\n' "$op" > "$WORK/test.c"
    if "$TCC" -mt414 -c "$WORK/test.c" -o "$WORK/test.o" 2>"$WORK/error"; then
        echo "T414 accepted __move2d$op" >&2
        exit 1
    fi
    grep -q "__move2d$op is a T425 instruction" "$WORK/error"
    "$TCC" -mt425 -c "$WORK/test.c" -o "$WORK/test.o"
done
echo 'MOVE2D target diagnostics passed.'
