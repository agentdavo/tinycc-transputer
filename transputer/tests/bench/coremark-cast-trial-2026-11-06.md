# Narrowing-cast spill trial, 2026-11-06

The proposed `gen_cvt_csti` trial replaced its `Tp_LDL(slot)` after
`gv(RC_INT)` with `tp_push_reg_or_cancel_stl(slot, vtop)`. The latter checks
that the immediately preceding `STL` is dead, accounts for other live vstack
references, and does not roll back in no-code mode. This was tested as a
private trial and **not retained**: the normal scheduler already removes the
same pairs for the workload below.

With fixed CoreMark performance seeds and one iteration, the before/after
executables were byte-for-byte identical on all four targets. `crcu8` had
143 decoded instructions on T414, T425, and T800, and 98 on T9000, both
before and after. All runs matched the reference CoreMark CRCs; T4's
T800-cycle estimates for the timed region were also unchanged:

| Target | Before | After |
|---|---:|---:|
| T414 | 5,034,850 | 5,034,850 |
| T425 | 5,008,258 | 5,008,258 |
| T800 | 5,008,259 | 5,008,259 |
| T9000 | 3,319,286 | 3,319,286 |

These are diagnostic estimates, not official CoreMark scores. A temporary
instrumented compiler recorded 16 cancellations at cast emission per build,
including four while compiling `core_util.c`. They produced no difference in
the normal scheduled executables. With `TP_NO_SCHED=1`, the T414 executable
fell from 28,476 to 28,452 bytes; the `core_util.o` disassembly contained 14
adjacent `stl`/`ldl` pairs before and 11 after. The `crcu8` disassembly itself
was unchanged even in that diagnostic configuration. These results explain
why the candidate is visible during emission but does not improve the normal
build; they do not establish a benefit in a scheduler-disabled product.

Both compilers produced identical objects and passed emulator probes for
signed and unsigned byte/halfword boundaries, casts in expressions, a shared
input value, volatile loads, packed fields, and a `sizeof` no-code context.
The probes passed on `-mt414/-s414`, `-mt425/-s425`, `-mt800/-s8`,
`-mt805/-s8`, and `-mt9000` and `-mt9000tvx` on `-s9`. The `-s8` T805 run
uses the shared T800 model, and the TVX probe does not execute TVX operations.

The baseline compiler was built from the checkout headed at `60f7d61e`; the
trial was the single cast-site change described above. The CoreMark setup and
measurement limitations are described in
[the baseline report](coremark-baseline-2026-11-06.md). No benchmark source
was modified.
