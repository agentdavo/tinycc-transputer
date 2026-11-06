# Distributed framebuffer demo

Four emulated T800s each render an 80 × 60 pixel tile in their own local
memory. For each frame, node 0 sends a frame number to the three workers,
renders its own tile, receives and checks the other tiles over links 1–3,
and scales them twofold into a 320 × 240 VGA back buffer. A textured cube
spans the four tiles. The next
frame is requested only after the current frame has been assembled and
presented. The buffers alternate on each flip, so drawing never changes the
displayed buffer. The network remains alive until the runner stops it.

From a Linux checkout with the project's cross compiler, netloader and T4
emulator already built:

```sh
bash transputer/examples/distributed-fb/run.sh
# Example live run: FRAMES=120 FPS=20 CAPTURE=0 LIVE=1 bash transputer/examples/distributed-fb/run.sh
# TCC_DIR and T4_BIN can select alternative toolchain and emulator binaries.
```

The default run captures 12 frames at up to 20 frames per second. `FRAMES`
accepts 1–120 and `FPS` accepts 1–60. Frames are paced by the low-priority
transputer timer; if rendering or capture takes too long, the actual rate
falls instead of issuing a burst of late frames. Each frame is saved as
`screenshot_N.bmp` in the output directory unless `CAPTURE=0`, which avoids
capture overhead during a live run. The runner checks the guest's `DISTFB OK`
verdict and, when enabled, that every capture exists. `distfb.uart` contains
the frame number, buffer address and low-priority timer ticks for each flip.

`CAMERA_DISTANCE` sets the camera distance in Q8 world units (default 1152,
or 4.5 units); `FOCAL_LENGTH` sets the focal length in pixels (default 110).
For example, `CAMERA_DISTANCE=1600 FOCAL_LENGTH=90` moves the camera back
and widens the view. The runner limits the zoom to keep the fixed-point
rasterizer's intermediate arithmetic in range.

The runner is headless by default; `LIVE=1` preserves `DISPLAY` for an SDL
window when a display server is available. It requests SDL vertical sync and
updates the window only on flips. Actual screen synchronization depends on
the SDL renderer and display driver. `distfb.out` and `distfb.uart` contain
diagnostics on failure.

This demonstrates spatial distribution and local framebuffer ownership from
INMOS Technical Note 46. T4 does not model that design's dual-port VRAM or
open-collector scanout bus: here the tiles travel over transputer links and
node 0 copies them into an offscreen part of its framebuffer and flips to it.
The smaller resolution lets two complete images fit in T4's guest-visible
framebuffer without changing the emulator. This is not a claim of equivalent
scanout bandwidth or performance to the original hardware.

## Rendering subset

`tinygl.c` is a small **GLES-inspired** software pipeline, not an OpenGL ES
2.0 implementation. `gles2_subset.h` adds a narrow ES 2.0-style API slice:
`glClearColor`, `glClear`, `glColorMask`, `glDepthMask`, `glGetError`,
`glScissor`, `glEnable`, `glDisable`, `glIsEnabled`, `glGetBooleanv`,
`glGetIntegerv`, and `glGetFloatv`. The enable/disable/is-enabled commands
currently accept only `GL_SCISSOR_TEST`; the query commands support
`GL_SCISSOR_TEST`, `GL_SCISSOR_BOX`, `GL_COLOR_WRITEMASK` and
`GL_DEPTH_WRITEMASK`, including cross-type conversion and `GL_INVALID_ENUM`
for unsupported names. Other OpenGL ES state queries are not implemented.
Scissoring uses OpenGL's
bottom-left coordinates on the 160 × 120 logical framebuffer and clips both
clear and triangle rasterization.
The cube uses this slice for its color and default depth clear. Embedding code
must call `tgGlesInit` and `tgGlesMakeCurrent`; there is no EGL, and a node
supports one current state at a time. Only the implemented state affects
clears; dithering and other ES features are not implemented. The
software pipeline supports indexed triangles, a Q8
rotation transform, a fixed perspective camera, per-tile viewport clipping,
back-face culling, a depth buffer, perspective-correct texture coordinates,
and nearest sampling from an 8 × 8 ARGB texture. Its fixed cube shader is
implemented in C: there is no GLSL compiler, programmable shader API, blend
state, general resource management, or near-plane triangle clipping. Geometry
must remain in front of the camera; the demo's constrained camera settings
ensure that for the cube. Every node renders the scene into its own color and
depth tile. Workers return the frame number and a pixel checksum with each
tile; node 0 verifies both before presenting.

The supported clear entry points follow the relevant default-state and error
rules of the [OpenGL ES 2.0 specification](https://registry.khronos.org/OpenGL/specs/es/2.0/es_full_spec_2.0.pdf),
but only within the implemented state and framebuffer model. This subset has
not been certified as OpenGL ES conformant.

The renderer advances triangle edges and interpolants incrementally, reuses
the cube's eight transformed positions, and clears buffers with T800 `MOVE`.
Node 0 expands each row and uses `MOVE2D` for its two output rows. Run
`bash transputer/examples/distributed-fb/bench.sh` for a 32-orientation,
four-tile T800 cycle estimate and output checksum; `SCALAR_CLEAR=1` selects
the scalar-clear comparison, and `BENCH=blit` compares scalar and MOVE2D
output transfers and checks the resulting pixels. These are emulator estimates,
not measured hardware cycles. The rendering benchmark includes checksumming
and excludes links, display, pacing, and capture. `NO_CULL=1` disables
back-face culling for experiments; shared-edge colors can differ because
back-facing triangles are then allowed to write depth.

Host regression checks can be built with:

```sh
cc -O2 -o test-tinygl transputer/examples/distributed-fb/{test_tinygl,tinygl,gles2_subset,cube}.c
./test-tinygl
```

Run `bash transputer/examples/distributed-fb/test-target.sh` to build the
same checks as a bare-metal T800 program and run them in T4.

## Node-count experiment

`run-scale.sh` divides the 160 × 120 logical image into 32 horizontal tile
bands. Each band is assigned round-robin to one of 1, 2, 4, 8, 16 or 32
nodes; workers rasterize only the assigned rows. A binary tree relays tagged,
checksummed bands to node 0, which verifies and assembles a double-buffered
frame. This is a separate experimental harness, not the original four-node
star topology. Every node still allocates a full 80 × 60 color/depth tile;
each band independently repeats vertex transforms and the triangle setup.
Root assembly and serialized tree forwarding are shared bottlenecks. In
particular, this is not a direct benchmark of a hardware VRAM scanout bus.

```sh
# Correctness: two captured frames at every supported node count must match
# the 1-node BMPs byte for byte.
bash transputer/examples/distributed-fb/test-scale.sh

# Unpaced, capture-free timing sample for one configuration.
NODES=16 FRAMES=12 CAPTURE=0 FPS=0 bash transputer/examples/distributed-fb/run-scale.sh

# Optional bridge using the separately cloned dEngine matrix implementation.
RENDERER=dengine NODES=4 FRAMES=64 FPS=20 bash transputer/examples/distributed-fb/run-scale.sh

# T800 cycle-estimator comparison for identical band output.
BENCH=bands FULL_CLEAR=1 bash transputer/examples/distributed-fb/bench.sh
BENCH=bands bash transputer/examples/distributed-fb/bench.sh
```

On the WSL2 T4 environment used for development, one sequential 12-frame run
per node count gave these medians (first frame excluded):

| Nodes | Root frame assembly, ms |
| ---: | ---: |
| 1 | 12.096 |
| 2 | 12.800 |
| 4 | 13.056 |
| 8 | 12.160 |
| 16 | 9.216 |
| 32 | 7.680 |

These are guest low-priority timer measurements of wall time, including root
verification, copy and link transfer, but excluding capture and pacing.
They are one short sample on an oversubscribed 20-logical-CPU host, not
physical T800 timings, simulated link-bandwidth limits, a scaling guarantee,
or a stable benchmark. Independent emulated node threads run concurrently.
The full-vs-scissored clear benchmark estimated 237,246,609 vs 219,710,609
T800 cycles across 32 frames × 32 bands, with matching checksums (7.4% less).
The cycle-estimator mode is separate from the wall-time scaling runs.
The dEngine bridge and its upstream-source requirements are documented in
`transputer/examples/dengine-t4/README.md`; it is not the dEngine renderer.

### Per-node CPU affinity experiment

The Linux T4 threaded-array mode can optionally pin nodes with
`T4_NODE_CPUS=auto` or a comma-separated list of allowed logical CPU IDs;
see `transputer/t4/T4_PERF.md` for topology, oversubscription and precedence.
The following runs three interleaved pinned/unpinned samples for each of 1,
2, 4, 8, 16 and 32 nodes, using the same `T4_BIN` for both modes:

```sh
T4_BIN=/path/to/new/t4 python3 transputer/examples/distributed-fb/benchmark-affinity.py /path/to/results
T4_BIN=/path/to/new/t4 python3 transputer/examples/distributed-fb/test-affinity.py /path/to/test-results
```

Each sample renders 24 unpaced, capture-free frames; the figure for that run
is the median frame assembly time after discarding its first frame. The table
uses the median of the three run medians, in milliseconds. These are guest
timer wall times, not physical transputer cycles. On 6 November 2026 in the
WSL2 environment with 20 visible logical CPUs:

| Nodes | Unpinned ms | Pinned ms | Pinned / unpinned |
| ---: | ---: | ---: | ---: |
| 1 | 14.272 | 15.040 | 1.054 |
| 2 | 13.888 | 14.016 | 1.009 |
| 4 | 14.464 | 14.400 | 0.996 |
| 8 | 13.696 | 13.312 | 0.972 |
| 16 | 11.520 | 11.200 | 0.972 |
| 32 | 9.216 | 9.280 | 1.007 |

These short samples show no consistent benefit from pinning; the 32-node run
shares 20 logical CPUs, and one unpinned 1-node sample was an outlier (65.408
ms median). WSL reports virtual CPU topology, not guaranteed physical-core
placement. The timer helper can inherit node 0's affinity and compete for
that CPU. The benchmark excludes build, boot and screenshot time and is not a
hardware-cycle measurement. JSON output retains individual samples, frame
ticks, paths and node-to-CPU logs for further analysis.
