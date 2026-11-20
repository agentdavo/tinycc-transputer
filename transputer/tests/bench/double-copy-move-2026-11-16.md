# One block move for the inverse transform results

`MInverseTransformVector` and `MInvTransVector` compute three doubles in
local storage, then copy them to the caller's vector. Both functions now
use one `MOVE` for the 24-byte result.

```text
LDLP source
LDL destination
LDC 24
MOVE
```

The function pass starts with three typed, aligned double copies whose
results were discarded at statement boundaries. It requires six consecutive
source words in the callee's local frame and three consecutive destination
doubles based on the first pointer argument. That pointer must still hold
its incoming value. Its home may have no other writes or indirect accesses,
and its address must stay private. The caller's storage and the new frame
are therefore disjoint. The copy tail must be in a single basic block
immediately before a void epilogue. Frame remapping keeps the six source
words contiguous.

Set `TP_NO_COPY3_MOVE=1` to disable the rewrite for comparison.
Compiling `matrices.c` with this switch produces an object byte-identical
to the same source compiled by the frozen `2b374ed1` baseline.

## POV-Ray box profile

The paired renders use the same inputs. The emulator runs the linked image
in T9000 mode for libc, and the instruction histogram is evaluated with the
T800 timing model. The model charges the 24-byte `MOVE` for six words.

| Measurement | Baseline | Candidate | Saved |
| --- | ---: | ---: | ---: |
| `MInverseTransformVector` cycles | 401,822,228 | 392,739,056 | 9,083,172 (2.26%) |
| `MInvTransVector` cycles | 341,185,473 | 332,625,264 | 8,560,209 (2.51%) |
| `MTransNormal` cycles | 18,627,444 | 18,104,481 | 522,963 (2.81%) |
| Whole render cycles | 4,633,779,615 | 4,615,613,271 | 18,166,344 (0.392%) |
| Whole render instructions | 1,364,636,653 | 1,345,605,245 | 19,031,408 (1.39%) |
| Linked renderer ELF bytes | 325,724 | 325,628 | 96 |

The three functions execute the `MOVE` 432,532, 407,629 and 24,903 times,
respectively. Each replaces 26 decoded instructions with four and saves
21 modeled cycles. Across the render this removes 12,110,896 workspace
loads and 2,595,192 workspace stores. The paired double FPU operations
execute the same number of times. Basicvue exercises none of the three
sites and takes 7,862,371,089 modeled cycles in both runs.

## Validation

- Doom's eleven phase hashes, modeled cycles and `check 0 0` match on
  T800 and T9000. CoreMark passes its reference CRC checks on both CPUs,
  with 4,970,600 T800 ticks and 3,271,251 T9000 ticks in each build.
- All four raytracer scenes match the reference images at one and four
  nodes with both compilers.
- Both compilers reproduce the complete 640×480 POV-Ray box, basicvue
  and glass references byte for byte with the recovered INMOS random
  generator and buffered RGB output. The emulator exits normally in
  each run. Glass SHA-256 is
  `79a805d547b93c68fc81c16e2c476ea09eed23aab17e091712fc8f0d1c9ef54c`.
- The raw-copy fixture passes 16 executions across the baseline and
  candidate, T414/T425/T800/T9000, and code pads of 0 and 1027 bytes.
  It checks signaling NaN bits, signed zero, a subnormal, adjacent
  sentinels, direct and indirect pointer reassignment, overlapping
  copies, a branch into the middle, a volatile pointer and a source gap.
  Disassembly shows one 24-byte `MOVE` in the eligible function on each
  CPU. The six controls keep separate copies.
- The existing UART runner passes 42 target regressions. Differential
  fuzzing passes seed 329 and seeds 24401–24500 at both code layouts.

| Frozen input | SHA-256 |
| --- | --- |
| Baseline compiler | `0f1d7550f877b421ed314a55418654efc06d6decb571e8498068bcc4824eeebf` |
| Candidate compiler | `147dafab1fc2a0dfc6b045e79cd22d95c5db6c576710d768c8c03d80872f24b0` |
| Candidate `transputer-fnopt.h` | `386d8f86a57c59e14989366034e4cfa3931fa16424d21dc596449b14a1534e97` |
| Candidate `transputer-fp.h` | `9203b3a4993585d7d4da5b1f4c8fdabc1a2a2b8f2d245a61085186336a3e4958` |
| Candidate `transputer-gen.c` | `6412efe2217f593ff71315bbb7c8d227fa29d7fd160515dba4285efc47831551` |
| Candidate `transputer-knobs.h` | `0827b77fea08cb00943cb9a48287256b61249e8c517bb1c5907e206a281b2868` |

The rendered images, profiles, disassembly and validation receipt are in
`logs/compiler-coordination/double-copy-move/`. The frozen baseline runs
are in `logs/compiler-coordination/double-copy-stack/`.
