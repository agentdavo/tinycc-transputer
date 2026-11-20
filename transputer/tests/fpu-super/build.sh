#!/bin/bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
src=$root/transputer/tests/fpu-super
out=${FPU_SUPER_OUT:-$root/logs/fpu-super}
cc=${TCC:-$root/transputer-tcc}
netload=${NETLOAD:-$root/transputer-netload}
timing=${FPU_TIMING:-ticks}
case "$timing" in ticks|instructions|t800_model_cycles) ;; *) echo "Invalid FPU_TIMING" >&2; exit 2;; esac
oracle=()
vectors=${FPU_SOFTFLOAT_VECTORS:-$src/softfloat-vectors.json}
case "${FPU_SOFTFLOAT:-0}" in
  0) ;;
  1) oracle=(--softfloat-vectors "$vectors") ;;
  only) oracle=(--softfloat-vectors "$vectors" --softfloat-only) ;;
  *) echo "Invalid FPU_SOFTFLOAT (use 0, 1 or only)" >&2; exit 2;;
esac
mkdir -p "$out"
bf16=${FPU_BF16:-1}
[[ ${FPU_SOFTFLOAT:-0} == only ]] && bf16=0
extra=()
if [[ $bf16 == 1 ]]; then
  python3 - "$root/transputer/tests/bf16-softfloat" <<'PY'
import hashlib,json,sys
from pathlib import Path
p=Path(sys.argv[1]); reference=json.loads((p/'super-reference.json').read_text())
assert hashlib.sha256((p/'super-vectors.h').read_bytes()).hexdigest()==reference['header_sha256'], 'bfloat16 reference header mismatch'
PY
  extra=(-DFPU_BF16 "$src/bf16_super.c" "$root/lib/transputer/transputer-f16.c")
elif [[ $bf16 != 0 ]]; then
  echo "Invalid FPU_BF16 (use 0 or 1)" >&2; exit 2
fi
printf '%s\n' "$timing" > "$out/timing-mode"
cpus=(t805 t9000)
[[ $bf16 == 1 ]] && cpus+=(t9000tvx)
: > "$out/cpu-list"
for cpu in "${cpus[@]}"; do
  [[ $timing == t800_model_cycles && $cpu != t805 ]] && continue
  printf '%s\n' "$cpu" >> "$out/cpu-list"
  work=$out/$cpu
  mkdir -p "$work"
  model=$cpu; [[ $cpu == t9000tvx ]] && model=t9000
  tvx=(); [[ $cpu == t9000tvx ]] && tvx=(-DFPU_BF16_TVX -DMEMORY_PATHS "$src/tvx_bf16_arithmetic.c")
  traps=(); [[ $cpu == t9000tvx ]] && traps=(--tvx-traps)
  python3 "$src/generate.py" "${traps[@]}" --cpu "$model" --out "$work" "${oracle[@]}"
  if [[ $bf16 == 1 ]]; then cp "$root/transputer/tests/bf16-softfloat/super-reference.json" "$work/bf16-manifest.json"; else rm -f "$work/bf16-manifest.json"; fi
  if [[ $cpu == t9000tvx ]]; then
    python3 - "$src" <<'PY'
import hashlib,json,sys
from pathlib import Path
p=Path(sys.argv[1]);d=json.loads((p/'tvx-bf16-reference.json').read_text())
assert hashlib.sha256((p/'tvx-bf16-vectors.h').read_bytes()).hexdigest()==d['header_sha256'], 'TVX arithmetic reference header mismatch'
PY
    cp "$src/tvx-bf16-reference.json" "$work/tvx-bf16-reference.json"
    python3 - "$work/bf16-manifest.json" <<'PY'
import json,sys
from pathlib import Path
p=Path(sys.argv[1]); d=json.loads(p.read_text()); d.update(memory_paths=True,backend="tvx"); p.write_text(json.dumps(d))
PY
  fi
  "$cc" "${tvx[@]}" -m$cpu -nostdlib -nostdinc -I"$work" -DCPU_NAME=\"$cpu\" -DTIMING_UNIT=\"$timing\" -o "$work/fpu-super.elf" "$src/startup.S" "$work/probes.S" "$src/main.c" "${extra[@]}" > "$work/build.log" 2>&1
  if grep -Ei 'undefined symbol|unresolved' "$work/build.log"; then exit 1; fi
  if [[ $cpu == t9000tvx ]]; then python3 "$src/audit_tvx_bf16.py" "$work/fpu-super.elf" --backend tvx; fi
  "$netload" "$work/fpu-super.elf" "$out/fpu-super-$cpu-direct.btl" > "$work/netload.log" 2>&1
  python3 "$src/link_boot.py" "$out/fpu-super-$cpu-direct.btl" "$out/fpu-super-$cpu-link.btl"
done
if [[ ${FPU_SOFTFLOAT:-0} != only && $timing != t800_model_cycles ]]; then
  python3 "$src/audit_opcodes.py" --suite "$out/t9000" --tcc "$cc"
fi
