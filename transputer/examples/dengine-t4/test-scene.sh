#!/usr/bin/env bash
# Integration controls distinguish the animated character and original room.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../../.." && pwd)
BASE=${WORK:-$(mktemp -d)}; mkdir -p "$BASE"; BASE=$(cd "$BASE" && pwd)
run() {
    local label=$1; shift
    env RENDERER=techdemo NODES=1 FRAMES=2 CAPTURE=1 FPS=0 WORK="$BASE" "$@" \
        bash "$ROOT/transputer/examples/distributed-fb/run-scale.sh" >"$BASE/$label.log"
    sed -n 's/^SCALE OK: .*: //p' "$BASE/$label.log"
}
full=$(run scene); empty=$(run empty ROOM=0); static=$(run static ANIMATE=0); no_shadow=$(run no_shadow ROOM_SHADOW=0); no_bump=$(run no_bump ROOM_BUMP=0)
python3 - "$full" "$empty" "$static" "$no_shadow" "$no_bump" <<'PY'
from pathlib import Path
import sys
scene,empty,static,no_shadow,no_bump=(Path(p) for p in sys.argv[1:])
def image(p,n=0): return (p/f'screenshot_{n}.bmp').read_bytes()
assert image(scene)!=image(scene,1), 'animation did not change the image'
assert image(static)==image(static,1), 'static scene control changed'
assert image(scene)!=image(empty), 'room did not change the image'
assert image(scene)!=image(no_shadow), 'room shadow casting did not change the image'
assert image(scene)!=image(no_bump), 'room normal mapping did not change the image'
data=image(scene); offset=int.from_bytes(data[10:14], 'little')
assert not any(data[i:i+3]==b'\xff\x00\xff' for i in range(offset,len(data),3)), 'material error color in scene'
print('room and animated-character controls OK')
PY
