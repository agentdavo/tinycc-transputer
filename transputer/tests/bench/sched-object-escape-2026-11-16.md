# Scheduler tracking for escaped workspace objects

Passing a local object's address to a callee previously limited scheduler
liveness to the pseudo-registers. The pre-scan now applies the existing
workspace object boundary analysis to identify the affected words. Those
words stay live throughout the function, so a store remains visible through
an interior pointer, a one-past pointer adjusted back into the object, or a
pointer retained by another function. The scheduler can still eliminate
round trips involving private words.

Inline assembly, uncertain allocation boundaries and unrecognized escapes
from reserved workspace retain conservative handling. The escape analysis
uses the recorded `loc` boundaries and checks the scheduler's existing
32-word window. Copy propagation still requires its existing private frame
proof.

## Paired POV-Ray box measurement

Both compilers rebuilt the generic renderer and fdlibm with the same runtime
archives and libc. The baseline is `c335c128`. The single-node reference
render was profiled with instruction combination disabled and the T800
cycle table. These are modeled cycles.

| Complete box render | Baseline | Candidate |
| --- | ---: | ---: |
| Modeled T800 cycles | 4,711,670,050 | 4,701,062,422 |
| Decoded instructions | 1,385,332,032 | 1,380,323,052 |
| Executed prefix bytes | 918,380,718 | 915,591,178 |
| Workspace word loads (`LDL`) | 354,112,823 | 351,303,715 |
| Workspace word stores (`STL`) | 189,574,691 | 186,765,583 |
| FPU double stores (`FPSTNLDB`) | 25,485,812 | 25,485,812 |
| FPU double loads, including indexed and fused reads | 92,584,232 | 92,584,232 |
| Linked ELF bytes | 327,724 | 326,644 |

The render uses **10,607,628 fewer modeled cycles (0.225%)** and executes
**2,809,108 fewer workspace loads and 2,809,108 fewer workspace stores**.
The largest savings come from `Compute_Reflected_Colour` (3.41 million
cycles), `Start_Tracing` (2.76 million), `Diffuse` (1.47 million) and the
two inverse vector transforms (2.52 million combined).

The scheduler removes double staging through the integer stack. In fdlibm's
`sin`, the two doubles written by range reduction are passed to the kernel
trig routines. One of the four switch arms removes one word spill and reload
per call and the other three remove two each. The corresponding executed
call counts are 125, 249, 250 and 250. This accounts for **1,623 double word
spill/reload pairs** within the overall workspace savings. Direct FPU double
stores and reads, including those whose addresses come from `LDLP`, have
equal counts across the pair.

## Validation

- Doom's eleven phase hashes and instruction and cycle counts match across
  both compilers on T800 and T9000. Each run reports `check 0 0`.
- CoreMark retains `e9f5/e714/1fd7/8e3a/e714` on both targets. The single
  diagnostic iteration takes 4,976,147 → 4,970,600 modeled cycles for T800
  code and 3,278,022 → 3,271,251 for T9000 code, using the T800 timing model.
- The raytracer matches all four reference images at one and four nodes on
  both compilers, for sixteen full image comparisons.
- Both POV-Ray builds match the complete 640×480 box, basicvue and glass
  references with the recovered INMOS random generator and buffered RGB
  output. Strict linker logs are empty and both builds exit normally.
- The escape regression covers array interior and one-past pointers,
  retained aliases, double high-word writes, conditional escape and a
  zero-trip loop. It also checks stores read only through an alias. The
  scheduler, packed-value and double fixtures pass 152 runs across two
  compilers, four CPUs and two code layouts. The FPU-only fixture runs on
  T800 and T9000. The final escape fixture passes all 16 configurations.
- Differential fuzzing covers seed 329 and seeds 24001–24100 at both
  code layouts.

| Frozen input | SHA-256 |
| --- | --- |
| Baseline compiler | `b4212fe7ed1809dfd64248db9693906364f4ef45c0eae2e3131433ff2edca84e` |
| Candidate compiler | `8560a83d6a31d8cb5d6d461379d087bf1980ce06ef28820e92c65861daa1a565` |
| Baseline scheduler source | `08c37d610a36e6d7f48d52ae2fbbff2ba4283148398e655ff809f94ebec64448` |
| Candidate scheduler source | `6e4d7271527c0c1b2287b64d9ce3d6dc82017c04d6a6c53083547560a0c23677` |

The acceptance receipt, cold builds, opcode profiles and reference
comparisons are in `logs/compiler-coordination/sched-object-escape/`.
