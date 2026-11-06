# POV-Ray compiler image gate

This fixture rebuilds the complete generic POV-Ray 1.0 C renderer with the
selected TinyCC compiler and compares complete 640 × 480 PPM files against
`transputer/t4/examples/povray/pictures.zip`. The default scenes are `box`,
`basicvue` (the timing-test scene) and procedural/refraction `glass`. It does not rebuild
the historical INMOS ICC sixteen-node process/channel runtime.

Run under Linux/WSL with a built compiler, runtime archives, uClibc static
`libc.a`, its fdlibm sources and the T4 emulator:

```sh
TCC_DIR=/path/to/compiler-bundle \
T4_BIN=/path/to/t4 \
UCLIBC_HEADERS=/path/to/populated/uclibc/include \
WORK=/path/to/isolated/results \
bash transputer/tests/bench/povray/run.sh
```

`TCC_DIR` must contain `transputer-tcc`, `transputer-netload` and
`transputer-libtcc1-t800.a`. `UCLIBC` defaults to the in-tree uClibc checkout;
`UCLIBC_HEADERS` can point at a copy with real header files where a Windows
checkout has empty symlink stubs. `SCENES` can select packaged level-1 scenes;
`TIMEOUT` defaults to 1800 seconds **per scene**; glass is much heavier than
box. Each variant must use a separate `WORK` directory. Keep runners unchanged
while they are running; the generated source and hashes describe that build.

`POV_RNG=inmos` (the default) explicitly links the recovered historical
generator into this fixture. `POV_RNG=libc` retains the installed libc's
generator; use `SCENES='box basicvue'` for that comparison because its noise
sequence does not reproduce the packaged glass image. This does not replace
the installed libc or compiler runtime. See [captured RNG tests](INMOS-RAND.md).

`POV_OUTPUT=row` (the default) accumulates a 24-bit RGB row and writes one
block. `POV_OUTPUT=pixel` retains the earlier per-pixel writer for paired
measurements. The row cache honors coordinates, seeks for reordered rows,
reloads prior rows before partial updates and checks close/write failures.

The renderer and fdlibm are compiled for the T800 subset. The existing
in-tree `libc.a` contains T9000 instructions, so this linked fixture runs with
T4 `-s9`. It does not establish T800 hardware compatibility of that libc.
Source-level square roots use the compiler's `__fpsqrt` builtin. The fdlibm
transcendental implementations are compiled from their original uClibc files;
the fixture supplies IEEE entry wrappers and a 32-bit `scalbn` alias wrapper.

`prepare.py` copies the historical generic source to `WORK/source`, preserving
its notices. It installs a PPM display callback and the shared collector
`rgbrow.h`, sets the same `1e-5` tolerance
as the historical Transputer port, disables elapsed-time reporting and fixes
nine external/static declaration conflicts in `txtmap.c`. It preserves the
renderer expression order and vector macro definitions. Original files are
not edited.

The test-only adapter in `runtime.c` implements host file I/O through T4's
iserver link-0 packet protocol. It initializes uClibc, uses an 8 MiB workspace
adjustment above the loaded image and bounds its anonymous heap mappings to
16–24 MiB and `brk` arena to 24–32 MiB of the 64 MiB emulated memory. Freed
mappings remain allocated until fixture exit. This adapter supplies only the
operations this single-process fixture needs; unsupported syscalls return
`ENOSYS`. It is not a general Linux or INMOS runtime.
Both legacy lseek and libc's five-argument llseek ABI are supported. The latter
writes a two-word caller-owned result; offsets outside the iserver's signed
32-bit range fail with `EOVERFLOW` instead of truncating.

Successful runs require an empty strict linker log, normal iserver exit,
absence of emulator fault diagnostics and byte-for-byte equality with the
packaged image. `inputs.sha256` records compiler, emulator, runtime archive,
adapter, generated source and fdlibm input hashes. Outputs include the ELF,
BTL, symbol map, complete images and per-scene logs.

Use paired runs with identical inputs to assess a compiler change. Image
equality proves this numerical gate; it does not establish a speedup. Keep
executed-instruction/cycle measurements separate from image acceptance and
from scaling measurements of the historical sixteen-node binary.

The [BTL analysis](../../../POVRAY-BTL-ANALYSIS-2026-11-06.md#recovered-historical-random-number-contract)
records the recovered generator, state and wrap boundaries. Both pixel and
row output now match complete box/basicvue/glass target images with this
generator; exact comparison has not been weakened.

With the same tool/header environment, run `test-rand.sh` for the captured
128-word sequence and reseeding on four CPUs. Run `test-rows.sh` for target
packet replays, source/destination canaries, alignment, row order, rewrites,
callback revisits, llseek success/overflow/error behavior, and sanitized host
stdio failures. It also benchmarks the actual shared packer implementations.
`POV_RGB_PACK=move2d` exercises the optional packet packer in replay tests;
the portable byte packer remains the default. MOVE2D requires a capable CPU
and runtime preservation of block state across preemption. Its three-byte
unaligned timing estimate is approximate, not a silicon speed claim.

The shared helper and original collector source changes are committed in the
nested T4 repository (`b3ab1e0`, optional MOVE2D `2d6cf12`). The historical
16-node BTL remains unchanged until ICC/link/config/collect integration is
available. See the [row-output acceptance report](../povray-rgb-rows-2026-11-06.md).
