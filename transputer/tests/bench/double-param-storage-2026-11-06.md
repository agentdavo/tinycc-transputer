# Reuse incoming double parameter storage — 6 November 2026

Non-variadic functions previously copied every incoming double into a new
local pair before using it. `gfunc_prolog` now binds nonvolatile, eight-byte
`double`/`long double` parameters directly to their existing ABI storage.
Other parameter types and volatile initialization retain their previous
handling. Caller code and the calling convention are unchanged.

## Storage and correctness

The entry workspace uses contiguous ascending ABI words:

| ABI word | Source | TCC byte offset before local-frame adjustment |
| --- | --- | ---: |
| 0 | Saved A | 4 |
| 1 | Saved B | 8 |
| 2 | Saved C | 12 |
| 3 | First stacked word | 16 |
| n | Subsequent word | 4 + 4*n |

A double can straddle saved C and the first stacked word, but its halves
remain contiguous and word-aligned. These are the callee's by-value parameter
copies, not the caller's original variables. They survive nested calls, whose
save/outgoing areas are below them. The backend already uses this storage
for named variadic parameters; the same frame patching, address generation
and subsequent frame-growth passes apply here.

The change removes both integer copy operations and the allocation of the
redundant local pair. It introduces no floating-point operation or arithmetic
reassociation. Mutating a parameter, taking its address or returning a hidden
struct still follows the existing ABI.

## Accepted workloads

Baseline backend: `b70eb63c`, frozen in the `9c82e721` integration snapshot.
The baseline compiler is byte-identical to the previously accepted sqrt
compiler. Both snapshots use identical runtime archives and isolated T4
`2853b29`. All tested source inputs are pinned in the retained artifacts.

* **Doom:** all eleven hardened phase hashes pass, with `check 0 0`, T800
  and T9000. The paired ELFs are byte-identical for each target. This is the
  extracted phase benchmark, not a full game-play validation.
* **CoreMark:** both targets retain the complete CRC sequence
  `e9f5/e714/1fd7/8e3a/e714`. One iteration is a diagnostic, not a published
  score. The T800 reported kernel ticks remain 5,008,259. Whole-fixture T800
  estimates improve 5,333,697 → 5,333,324 cycles; T9000-code estimates improve
  3,540,011 → 3,539,635 using the same T800 timing model.
* **Raytracer:** all four reference images match at one and four nodes for
  both snapshots, sixteen complete comparisons. The paired ray ELF is
  byte-identical, so there is no ray instruction/performance change.
* **POV-Ray:** complete box, basicvue and glass images match the packaged
  references for both compilers, six comparisons. The recovered INMOS RNG
  and buffered row output are fixed across the pair. Strict link logs are
  empty, all emulator runs exit normally, and renderer statistics match.

| Full POV scene | Bytes | SHA-256, both versions and reference |
| --- | ---: | --- |
| box | 921,615 | `fcaeaccaea31d263bede7a2f8774e2e7af8dcfc6f640a893a3fd1d3b8d1d6db0` |
| basicvue | 921,615 | `d537ba4e52842f8cd3e35f8c59bd3d30ac2c3008b60eafc10f7b6ff0cf17cde7` |
| glass | 921,615 | `79a805d547b93c68fc81c16e2c476ea09eed23aab17e091712fc8f0d1c9ef54c` |

POV recompiles its generic renderer and fdlibm with each compiler; the
existing libc remains fixed and requires T9000 emulator mode. This numerical
gate does not rebuild the historical sixteen-node ICC application.

## Traffic, code and frame measurements

For `double horner(double a,double b,double c) { return a*b+c; }`:

* The frame shrinks from 16 to 10 words, saving 24 bytes per active call.
* Twelve integer parameter-copy instructions disappear.
* T800 and T9000 function sizes shrink from 42 to 23 bytes.
* The unary double-sqrt probe shrinks 67 → 59 bytes on T800 and 48 → 40
  bytes on T9000; the single-precision probes are unchanged.

| Full generic POV box | Baseline | Candidate |
| --- | ---: | ---: |
| Decoded instructions | 1,806,221,594 | 1,802,101,877 |
| Modeled T800 cycles | 5,606,252,647 | 5,596,242,656 |
| Executed prefix bytes | 1,114,266,634 | 1,110,311,195 |
| Linked ELF bytes | 378,260 | 377,052 |

This removes **4,119,717 instructions** and **10,009,991 modeled cycles**
(0.17855%). Opcode differences are exactly `ldl -1,934,835`,
`stl -1,934,835`, `rev -250,047`; all FPU opcode totals are unchanged.
`Create_Ray` accounts for 2,457,600 removed instructions. These are decoded
single-node counts and T800 table estimates, not hardware timings or array
scaling claims. Profiling disables emulator instruction combination.

A focused T800 leaf-call kernel improves 606,502 → 500,006 modeled cycles
(17.56%). Two 20,000-iteration parameter-loop probes improve
1,540,401 → 1,520,371 and 980,626 → 980,596, including their timer/label
overhead. Existing slot-profitability policy is otherwise unchanged; these
measurements do not assert a universal runtime speedup for every program.

## ABI regressions and controls

The new fixture covers nineteen cases: three pairs, mixed-width arguments,
C/stack splits, indirect calls, parameter mutation, address escapes, caller
copy isolation, recursion, wide outgoing argument areas, volatile parameters,
long double, variadics, hidden struct returns, packed callers, byte mutation,
signed zero, infinity and NaN payload preservation.

The fixture, retained scalar64 alignment test and sqrt composition suite pass
baseline/candidate × T414/T425/T800/T9000 × link pads 0/1027: 48 executions.
Separately compiled baseline callers with candidate helpers and the reverse
combination pass all four targets and both layouts: sixteen interoperability
runs, including external split doubles and hidden struct returns.

Volatile-double-only, variadic, integer and float control functions are
byte-identical before/after on all four targets. The common UART runner lists
the new helper source, so its normal test discovery can run this fixture.

| Frozen input | SHA-256 |
| --- | --- |
| Baseline compiler | `e5cf142febdb1f28fa9f0310425dc5323d61791db52e9c68dc6ff87c59315d39` |
| Candidate compiler | `36a5860a650d0eabeede39563dfb3898c58ec2f70bc88f00f78ab758dfadeeee` |
| Baseline backend | `a5eb7433cf779fd1364ced96fea6afb7a4bb0096909385ef21e81bcadedd5c29` |
| Candidate backend | `5bf212b7d7955dd70d6b6192e4d51c4e0cf426420a88f6b4a8b60a1b1acaf792` |

Build scripts, complete applications, strict logs, source/negative probes,
loop measurements, decoded histograms and the independent acceptance receipt
are retained in `logs/compiler-coordination/double-params/`. No kernel,
installed libc, historical BTL or original POV math source was changed.
