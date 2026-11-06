#!/usr/bin/env bash
# Test Linux bFLT address placement with each Transputer code-generation target.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TCC=${TCC:-$ROOT/transputer-tcc}
ELF2FLT=${ELF2FLT:-$ROOT/transputer-elf2flt}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cat > "$WORK/probe.c" <<'C'
volatile unsigned char data[64];
volatile unsigned char * volatile pointer = data;
volatile unsigned char * volatile gate = (unsigned char *)0x80000060u;
void _start(void) { *pointer = 1; if (*gate) *pointer = 2; for (;;) {} }
C
"$TCC" -mt414 -nostdlib -o "$WORK/unsafe.elf" "$WORK/probe.c"
if "$ELF2FLT" "$WORK/unsafe.elf" "$WORK/unsafe.flt" >"$WORK/unsafe.log" 2>&1; then
    echo 'FAIL: accepted a T414 image overlapping the syscall gate' >&2; exit 1
fi
grep -q 'overlaps the Linux syscall gate' "$WORK/unsafe.log"
# This layout can contain a legitimate image symbol at the gate address too:
# reject the ambiguity, rather than silently exempting that numeric value.
"$TCC" -mt414 -nostdlib -Wl,-Ttext=80000060 -o "$WORK/overlap.elf" "$WORK/probe.c"
if "$ELF2FLT" "$WORK/overlap.elf" "$WORK/overlap.flt" >"$WORK/overlap.log" 2>&1; then
    echo 'FAIL: accepted an image beginning at the syscall gate' >&2; exit 1
fi
grep -q 'overlaps the Linux syscall gate' "$WORK/overlap.log"
for cpu in t414 t425 t800 t805 t9000 t9000tvx; do
    placement=()
    [ "$cpu" != t414 ] || placement=(-Wl,-Ttext=80000070)
    "$TCC" -m"$cpu" -nostdlib "${placement[@]}" -o "$WORK/$cpu.elf" "$WORK/probe.c"
    "$ELF2FLT" "$WORK/$cpu.elf" "$WORK/$cpu.flt" >"$WORK/$cpu.log" 2>&1
    python3 - "$WORK/$cpu.elf" "$WORK/$cpu.flt" <<'PY'
import struct, sys
elf, flt = (open(p, 'rb').read() for p in sys.argv[1:])
assert struct.unpack_from('<I', elf, 24)[0] == 0x80000070, 'incorrect ELF entry address'
assert flt[:4] == b'bFLT', 'invalid bFLT'
reloc_start, reloc_count = struct.unpack_from('>II', flt, 28)
assert reloc_count > 0 and reloc_start + 4 * reloc_count <= len(flt), 'missing bFLT relocations'
# The data word holding the fixed gate must remain unchanged in the image.
assert struct.pack('<I', 0x80000060) in flt[64:reloc_start], 'fixed gate address changed'
PY
    printf 'PASS %s safe bFLT layout\n' "$cpu"
done
