#!/usr/bin/env bash
# End-to-end controls: animation, tangent-space bump and specular sampling.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../../.." && pwd)
BASE=${WORK:-$(mktemp -d)}; mkdir -p "$BASE"; BASE=$(cd "$BASE" && pwd)
run() {
    local label=$1; shift
    env RENDERER=hellknight-material NODES=1 FRAMES=2 CAPTURE=1 FPS=0 ROTATE=0 WORK="$BASE" "$@" \
        bash "$ROOT/transputer/examples/distributed-fb/run-scale.sh" >"$BASE/$label.log"
    sed -n 's/^SCALE OK: .*: //p' "$BASE/$label.log"
}
full=$(run full); static=$(run static ANIMATE=0); nobump=$(run nobump BUMP=0); nospec=$(run nospec SPECULAR=0)
python3 - "$full" "$static" "$nobump" "$nospec" <<'PY'
from pathlib import Path
import sys
full,static,nobump,nospec=(Path(p) for p in sys.argv[1:])
def image(path,n=0): return (path/f'screenshot_{n}.bmp').read_bytes()
assert image(full)!=image(full,1), 'animation did not change the fixed-rotation frame'
assert image(static)==image(static,1), 'static control changed'
assert image(full)!=image(nobump), 'normal mapping did not change the image'
assert image(full)!=image(nospec), 'specular mapping did not change the image'
print('Hellknight animation and material feature controls OK')
PY
