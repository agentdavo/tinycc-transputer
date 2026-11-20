# Return double literals without memory staging — 6 November 2026

The frozen baseline/candidate pair passes all four workload gates,
including full glass, with exact-reference and normal-exit receipts.

`tp_push_double_return` now emits the already-converted bit pattern of a
double literal directly into ABI B/A, high word then low word. Previously
`gv` created read-only literal data, loaded its address, read the two words
and staged them through workspace. The direct path introduces no FPU
operation or target arithmetic, including for zero: its sign is preserved.

Eligibility requires a matching double/long-double result type, exactly
`VT_CONST` or `VT_CONST | VT_NONCONST`, and no symbol back-pointer. Symbolic,
lvalue, deferred-cast and bounds-flagged operands retain their existing
handling. Integer, float, long long, computed-pair and memory returns are
unchanged. Long double remains this target's REAL64 representation.

## Code, data and frame measurements

Baseline is the backend accepted in `312869d6`. Frozen compilers use the
same runtime archives and isolated T4 `2853b29`.

| Standalone literal return | Baseline function bytes | Candidate function bytes |
| --- | ---: | ---: |
| Positive zero | 19 | 7 |
| Negative zero | 19 | 8 |
| One | 19 | 14 |
| Distinct dense high/low words | 19 | 21 |
| Low word `0x80000000` | 19 | 15 |
| Smallest positive subnormal | 19 | 7 |
| Largest finite double | 19 | 15 |
| Long-double literal 1.25 | 19 | 14 |

All eight standalone frames shrink **10 → 1 words**, saving 36 bytes per
active call, on T414/T425/T800/T9000. Their target bodies contain only the
two bit-pattern pushes and AJW/RET; MINT supplies a word equal to
`0x80000000`. Assembly checks verify the returned words exactly and exclude
workspace transfers and FPU instructions.

The eight-function T800 probe's literal data shrinks **64 → 0 bytes** and
text address relocations **8 → 0**. Its object shrinks 1,471 → 1,093 bytes.
Dense literals can grow the instruction body because both immediates need
long encodings; the table includes that case rather than assuming every
function body shrinks.

| Focused T800 4,096-call kernel | Baseline modeled cycles | Candidate | Saving |
| --- | ---: | ---: | ---: |
| Return zero | 233,756 | 168,220 | 28.04% |
| Return one | 233,715 | 196,850 | 15.77% |
| Return dense literal | 233,757 | 225,565 | 3.50% |

These include timer/label overhead. The retained arithmetic leaf and two
20,000-iteration parameter-loop probes are unchanged at 475,430,
1,520,365 and 980,590 modeled cycles respectively.

## Full workload measurements

The rebuilt POV ELF shrinks **376,468 → 376,212 bytes**. Full box and
basicvue decoded profiles retain identical opcode, instruction and cycle
totals across the pair; these scenes show no measured runtime saving.

| Full generic POV profile | Decoded instructions, both | Modeled T800 cycles, both |
| --- | ---: | ---: |
| box | 1,801,402,525 | 5,595,120,907 |
| basicvue | 2,960,936,813 | 9,034,313,104 |

Box prefix bytes remain 1,110,238,478 and basicvue 1,915,355,447. All FPU
opcode totals are unchanged. Profiling disables emulator instruction
combination. These are single-node decoded counts and T800 table estimates,
not native hardware timings or array scaling measurements.

* **Doom:** all eleven hardened phase hashes pass with `check 0 0`, both
  compilers on T800 and T9000. Paired ELFs are byte-identical for each CPU.
  This is the extracted phase benchmark, not full gameplay.
* **CoreMark:** four diagnostic runs retain `e9f5/e714/1fd7/8e3a/e714`.
  Whole-fixture modeled cycles remain 5,333,156 for T800 code and 3,539,467
  for T9000 code using the same T800 model. One iteration is not a published
  benchmark score.
* **Raytracer:** all four reference scenes match at one/four nodes with
  both compilers, sixteen comparisons. The paired ray ELF is byte-identical.
* **POV-Ray:** complete box, basicvue and glass references match with both
  compilers: six full 640×480 comparisons. Strict link logs are empty,
  emulator exits are normal, and renderer statistics match across the pair.

| Full POV scene | Bytes | SHA-256, both versions and reference |
| --- | ---: | --- |
| box | 921,615 | `fcaeaccaea31d263bede7a2f8774e2e7af8dcfc6f640a893a3fd1d3b8d1d6db0` |
| basicvue | 921,615 | `d537ba4e52842f8cd3e35f8c59bd3d30ac2c3008b60eafc10f7b6ff0cf17cde7` |
| glass | 921,615 | `79a805d547b93c68fc81c16e2c476ea09eed23aab17e091712fc8f0d1c9ef54c` |

POV rebuilds the generic renderer and fdlibm with each compiler, with
recovered INMOS RNG and buffered RGB rows fixed across both builds.
Existing libc is fixed and requires T9000 emulator mode. This source fixture
is distinct from the historical sixteen-node ICC application.

## Return and ABI checks

The return fixture now has **40 cases**: the previous 29 plus ten literal
bit-pattern checks and an early/final literal return from a frame over
4,095 words, exercising widened epilogues. It covers positive/negative zero,
one/minus one, dense distinct halves, MINT in the low word, minimum
subnormal, maximum finite, negative dense and long-double literals. The
normal UART runner checks its complete expected output.

The fixture plus double-parameter, scalar64 alignment, sqrt-composition and
return-epilogue suites pass baseline/candidate × T414/T425/T800/T9000 ×
link pads 0/1027: **80 executions**. Both double fixtures pass separately
compiled old callers/new helpers and the reverse on all four CPUs/layouts:
**32 interoperability runs**. Eight VLA-return runs retain scope release,
nested/early returns and arena reuse. Ten volatile, packed, address-taken,
integer, float and long-long control functions remain byte-identical on
all four CPUs, including non-double literal returns.

| Frozen input | SHA-256 |
| --- | --- |
| Baseline compiler | `d415b30679268dbc60b70c93286ce959e72fc980583ed706fab6a627bdcc2b60` |
| Candidate compiler | `edea46fcb265faccab78194646ed29063c697e59b1a08c8a1cc8985c38754fa7` |
| Baseline backend | `8e247f3811773e7903a45996658575e8346be307f3eac3a549dab6059721538d` |
| Candidate backend | `ed730f3e71bc3525f722ebba00736a66175580dc1d08f313e3b420148c8c6ea5` |

Frozen sources, scripts, strict logs, source fingerprints, applications,
images, disassemblies, decoded profiles and independent checks are retained
in `logs/compiler-coordination/double-constant-returns/`. `acceptance.json`
checks all six full references, exits/statistics, matching paired POV inputs,
regression receipts and the exact tested backend. No installed
runtime archive, kernel, historical BTL or original POV math source changes
are included.
