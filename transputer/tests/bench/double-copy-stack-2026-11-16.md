# Keep both words of a local double copy on the evaluation stack

The previous double copy already used `STNL 0` and `STNL 1` through one
parked address. The inverse vector transforms still parked the high word
before the low-word store and reloaded it afterwards. The new lowering
removes that round trip.

```text
LDL high
LDL low
LDL destination
STNL 0
LDL destination
STNL 1
```

The first store consumes the address and low word, leaving high on the
evaluation stack. Both source words are read before either write. The raw
integer stores preserve the bit pattern, including for an exact self-copy.

The typed copy emitter marks its final store in the emission history.
Statement cleanup requires an empty value stack, the exact marked trailing
copy and two consecutive aligned local loads. This gives the copy all three
evaluation registers and lets it read the nonvolatile high word first.
History barriers preserve label and relocation boundaries. Assignments
that supply a value, nested expressions with pending values, packed accesses
and volatile accesses use their existing paths.

## Measured gain

The comparison uses the frozen `31fab6a8` baseline compiler, its reference
image runs and its profiles, and verifies matching non-compiler
inputs. Complete renders use one node and the T800 cycle model, with
emulator instruction combination disabled for profiling.

| Box measurement | Baseline | Candidate | Saved |
| --- | ---: | ---: | ---: |
| `MInverseTransformVector` cycles | 405,715,016 | 401,822,228 | 3,892,788 (0.96%) |
| `MInvTransVector` cycles | 344,854,134 | 341,185,473 | 3,668,661 (1.06%) |
| Whole render cycles | 4,641,992,883 | 4,633,779,615 | 8,213,268 (0.177%) |
| Whole render decoded instructions | 1,370,112,165 | 1,364,636,653 | 5,475,512 |
| Linked renderer ELF bytes | 326,036 | 325,724 | 312 |

The profile identifies 1,297,596 copies in `MInverseTransformVector` and
1,222,887 in `MInvTransVector`. The same sites execute in both builds.
Each candidate copy removes one workspace store and one reload, saving
three modeled cycles. Together the transforms save **7,561,449 cycles**.
`Intersect_Boxx` and `MTransNormal` account for most of the remaining
savings. Across box the candidate removes 2,737,756 workspace stores and
the same number of reloads. Paired FPU double transfers and nonlocal word
stores have identical counts.

The complete basicvue renders both take 7,862,371,089 modeled cycles. The
inverse transform copy sites counted in box are absent from its execution
profile.

## Validation

- Doom's eleven phase hashes, modeled cycles and `check 0 0` match on
  T800 and T9000. CoreMark passes its CRC checks on both targets.
- All four raytracer references match at one and four nodes with both
  compilers, for sixteen comparisons.
- Complete 640×480 POV-Ray box, basicvue and glass images match byte for
  byte with the recovered INMOS random generator and buffered RGB output.
  Strict link logs are empty and the emulator exits normally.
- The new regression passes 16 executions across two compilers, four
  CPU targets and two code layouts. It covers raw NaN bits, signed zero,
  exact aliasing, a for-loop step, an assignment result used by a call,
  and a pending outer expression. Static checks verify the stack-only
  alias and for-step copies and byte identity of the result-used, nested,
  packed and volatile controls on all four CPUs.
- The standard UART runner passes 42 target regressions. Differential
  fuzzing passes seed 329 and seeds 24301–24400 at pads 0 and 1027.

| Frozen input | SHA-256 |
| --- | --- |
| Baseline compiler | `9193b693b29a0c4a92373ca4eef06ffbeb146537507c97b340b99914c9ff5744` |
| Candidate compiler | `0f1d7550f877b421ed314a55418654efc06d6decb571e8498068bcc4824eeebf` |
| Candidate `transputer-gen.c` | `2e186f057c715bf8a361deb166b9d8b2c524643ed2dd547416d5868d664fae99` |
| Candidate `transputer-fp.h` | `e73925599b590bbf7e3a382d7c5d200c212e263be23974d4276cf6afeaefa9b9` |
| Candidate `transputer-hist.h` | `576049422b84ad6b933492ad3b205852f168f1c6a7d6e0ae453bd03758e7c381` |

Profiles, instruction-site counts, objects, scripts and the acceptance
receipt are in `logs/compiler-coordination/double-copy-stack/`. The frozen
baseline runs are in `logs/compiler-coordination/double-copy-words/`.
