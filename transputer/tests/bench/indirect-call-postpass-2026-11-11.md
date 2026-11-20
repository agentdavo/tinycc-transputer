# Functions with indirect calls through the post-pass — 11 November 2026

A call through a function pointer no longer keeps its function out of the
scheduler, the slot remap and the global-address cache. The complete POV box
render saves 1.22% of its modeled T800 cycles, reference-exact; Doom's wall
and sprite loops gain 3-4%.

## How this was found

The plan was the wide-constant lane from the 10 November re-profile: 281 M
cycles of the box render sit in `ldc` instructions of six bytes or more,
226 M of them global addresses and 56 M constants.

Lowering the address cache's threshold from three uses to two was measured
first and is **not** landed: 5.6 M cycles (0.115%), with `Intersect_Boxx`,
`Intersect_Sphere` and `Reflect` slower, because an entry load is paid on
paths that use the address once or not at all.

The cache statistics showed the real limit. `Start_Tracing`, `Trace`,
`Determine_Surface_Colour`, `Diffuse` and `Compute_Reflected_Colour` cached
nothing, though `Start_Tracing` loads one address four times. All five call
through function pointers (POV's object methods), and `gfunc_call` ended such
a call with

```text
ajw -4; ldc k; ldpi; stl 0; ldl fn+4; gcall
```

Between the AJW and the gcall a slot number means a word four higher, so the
cache, the scheduler ("ajw-midfunction") and the slot remap each refused any
function containing one: 47 functions of the POV build, 40 of which the
scheduler had no other reason to decline.

## Change

```text
ldl fn; ldc 5; ldpi; stl -4; ajw -4; gcall
```

The return address goes to `Wptr[-4]`, the callee's `W[0]`, before the AJW,
beside the register words already staged at `Wptr[-1..-3]`. Nothing between
the AJW and the gcall names a workspace word, the callee's `ret` moves Wptr
back, and every slot number in the function has one meaning. The sequence is
one byte longer and one cycle slower than the old one where no pass applies.

`tp_relax_icalls_find` recognises the five-instruction tail by shape
(`ldc 5; ldpi; stl -4; ajw -4; gcall`, the three after the ldpi exactly five
bytes) and pins it: the `ldc 5` counts those bytes, so none may change width
or be separated. The three passes accept an AJW carrying that mark, and the
cache leaves its operand alone when it grows the frame. Any other
mid-function AJW (the computed-goto trampoline) is refused as before.

What the callee sees is unchanged, so objects built before and after link
together.

## Measured

Baseline `main` at `00e15657`; same runtime archives, libc and emulators as
the 10 November acceptance.

| Complete generic POV box | Baseline | Candidate |
| --- | ---: | ---: |
| Decoded instructions | 1,456,420,804 | 1,430,736,765 |
| Modeled T800 cycles | 4,846,394,177 | 4,787,321,056 |
| Linked ELF bytes | 341,492 | 336,740 |

**59,073,121 fewer modeled cycles (1.219%), 25,684,039 fewer instructions**,
4,752 bytes smaller.

| Routine | Fewer modeled cycles |
| --- | ---: |
| Object_Intersect | 31,501,180 |
| Start_Tracing | 18,436,741 |
| Trace | 18,371,808 |
| Diffuse | 4,152,671 |
| Compute_Reflected_Colour | 3,414,586 |
| All_Object_Intersections | −15,492,839 |
| Determine_Surface_Colour | −1,317,722 |

`All_Object_Intersections` is slower. The address cache now sees it, and its
rule that a use inside a loop weighs four loads five statistics counters at
entry, 45 cycles a call; the box scene's object list is too short for the
loop to earn that back. That is the cache's heuristic, not this change's
sequence; `Determine_Surface_Colour`'s smaller loss was not traced. Both are
inside the total.

Doom (`tests/bench/doom.sh`, estimated cycles, every phase hash identical):

| Phase | T800 before | T800 after | T9000 before | T9000 after |
| --- | ---: | ---: | ---: | ---: |
| segloop | 2,445,409 | 2,379,833 | 2,894,881 | 2,834,417 |
| masked | 1,463,498 | 1,403,136 | 1,625,458 | 1,565,096 |
| mapplane | 8,783,742 | 8,744,862 | 10,260,222 | 10,222,622 |

The other eight phases are cycle-identical.

These are model results from decoded single-node counts, not hardware
timings.

## Validation

* **Fixture:** `test_indirect_call_frame.c`, 25 cases: no, register, stacked,
  double, mixed, narrow, float and variadic arguments; long long and struct
  returns; member, nested, returned and recursive pointers; a table in a
  loop; a cached global beside the call and one the callee writes; calls in
  conditions and with values pending; seventeen locals; a callee that itself
  calls through a pointer with stacked arguments; a VLA, a computed goto and
  a switch in the same function. Passes with both compilers on four CPUs at
  two layouts.
* **Object identity** against `00e15657`: 38 of the local and c-testsuite
  objects change at each part (those with indirect calls); 414-422 are
  byte-identical.
* **Mixed-version ABI:** 32 runs, callers and callees built by different
  compilers. **VLA return:** eight. **Generated expressions:** four CPUs.
* **Doom** hashes and `check 0 0` on T800/T9000; **CoreMark** CRCs
  `e9f5/e714/1fd7/8e3a/e714` on both; **raytracer** sixteen reference
  matches; **POV-Ray** box, basicvue and glass match their references.
* **Full suites** on the candidate: c-testsuite 244/244 at `-mt9000`,
  `-mt800`, `-mt425` (`-s425`) and `-mt414` (`-s414`); local suite 264, 248
  and 249 passes at `-mt9000`, `-mt800` and `-mt425`, no failures.
  `main` at `ff6a90ba` was run the same day as the baseline: 244/244 at
  three parts, local 263 and 247, no failures.
* **Differential fuzz:** `difftest.sh` seeds 21001-21150 (function-pointer
  tables among its constructs) at pads 0 and 1027: 150 pass, 0 fail.
* **Kernel:** linux-7 and its userspace (uClibc, busybox, init, tpsh, fbwm)
  rebuilt with the candidate in a scratch tree boot to `DESKTOP_LOOP`, no
  panic or BUG in the UART log.

## Not done, and next

* The entry cost that made threshold 2 lose is still there; a use-site cache
  (`ldc; stl w; ldl w` at the first of two uses in one block) would take
  `pq_free`'s 13 M without it.
* The scheduler still declines 126 POV functions for "ldlp-escape": an
  `ldlp r; fpldnldb` or `fpstnldb` on a register pair counts as the address
  escaping. `Start_Tracing`, `Determine_Surface_Colour` and
  `Intersect_Sphere` are among them. That is the larger lane.

Scripts, compilers, profiles and logs are in
`logs/compiler-coordination/wide-consts/`.
