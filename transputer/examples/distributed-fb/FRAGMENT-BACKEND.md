# Standalone dEngine-style fragment backend

`gles2_fragment.c` is a tested **C specialization** of the material calculation
in the separately cloned dEngine's `data/shaders/f_uber.glsl`. It is a backend
building block, not a GLSL ES parser, compiler, interpreter, OpenGL entry point,
or integrated renderer. It does not run Hellknight or the original dEngine
application. No upstream source code or assets are copied into this component.

`tg_fragment_uber` takes a caller-owned material, textures and already
interpolated per-fragment inputs. It implements the shader's eight combinations
of optional bump, specular-map and packed-color shadow features:

- Nearest RGBA8 base sampling, with clamp-to-edge or repeat coordinates.
- Diffuse/ambient lighting and specular highlight; the supplied normal or a
  sampled normal map is used exactly as in the fragment shader.
- Specular color from the sampled map or uniform material.
- Optional light-clip division, shadow texture lookup, color unpacking and
  the shader's `0.0001` depth comparison bias.
- Unclamped float RGBA output; `tg_fragment_argb` provides a separate clamped
  conversion for the existing ARGB framebuffer.

The caller must provide a `pow_fn` implementing GLSL `pow` on its defined
domain. The backend has no built-in approximation or dependency on host libm.
Like the original shader it evaluates `pow(dot(...), shininess)` *before*
`max`; a negative base or zero base with nonpositive exponent is undefined by
GLSL. This backend reports `TG_FRAGMENT_UNDEFINED` and leaves the output
unchanged for those cases and for a zero shadow-coordinate divisor. It reports
`TG_FRAGMENT_INVALID` for missing textures/callbacks, unsupported flags, or
invalid texture coordinates. Valid coordinates are bounded to ±32768 to avoid
undefined float-to-integer conversions on the target.

Texture pixels are caller-owned row-major RGBA8, with the caller choosing which
row corresponds to `t=0`. Only nearest filtering, repeat and clamp-to-edge are
implemented. There are no mipmaps, linear filtering, texture objects, PVRTC,
format conversion, blending or sRGB support. Float32 computation does not claim
bit-identical GPU `mediump` behavior. Normals and vectors are not automatically
renormalized; `f_uber.glsl` uses the interpolated values directly.

Integration needs more than wiring a function call: the vertex stage must
produce the normal or tangent-space light/half vectors, UV and optional
light-space clip coordinates. The rasterizer must interpolate the GLSL varying
values perspective-correctly at each fragment, invoke this backend with a
valid `pow` implementation, then perform depth/stencil/blend and write the
result in the proper order. A programmable shader system must still compile
and execute GLSL ES 1.00 and manage uniforms, sampler bindings and shader
objects. The existing 8 × 8 fixed-texture cube rasterizer does not yet do that.
The shadow feature expects an **existing** color-packed shadow texture; it does
not render one, implement framebuffers, or execute the shadow-map vertex and
fragment passes.

Host and bare-metal T800 tests cover texture selection and wrapping, material
variants, diffuse/specular output, packed shadows, output alpha, invalid and
undefined inputs, and framebuffer clamping:

```sh
cc -O2 -ffp-contract=off -o test-fragment \
  transputer/examples/distributed-fb/test_gles_fragment.c \
  transputer/examples/distributed-fb/gles2_fragment.c
./test-fragment
bash transputer/examples/distributed-fb/test-fragment.sh
```

The target script accepts the same `T4_BIN` and `TCC_DIR` overrides as the
other demo tests. The test uses a square function as the supplied `pow_fn` for
shininess 2; this is a fixture, not a general-purpose GLSL power function.

`bench-fragment.sh` runs 1024 base-only and 1024 bump/specular/shadow
evaluations with output conversion and a checksum under `T4_EST_CYCLES=1`.
On the development T4 build, the estimator reported 4,151,831 and 8,634,903
cycles respectively, about 4,055 and 8,433 cycles per evaluation. The
benchmark uses 1 × 1 textures and a square-only test callback; it measures
neither cache behavior for real 512 × 512 assets nor general `pow`, shader
compilation, varying interpolation, framebuffer access or network traffic.
T4's estimated cycles are not hardware measurements. These numbers provide
a baseline for later optimization rather than an end-to-end frame rate.
