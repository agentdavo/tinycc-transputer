#!/usr/bin/env bash
# 32 scissored bands distributed across a binary link tree.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
R=${TCC_DIR:-$ROOT}; T4=${T4_BIN:-$ROOT/transputer/t4/t4}
NODES=${NODES:-4}; FRAMES=${FRAMES:-4}; CAPTURE=${CAPTURE:-1}; FPS=${FPS:-0}
RENDERER=${RENDERER:-tinygl}
case "$RENDERER" in
    tinygl) render_sources=("$HERE/cube.c"); render_include=(); render_flags=() ;;
    glesdraw) render_sources=("$HERE/cube.c" "$HERE/gles2_draw.c"); render_include=(); render_flags=(-DTG_GLES_DRAW=1) ;;
    dengine)
        upstream="$ROOT/transputer/examples/dEngine/src/matrix.c"
        [[ -f $upstream ]] || { echo 'dEngine clone missing: see transputer/examples/dengine-t4/README.md' >&2; exit 1; }
        render_sources=("$ROOT/transputer/examples/dengine-t4/cube_dengine.c" "$upstream" "$ROOT/transputer/examples/dengine-t4/stdio_stub.c") ;;
    glsl)
        upstream="$ROOT/transputer/examples/dEngine/src/matrix.c"
        [[ -f $upstream ]] || { echo 'dEngine clone missing: see transputer/examples/dengine-t4/README.md' >&2; exit 1; }
        render_sources=("$ROOT/transputer/examples/dengine-t4/cube_glsl.c" "$HERE/gles2_draw.c" "$HERE/gles2_program.c" "$upstream" "$ROOT/transputer/examples/dengine-t4/stdio_stub.c") ;;
    gl-uber|gl-uber-fbo)
        render_sources=("$ROOT/transputer/examples/dengine-t4/uber_gl_array.c" "$HERE/gles2_draw.c" "$HERE/gles2_program.c" "$HERE/gles2_vm_bridge.c" "$HERE/gles2_texture.c" "$HERE/glsl_fragment_compiler.c" "$HERE/glsl_fragment_shader.c" "$HERE/glsl_uber_vertex_shader.c" "$HERE/glsl_vm.c" "$HERE/gles2_fragment.c" "$HERE/material_raster.c" "$HERE/material_clip.c")
        render_include=();render_flags=(-DTG_GLES_VM_PROGRAM=1 -DTEST_LARGE_WORKSPACE=1)
        if [[ $RENDERER == gl-uber-fbo ]]; then render_flags+=(-DTG_GLES_FRAMEBUFFER=1);render_sources+=("$HERE/gles2_framebuffer.c");fi ;;
    hellknight)
        upstream="$ROOT/transputer/examples/dEngine/src/matrix.c"
        [[ -f $upstream ]] || { echo 'dEngine clone missing: see transputer/examples/dengine-t4/README.md' >&2; exit 1; }
        render_sources=("$ROOT/transputer/examples/dengine-t4/hellknight.c" "$HERE/gles2_draw.c" "$HERE/gles2_program.c" "$upstream" "$ROOT/transputer/examples/dengine-t4/stdio_stub.c") ;;
    hellknight-material)
        render_sources=("$ROOT/transputer/examples/dengine-t4/hellknight_material.c" "$HERE/material_raster.c" "$HERE/gles2_fragment.c" "$HERE/shadow_map.c"); render_include=(); render_flags=()
        if [[ ${SHADOWS:-1} == 0 ]]; then render_flags+=(-DTG_DISABLE_SHADOW=1); fi
        if [[ ${BUMP:-1} == 0 ]]; then render_flags+=(-DTG_DISABLE_BUMP=1); fi
        if [[ ${SPECULAR:-1} == 0 ]]; then render_flags+=(-DTG_DISABLE_SPECULAR=1); fi
        if [[ ${ANIMATE:-1} == 0 ]]; then render_flags+=(-DTG_DISABLE_ANIMATION=1); fi
        if [[ ${ROTATE:-1} == 0 ]]; then render_flags+=(-DTG_DISABLE_ROTATION=1); fi ;;
    techdemo)
        render_sources=("$ROOT/transputer/examples/dengine-t4/hellknight_material.c" "$ROOT/transputer/examples/dengine-t4/demo_room.c" "$HERE/material_raster.c" "$HERE/gles2_fragment.c" "$HERE/gles2_uber_vertex.c" "$HERE/shadow_map.c"); render_include=(); render_flags=(-DTG_DEMO_ROOM=1)
        if [[ ${ROOM:-1} == 0 ]]; then render_flags+=(-DTG_DISABLE_ROOM_DRAW=1); fi
        if [[ ${CHAR_SHADOW:-1} == 0 ]]; then render_flags+=(-DTG_DISABLE_CHARACTER_SHADOW=1); fi
        if [[ ${ROOM_SHADOW:-1} == 0 ]]; then render_flags+=(-DTG_DISABLE_ROOM_SHADOW=1); fi
        if [[ ${ROOM_BUMP:-1} == 0 ]]; then render_flags+=(-DTG_DISABLE_ROOM_BUMP=1); fi
        if [[ ${ROOM_SPECULAR:-1} == 0 ]]; then render_flags+=(-DTG_DISABLE_ROOM_SPECULAR=1); fi
        if [[ ${VERTEX_SOURCE:-0} == uber ]]; then render_flags+=(-DTG_DEMO_VERTEX_SOURCE=1); render_sources+=("$HERE/glsl_uber_vertex_shader.c"); fi
        if [[ ${VERTEX_VM:-0} == 1 && ${VERTEX_SOURCE:-0} != uber ]]; then render_flags+=(-DTG_DEMO_VERTEX_VM=1); render_sources+=("$HERE/gles2_uber_vm.c"); fi
        if [[ ${FRAGMENT_SOURCE:-0} == 1 || ${FRAGMENT_SOURCE:-0} == uber ]]; then
            render_flags+=(-DTG_DEMO_FRAGMENT_SOURCE=1)
            if [[ ${FRAGMENT_SOURCE:-0} == uber ]]; then render_flags+=(-DTG_DEMO_FRAGMENT_UBER=1); fi
            render_sources+=("$HERE/glsl_fragment_shader.c")
        fi
        if [[ ${VERTEX_SOURCE:-0} == uber || ${FRAGMENT_SOURCE:-0} == 1 || ${FRAGMENT_SOURCE:-0} == uber ]]; then render_sources+=("$HERE/glsl_fragment_compiler.c"); fi
        if [[ ${VERTEX_SOURCE:-0} == uber || ${VERTEX_VM:-0} == 1 || ${FRAGMENT_SOURCE:-0} == 1 || ${FRAGMENT_SOURCE:-0} == uber ]]; then render_sources+=("$HERE/glsl_vm.c"); fi
        if [[ ${ANIMATE:-1} == 0 ]]; then render_flags+=(-DTG_DISABLE_ANIMATION=1); fi ;;
    *) echo 'Unknown RENDERER' >&2; exit 1 ;;
esac
if [[ $RENDERER == dengine || $RENDERER == glsl || $RENDERER == hellknight ]]; then render_include=(-I"$ROOT/transputer/native-headers"); render_flags=(); fi
case "$NODES" in 1|2|4|8|16|32) ;; *) echo 'NODES must be 1, 2, 4, 8, 16, or 32' >&2; exit 1;; esac
[[ $FRAMES =~ ^[0-9]+$ ]] && (( 10#$FRAMES >= 1 && 10#$FRAMES <= 120 )) || { echo 'FRAMES must be 1–120' >&2; exit 1; }
[[ $FPS =~ ^[0-9]+$ ]] && (( 10#$FPS <= 60 )) || { echo 'FPS must be 0–60' >&2; exit 1; }
[[ $CAPTURE == 0 || $CAPTURE == 1 ]] || { echo 'CAPTURE must be 0 or 1' >&2; exit 1; }
FRAMES=$((10#$FRAMES)); FPS=$((10#$FPS))
if [[ -n ${WORK:-} ]]; then mkdir -p "$WORK"; WORK=$(cd "$WORK" && pwd); WORK=$(mktemp -d "$WORK/scale.XXXXXX"); else WORK=$(mktemp -d); fi
cd "$WORK"
if [[ $RENDERER == gl-uber || $RENDERER == gl-uber-fbo ]]; then
    python3 - "$ROOT/transputer/examples/dEngine/data/shaders" "$WORK/uber_gl_sources.h" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]);lines=[]
for name,file in [('vertex_uber_source','v_uber.glsl'),('fragment_uber_source','f_uber.glsl')]:
    lines.append('static const char '+name+'[]={'+','.join(map(str,(p/file).read_bytes()))+',0};')
Path(sys.argv[2]).write_text('\n'.join(lines)+'\n')
PY
    render_include=(-I"$WORK")
    python3 - "$ROOT/transputer/examples/dEngine/src/renderer_progr.c" "$WORK/upstream_obj_draw.h" <<'PY'
from pathlib import Path
import sys
text=Path(sys.argv[1]).read_text();marker='void RenderEntitiesOBJ(void* objVoid)'
start=text.index(marker);opening=text.index('{',start);depth=0
for i in range(opening,len(text)):
    if text[i]=='{': depth+=1
    elif text[i]=='}':
        depth-=1
        if depth==0:
            Path(sys.argv[2]).write_text(text[start:i+1]+'\n');break
else: raise SystemExit('unterminated upstream OBJ draw function')
PY
fi
if [[ $RENDERER == hellknight || $RENDERER == hellknight-material || $RENDERER == techdemo ]]; then
    CHAR_TEXTURE_SIZE=${CHAR_TEXTURE_SIZE:-32};ROOM_TEXTURE_SIZE=${ROOM_TEXTURE_SIZE:-16}
    [[ $CHAR_TEXTURE_SIZE =~ ^[0-9]+$ && $ROOM_TEXTURE_SIZE =~ ^[0-9]+$ ]] || { echo 'texture sizes must be integers' >&2;exit 1; }
    CHAR_TEXTURE_SIZE=$((10#$CHAR_TEXTURE_SIZE));ROOM_TEXTURE_SIZE=$((10#$ROOM_TEXTURE_SIZE))
    (( CHAR_TEXTURE_SIZE>=1 && CHAR_TEXTURE_SIZE<=64 && ROOM_TEXTURE_SIZE>=1 && ROOM_TEXTURE_SIZE<=32 )) || { echo 'texture sizes exceed the T4 preview bounds (character 1..64, room 1..32)' >&2;exit 1; }
    texture_args=()
    if [[ $RENDERER == hellknight-material || $RENDERER == techdemo ]]; then texture_args=(--texture "$ROOT/transputer/examples/dEngine/data/textures/monsters/hellknight.png" --normal-map "$ROOT/transputer/examples/dEngine/data/textures/monsters/hellknight_local.png" --specular-map "$ROOT/transputer/examples/dEngine/data/textures/monsters/hellBaronSpec.png" --animation "$ROOT/transputer/examples/dEngine/data/models/monsters/hellKnight/hellknight_idle2.md5anim"); fi
    python3 "$ROOT/transputer/examples/dengine-t4/export_hellknight.py" \
        "$ROOT/transputer/examples/dEngine/data/models/monsters/hellKnight/hellknight.pvr.2bpp.md5mesh" "$WORK/hellknight_asset.h" --texture-size "$CHAR_TEXTURE_SIZE" "${texture_args[@]}"
    render_include+=(-I"$WORK")
    render_flags+=(-DTEST_LARGE_WORKSPACE=1)
    if [[ $RENDERER == techdemo ]]; then PYTHONDONTWRITEBYTECODE=1 python3 "$ROOT/transputer/examples/dengine-t4/export_room.py" "$ROOT/transputer/examples/dEngine" "$WORK/room_asset.h" --texture-size "$ROOM_TEXTURE_SIZE"; fi
    if [[ $RENDERER == techdemo && ${FRAGMENT_SOURCE:-0} == uber ]]; then
        python3 - "$ROOT/transputer/examples/dEngine/data/shaders/f_uber.glsl" "$WORK/fragment_uber_source.h" <<'PY'
from pathlib import Path
import sys
data=Path(sys.argv[1]).read_bytes()
Path(sys.argv[2]).write_text('static const char fragment_uber_source[]={'+','.join(map(str,data))+',0};\n')
PY
    fi
    if [[ $RENDERER == techdemo && ${VERTEX_SOURCE:-0} == uber ]]; then
        python3 - "$ROOT/transputer/examples/dEngine/data/shaders/v_uber.glsl" "$WORK/vertex_uber_source.h" <<'PY'
from pathlib import Path
import sys
data=Path(sys.argv[1]).read_bytes()
Path(sys.argv[2]).write_text('static const char vertex_uber_source[]={'+','.join(map(str,data))+',0};\n')
PY
    fi
fi
{
    echo 'Connect HOST to processor 0 link 0'
    for ((n=1;n<NODES;n++)); do
        parent=$(((n-1)/2)); link=$(((n-1)%2+1))
        echo "Connect processor $parent link $link to processor $n link 0"
    done
} >scale.map
for ((n=0;n<NODES;n++)); do
    "$R/transputer-tcc" -mt800 -static -nostdlib -I"$R/include" "${render_include[@]}" "${render_flags[@]}" -DNODE="$n" -DNNODES="$NODES" -DFRAMES="$FRAMES" -DFPS="$FPS" -DCAPTURE="$CAPTURE" \
        -o "n$n.elf" "$ROOT/transputer/tests/test_boot.S" "$HERE/scale.c" "$HERE/tinygl.c" "$HERE/gles2_subset.c" "${render_sources[@]}" \
        "$R/lib/transputer/transputer-conc.c" "$R/transputer-libtcc1-t800.a" >"n$n.compile.log" 2>&1 || { cat "n$n.compile.log" >&2; exit 1; }
    "$R/transputer-netload" "n$n.elf" "n$n.btl" >"n$n.netload.log" 2>&1
done
export SPYNET="$WORK/scale.map" T4_NODE_BTL="$WORK/n%d.btl"
env -u DISPLAY -u T4_EST_CYCLES T4_UART_CONSOLE=0 T4_UART_LOG="$WORK/scale.uart" T4_PRESENT_ON_FLIP=1 \
    "$T4" -s8 -sm 22 -ss -st -sn "-$NODES" -sb "$WORK/n0.btl" >scale.out 2>&1 &
pid=$!
trap 'kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true' EXIT
for ((i=0;i<360;i++)); do
    if grep -qaE 'SCALE (OK|BAD)' scale.uart 2>/dev/null; then break; fi
    if ! kill -0 "$pid" 2>/dev/null; then break; fi
    sleep 0.5
done
if ! grep -qa 'SCALE OK' scale.uart 2>/dev/null; then
    echo "SCALE failed: inspect $WORK/scale.uart and scale.out" >&2; exit 1
fi
if (( CAPTURE )); then
    for ((i=0;i<FRAMES;i++)); do [[ -s screenshot_$i.bmp ]] || { echo "missing screenshot $i in $WORK" >&2; exit 1; }; done
fi
echo "SCALE OK: $NODES nodes, $FRAMES frames: $WORK"
python3 "$HERE/scale_summary.py" "$WORK/scale.uart"
