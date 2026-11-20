#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TCC="${TCC_BIN:-$ROOT/transputer-tcc}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

cat > "$WORK/copy.c" <<'EOF'
void *memcpy(void *, const void *, unsigned);
struct __attribute__((packed)) P { char pad; unsigned length; };
void *aligned(void *d, const void *s, unsigned n) { return memcpy(d, s, n); }
void *packed(void *d, const void *s, struct P *p) { return memcpy(d, s, p->length); }
EOF
"$TCC" -mt800 -c "$WORK/copy.c" -o "$WORK/copy.o"
python3 "$ROOT/transputer/scripts/tpdis.py" "$WORK/copy.o" aligned > "$WORK/aligned"
python3 "$ROOT/transputer/scripts/tpdis.py" "$WORK/copy.o" packed > "$WORK/packed"
grep -Eq '[[:space:]]move$' "$WORK/aligned"
grep -Eq '[[:space:]]call ' "$WORK/packed"
if grep -Eq '[[:space:]]move$' "$WORK/packed"; then
    echo 'packed length used the unsafe inline MOVE' >&2
    exit 1
fi
echo 'Aligned memcpy uses MOVE; packed length uses a call.'
