#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TCC="${TCC_BIN:-$ROOT/transputer-tcc}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
"$TCC" -mt800 -c "$ROOT/transputer/tests/test_sched_indexed_fp.c" -o "$WORK/test.o"
for fn in plain volatile_index volatile_cast volatile_alias; do
    python3 "$ROOT/transputer/scripts/tpdis.py" "$WORK/test.o" "$fn" > "$WORK/$fn"
done
grep -Eq '[[:space:]]dup$' "$WORK/plain"
grep -Eq '[[:space:]]fpldnlsn$' "$WORK/plain"
for fn in volatile_index volatile_cast volatile_alias; do
    if grep -Eq '[[:space:]]dup$' "$WORK/$fn"; then
        echo "unexpected duplicate-address rewrite in $fn" >&2
        exit 1
    fi
    [ "$(grep -Ec '[[:space:]]fpldnlsni$' "$WORK/$fn")" -ge 2 ]
done
"$TCC" -mt414 -c "$ROOT/transputer/tests/test_sched_indexed_fp.c" -o "$WORK/t414.o"
python3 "$ROOT/transputer/scripts/tpdis.py" "$WORK/t414.o" plain > "$WORK/t414"
if grep -Eq '[[:space:]]dup$' "$WORK/t414"; then
    echo 'T414 cannot execute DUP' >&2
    exit 1
fi
echo 'Nonvolatile index reused; volatile index reads retained.'
