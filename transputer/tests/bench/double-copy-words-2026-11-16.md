# Address both words of a double copy from one pointer

The compiler now emits `STNL 0` and `STNL 1` for aligned double copies into
memory whose destination address is parked in a workspace register. The
second store uses the word displacement encoded in the instruction. The
previous path advanced the pointer by four bytes with `LDC 4; BSUB` and
stored through the advanced address.

The front end recognizes a memory source of the same double type before
the normal load turns it into a workspace pair. That load captures both
source words before the first write. The new store reads the saved words
low then high and uses a distinct, already evaluated address slot for both
writes. The original pair is the value of the assignment. This preserves
the stored bits, including signed zero, subnormals and NaN payloads, and
works when the source and destination are the same object. Packed and
volatile accesses and other address shapes use the general store path.

## Profile and measured gain

The baseline at `a8b91982` executes the old pointer-advance pattern at three
double copies in each inverse vector transform. The complete box profile
counts **1,297,596** copies in `MInverseTransformVector` and **1,222,887** in
`MInvTransVector`. The candidate executes the same counts at the new word
displacement sites. Each copy saves two modeled T800 cycles in those
routines, for **5,040,966 cycles** between them.

| Complete POV-Ray render | Baseline cycles | Candidate cycles | Saving |
| --- | ---: | ---: | ---: |
| Box | 4,650,716,798 | 4,641,992,883 | 8,723,915 (0.188%) |
| Basicvue | 7,868,695,945 | 7,862,371,089 | 6,324,856 (0.080%) |

Box executes 8,522,041 fewer decoded instructions and 4,114,441 fewer
`BSUB` instructions. `Clip_Colour` saves 2,448,684 modeled cycles by copying
in-range components more cheaply. The main basicvue gains are in
`Object_Intersect` (2,821,375 cycles), `Clip_Colour` (2,285,474) and
`Intersect_Sphere` (1,218,004). The linked renderer shrinks 520 bytes,
from 326,556 to 326,036. The profiles attribute the gains to the integer
address and staging instructions. FPU double transfer counts and total
nonlocal word store counts match.

The comparison reuses the frozen `a8b91982` builds and exact image profiles.
It verifies matching non-compiler inputs. Both full render profiles use one
node, decoded instructions with combination disabled and a T800 cycle
model.

## Validation

- Doom passes all eleven phase hashes and `check 0 0` on T800 and T9000.
  Modeled cycles match on every measured phase.
- CoreMark preserves CRCs `e9f5/e714/1fd7/8e3a/e714` on both targets.
  The diagnostic iteration measures 4,970,600 modeled cycles for T800
  code and 3,271,251 for T9000 code on both compilers.
- All four raytracer scenes match at one and four nodes across both
  compilers, for sixteen full reference comparisons.
- Complete 640×480 POV-Ray box, basicvue and glass images match byte for
  byte with the recovered INMOS random generator and buffered RGB output.
  The strict links are clean and the emulator exits normally.
- The raw-copy fixture passes 16 executions across two compilers, four
  CPU targets and two layouts. It covers signaling NaN bits, signed zero,
  a subnormal, infinity, self-copy, results carried across a call, chained
  assignments, indexed members, neighboring words, volatile and packed
  storage. Static checks verify the encoded word offset and byte-identical
  packed and volatile routines on all four CPUs.
- The standard UART runner passes 38 retained and new target regressions.
  Differential fuzzing passes seed 329 and seeds 24201–24300 at pads 0 and
  1027.

| Frozen input | SHA-256 |
| --- | --- |
| Baseline compiler | `2b8dc0c780a7cedbbc1b48fc450a4cc87534dfa246d2a5a1b7821d0f00089aec` |
| Candidate compiler | `9193b693b29a0c4a92373ca4eef06ffbeb146537507c97b340b99914c9ff5744` |
| Candidate `transputer-gen.c` | `a1f70315f7262489c54fe693bcb0a4b50a39b7103852ad4a30c1828004b17b19` |
| Candidate `tccgen.c` | `8463faf744c62fe80da70cfb305f549bef1a37533e5fef243633747c84de9bc5` |
| Candidate `tcc.h` | `eb62898b92bc7adf8b0433af18f9de2270907a2b98c91560bfb586684c5d7bfc` |

Profiles, objects, validation scripts and the acceptance receipt are in
`logs/compiler-coordination/double-copy-words/`. The frozen baseline
images and profiles are in `logs/compiler-coordination/gaddr-first-use/`.
