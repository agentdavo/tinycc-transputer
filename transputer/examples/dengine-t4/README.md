# dEngine on T4: matrix bridge and compatibility target

This is a small stepping stone towards running Fabien Sanglard's dEngine on
T4. It compiles **the original `src/matrix.c` from the separate dEngine clone**
into each T800 program and uses that code for rotation, camera matrices and
perspective division. The existing T4 software rasterizer, test texture,
four-node transport, and double-buffered display produce the visible cube.
It does **not** run dEngine's renderer, materials, assets, shaders, iOS
application, or Doom 3 scene.

The `hellknight` renderer is a separate geometry preview. It extracts the
active mesh's bind pose from the local checkout at build time (110 joints,
1,656 vertices and 2,626 triangles), then draws it using indexed buffers and
the bounded GLSL subset. It rotates the model with flat, baked lighting; it
does not run dEngine's renderer or its animation, textures, materials or
shadow pass. No upstream model data is committed here.

```sh
RENDERER=hellknight NODES=4 FRAMES=2 CAPTURE=1 \
  bash transputer/examples/distributed-fb/run-scale.sh
RENDERER=hellknight bash transputer/examples/distributed-fb/test-scale.sh
```

`RENDERER=hellknight-material` selects an animated preview. It
generates a 32×32 nearest-sampled diffuse texture from the local original
PNG, interpolates UVs and lighting inputs with perspective correction, and
generates a 64×64 RGBA-packed shadow map on each node for each frame. The
light is orthographic and the scene shows self-shadowing; it has no floor or
other shadow receivers. `SHADOWS=0` disables shadow sampling and generation
for comparisons. This is an explicit C material path, not GLSL execution or
the original dEngine renderer. It reconstructs the 110-joint hierarchy and
skins eight equally spaced frames of `hellknight_idle2.md5anim` offline, with
normals recomputed for each pose. The T4 program linearly interpolates those
pre-skinned poses; it does not calculate skeletal skinning at runtime, and
vertex interpolation is an approximation to interpolating joint transforms.
It uses the original Hellknight normal map and a **substitute** specular map
from the bundled Hellbaron material: the Hellknight material itself specifies
no specular map. Both are downsampled to 32×32. Tangent vectors are derived
from mesh UVs for each baked pose, and the preview approximates both light and
eye direction as world +Z. To avoid undefined negative-base GLSL `pow`, the
preview opts into clamping the specular dot product before the square; strict
fragment-backend callers retain the original undefined-domain behavior.
The preview has no texture filtering, general clipping or ES framebuffer
objects. `ANIMATE=0` restores the bind pose, `ROTATE=0` holds model rotation
fixed, and `BUMP=0` / `SPECULAR=0` disable those material features.
Texture PNG decoding uses Python's standard library and expects the bundled
non-interlaced 8-bit RGB/RGBA format. Generated model and texture data remain in
the run directory.

```sh
RENDERER=hellknight-material NODES=4 FRAMES=2 CAPTURE=1 \
  bash transputer/examples/distributed-fb/run-scale.sh
RENDERER=hellknight-material bash transputer/examples/distributed-fb/test-scale.sh
bash transputer/examples/distributed-fb/test-material.sh
bash transputer/examples/dengine-t4/test-features.sh
```

On the WSL T4 setup, the two captured frames matched across 1, 2, 4, 8, 16
and 32 nodes. A separate one-node capture with shadows disabled differed
from the shadowed image. These checks do not establish image agreement with
the iOS renderer. Each node generates its own shadow map; nodes do not share
or distribute that pass.

The upstream default configuration selects `data/scenes/techDemo.scene`,
which references `data/map/demo.map`, a room made of OBJ meshes, the
Hellknight MD5 mesh and its idle animation. `RENDERER=techdemo` exports the
29 OBJ objects using the map's room transform and renders them around the
animated character, using their original diffuse material assignments. It
reverses OBJ winding as dEngine does and clips room triangles at the near
plane. The 28 distinct diffuse maps are reduced to 16×16 and alpha is
discarded. The character uses the approximate pre-skinned animation and
material path described above. The static room projection is prepared once
per node; every node separately prepares its character-only shadow map.
The room receives that map but does not contribute to the shadow pass.

This is a scene preview: its fixed camera and directional light are chosen
for visibility, not the original camera path or scene light. It does not
implement room bump/specular maps, transparency, animation-driven skinning
on T4, or dEngine's actual GLSL renderer. The scene, mesh and texture
exports are generated from the separate checkout in the run directory.

```sh
RENDERER=techdemo NODES=4 FRAMES=12 CAPTURE=1 \
  bash transputer/examples/distributed-fb/run-scale.sh
RENDERER=techdemo bash transputer/examples/distributed-fb/test-scale.sh
bash transputer/examples/dengine-t4/test-room.sh
bash transputer/examples/dengine-t4/test-scene.sh
python3 transputer/examples/dengine-t4/audit_scene.py
```

In one WSL T4 experiment (12 unpaced, capture-free frames per configuration,
median after discarding the first), root assembly wall times were:

| Nodes | ms/frame |
| ---: | ---: |
| 1 | 556.544 |
| 2 | 548.160 |
| 4 | 504.768 |
| 8 | 454.144 |
| 16 | 347.840 |
| 32 | 176.512 |

Each is one short emulator sample, includes tree transport and root assembly,
and excludes build, capture and pacing. The host exposed 20 logical CPUs;
32 emulated nodes are oversubscribed. These numbers are not physical T800
times or a scaling guarantee. Two captured frames matched byte-for-byte at
all six counts; independent room, animation and static controls passed.

The expected upstream checkout is `transputer/examples/dEngine`, cloned from
`https://github.com/fabiensanglard/dEngine.git`; the checkout used for these
tests was `7f68c628142e69fa088849103a8b9604b701edbc`. It remains a separate Git
repository; no upstream source is copied into this bridge. The upstream matrix
file includes debug-print functions; the bare-metal test supplies a `printf`
stub for those unused functions, rather than linking a full C library.

```sh
# Run a 64-frame animation on four emulated T800s; output directory is printed.
RENDERER=dengine NODES=4 FRAMES=64 FPS=20 CAPTURE=1 \
  bash transputer/examples/distributed-fb/run-scale.sh

# Verify two images agree at 1, 2, 4, 8, 16 and 32 nodes.
RENDERER=dengine bash transputer/examples/distributed-fb/test-scale.sh

# Check the upstream matrix math on a bare-metal T800.
bash transputer/examples/dengine-t4/test-matrix.sh

# List direct GL call names used by the programmable renderer.
python3 transputer/examples/dengine-t4/audit.py
```

`T4_BIN` and `TCC_DIR` select a different emulator/toolchain. On the tested
WSL setup the affinity-enabled binary can be selected with
`T4_BIN=/mnt/c/projects/tinycc_transputer/.tmp-distfb/t4-affinity`; pinning is
optional (`T4_NODE_CPUS=auto`). The bridge produced 64 distinct captured
frames; its 1/2/4/8/16/32-node suite produced byte-identical frames across
node counts. The first four captured frames also matched a host build of the
bridge pixel-for-pixel. Host and T800 tests of the original matrix functions
passed.

## What the real programmable renderer needs

The source's `renderer_progr.c` directly calls shader creation/compilation and
linking, attributes and uniforms, indexed drawing, texture allocation and
upload, and framebuffer/renderbuffer APIs. It loads `data/shaders/v_uber.glsl`
and `f_uber.glsl` with conditional material features. Those shaders use
matrix/vector arithmetic, varyings, `texture2D`, `cross`, `dot`, `normalize`,
`max`, and `pow`; bump and specular maps, plus a color-packed shadow-map pass,
need more than the bridge's fixed C shader. The older engine also has a
separate OpenGL ES 1 fixed-pipeline renderer; that is not the ES 2 target.

The current bridge includes bounded vertex attributes, buffers, indexed
drawing and a deliberately narrow GLSL program compiler. Next steps include
texture objects and sampling, broader GLSL ES 1.00 execution, then
framebuffer/renderbuffer support, shadow passes, texture/asset loading and a
non-iOS platform loop. The name inventory in `audit.py` is a planning aid,
not a conformance score: `glEnable` and `glDisable` currently accept only
scissor state even though dEngine uses other capabilities. The clone contains
an Xcode/iOS application and PVR assets, so a working end-to-end port needs
platform and asset work as well as the GL API. No EGL or general OpenGL ES
2.0 conformance is claimed here.
