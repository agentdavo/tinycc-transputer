# Keep signed 32-bit conversions on the FP stack

`gen_cvt_itof` records the workspace store for a signed 32-bit to double
conversion in the same FP history used for arithmetic results. The
conversion is exact in REAL64. The existing consumer can take the store
back if the FP operand is the only user of its workspace pair and the
intervening instructions leave the FP result intact. The existing
spill of a pending call result still takes place before the conversion.

`Create_Ray` generates five `FPI32TOR64` instructions per call on T800.
All five now feed their next FP operation from the stack. The two
divisions use `FPREV` to put numerator and denominator in the required
order. The fixture checks both sides of subtraction, comparison,
signed limits, volatile integer loads, two conversions in one expression
and calls on either side of the conversion. Disassembly confirms matching
instruction sequences for the unsigned, wide and single-precision control
functions and for the tested functions in the software-FP builds.

## Profiles

The baseline uses the compiler at `5aa73157`, with the same renderer,
runtime, scene inputs, and emulator. The linked images run in T9000
emulator mode for libc, and cycles are estimated with the T800 timing
model from decoded instruction histograms.

| Measurement | Baseline | Candidate | Saved |
| --- | ---: | ---: | ---: |
| Box `Create_Ray` cycles | 443,583,690 | 429,452,490 | 14,131,200 (3.19%) |
| Box whole render cycles | 4,615,613,271 | 4,599,992,317 | 15,620,954 (0.338%) |
| Box whole render instructions | 1,345,605,245 | 1,339,331,961 | 6,273,284 |
| Basicvue `Create_Ray` cycles | 443,522,590 | 429,391,390 | 14,131,200 (3.19%) |
| Basicvue whole render cycles | 7,862,371,089 | 7,843,431,862 | 18,939,227 (0.241%) |
| Basicvue whole render instructions | 2,454,252,816 | 2,446,320,396 | 7,932,420 |
| Linked renderer ELF bytes | 325,572 | 324,612 | 960 |

`Create_Ray` executes 1,536,000 signed conversions in each scene. The
paired profile removes the same number of double stores and reloads in
the routine, plus 3,072,000 `LDLP` instructions. It adds 614,400 `FPREV`
instructions to preserve division order. `do_diffuse` accounts for most
of the gain outside ray setup, saving 1,478,208 cycles in box and
4,796,480 in basicvue.

## Validation

- Doom matches the eleven phase hashes and modeled cycle counts on
  T800 and T9000. CoreMark validates its CRCs and matches both baseline
  tick totals.
- All four raytracer images match their references at one and four nodes
  with both compilers.
- Complete POV-Ray box, basicvue and glass match the 640×480 packaged
  images byte for byte with the recovered INMOS random generator. The
  strict links and emulator exits pass. Glass SHA-256 is
  `79a805d547b93c68fc81c16e2c476ea09eed23aab17e091712fc8f0d1c9ef54c`.
- The focused fixture passes 16 runs across the baseline and candidate,
  T414/T425/T800/T9000, and code pads of 0 and 1027 bytes. An additional
  58 retained target runs pass. Differential fuzzing passes seed 329 and
  seeds 24601–24700 at both code layouts.

| Frozen input | SHA-256 |
| --- | --- |
| Baseline compiler | `0b7c47027b1a4c97f44e2c223bb730d91cf32520c3f15a90f5a163afb63ccb96` |
| Candidate compiler | `c0cc5d43dc0699961abaee9534a458e8b1d6c31cda55e9a0f8e69327eb8a9311` |
| Candidate `transputer-gen.c` | `dc603fbcbba2191af62022558ba87450fef19bcfcf5dd7d31b878c326d48c2b7` |

Profiles, emitted instructions, fixtures and the validation receipt are in
`logs/compiler-coordination/signed-i32-fp-stack/`.
