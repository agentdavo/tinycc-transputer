# Reuse consumed double operand pairs — 6 November 2026

The fully guarded baseline/candidate pair passes all four workload gates,
including complete glass, with exact-reference and normal-exit receipts.

Double arithmetic previously allocated a fresh result pair even when an
operand's pair was about to die. The existing float destination-reuse helper
now accepts doubles too, requiring a raw, contiguous pair and no reference
to either word from any value outside the two operands being consumed.
Nonadjacent or flagged pairs retain the allocation/spill path.

Both operands are consumed before the result store overwrites the donated
pair. No caller ABI changes. The existing FP residency rules can then keep
more intermediate results in FA/FB rather than spilling and reloading them.

## Producer-copy dependency repair

The new pressure fixture exposed a dependency missing from the existing FP
producer-reordering rule. In this valid expression:

```c
double x;
double y = (x = a + b) * (a - b);
return x + y;
```

An integer copy into `x` still reads the producer's parked result. Moving
the producer past that copy and removing its store makes the copy read
uninitialized workspace. The reordering helper now rejects direct workspace
accesses overlapping either producer word; the single-precision case checks
its one word. It reuses the shared opcode classifier and overlap helper.

The baseline already miscompiles the analogous float expression on T800
and T9000. The new standalone regression reproduces those two failures;
the corrected candidate passes float/double cases on all four CPUs.
T414/T425 baseline controls pass. This is a reproduced baseline defect,
separate from the 128 passing paired target/layout regressions below.

## Preserve pairs at the outgoing-prefix boundary

Full glass exposed a second prerequisite. A turbulence loop calls a helper
with a pointer and three doubles: seven argument words, four outgoing stack
words. Reuse can leave all its FP temporary pairs at slots 3/4. The slot
remapper previously kept slot 3 pinned as outgoing storage while moving
slot 4 independently. FPSTNLDB still writes adjacent words, so subsequent
loads read the wrong high word and overwrite another remapped value.

The remapper now queries the shared LDLP-consumer extent and declines
remapping when a multiword access crosses from the pinned outgoing prefix
into movable words. This preserves the pair without hard-coding function
names. The minimized turbulence regression reproduces the issue; disabling
remapping and the final guard both restore its exact values. Object-group
and function-level four-row glass checks isolated this defect to DTurbulence
before restarting full acceptance. The mismatching intermediate build is
not an accepted compiler.

## Real workload gain

Baseline: backend accepted in `72cfafde`. Both snapshots use identical
runtime archives and isolated T4 `2853b29`. All candidate results below
use the final dependency- and boundary-checked compiler.

| Full generic POV box | Baseline | Candidate |
| --- | ---: | ---: |
| Decoded instructions | 1,756,097,163 | 1,592,403,106 |
| Modeled T800 cycles | 5,523,527,759 | 5,151,451,525 |
| Executed prefix bytes | 1,104,648,796 | 1,043,357,827 |
| Linked ELF bytes | 370,908 | 355,276 |

The complete reference-exact render removes **163,694,057 decoded
instructions** and **372,076,234 modeled cycles (6.73621%)**. The ELF
shrinks **15,632 bytes**. Gains concentrate in actual renderer math:

| Function | Removed decoded instructions |
| --- | ---: |
| MInvTransVector | 53,807,028 |
| MInverseTransformVector | 53,633,968 |
| Intersect_Sphere | 20,999,367 |
| Create_Ray | 9,830,400 |
| do_phong | 7,017,174 |
| __ieee754_pow | 5,414,772 |
| do_light | 3,916,731 |
| Compute_Reflected_Colour | 2,912,441 |
| do_diffuse | 2,864,028 |

FPSTNLDB executions fall by **15,504,518**. Ordinary FPLDNLDB loads fall
by 5,572,997 and fused load/add operations by 9,972,782; those additions
become register FPADD operations. Likewise 504,880 fused load/multiply
operations become FPMUL, with FP DUP supplying an already-available square
operand. Logical add, subtract, multiply and divide totals remain unchanged.
The change removes transfers and allocation spills, preserving arithmetic
operations rather than reassociating them.

Profiling disables emulator instruction combination. Counts are decoded
single-node executions with the T800 timing table, not native hardware
timings or sixteen-node array scaling measurements.

## Compound-loop measurements

| Focused T800 kernel | Baseline modeled cycles | Candidate | Saving |
| --- | ---: | ---: | ---: |
| 4,096 compound leaf calls | 791,200 | 676,512 | 14.49% |
| 20,000 compound-product iterations | 3,800,566 | 2,460,553 | 35.26% |
| 20,000 dot-product iterations | 3,080,366 | 1,720,349 | 44.15% |

These include timer/label overhead. The compound product loop's T800/T9000
function shrinks **94 → 50 bytes**, frame **18 → 13 words**, static LDL/STL
instructions **18 → 7**, and FP double stores **4 → 1**, with matching
logical arithmetic counts. Both probe functions remain byte-identical on
T414/T425. Ten retained type/volatile/packed controls are byte-identical
on all four CPUs.

## Workload and numerical gates

* **Doom:** eleven hardened phase hashes pass with `check 0 0`, baseline
  and final candidate on T800/T9000. Paired ELFs are byte-identical per CPU.
  This is the extracted phase benchmark, not full gameplay.
* **CoreMark:** four diagnostic runs retain `e9f5/e714/1fd7/8e3a/e714`.
  Whole-fixture modeled cycles change 5,333,037 → 5,333,026 for T800
  code and 3,539,348 → 3,539,337 for T9000 code, using the same T800 model.
  One iteration is not a published score.
* **Raytracer:** all four references match at one/four nodes with both
  compilers, sixteen comparisons. Paired ray ELFs are byte-identical.
* **POV-Ray:** baseline and final candidate match box, basicvue and glass:
  six complete 640×480 reference comparisons. Strict link logs are empty,
  emulator exits are normal and renderer statistics match across the pair.

| Full POV scene | Bytes | SHA-256, both versions and reference |
| --- | ---: | --- |
| box | 921,615 | `fcaeaccaea31d263bede7a2f8774e2e7af8dcfc6f640a893a3fd1d3b8d1d6db0` |
| basicvue | 921,615 | `d537ba4e52842f8cd3e35f8c59bd3d30ac2c3008b60eafc10f7b6ff0cf17cde7` |
| glass | 921,615 | `79a805d547b93c68fc81c16e2c476ea09eed23aab17e091712fc8f0d1c9ef54c` |

POV rebuilds the generic renderer and fdlibm with each compiler. Recovered
INMOS RNG and buffered RGB rows are fixed across both builds. Existing libc
is fixed and requires T9000 emulator mode. The fixture is distinct from the
historical sixteen-node ICC application.

The new seventeen-case pair fixture covers chains, computed pairs,
subtraction/division, three products, deep expressions, wide/indirect call
arguments, assignment values, branches, nested calls, mixed precision,
long double, compound loops, signed zero, NaN classification and wide frames.
It plus the retained store/return/parameter/alignment/sqrt/epilogue suites
and the new outgoing-prefix boundary fixture pass baseline/candidate × four
CPUs × two layouts: **128 executions**.
Both double ABI fixtures pass old callers/new helpers and the reverse:
**32 interoperability runs**. Eight VLA-return runs also pass.

An independent generated-expression check uses seed **32972**, 48 finite
compound expressions and Python IEEE double bit patterns. Baseline and
candidate pass on all four CPUs: **384 bit comparisons**. Sources and the
generator are retained. The normal UART runner checks the complete oracles
of all three new fixtures and the retained store/return fixtures.

| Frozen input | SHA-256 |
| --- | --- |
| Baseline compiler | `1964959068b10b836a60b3b00371d037be33ed1f6f5045d1bb6ebef7583b7519` |
| Candidate compiler | `8d6ae7997722fba4d7159bd7c2da9bfcac7c4eb6b9dd1a1d0a27826b14ea8b7a` |
| Baseline backend | `131777b649a1420c963fef027bffa94e1454116c4a11f144e30bf27cc1f4822c` |
| Candidate backend | `99709e0da77a886e814c02f52dc354c2d1fd500087c25ed6ff43549456af6efe` |

Artifacts are retained in `logs/compiler-coordination/double-pair-reuse/`:
frozen compilers/sources, strict logs, apps/images, input fingerprints,
disassemblies, loop timings, generated expressions and decoded profiles.
`acceptance.json` verifies all six full images, exits/statistics, matching
paired POV inputs, regression receipts and the exact tested backend.
No kernel, installed runtime archive, historical BTL or original POV math
source changes are included.
