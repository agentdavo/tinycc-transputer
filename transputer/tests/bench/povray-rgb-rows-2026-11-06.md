# Buffered POV RGB rows and historical random compatibility

The generic TinyCC renderer now accumulates one RGB row and writes a block,
replacing one three-byte `fwrite` per pixel. The original collector sources
also use the shared writer, replacing three `fputc` calls per pixel. These
are separate applications of the same output primitive; the historical
sixteen-node BTL has not been rebuilt.

## Results and commits

* Root `b0b79743`: scoped INMOS random compatibility and captured-sequence
  tests. It leaves the installed libc and compiler runtime unchanged.
* T4 `b3ab1e0`: original collector source copies, shared `rgbrow.h`, build
  dependencies and collector acceptance notes.
* T4 `2d6cf12`: optional TinyCC MOVE2D packet packing. The byte packer remains
  the default, including for the original ICC toolset.
* The root harness integration supplies `display-row.c`, mode selection,
  llseek support and target/host replay/edge tests.

The selected compiler is the previously validated sqrt compiler, SHA-256
`e5cf142febdb1f28fa9f0310425dc5323d61791db52e9c68dc6ff87c59315d39`.
No compiler or math-source optimization is included in this change. All
renderer/fdlibm expression order and precision are retained.

## Exact full images

Both pixel and buffered-row builds link the recovered generator. All six
complete 640x480 images equal their packaged references byte for byte:

| Scene | Bytes | SHA-256, both variants and reference |
| --- | ---: | --- |
| box | 921,615 | `fcaeaccaea31d263bede7a2f8774e2e7af8dcfc6f640a893a3fd1d3b8d1d6db0` |
| basicvue | 921,615 | `d537ba4e52842f8cd3e35f8c59bd3d30ac2c3008b60eafc10f7b6ff0cf17cde7` |
| glass | 921,615 | `79a805d547b93c68fc81c16e2c476ea09eed23aab17e091712fc8f0d1c9ef54c` |

Every emulator render exits normally; strict linker logs are empty. Rays,
intersections, noise calls, shadow/reflection/refraction counts match between
variants. Glass exercises 1,218,636 rays, 9,999,016 DNoise calls and 455,718
each of reflected and refracted rays. This closes the previous target glass
compatibility gap with exact equality, without pixel tolerances.

The fixture compiles renderer/fdlibm for T800, but the existing libc needs
T4 `-s9`. It is a single-node 64 MiB fixture, not T800-board deployment.
The original ICC/link/config/collect tools are unavailable on PATH, so no
updated sixteen-node ICC image or sixteen-node scaling result is claimed.

## Measured output improvement

Decoded profiles disable emulator instruction combination and use the same
compiler, recovered generator, runtime and box input for both variants.

| Full generic box | Pixel output | Buffered row output |
| --- | ---: | ---: |
| Logical instructions | 1,838,454,969 | 1,806,221,654 |
| Modeled T800 cycles | 5,717,529,669 | 5,606,252,501 |
| `display_plot` calls | 307,200 | 307,200 |
| All `fwrite` entries, including non-image output | 307,226 | 506 |
| Image `fwrite` calls | 307,200 | 480 |

This saves 32,233,315 instructions (1.753%) and 111,277,168 modeled cycles
(1.946%). It is a modeled single-node improvement, not a hardware speedup.
The collector's corresponding source change replaces 921,600 `fputc`
calls with 480 row writes; its new ICC executable still needs integration.

## Format, memory and ordering

Worker packets contain little-endian `0x00BBGGRR` words at a nine-byte header
offset: byte order is R,G,B,padding. The old collector's unaligned word cast
and shifts depended on target word-rounded access. The shared byte decoder
uses the actual packet layout, accepts all byte alignments and discards the
padding byte. Copying three shifted channels from an unaligned C word would
have selected the wrong bytes on a byte-accurate host.

The shared writer derives header length from `fprintf`, checks dimensions
and offset overflow, writes exactly width*3 bytes, and seeks when a row is
out of order. Its tracked position avoids redundant seeks for consecutive
rows. The original collector uses a static 1,920-byte buffer instead of
adding it to process workspace. New collection must check the root map;
worker budgets are unaffected by these collector-only changes.

The generic callback uses one RGB row plus a height-byte written-row table.
It preserves arbitrary pixel/row order and partial revisits by reading a
previous row before modifying it. Dirty rows flush on a row change or close;
close is idempotent. Header, seek, write and close failures are checked.

## Independent tests and adapter correction

* The captured 128-word `srand(0)` sequence and reseeding pass on T414,
  T425, T800 and T9000. The same test passes native UBSan.
* Both byte and optional MOVE2D packers replay all three packaged images at
  four alignments: 24 complete output comparisons. Sequential, reversed and
  shuffled rows, rewrites, discarded nonzero padding, source/destination
  canaries and invalid row bounds are checked.
* Target and ASan/UBSan host callbacks pass out-of-order pixels, partial
  revisits and repeated close. Real `/dev/full` writes, pipe seeks, invalid
  dimensions and header failures are rejected.
* Reordered-row testing exposed missing libc `llseek` (syscall 62) in the
  fixture adapter. It now implements the high/low offset and caller-owned
  two-word result ABI. Tests cover positive and negative offsets, legacy
  lseek, invalid origins, overflow rejection and unchanged result words on
  errors. Iserver offsets remain signed 32-bit; larger requests return
  EOVERFLOW rather than truncating.

The full-render/profile pair was frozen before the added llseek branch;
its sequential output path did not need it. The final adapter's seek paths
are exercised directly by the complete target replays and callback revisits.
The final integrated row runner/adapter is also rebuilt and smoke-tested
against complete box/basicvue references in `final-row/`.
An edit of the running pixel shell driver caused a trailing command error
after its three successful comparisons. Independent full-file comparisons,
empty link logs and normal emulator-exit receipts accept those artifacts;
the buffered-row driver itself completes successfully. Keep runners stable
during execution. Raw acceptance is `pov-rgb-rows/acceptance.json` below.

## Optional MOVE2D packing

RGBx-to-RGB is a three-byte-wide rectangle, with one row per pixel and source/
destination strides four/three. `move2dinit(pixel_count,3,4); move2dall(3,...)`
therefore strips padding without a C byte loop. The explicit option requires
TinyCC Transputer builtins and a capable target (not T414). It is disabled by
default and requires preservation of block-move state across preemption.

The actual shared implementations, including call/setup/loop overhead, take
25,613,664 modeled cycles for bytes versus 8,243,552 for MOVE2D across 512
packs of 640 pixels. All sixteen source/destination alignment combinations,
whole source/destination canaries and zero width pass. This is 67.8% lower
modeled packing cost, not whole-render improvement. T4 timings for three-byte
unaligned MOVE2D rows are approximate; native bus timing remains a gate.

## Reproduction and retained artifacts

See [fixture usage](povray/README.md). Defaults are `POV_OUTPUT=row`,
`POV_RNG=inmos`, `SCENES='box basicvue glass'`. Pixel and libc modes remain
explicit comparison options. Run `test-rand.sh`, then `test-rows.sh` with the
same tool/header environment; `POV_RGB_PACK=move2d` selects the optional
packer in the replay tests. Tests include the actual shared header rather
than a copied implementation.

Snapshots, strict logs, input hashes, all full images, decoded histograms,
source/ABI tests and receipts remain in
`logs/compiler-coordination/pov-rgb-rows/`. The original BTL's historical
profile and random recovery remain in `povray-deep/` beside it.
