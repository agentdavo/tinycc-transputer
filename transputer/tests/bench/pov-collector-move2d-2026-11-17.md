# MOVE2D in the POV-Ray collector

The packaged 16-node T800 image renders each scene while a private T4
build captures the actual `FRead`→`Cache` RGB messages at the root.
Each capture contains 480 unique rows from three channels in arrival
order. The capture tool checks the decoded bytes against the image the
historical collector writes.

Two TinyCC builds of the original `muxdemux.c` receive those packets on
three Transputer channels. Both execute the collector's real `ProcAltList`,
`ChanIn`, tag decoding, `pov_rgb_unpack`, and seek/write path. The test
driver loads the capture first, feeds the original payloads in order and
exchanges three exit handshakes. The TinyCC channel and startup adapters
use the legacy unsigned byte and little-endian packet conventions.

The existing `POV_RGB_USE_MOVE2D` option copies each three-byte RGB field
from the four-byte worker pixel to a separate output buffer. At the packer
call site, the `Buffer` payload begins nine bytes into a packet and the
packed RGB scanline occupies its own object. Disassembly shows setup with
source stride four and destination stride three, followed by two operand
loads, the width literal and `MOVE2DALL`. The trace counts 480 instruction
pairs per image. The collector is the only process executing `MOVE2D` in
this fixture.

## Profile

The timed interval follows the collector's `START_TIME` and `STOP_TIME`
hooks. It includes setup, channel delivery, row reordering, packing and
PPM writes, and ends before the final statistics and close. The historical
worker network supplies the capture before this interval. The rebuilt
collector uses T9000 emulator mode for libc, with the T800 counter's
size-dependent transfer charge. The model approximates timing for the
three-byte unaligned rows.

| 640×480 capture | Byte cycles | MOVE2D cycles | Byte instructions | MOVE2D instructions |
| --- | ---: | ---: | ---: | ---: |
| Box | 30,346,309 | 14,061,829 | 12,296,881 | 1,235,761 |
| Basicvue | 30,357,280 | 14,072,800 | 12,300,447 | 1,239,327 |
| Glass | 30,339,389 | 14,054,909 | 12,294,627 | 1,233,507 |

Every scene saves **16,284,480 modeled cycles (53.6–53.7%)** and
**11,061,120 decoded instructions (about 90%)** in this collector interval.
The default capture emulator and the independent size-aware T4 build give
identical profiles. The runner rejects an older T4 binary that omits the
transfer charge.

## Validation and reproduction

- The packaged cluster image and both rebuilt collectors reproduce the
  complete box, basicvue and glass PPM references byte for byte. The rebuilt
  versions also match across cycle-count and instruction-count runs.
- The full generic TinyCC renderer separately matches those three images
  with the recovered INMOS random generator. Glass SHA-256 is
  `79a805d547b93c68fc81c16e2c476ea09eed23aab17e091712fc8f0d1c9ef54c`.
- The RGB row suite passes alignment, canary, reordered row, callback
  revisit and write/seek failure checks with the `MOVE2D` packer.
- Doom's phase outputs and CoreMark's reference CRCs pass on T800 and T9000.
  The T800 Doom rectangle suite passes as well. All four raytracer scenes
  match at one and four nodes.

Build and capture, then profile from the repository root. `TCC_DIR` selects
the TinyCC toolchain, and `UCLIBC_HEADERS` can select the dereferenced
headers used by the existing POV-Ray fixture on WSL.

```sh
WORK=/tmp/pov-capture bash transputer/tests/bench/pov-collector/capture.sh
WORK=/tmp/pov-collector bash transputer/tests/bench/pov-collector/run.sh /tmp/pov-capture
```

`run.sh` uses the private emulator built by `capture.sh` by default.
`T4_BIN` selects another executable with size-aware `MOVE2D` timing.
Captured packets, target outputs, disassembly and the acceptance receipt
are in `logs/compiler-coordination/pov-collector-move2d/`.

## Next compiler candidates

The frozen full-render `box` profile shows three `Create_Ray` sites where
`FPI32TOR64` spills and immediately reloads a double before further FP
arithmetic. They execute 921,600 times. The first experiment is to extend
the existing resident-FP-result tracking to signed 32-bit conversions in
`gen_cvt_itof`, retaining the workspace alias check, operation order and
rounding.

`Intersect_Boxx` executes nine sites that copy two words into an FP scratch
pair and immediately load it, for 1,477,798 executions. Three sites already
load adjacent source words and execute 989,860 times. Start with a direct
`FPLDNLDB` when the scratch pair's lifetime permits removing the copy. The
other six source pairs are nonconsecutive even with frame remapping
disabled, so they need producer provenance before grouping the words.
