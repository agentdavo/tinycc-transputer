#!/bin/sh
# The compiler carries verbatim copies of the T4 emulator's TVX semantic
# headers in lib/transputer/tvx/, so it builds without transputer/: tccgen.c
# folds __bf16 constants with tvx.h, and lib/transputer/transputer-tvx.c (software TVX
# for parts without it) is built from all of them.  Run this after changing
# any of them in transputer/t4.
#
#   sh transputer/scripts/sync-tvx-headers.sh          copy t4 -> lib
#   sh transputer/scripts/sync-tvx-headers.sh --check  exit 1 if they differ
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/transputer/t4"
DST="$ROOT/lib/transputer/tvx"
FILES="tvx.h tvx_iq.h tvx_vec.h tvx_gguf_tables.h tvx_gguf_decode.h"

if [ "${1:-}" = --check ]; then
    bad=0
    for f in $FILES; do
        cmp -s "$SRC/$f" "$DST/$f" || { echo "lib/transputer/tvx/$f differs from transputer/t4/$f"; bad=1; }
    done
    [ $bad = 0 ] && echo "lib/transputer/tvx matches transputer/t4"
    exit $bad
fi
for f in $FILES; do
    cp "$SRC/$f" "$DST/$f"
done
echo "copied $FILES"
