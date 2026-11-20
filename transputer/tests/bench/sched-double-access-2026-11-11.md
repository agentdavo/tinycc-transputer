# The scheduler takes functions with double accesses — 11 November 2026

`tp_sched_function` declined any function that loaded or stored a double
through a pseudo-register pair. It now models the access, and the complete
POV box render saves a further 1.21% of its modeled T800 cycles,
reference-exact, with no routine slower.

## What was declined

The pass refuses a function when a pseudo-register's address goes somewhere
it cannot follow ("ldlp-escape"). It followed `ldlp r; ldnl/stnl` and the
single-length FP forms, but not `ldlp r; fpldnldb`, `fpldnladddb`,
`fpldnlmuldb` or `fpstnldb`, which reach words r and r+1, nor the integer
conversions `fpi32tor32`, `fpi32tor64`, `fpb32tor64` and `fpstnli32`. Every
double that passes through a register pair uses one of these, so after the
indirect-call change the same day 126 functions of the POV build were still
declined for this reason alone, `Start_Tracing`, `Determine_Surface_Colour`
and `Intersect_Sphere` among them.

## Change and safety boundary

`tps_annotate` records a double access as a read of both words (`uses`, the
mask calls already use for stacked arguments) and a conversion as a read of
its word. A store is recorded as a read too, as the single-length stores
already are: the pair is then live wherever an access can see it, so no rule
drops a store to either word before it, and nothing is treated as defined by
it.

Each rule was checked against an access inside its window:

* S1, S2, S3, S6, S7, S9 and S10 decide by liveness, which now includes both
  words.
* S4/S5 carried a value across code that read its slot only through `use`
  and `def`; they now stop at a `uses` reader as well. That was the one rule
  that needed a change.
* S11 already stops at any `ldlp` of x, x-1 or a, and at a `uses` reader.
* Copy propagation needs `frame_private`, which any FP access clears.
* The push hoist admits no FPU instruction in its run.

FP instructions other than the loads, stores and the four arithmetic ones
stay unknown to the stack model, so no rule reasons across them. The local
range is still given up when a local's address feeds an FP access (the
pre-scan is unchanged), so only the register file is reasoned about in these
functions. `TP_SCHED_DBL=0` declines them as before for an A/B run.

Soft-float parts emit none of these instructions: every `-mt425` and
`-mt414` object is byte-identical.

## Measured

Baseline `main` at `27efe07e`; same runtime archives, libc and emulators as
the earlier acceptances.

| Complete generic POV box | Baseline | Candidate |
| --- | ---: | ---: |
| Decoded instructions | 1,430,736,765 | 1,393,460,486 |
| Modeled T800 cycles | 4,787,321,056 | 4,729,581,591 |
| Linked ELF bytes | 336,740 | 328,876 |

**57,739,465 fewer modeled cycles (1.206%), 37,276,279 fewer instructions**,
7,864 bytes smaller. `ldl` falls by 30.5 M, `stl` by 20.8 M and `ldnlp` by
6.8 M; `rev` rises by 1.6 M.

| Routine | Fewer modeled cycles |
| --- | ---: |
| Start_Tracing | 13,216,338 |
| Intersect_Sphere | 6,967,540 |
| Compute_Reflected_Colour | 6,728,743 |
| Determine_Surface_Colour | 6,672,790 |
| All_Sphere_Intersections | 6,369,824 |
| All_Box_Intersections | 6,167,814 |
| Create_Ray | 5,529,600 |
| __ieee754_pow | 3,042,217 |

No routine is slower. Doom has no doubles in its measured phases and is
cycle-identical. Model results from decoded single-node counts, not hardware
timings.

Since `6e2f6d81` on 10 November the box render has gone from 5,091,332,488
to 4,729,581,591 modeled cycles, 7.1%.

## Validation

* **Fixture:** `test_sched_double_access.c`, 20 cases (`Needs: fpu`): a pair
  written by words then loaded, stored then read by words, negation and sign
  masking on the high word, int, unsigned and long long conversions each way,
  integers carried beside double arithmetic, compares, selects, a call
  between uses, stacked arguments, loops, word copies, a union returned,
  float with double, indexed loads, a struct with both. With seven retained
  double and packed fixtures and the indirect-call one: 64 runs on four CPUs
  at two layouts.
* **Object identity** against `27efe07e`: 453 and 457 objects identical at
  `-mt414` and `-mt425`; 47 and 48 change at `-mt800` and `-mt9000`.
* **Full suites:** c-testsuite 244/244 at `-mt9000`, `-mt800`, `-mt425` and
  `-mt414`; local suite 265, 249 and 249 passes at `-mt9000`, `-mt800` and
  `-mt425`, no failures.
* **Differential fuzz:** seeds 22001-22150 at pads 0 and 1027, 150 pass.
* **Mixed-version ABI** 32 runs, **VLA return** eight, **generated
  expressions** on four CPUs.
* **Doom** hashes and `check 0 0`, **CoreMark** CRCs, sixteen **raytracer**
  reference matches, **POV-Ray** box, basicvue and glass reference matches.
* **Kernel:** linux-7 and userspace rebuilt with the candidate boot to
  `DESKTOP_LOOP`.

## Second step: locals

The pre-scan gave up the whole local range (`nslot = nreg`) when any local's
address fed an FP access, which is how every float or double local is read.
Such an access reaches one or two known words and the address goes nowhere
else, so `tps_prescan` now passes over it (`tps_fp_extent`) and the
annotation records the read as it does for a register pair. An address that
does go elsewhere (`&v` to a callee, an indexed load) still gives the range
up. `TP_SCHED_DBL=1` keeps the first step alone.

Baseline `main` at `692f20a6`:

| Complete generic POV box | Baseline | Candidate |
| --- | ---: | ---: |
| Decoded instructions | 1,393,460,486 | 1,385,332,032 |
| Modeled T800 cycles | 4,729,581,591 | 4,711,670,050 |
| Linked ELF bytes | 328,876 | 327,724 |

**17,911,541 fewer modeled cycles (0.379%)**, no routine slower: `Trace`
13.5 M, `__ieee754_pow` 2.3 M, `Determine_Surface_Colour` 1.4 M, `do_light`
0.7 M. The gain is small because most POV routines pass a local vector's
address to a callee, a real escape.

Measured and dropped: leaving `frame_private` set as well, so copy
propagation runs in these functions (with a guard for FP readers and
writers of the copied words). It removed 2.7 M more instructions but cost
2.2 M cycles, the redirected loads reading parameter slots that need a
prefix byte.

Validation of this step: the fixture gained six local cases (26 in all) and
passes with both compilers on four CPUs at two layouts, 64 runs with the
retained fixtures; soft-float objects byte-identical, 43 and 40 objects
change at `-mt800` and `-mt9000`; c-testsuite 244/244 at four parts; local
suite 265, 249 and 249, no failures; difftest seeds 23001-23150 clean;
mixed ABI, VLA return, generated expressions, Doom (cycle-identical),
CoreMark, raytracer and the three POV scenes pass; linux-7 and userspace
boot to `DESKTOP_LOOP`.

## Next

What still gives up the local range is an address that really is handed
out. `tps_word_escapes` already answers that per word for S11; using it in
the pre-scan, so that only the escaped object's words leave the range, is
the remaining step. Global-address entry loads on short loops
(`All_Object_Intersections`) and a use-site cache for two-use addresses are
open from the indirect-call report.

Scripts, compilers, profiles and logs are in
`logs/compiler-coordination/wide-consts/`.
