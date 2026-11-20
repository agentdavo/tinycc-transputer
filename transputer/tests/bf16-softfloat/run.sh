#!/bin/bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
src=$root/transputer/tests/bf16-softfloat
: "${TCC:?}" "${NETLOAD:?}" "${T4_BIN:?}" "${BF16_OUT:?}"
mkdir -p "$BF16_OUT"
format=${FLOAT16_FORMAT:-bf16}
defines=(); [[ $format == f16 ]] && defines=(-DBINARY16)
defines+=(-DMEMORY_PATHS)
printf '1\n' > "$BF16_OUT/memory-paths"
python3 "$src/generate.py" --format "$format" --source "${SOFTFLOAT_SOURCE:-$root/transputer/softfloat-3e}" --out "$BF16_OUT/reference"
work=$(mktemp -d /tmp/bf16-target.XXXXXX)
echo "$work" > "$BF16_OUT/work.path"
for path in software tvx; do
  target=t9000; runtime=("$root/lib/transputer/transputer-f16.c")
  if [[ $path == tvx ]]; then target=t9000tvx; runtime=(); fi
  for header in "$BF16_OUT"/reference/vectors-*.h; do
    part=${header##*/vectors-}; part=${part%.h}
    dir=$work/$path-$part; mkdir -p "$dir"
    cp "$header" "$dir/vectors.h"
    "$TCC" -m"$target" -nostdlib -nostdinc -I"$dir" "${defines[@]}" -DPATH_NAME=\"$format-$path\" -DPART="$part" -o "$dir/test.elf" "$root/transputer/tests/fpu-super/startup.S" "$src/main.c" "${runtime[@]}" > "$dir/build.log" 2>&1
    if grep -Ei 'undefined symbol|unresolved' "$dir/build.log"; then exit 1; fi
    "$NETLOAD" "$dir/test.elf" "$dir/direct.btl" > "$dir/netload.log" 2>&1
    python3 "$root/transputer/tests/fpu-super/link_boot.py" "$dir/direct.btl" "$dir/link.btl" > "$dir/link.json"
    for format in direct link; do
      status=0
      (cd "$dir"; T4_PROTECT=1 T4_EST_CYCLES=2 T4_HEADLESS=1 SDL_VIDEODRIVER=dummy T4_UART_CONSOLE=0 T4_UART_LOG="$dir/$format.uart" timeout -k 2 120 "$T4_BIN" -s9 -si -sm 18 -sb "$dir/$format.btl" > "$dir/$format.log" 2>&1) || status=$?
      echo "$path part=$part $format emulator_exit=$status"
    done
  done
done
cp -a "$work/." "$BF16_OUT/"
python3 "$src/validate.py" "$BF16_OUT"
