#!/usr/bin/env bash
# Compare original source-compiled uber stages with the native reference.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd);ROOT=$(cd "$HERE/../../.." && pwd)
BASE=${WORK:-$(mktemp -d)};mkdir -p "$BASE";BASE=$(cd "$BASE" && pwd)
run() {
    local label=$1;shift
    env RENDERER=techdemo NODES=1 FRAMES=2 CAPTURE=1 FPS=0 WORK="$BASE" "$@" \
        bash "$ROOT/transputer/examples/distributed-fb/run-scale.sh" >"$BASE/$label.log"
    sed -n 's/^SCALE OK: .*: //p' "$BASE/$label.log"
}
native=$(run native VERTEX_VM=1);source=$(run source VERTEX_SOURCE=uber FRAGMENT_SOURCE=uber)
for frame in 0 1;do cmp "$native/screenshot_$frame.bmp" "$source/screenshot_$frame.bmp";done
python3 - "$source/screenshot_0.bmp" <<'PY'
from pathlib import Path
import sys
data=Path(sys.argv[1]).read_bytes();offset=int.from_bytes(data[10:14],'little')
assert not any(data[i:i+3]==b'\xff\x00\xff' for i in range(offset,len(data),3)), 'shader failure marker in frame'
print('original source-compiled uber scene matches native reference')
PY
