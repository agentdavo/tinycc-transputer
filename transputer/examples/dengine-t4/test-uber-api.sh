#!/usr/bin/env bash
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd);ROOT=$(cd "$HERE/../../.." && pwd)
R=${TCC_DIR:-$ROOT};T4=${T4_BIN:-$ROOT/transputer/t4/t4};WORK=$(mktemp -d);pid=
trap 'if [[ -n $pid ]]; then kill "$pid" 2>/dev/null || true;wait "$pid" 2>/dev/null || true;fi;rm -rf "$WORK"' EXIT
python3 - "$ROOT/transputer/examples/dEngine/data/shaders" "$WORK/uber_source.h" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]);lines=[]
for name,file in [('vertex_source','v_uber.glsl'),('fragment_source','f_uber.glsl')]:
    lines.append('static const char '+name+'[]={'+','.join(map(str,(p/file).read_bytes()))+',0};')
Path(sys.argv[2]).write_text('\n'.join(lines)+'\n')
PY
D="$ROOT/transputer/examples/distributed-fb"
sources=("$HERE/test_uber_api.c" "$D/tinygl.c" "$D/gles2_subset.c" "$D/gles2_draw.c" "$D/gles2_program.c" "$D/glsl_fragment_compiler.c")
cc -O2 -DTG_GLES_VM_PROGRAM=1 -I"$WORK" -o "$WORK/host" "${sources[@]}";"$WORK/host"
"$R/transputer-tcc" -mt800 -static -nostdlib -I"$R/include" -I"$ROOT/transputer/native-headers" -I"$WORK" -DTG_GLES_VM_PROGRAM=1 -DTG_TARGET_TEST=1 -o "$WORK/test.elf" "$ROOT/transputer/tests/test_boot.S" "${sources[@]}" "$R/transputer-libtcc1-t800.a"
"$R/transputer-netload" "$WORK/test.elf" "$WORK/test.btl" >"$WORK/build.log" 2>&1
env -u DISPLAY T4_UART_CONSOLE=0 T4_UART_LOG="$WORK/test.uart" "$T4" -s8 -sm 26 -ss -sb "$WORK/test.btl" >"$WORK/t4.log" 2>&1 & pid=$!
for ((i=0;i<60;i++));do if grep -qa 'UBER API ' "$WORK/test.uart" 2>/dev/null;then break;fi;if ! kill -0 "$pid" 2>/dev/null;then break;fi;sleep 0.5;done
if ! grep -qa 'UBER API OK' "$WORK/test.uart" 2>/dev/null;then cat "$WORK/test.uart" "$WORK/t4.log" >&2 2>/dev/null || true;exit 1;fi
echo 'uber compile/link T800 tests OK'
