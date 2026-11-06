# POV-Ray compiler image gate

This fixture rebuilds the complete generic POV-Ray 1.0 C renderer with the
selected TinyCC compiler and compares complete 640 × 480 PPM files against
`transputer/t4/examples/povray/pictures.zip`. The default scenes are `box` and
`basicvue`, the original source's timing-test scene. It does not rebuild
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
`TIMEOUT` defaults to 600 seconds **per scene**. Each compiler must use a
separate `WORK` directory.

The renderer and fdlibm are compiled for the T800 subset. The existing
in-tree `libc.a` contains T9000 instructions, so this linked fixture runs with
T4 `-s9`. It does not establish T800 hardware compatibility of that libc.
Source-level square roots use the compiler's `__fpsqrt` builtin. The fdlibm
transcendental implementations are compiled from their original uClibc files;
the fixture supplies IEEE entry wrappers and a 32-bit `scalbn` alias wrapper.

`prepare.py` copies the historical generic source to `WORK/source`, preserving
its notices. It installs a PPM display callback, sets the same `1e-5` tolerance
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

Successful runs require an empty strict linker log, normal iserver exit,
absence of emulator fault diagnostics and byte-for-byte equality with the
packaged image. `inputs.sha256` records compiler, emulator, runtime archive,
adapter, generated source and fdlibm input hashes. Outputs include the ELF,
BTL, symbol map, complete images and per-scene logs.

Use paired runs with identical inputs to assess a compiler change. Image
equality proves this numerical gate; it does not establish a speedup. Keep
executed-instruction/cycle measurements separate from image acceptance and
from scaling measurements of the historical sixteen-node binary.

Procedural textures need another compatibility check before they can use the
INMOS images as exact oracles. `InitTextureTable()` permutes its hash table
with `srand(0)` and `rand()`, whose sequence is library dependent. The generic
host renderer's `glass` scene differs from the packaged image even without a
TinyCC change; it is therefore not a default compiler gate. Preserve that
port limitation instead of interpreting a noise-texture difference as an FP
optimization failure or weakening the exact comparison.

The [BTL analysis](../../../POVRAY-BTL-ANALYSIS-2026-11-06.md#recovered-historical-random-number-contract)
recovers the historical generator and verifies its state and wrap boundaries.
A host-only override produces an exact `glass` reference image. This harness
still uses the existing target libc generator; a target compatibility option
and full procedural-scene acceptance are separate follow-up work.
