#!/usr/bin/env bash
# The linker's relocation-index fast path and diagnostic path must agree.
set -eu
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TCC="${TCC_BIN:-$ROOT/transputer-tcc}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

cat > "$WORK/one.c" <<'EOF'
static int same(int x) { return x + 11; }
int unique(int x) { return x * 3; }
int (*const local_pointer)(int) = same;
int (*const unique_pointer)(int) = unique;
extern int second(int);
void _start(void) { volatile int result = local_pointer(2) + unique_pointer(3) + second(4); (void)result; }
EOF
cat > "$WORK/two.c" <<'EOF'
static int same(int x) { return x + 17; }
int (*const second_pointer)(int) = same;
int second(int x) { return second_pointer(x); }
EOF
for cpu in t414 t425 t800 t805 t9000 t9000tvx; do
    "$TCC" -m"$cpu" -c -o "$WORK/one.o" "$WORK/one.c"
    "$TCC" -m"$cpu" -c -o "$WORK/two.o" "$WORK/two.c"
    "$TCC" -m"$cpu" -r -nostdlib -o "$WORK/partial.o" "$WORK/one.o" "$WORK/two.o"
    T4_STRICT_UNRESOLVED_CALLS=1 "$TCC" -m"$cpu" -nostdlib -o "$WORK/out.elf" "$WORK/partial.o" 2>"$WORK/normal.err"
    cp "$WORK/out.elf" "$WORK/normal.elf"
    T4_RELOC_FIX_DEBUG=1 T4_STRICT_UNRESOLVED_CALLS=1 "$TCC" -m"$cpu" -nostdlib -o "$WORK/out.elf" "$WORK/partial.o" 2>"$WORK/debug.err"
    cmp "$WORK/normal.elf" "$WORK/out.elf"
done
echo 'Relocation index fast and diagnostic paths agree on six targets.'
