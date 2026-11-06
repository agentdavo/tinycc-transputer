# Store double arithmetic directly to locals — 6 November 2026

The frozen baseline/candidate pair passes all four workload gates,
including full glass, with exact-reference and normal-exit receipts.

The statement-end parking cleanup already redirected single-precision FPU
results to their destination. It now recognizes a bounded double copy into
an aligned, nonvolatile, unescaped local or parameter. Instead of storing
the FPU result in a temporary pair and copying both words, it stores the
same result directly to the destination.

One common form includes `gv` moving the high word from slot 6 to slot 3:

```text
before: ldlp 5; fpstnldb; ldl 6; stl 3;
        ldl 5; stl local_y; ldl 3; stl local_y_plus_4
after:  ldlp local_y; fpstnldb
```

The simpler form copies the original low/high pair without that extra
move. No arithmetic is reassociated; the same FPSTNLDB still rounds/stores
the same result once. The compiler changes its destination, rather than
introducing a new arithmetic instruction or keeping a value across a call.

## Safety boundaries

The rule runs only inside the existing statement-end history window. It
requires exactly four or six remaining copy instructions after FPSTNLDB,
with adjacent local destinations and no intervening address calculation,
call, branch, FPU operation or other work. Both original pair words and
the optional high-word copy must be absent from the live value stack.

The destination must match an active double/long-double local symbol,
word alignment, no volatile/bitfield qualifier and no address escape.
The original symbol supplies this proof because casts can erase qualifiers.
Packed, escaped, volatile and anonymous destinations retain their existing
path. Existing label/history boundaries, rollback and local-patch handling
are reused. Inline-assembly functions already disable this cleanup.

The rewrite preserves the evaluation-stack balance and FPU stack pop.
It applies to hardware FPU double stores; T414/T425 soft-float code is
unchanged. Caller/return ABI, arithmetic order and runtime archives are
unchanged.

## Real workload gain

Baseline: backend accepted in `25ea0571`. Both compilers use identical
runtime archives and isolated T4 `2853b29`.

| Full generic POV box | Baseline | Candidate |
| --- | ---: | ---: |
| Decoded instructions | 1,801,402,525 | 1,756,097,163 |
| Modeled T800 cycles | 5,595,120,907 | 5,523,527,759 |
| Executed prefix bytes | 1,110,238,478 | 1,104,648,796 |
| Linked ELF bytes | 376,212 | 370,908 |

This removes **45,305,362 decoded instructions** and **71,593,148 modeled
T800 cycles (1.27956%)** from a complete, reference-exact box render.
The ELF shrinks **5,304 bytes**. Every FPU opcode total is unchanged.

Opcode differences are `ldl -20,698,104`, `stl -21,921,043`,
`ldnlp -2,686,215`, `ldc -407,629` and `rev +407,629`. In particular,
**42,619,147 workspace loads/stores** disappear. The rewritten window also
lets existing integer cleanup remove additional loop-counter parking.

| Function | Removed decoded instructions |
| --- | ---: |
| Intersect_Boxx | 25,404,144 |
| __ieee754_pow | 6,151,334 |
| Intersect_Sphere | 4,879,504 |
| Create_Ray | 3,686,400 |
| do_diffuse | 2,217,312 |
| do_phong | 1,736,208 |
| Compute_Reflected_Colour | 1,205,148 |

Profiling disables emulator instruction combination. These are decoded
single-node counts and T800 table estimates, not hardware timings or
sixteen-node array scaling measurements.

## Loop probes and controls

| Focused T800 kernel | Baseline modeled cycles | Candidate |
| --- | ---: | ---: |
| 4,096 arithmetic leaf calls | 475,430 | 475,430 |
| 20,000 multiply/add recurrence iterations | 1,520,365 | 1,220,358 |
| 20,000 accumulation iterations | 980,590 | 860,590 |

The two loop gains are **19.73%** and **12.24%**, including timer/label
overhead. In standalone T800/T9000 disassemblies, the recurrence shrinks
44 → 33 bytes and static LDL/STL instructions 17 → 7. A simple sum loop
shrinks 37 → 29 bytes and LDL/STL instructions 15 → 7. FPU instructions
are unchanged in both loops.

Five store-boundary controls remain byte-identical on T800/T9000: volatile
local, escaped destination, packed destination, single-precision store and
used assignment result. All seven store probes are byte-identical on
T414/T425. Ten retained return/type controls also remain byte-identical on
all four targets.

## Workload and regression gates

* **Doom:** eleven hardened phase hashes pass with `check 0 0`, both
  compilers on T800/T9000. Paired ELFs are byte-identical per CPU. This is
  the extracted phase benchmark, not full gameplay.
* **CoreMark:** four diagnostic runs retain `e9f5/e714/1fd7/8e3a/e714`.
  Whole-fixture modeled cycles improve 5,333,156 → 5,333,037 for T800
  code and 3,539,467 → 3,539,348 for T9000 code, using the same T800 model.
  The one-iteration run is not a published score.
* **Raytracer:** four reference scenes match at one/four nodes with both
  compilers, sixteen comparisons. Paired ray ELFs are byte-identical.
* **POV-Ray:** complete box, basicvue and glass references match for both
  compilers: six full 640×480 comparisons. Strict link logs are empty,
  emulator exits are normal and renderer statistics match across the pair.

| Full POV scene | Bytes | SHA-256, both versions and reference |
| --- | ---: | --- |
| box | 921,615 | `fcaeaccaea31d263bede7a2f8774e2e7af8dcfc6f640a893a3fd1d3b8d1d6db0` |
| basicvue | 921,615 | `d537ba4e52842f8cd3e35f8c59bd3d30ac2c3008b60eafc10f7b6ff0cf17cde7` |
| glass | 921,615 | `79a805d547b93c68fc81c16e2c476ea09eed23aab17e091712fc8f0d1c9ef54c` |

POV rebuilds the generic renderer and fdlibm with each compiler. Recovered
INMOS RNG and buffered RGB rows are fixed across both builds. Existing libc
is fixed and requires T9000 emulator mode. The source fixture is distinct
from the historical sixteen-node ICC application.

The new seventeen-case UART fixture covers recurrence and accumulation,
noncommutative arithmetic, chained/used assignment values, snapshots,
escaped/volatile/cast/packed destinations, pending call arguments, goto and
for-step boundaries, branches, signed zero, mixed precision and wide frames.
It and the retained forty-case return fixture pass the normal runner's
complete expected-output oracle.

The new fixture plus return, double-parameter, scalar64 alignment,
sqrt-composition and return-epilogue suites pass baseline/candidate ×
T414/T425/T800/T9000 × link pads 0/1027: **96 executions**. Both double
ABI fixtures pass old callers/new helpers and the reverse: **32 mixed
interoperability runs**. Eight VLA-return runs retain scope release,
nested/early returns and arena reuse.

| Frozen input | SHA-256 |
| --- | --- |
| Baseline compiler | `edea46fcb265faccab78194646ed29063c697e59b1a08c8a1cc8985c38754fa7` |
| Candidate compiler | `1964959068b10b836a60b3b00371d037be33ed1f6f5045d1bb6ebef7583b7519` |
| Baseline backend | `ed730f3e71bc3525f722ebba00736a66175580dc1d08f313e3b420148c8c6ea5` |
| Candidate backend | `131777b649a1420c963fef027bffa94e1454116c4a11f144e30bf27cc1f4822c` |

Artifacts are retained in `logs/compiler-coordination/double-local-stores/`:
frozen sources/compilers, scripts, strict logs, complete applications/images,
input fingerprints, disassemblies, loop timings and decoded profiles.
`acceptance.json` verifies workload results, six full references, exits and
statistics, matching paired POV inputs, regression receipts and the current
backend against its frozen tested copy.
No kernel, installed runtime archive, historical BTL or original POV math
source changes are included.
