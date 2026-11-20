# dEngine on T4

## The original renderer (`port/`)

`port/` runs Fabien Sanglard's dEngine itself on emulated T800s: its
unmodified `src/*.c` from the separate checkout (all of it except
`renderer_fixed.c`, the OpenGL ES 1.1 path), with the original scene, map,
OBJ and MD5 loaders, CPU skeletal animation, camera-path playback and
`renderer_progr.c` drawing with the original GLSL uber, shadow-generator and
text shaders. Nothing upstream is copied or patched; the port supplies what
the iOS application and its system libraries supplied:

| File | Replaces |
|---|---|
| `libc.c`, `include/` | the C library: stdio over a read-only ROM, printf/scanf, strtod, a heap, a software libm |
| `mkrom.py`, `romfs.c`, `rom.S` | the application bundle: the files the scene references, linked into every image |
| `png.c` | UIImage/CoreGraphics PNG decoding (premultiplied RGBA, rows top first) |
| `pvrtc.c` | the GPU's PVRTC 2/4bpp decoding, for `glCompressedTexImage2D` |
| `gles2.c`, `include/OpenGLES/` | OpenGL ES 2.0: textures, FBOs and renderbuffers, buffers, shaders and programs, clipping, rasterisation, depth, blending |
| `dengine_app.c` | `EAGLView.m`: start-up, the PNG loader hook, the per-frame call |
| `node.c`, `boot.S`, `host_main.c` | the device: a T4 node driver, or a host driver for the same code |

Shaders go through a preprocessor (`#define`/`#undef`/`#ifdef` and `#if`
on `defined`) and the bounded compiler in `../distributed-fb`
(`glsl_fragment_compiler.c`, unchanged). At link time each stage is
decoded into scalar operations on its register file, its helper functions
inlined, then copy propagation and dead-code elimination: the uber fragment
shader runs 62 operations per pixel (82 compiler instructions), the shadow
generator 26.

Start-up follows the iOS view: the ES 2.0 renderer, the user defaults
(high material quality; shadow, bump and specular mapping on; statistics
off), then the window framebuffer bound, as `createFramebuffer` did after
`dEngine_Init` had left the shadow FBO bound. Each frame follows
`-drawView:` with `PLAY_VIDEO`: the first frame starts
`data/cameraPath/fps.cp`, then `dEngine_HostFrame()` runs. The clock is a
frame clock (`FRAME_MS`, 33 ms), so every node computes the same frame;
`START_MS` moves the engine's clock on after the camera path has set it.
The buffer is the iPhone's 320x480 portrait one; node 0 shows it turned
to landscape, 480x320, as the phone was held.

```sh
# Host build of the same code: PPM frames, quick checks
bash transputer/examples/dengine-t4/port/build.sh host /tmp/dh
(cd /tmp/dh && ./dengine-host --rom rom.bin --out f --frames 2)

# NODES emulated T800s; screenshots in the printed run directory
NODES=8 FRAMES=4 START_MS=9000 bash transputer/examples/dengine-t4/port/run-scene.sh

# 1, 2, 4, 8, 16 and 32 nodes must give identical frames, equal to the host's
bash transputer/examples/dengine-t4/port/test-scene.sh
```

`TCC_DIR` selects the toolchain (it needs 9f993df1, below), `LOW_QUALITY=1`
the PVRTC material set, `SHADOWS`/`BUMP`/`SPECULAR`/`STATS`/`CAMERA_PATH`
the settings, `BAND_ROWS` the band height and `PROFILE=N` an IPtr profile
(`profile.py`). `test_pvrtc.c` checks the PVRTC decoder against the PNGs
the PVR files were made from (mean error 1-7 levels on diffuse maps), and
`compose_bands.py` checks host frames drawn by separate "nodes".

**Verification.** On one T800 the captured frames match the host build
pixel for pixel, and every node count gives the same bytes; the host and
T800 builds share the libc and libm, and the compilers keep the same IEEE
operations (no contraction), so this compares the transputer compiler with
gcc on the whole engine. Each optimisation below was checked to leave the
frames bit-identical on the host, across quality, shadow, bump, specular
and statistics settings.

**Distribution.** The window is cut into 5-row bands; node n draws bands
t with t % N == n and sends them, checksummed, up a binary link tree.
The shadow map is split the same way: `deGlDistributeFramebuffer` limits a
node to its rows of the shadow FBO, and when dEngine unbinds the FBO the
GL calls the node's gather, which sends its rows up the tree and receives
the whole map back, before the main pass samples it - what a multi-GPU
driver does with a shared render target. Each node first runs only the
position slice of the vertex program, and gives the rest only to triangles
that face the camera and cover one of its rows. Rasterisation is
deterministic per row (spans from fixed edge formulas, values from plane
equations at each row's first pixel), which is what makes every node count
produce the same image.

**Performance.** On one node, frame 1 took about 290k ticks (64 us, t4's
host clock) with the first working interpreter, 164k once the code was
decoded to scalar operations, and 112-136k with copy propagation, lazy
vertex shading and the texture fast path. t4's cycle model put one frame,
before the texture fast path, at about 0.58G estimated T800 cycles: about
half a minute for a 20 MHz T800. A profile of that node (`PROFILE=150000`): the shader interpreter
49%, texture sampling 28%, rasterisation 11%, pixel writes 8%; dEngine's
own code - MD5 skinning, camera, scene update - under 0.01%.

`timing.sh`, fastest of frames 1-5 (ticks; render, gather and band
collection on node 0):

| Nodes | Ticks | Speed-up | Render | Gather | Bands |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 135665 | 1.00 | 134778 | 0 | 887 |
| 2 | 75586 | 1.79 | 72794 | 1898 | 2792 |
| 4 | 44466 | 3.05 | 42418 | 3444 | 2045 |
| 8 | 32738 | 4.14 | 29080 | 3807 | 3658 |
| 16 | 28471 | 4.77 | 21805 | 3513 | 6660 |
| 32 | 28613 | 4.74 | 17625 | 4024 | 10985 |

These are host-time samples on a busy machine: the same build measured
42.8k ticks at 4 nodes and 23.2k at 16 in a quieter run. The emulator had
20 host cores, so 32 node threads get about 0.6 of a core each; at 32
nodes the frame time is the host's total CPU, not the program (node 0's
own render still falls from 21.8k to 17.6k, and its wait for the other
nodes' bands grows). What the program itself repeats on every node is
now small: positions for all vertices, the culling and row tests per
triangle, and dEngine's frame update. The shadow-map exchange costs
2-4k ticks. `CYCLES=1` cannot replace host time here: t4 counts cycles per
host thread, and in a threaded network those are not one node's.

t4 does not model T800 link speed (its threaded links copy memory). Per
frame, the shadow map (1.38 MB) goes down every tree link and the bands
(614 KB) converge on node 0; at a T800 link's 20 Mbit/s, about 1.7 MB/s,
the map alone is about 0.8 s per link, so on real hardware the exchange
would dominate long before 32 nodes.

**Memory.** A node is a 64 MiB T800 (`t4 -sm 26`): the image with its
4.3 MB ROM, a heap that skips t4's framebuffer window at 0x81E00000 (peak
about 25 MB, of which 12.8 MB is dEngine's transient OBJ vertex cache), and
the workspace 1 MiB below the top of RAM. t4 enters a BTL with WPtr at the
end of the image, so `boot.S` sets the workspace absolutely.

**Differences from the iPhone.** Colour is RGBA8 (the view was RGB565),
depth is a 32-bit float (the shadow FBO asked for 16 bits), and textures
are sampled at level 0 with the magnification filter (dEngine creates no
mipmaps). The shader compiler accepts only what dEngine's shaders need;
points and lines are not drawn and there is no stencil buffer. GLSL's
undefined `pow` of a negative base gives 0, as on GPUs that clamp.
Two upstream quirks are handled in the platform: `FS_OpenFile` callocs a
file's exact size and `LoadShader` passes it to `glShaderSource` as a
C string, so `calloc` adds a zero byte; `E_Sys_Milliseconds` takes a zero
seconds count for "not started", so the clock starts at 1000 s. The low
quality material list names `jumbodoor_s.png.pvr`, which the checkout lacks;
the engine reports it, as it would on the phone.

**A compiler bug it found.** dEngine's `gluLookAt` stores
`- DotProduct(vEye, vU)` through a pointer. `tp_stmt_end` took the
negation's `mint; xor` for address code and stored the sum unnegated, so
the light's matrix was wrong and the shadow pass drew nothing. Fixed in
9f993df1 (`transputer/tests/test_fp_neg_store.c`).

## Earlier previews (before the port)

The matrix bridge was a small stepping stone towards running Fabien
Sanglard's dEngine on T4. It compiles **the original `src/matrix.c` from the separate dEngine clone**
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
animated character, using their original diffuse, normal and specular material assignments. It
reverses OBJ winding as dEngine does and clips room triangles at the near
plane. Its 51 distinct maps default to 16×16; original alpha is retained.
`ROOM_TEXTURE_SIZE` can select 1–32, and `CHAR_TEXTURE_SIZE` can select 1–64
(default 32), within the current T4 preview memory budget. These sizes are
still downsampled from the original assets and the conversion happens offline.
Material-marked transparent triangles use depth-tested straight-alpha blending
after opaque geometry, sorted by average triangle depth. This approximation
does not solve intersecting translucent geometry. Tangents are generated per
triangle and light/eye direction is approximated by +Z. The character uses the approximate pre-skinned animation and
material path described above. The static room projection is prepared once
per node; every node separately prepares the 64×64 shadow map from the
character and opaque room geometry. Translucent room materials do not cast
shadows; alpha-tested shadow generation is not implemented.

This is a scene preview: its fixed camera and directional light are chosen
for visibility, not the original camera path or scene light. It does not
implement animation-driven skinning on T4 or dEngine's actual GLSL renderer.
The scene, mesh and texture
exports are generated from the separate checkout in the run directory.
The default `glCompileShader` subset rejects the uber shaders. In an opt-in
`TG_GLES_VM_PROGRAM` build, it uses the experimental compiler; `glLinkProgram`
checks varying interfaces, and the programmable draw bridge binds supported
uniforms, attributes and texture units. A separate explicit path can also run the
cloned `v_uber.glsl` and `f_uber.glsl` sources in the character preview:
use `VERTEX_SOURCE=uber FRAGMENT_SOURCE=uber`. The three dEngine feature flags
are resolved by a bounded preprocessor. Host and T800 tests compile all eight
feature combinations and compare their outputs with the native reference,
including lit and shadowed fragment inputs. The scene uses bump, specular and
shadow options together. Its source-compiled frames match the native reference.
The fragment shader's undefined negative-base `pow` inputs are clamped in the
preview, matching the existing native preview's policy.

The character's `techdemo` vertex path now calls the native
`gles2_uber_vertex` implementation of the source shader's matrix transform,
`cross`, `dot` and `normalize` operations (using T800 `__fpsqrt`), including
its different bump-mapped and unbumped half-vector formulas. It produces
the interpolated lighting and shadow inputs consumed by the existing material
backend. Zero-length normalization reports an undefined result; the demo
uses a deterministic ambient-only fallback for such vertices. This is a C
specialization, not compilation or execution of the GLSL source. The room
still uses its own approximate tangent-space vertex setup.

`VERTEX_VM=1` runs a hand-translated uber vertex instruction sequence through
the experimental vector interpreter instead of the native specialization.
Its eight feature combinations have been compared with the native stage on
host and T800. This mode is **not** source-generated bytecode. The interpreter supports bounded vector arithmetic,
matrix multiplication, swizzles, dot/cross/normalize and nearest texture2D.
The experimental `gles2_texture` module supplies four units and eight bounded
RGBA8 level-zero texture objects with nearest/bilinear sampling and repeat/clamp
wrap. It is integrated into the opt-in uber-program draw path and does not
support mipmaps; the last min/mag filter selection applies to all lookups
because derivatives and level-of-detail selection are absent.
The instruction interpreter's demo adapter uses static scratch state and is
intended for one thread per emulated node.

`FRAGMENT_SOURCE=1` is a separate diagnostic: it compiles a small GLSL
fragment expression that samples the Hellknight diffuse texture and multiplies
it by a uniform tint. The rasterizer supplies perspective-correct UVs, and
the compiled instructions run per fragment. The room remains on the native
material path, and this diagnostic character shader does not apply the uber
shader's lighting, normal/specular maps or shadows. The source compiler also
supports vector constructors, swizzles, arithmetic, `dot`, `cross`,
`normalize`, `max` and `pow`, local assignments, a restricted function form,
and comparison sufficient for the original uber shaders. It is not a general
GLSL ES compiler: loops, general control flow, general function signatures,
general linking and most language and ES state are missing. The `techdemo`
source-based path uses explicit named bindings and generated scene textures;
the opt-in OpenGL bridge is currently restricted to the dEngine uber interface.

`RENDERER=gl-uber` is a separate animated triangle diagnostic using
`glCompileShader`, `glLinkProgram`, indexed drawing and four bound texture
units. It extracts the original `RenderEntitiesOBJ` function from the local
dEngine checkout at build time and calls it for that test object. It does not
run the complete `RenderEntities` function, original iOS platform loop or the
whole demo scene. `RENDERER=gl-uber-fbo` adds a shadow pass using dEngine's
original shadow-generation shaders, an attached color texture and depth
renderbuffer, and then samples that texture in the main pass. The opt-in
`TG_GLES_FRAMEBUFFER` path uses caller-owned color and depth storage; supported
framebuffers require matching RGBA8 color and depth attachments. Depth is held
as a 32-bit software value even when `GL_DEPTH_COMPONENT16` is requested,
and redefining a texture while it is attached is rejected. The color scratch
is synchronized to the attached RGBA texture when the framebuffer is unbound
or changed. This is not a complete ES framebuffer implementation.

The programmable path now clips triangles in homogeneous space and supports
viewport, depth enable/write mask, front/back culling, source-alpha blending
and color masks. Only a narrow selection of state and blend factors exists.
PVR uploads, full-resolution runtime asset loading, broader render state,
additional shaders and platform integration remain for the original renderer.
The bridge currently requires integer shininess values and rejects shared-stage
uniforms and unsupported shader interfaces.

```sh
RENDERER=techdemo VERTEX_VM=1 FRAGMENT_SOURCE=1 \
  bash transputer/examples/distributed-fb/test-scale.sh
RENDERER=techdemo VERTEX_SOURCE=uber FRAGMENT_SOURCE=uber \
  bash transputer/examples/distributed-fb/test-scale.sh
RENDERER=techdemo CHAR_TEXTURE_SIZE=64 ROOM_TEXTURE_SIZE=32 \
  VERTEX_SOURCE=uber FRAGMENT_SOURCE=uber bash transputer/examples/distributed-fb/test-scale.sh
bash transputer/examples/dengine-t4/test-uber-scene.sh
bash transputer/examples/distributed-fb/test-glsl-source.sh
bash transputer/examples/distributed-fb/test-glsl-raster.sh
RENDERER=gl-uber bash transputer/examples/distributed-fb/test-scale.sh
RENDERER=gl-uber-fbo bash transputer/examples/distributed-fb/test-scale.sh
bash transputer/examples/dengine-t4/test-uber-api.sh
UBER_GL_DRAW=1 bash transputer/examples/dengine-t4/test-uber-api.sh
UBER_GL_DRAW=1 UBER_GL_FBO=1 bash transputer/examples/dengine-t4/test-uber-api.sh
bash transputer/examples/distributed-fb/test-framebuffer.sh
bash transputer/examples/distributed-fb/test-vm-cache.sh
```

`ROOM_BUMP=0`, `ROOM_SPECULAR=0` and `ROOM_SHADOW=0` disable individual
room features for comparisons. From the fixed default camera, disabling
the room normal maps changes visible pixels; disabling the specular map did
not change the captured frame, so its visible contribution has not been
verified in this view. The fragment backend's specular sampling has separate
tests. Room shadows are visibly different with room casting disabled.

```sh
RENDERER=techdemo NODES=4 FRAMES=12 CAPTURE=1 \
  bash transputer/examples/distributed-fb/run-scale.sh
RENDERER=techdemo bash transputer/examples/distributed-fb/test-scale.sh
bash transputer/examples/dengine-t4/test-room.sh
bash transputer/examples/dengine-t4/test-scene.sh
bash transputer/examples/dengine-t4/test-uber.sh
bash transputer/examples/distributed-fb/test-uber-vertex.sh
bash transputer/examples/distributed-fb/test-glsl-vm.sh
bash transputer/examples/distributed-fb/test-texture.sh
python3 transputer/examples/dengine-t4/audit_scene.py
```

Before the normal/specular, transparency and room-shadow additions, one WSL
T4 experiment (12 unpaced, capture-free frames per configuration,
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

### What the real programmable renderer needed

This was written before `port/`, which now supplies all of it with its own
GL layer; the bounded bridge below remains as it was.

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
