# Loop-pool surviving-reference trial, 2026-11-06

A trial of `tp_loop_pool_close` checked recorded LDC sites before emitting
preheader initializers and skipped an entry if none of its sites survived.
It preserved the existing slot allocation, VLA guard, goto/Duff handling and
reference validation. The trial was **not merged**: on the normal compiler,
Doom, CoreMark and raytracer executables were byte-for-byte identical to the
baseline, so no workload gain was established. It was tested in isolation;
no changes were made to `tp_slot_remap` or `tp_gaddr_cache`.

`TP_POOL_DEBUG=1` showed genuine dead candidates, including `R_DrawSpan`,
`R_DrawSpanLow`, `R_RenderMaskedSegRange`, and `fnv` in the Doom benchmark:
the baseline reported one initializer and zero surviving sites, while the
trial reported zero initializers and zero surviving sites. For an isolated
FNV loop, compiling with `TP_NO_SCHED=1` removed `ldc 16777619; stl slot`
from its preheader. With the normal scheduler enabled, the before and after
FNV objects were byte-identical: the later pass had already removed it.
This does not prove that every possible dead initializer is removed.

Both compilers passed `test_loop_pool`, `test_loop_pool_goto` (including
Duff's device), and `test_vla` on T414, T425, T800, T805, T9000 and T9000TVX.
The respective UART output matched byte-for-byte. T805 used the T800 emulator
model and the TVX test used the T9000 model without TVX instructions.

The baseline was the isolated compiler at `41ef080e`, equivalent for the
measured workloads to shared commit `16e7e460`. Its SHA-256 was
`a351ebc9c60278120110901b418fcdbd9da9e966c6e2e499b3312ba9b515ab20`;
the trial compiler was
`ca5fb08b75cbc16014751bb939396d149b1849c0d42086e6561ada37fcc8399c`.
All runs used the same T4 binary
`4175a6609d9222ca402e0a033000522e79dc137da33be256f389da13abb0966a`,
which reports modeled T800 cycles at normal quit, and the same runtime
archives, inputs and flags described in the [integer-to-FPU report](itof-direct-2026-11-06.md).
Modelled cycles are estimates, not hardware measurements.

As a separate cost check, compiling the baseline with `TP_POOL=0` disabled
pooling entirely. The following Doom results are the normal pool-on baseline
and pool-off comparison, both `-mt800`; phase hashes matched and the built-in
check returned `0 0`. The Doom harness's uncalled code has unresolved-symbol
warnings, as described in the integer-to-FPU report.

| Phase | Pool on cycles | Pool off cycles | Hash |
|---|---:|---:|---|
| column | 2,608,228 | 2,609,168 | `b6eadcb0` |
| tcolumn | 721,001 | 721,066 | `5a9ca119` |
| span | 4,413,304 | 4,414,288 | `05d26359` |
| segloop | 2,445,729 | 2,446,041 | `6b9d321f` |
| fixedmul | 663,647 | 667,740 | `58d667ae` |
| fixeddiv | 816,598 | 820,691 | `63da69d9` |
| palblit | 1,718,015 | 1,718,411 | `23091322` |
| rowcopy | 170,250 | 170,486 | `23091322` |
| fill | 992,020 | 992,615 | `a9ac55c5` |
| masked | 1,463,818 | 1,464,456 | `9c0f0700` |
| mapplane | 8,783,742 | 8,784,277 | `e3973655` |

CoreMark used fixed 2K performance seeds and one iteration. T800 estimated
cycles were 5,008,259 on and 5,013,469 off; T9000-generated code on the
T800 estimator was 3,319,286 on and 3,329,168 off. Both settings produced
seed CRC `e9f5`, list `e714`, matrix `1fd7`, and state `8e3a`. These short
cycle-clock diagnostic runs are not official CoreMark scores.

The 256×256 one-node model-3 raytracer ran 405,061 rays and produced the same
reference image with both settings, SHA-256
`672e73749ea0ee27c3e452fe0dbd558dbc23cdae61741b399557f7f1a26fdbb2`.
Whole-run modeled cycles were 3,654,480,420 with pooling and 3,655,224,264
without, a difference of 743,844 cycles. Pool-off executed fewer modeled
instructions (1,175,624,560 versus 1,175,888,819) but more prefix bytes
(693,287,538 versus 691,295,225). This comparison argues against disabling
pooling globally; it does not establish the profitability of each initializer.
A selective policy needs loop-frequency or other reliable profitability
metadata and must preserve preheader dominance, zero-trip behavior and VLA
slot lifetime.
