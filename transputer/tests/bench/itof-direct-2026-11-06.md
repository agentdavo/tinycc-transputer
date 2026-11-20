# Integer-to-FPU direct-storage trial, 2026-11-06

The compiler converts aligned 32-bit local or workspace-backed integers from
existing storage instead of copying them to a scratch word first. Constants,
indirect and packed lvalues, narrower and 64-bit source types, deferred casts,
and T414/T425 software floating point retain the old path. Unsigned words use
`fpb32tor64`; signed words use `fpi32tor32`/`fpi32tor64`. Existing pending FPU
return registers are still evicted first. No store that supplies an operand to
the memory-reading FPU instruction is removed.

Baseline was the isolated source checkout at `5afc5a61`, containing scheduler
fix `5a5ef711` and packed-argument fix `0fd07e87`. The candidate differs only
in `gen_cvt_itof`. Both were compiled using the same host GCC `-O2`; benchmarks
used the same runtime archives and flags. T4 was built from its separate
`2e44501` source with one diagnostic `t8c_report()` call at normal quit; that
one emulator binary was used for both sides of all benchmarks. T800 timings
are estimates, not measurements on silicon.

| Artifact | SHA-256 |
|---|---|
| Baseline compiler | `9105df43f47dfbe3bb5cc2abef76f3f3cc298276182bd13057df670831829edc` |
| Candidate compiler | `a351ebc9c60278120110901b418fcdbd9da9e966c6e2e499b3312ba9b515ab20` |
| Instrumented T4 | `4175a6609d9222ca402e0a033000522e79dc137da33be256f389da13abb0966a` |
| T800 runtime | `dbb6d39c4e13aec053eea8fbae2d550e1dbc64bc3cfb1b8b6fa5b006f3169e06` |
| T9000 runtime | `6750477284c50b500a90ec79e18f8918247d0dafd2c14849aca1ebb8cb72c818` |
| netload | `5f827f450a4993804b6ac99345f8d2dc702ef0226d10e5e233550420692f7a8d` |

The existing `test_float_int_cvt` and a new `test_int_to_float_local` passed
for both compilers on T414, T425, T800, T805, T9000 and T9000TVX (the T805
and TVX tests used the T800 and T9000 models, respectively). The tests cover
signed and unsigned limits, float and double, volatile local storage, packed
fields and pending call results. TVX instructions were not exercised.

## Doom

`transputer/tests/bench/doom.sh t800` used the same inputs and produced
identical phase hashes and T800 modeled cycles on both sides. Its link also
printed unresolved-symbol warnings for code outside these benchmarked phases;
the built-in `check` result was `0 0` on both sides.

| Phase | Cycles before and after | Hash before and after |
|---|---:|---|
| column | 2,608,228 | `b6eadcb0` |
| tcolumn | 721,001 | `5a9ca119` |
| span | 4,413,304 | `05d26359` |
| segloop | 2,445,729 | `6b9d321f` |
| fixedmul | 663,647 | `58d667ae` |
| fixeddiv | 816,598 | `63da69d9` |
| palblit | 1,718,015 | `23091322` |
| rowcopy | 170,250 | `23091322` |
| fill | 992,020 | `a9ac55c5` |
| masked | 1,463,818 | `9c0f0700` |
| mapplane | 8,783,742 | `e3973655` |

## CoreMark

The unchanged benchmark used `PERFORMANCE_RUN=1`, one iteration and the
standard 2K seeds. T800 was 5,008,259 estimated cycles before and after;
T9000-generated code was 3,319,286 before and after on the T800 timing model.
Both sides and targets produced seed CRC `e9f5`, list `e714`, matrix `1fd7`
and state `8e3a`. This one-iteration cycle-clock diagnostic is not an official
CoreMark score; in particular the displayed seconds and score are invalid.

## Raytracer

The unmodified one-node, model-3, 256×256 render used `RES=256`, `SIZES=1`,
`-mt800`, `-s8`, and `T4_EST_CYCLES=1`. Both rendered 405,061 rays and matched
the reference image byte for byte, SHA-256
`672e73749ea0ee27c3e452fe0dbd558dbc23cdae61741b399557f7f1a26fdbb2`.
Whole-run modeled cycles fell from **3,655,261,764 to 3,654,480,420**
(781,344 cycles, approximately 0.021%); modeled instructions fell from
1,176,409,704 to 1,175,888,819. The ELF fell from 21,368 to 21,360 bytes.
A conversion in `raytrace.o` changed from `ldl 19; stl 15; ldlp 15;
fpi32tor32` to `ldlp 19; fpi32tor32`, leaving the original slot store intact.
