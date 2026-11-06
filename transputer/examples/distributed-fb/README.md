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
`glClearColor`, `glClear`, `glColorMask`, `glDepthMask`, and `glGetError`.
The cube uses this slice for its color and default depth clear. Embedding code
must call `tgGlesInit` and `tgGlesMakeCurrent`; there is no EGL, and a node
supports one current state at a time. Only the implemented state affects
clears; scissor, dithering and other ES features are not implemented. The
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
