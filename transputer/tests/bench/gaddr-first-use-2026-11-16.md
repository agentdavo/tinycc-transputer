# Cache global addresses at first use

The compiler can cache a symbol with two address loads in one basic block.
It uses the original relocation at the first load, stores and reloads the
address in a private frame word, and replaces the second load with `LDL`.
This pays for the cache when the block runs. The existing entry-cache policy
continues to handle other symbols and wide constants.

Both complete address placeholders must lie in one block. Branch targets,
labels, jump tables, calls and returns delimit the regions used by the
analysis. Functions with unmodeled transfers decline the first-use form.
The `LDC; STL; LDL` initializer preserves the original evaluation stack
shape. The local replacement is selected only when the store and both
workspace loads encode in fewer than the eight bytes of an absolute address
load. The first relocation remains at its original use, with ordinary
relaxation moving it with the code. The second use's relocation is removed.

## POV-Ray measurement

The baseline is the validated compiler at `f21095b5`. The comparison reuses
its frozen image and profile, and verifies matching non-compiler inputs.
Both profiles use complete, reference-exact single-node renders, decoded
instructions with T4 instruction combination disabled, and a T800 cycle model.

| Box measurement | Baseline | Candidate |
| --- | ---: | ---: |
| `All_Object_Intersections` modeled cycles | 263,647,575 | 215,001,335 |
| `All_Object_Intersections` decoded instructions | 89,098,681 | 79,369,433 |
| Whole render modeled cycles | 4,701,062,422 | 4,650,716,798 |
| Whole render decoded instructions | 1,380,323,052 | 1,378,634,206 |
| Whole render executed prefix bytes | 915,591,178 | 859,897,128 |
| Linked renderer ELF bytes | 326,644 | 326,556 |

`All_Object_Intersections` executes **48,646,240 fewer modeled cycles
(18.45%)**. Its four counter addresses for bounding and clipping are now
loaded in the corresponding counter-update blocks. Box executes none of
those blocks, saving **40 cycles on each of 1,216,156 calls**. The entry
load for `Options` is shared across several blocks.

The whole render saves **50,345,624 modeled cycles (1.07%)**. `pq_free`
saves another 6,714,555 cycles by caching a pair that formerly loaded its
address twice. Where a block runs repeatedly, first-use initialization can
cost more than an entry load. In box, caching `Number_Of_Pixels` at first
use costs `Start_Tracing` 3,071,990 extra cycles. Caching `Shadow_Ray_Tests`
the same way costs `Diffuse` 1,941,270 extra cycles.

| Basicvue measurement | Baseline | Candidate | Saved |
| --- | ---: | ---: | ---: |
| `All_Object_Intersections` modeled cycles | 564,836,076 | 467,422,476 | 97,413,600 (17.25%) |
| Whole render modeled cycles | 7,965,956,131 | 7,868,695,945 | 97,260,186 (1.22%) |

The four counter loads save 40 cycles on each of 2,435,340 calls in basicvue.
`Diffuse` and `Start_Tracing` cost another 8,993,400 and 3,071,990 cycles,
respectively, in this scene. The whole-render figure includes those costs.

## Validation

- Doom passes all eleven phase hashes and `check 0 0` on T800 and T9000.
  On each target, `segloop` costs 3,510 more modeled cycles and `masked`
  saves 2,560. The other phases match their baseline cycles.
- CoreMark passes `e9f5/e714/1fd7/8e3a/e714` on both targets. The diagnostic
  iteration takes 4,970,600 modeled cycles for T800 code and 3,271,251 for
  T9000 code across both compilers, using the T800 cycle model.
- The raytracer matches all four reference images at one and four nodes,
  for sixteen baseline and candidate image comparisons.
- The complete 640×480 POV-Ray box, basicvue and glass reference images
  match with the recovered INMOS random generator and buffered RGB output.
  The strict link logs are empty and the emulator exits normally.
- The focused regression passes 16 runs across two compilers, four CPUs
  and two code layouts. Code generation selects first-use caching for
  the conditional and loop pairs. The goto, switch and wide-frame controls
  exercise other paths. The standard UART runner passes 30 executions of
  the new fixture and retained address, control-flow and scheduler fixtures.
- Differential fuzzing passes seed 329 and seeds 24101–24200 at pads 0
  and 1027.

| Frozen input | SHA-256 |
| --- | --- |
| Baseline compiler | `8560a83d6a31d8cb5d6d461379d087bf1980ce06ef28820e92c65861daa1a565` |
| Candidate compiler | `2b8dc0c780a7cedbbc1b48fc450a4cc87534dfa246d2a5a1b7821d0f00089aec` |
| Baseline backend | `6e4d7271527c0c1b2287b64d9ce3d6dc82017c04d6a6c53083547560a0c23677` |
| Candidate backend | `0f364bec7909374c74638135ee1ee23247357dfb3972781e7cff4931a84da46e` |

Paired profiles, objects, validation scripts and the acceptance receipt
are in `logs/compiler-coordination/gaddr-first-use/`. The frozen baseline
render and box profile are in `logs/compiler-coordination/sched-object-escape/`.
