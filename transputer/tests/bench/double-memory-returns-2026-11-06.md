# Direct double returns from aligned memory — 6 November 2026

The frozen baseline/candidate pair passes all four application gates,
including full glass, with the independent reference/exit receipt below.

`tp_push_double_return` shares the existing computed-pair return path with
new paths for aligned memory. Proven nonvolatile locals/parameters load the
high and low workspace words directly into ABI B/A. Other eligible memory
operands reuse `tp_emit_float_addr` for symbol, register-address and canonical
spilled-address handling, then retain the two words on the evaluation stack:

```text
address; dup; ldnl 0; rev; ldnl 1; rev
```

This reads low then high and leaves A=low, B=high. It removes the return
staging store/reload without introducing FPU loads, conversions or arithmetic.
T414 has no DUP, so nonlocal memory returns retain their existing path there.

## Eligibility and volatile reads

The helper handles only matching double/long-double types. It retains the
previous direct path for genuine rvalue pairs, using the actual `r2`.
Memory operands must have a recognized address form, alignment and no
pending bounds, cast or other location flags. Packed/unaligned operands,
unmarked symbol constants and surviving volatile qualifiers fall through
to `gv` and `load`.

`gen_cast` strips qualifiers before return emission, so the current type
alone cannot prove a local nonvolatile. Local eligibility requires the
original active symbol, matching storage offset and double type, no volatile
or bitfield qualifier, and no address escape. Opaque temporary storage,
cleanup-preserved returns and address-taken locals retain their existing
handling. Simply emitting low/high/REV would be insufficient: the scheduler
can swap two LDLs and eliminate REV.

The nonlocal sequence preserves the existing low-then-high memory reads,
including volatile pointer accesses whose qualifier has already been
stripped. Disassembly checks require LDNL offsets exactly 0 then 1, no
workspace store, and DUP/REV/REV/RET on T425/T800/T9000. T414 nonlocal
control functions remain byte-identical. Volatile-local, volatile-variadic,
volatile-cast/address-taken-local, packed, float and integer controls remain
byte-identical on all four CPUs.

## Measurements

Baseline: `f82e8393`, the accepted computed-pair return optimization. Both
snapshots use identical runtime archives and isolated T4 `2853b29`.

| Full generic POV box | Baseline | Candidate |
| --- | ---: | ---: |
| Decoded instructions | 1,801,929,729 | 1,801,402,525 |
| Modeled T800 cycles | 5,595,984,309 | 5,595,120,907 |
| Executed prefix bytes | 1,110,311,070 | 1,110,238,478 |
| Linked ELF bytes | 376,652 | 376,468 |

The saving is **527,204 decoded instructions** and **863,402 modeled cycles
(0.01543%)**. Opcode differences are `ldl -263,606`, `stl -263,606`,
`rev +8`; every FPU opcode total is unchanged. `Attenuate_Light` accounts
for 401,716 removed instructions and fdlibm `__ieee754_pow` for 125,284.
Profiling disables emulator instruction combination. These are decoded
single-node counts and T800 table estimates, not native hardware timings.

For `double identity(double x) { return x; }`, both compilers already
schedule the return down to two LDLs. Avoiding the IRET/IRE2 references
also lets the frame pass shrink its frame **10 → 1 words**, saving 36 bytes
per active call, on all four targets. The function remains seven bytes.
The global-read probe has the same frame reduction on T425/T800/T9000.
A computed-local probe shrinks **48 → 44 bytes** on T800 and **46 → 42**
on T9000 by removing its four residual copy instructions.

The retained arithmetic leaf benchmark is unchanged at 475,430 modeled
cycles. Two 20,000-iteration loop probes change 1,520,371 → 1,520,365 and
980,590 → 980,590. Savings depend on return form and call frequency.

## Workload and ABI gates

* **Doom:** all eleven hardened phase hashes pass with `check 0 0`, both
  compilers on T800 and T9000. Paired ELFs are byte-identical for each CPU.
  This is the extracted phase benchmark, not full gameplay.
* **CoreMark:** four runs retain `e9f5/e714/1fd7/8e3a/e714`. The one-iteration
  diagnostic is not a published score. Whole-fixture modeled cycles change
  5,333,186 → 5,333,156 for T800 code and 3,539,497 → 3,539,467 for
  T9000 code, using the same T800 model.
* **Raytracer:** all four reference scenes match at one/four nodes with
  both compilers, sixteen comparisons. The paired ray ELF is byte-identical.
* **POV-Ray:** complete box, basicvue and glass references match with both
  compilers: six full 640×480 comparisons. Strict link logs are empty,
  emulator exits are normal, and renderer statistics match across the pair.

The renderer and fdlibm are rebuilt with each compiler, with recovered INMOS
RNG and buffered RGB rows fixed across the pair. Existing libc is fixed and
requires T9000 emulator mode. This is the single-node generic source fixture,
distinct from the historical sixteen-node ICC BTL.

| Full POV scene | Bytes | SHA-256, both versions and reference |
| --- | ---: | --- |
| box | 921,615 | `fcaeaccaea31d263bede7a2f8774e2e7af8dcfc6f640a893a3fd1d3b8d1d6db0` |
| basicvue | 921,615 | `d537ba4e52842f8cd3e35f8c59bd3d30ac2c3008b60eafc10f7b6ff0cf17cde7` |
| glass | 921,615 | `79a805d547b93c68fc81c16e2c476ea09eed23aab17e091712fc8f0d1c9ef54c` |

The return fixture now contains 29 cases, adding computed locals, external
pointer reads, indexed and conditional addresses, pointer postincrement,
memory NaN payload transport, packed locals, wide-frame local returns and
volatile pointers and variadic named-parameter returns to the earlier
nineteen cases. It passes the normal UART
runner with an exact output oracle.

The return, double-parameter, scalar64 alignment, sqrt-composition and
return-epilogue suites pass baseline/candidate × T414/T425/T800/T9000 ×
link pads 0/1027: **80 executions**. Both double fixtures also pass with
separately compiled baseline callers/candidate helpers and the reverse:
**32 interoperability runs**. Eight further VLA-return runs check scope
release, nested scopes, early exits and arena reuse.

| Frozen input | SHA-256 |
| --- | --- |
| Baseline compiler | `a0348612c309c4346ad20a893684d55b20892b05f008d0f8b87bdb79bb0cf9b6` |
| Candidate compiler | `d415b30679268dbc60b70c93286ce959e72fc980583ed706fab6a627bdcc2b60` |
| Baseline backend | `79fa7f78d32b999e81de7a2e92b336e7619e052640e34726f60de25d7ab0016d` |
| Candidate backend | `8e247f3811773e7903a45996658575e8346be307f3eac3a549dab6059721538d` |

Artifacts are retained in `logs/compiler-coordination/double-memory-returns/`:
frozen compilers/sources, scripts, strict logs, applications, images, source
fingerprints, disassemblies, decoded profiles and independent checks.
`acceptance.json` verifies the complete workload results, matching paired
POV input fingerprints and the exact current backend against its tested copy.
No installed runtime archive, kernel, historical BTL or original POV math
source changes are included.
