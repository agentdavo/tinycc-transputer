# Keep double workspace accesses together — 6 November 2026

The accepted candidate safely enables workspace-slot remapping in more FPU
functions. Complete rebuilt POV box, basicvue and glass images match their
references with both compilers. Doom, CoreMark and the raytracer also pass.

## Change and safety boundary

The existing remapper declined functions whose double temporaries escaped
its single-word access model. Its recent safety guard also declined an entire
function when a double crossed the pinned outgoing-argument prefix. This
protected glass turbulence but prevented cheaper workspace addresses.

The remapper now uses the shared LDLP-consumer extent description to merge
both words of known double loads, stores and fused load/add/multiply accesses
into one allocation unit. Overlapping accesses merge transitively. A unit
touching the outgoing prefix stays pinned in its entirety, preserving the
previous boundary repair while allowing other units to move.

Unknown pseudo-register escapes, accesses crossing the frame edge, inline
assembly, dynamic workspace changes and frames above 4,096 words retain the
conservative rules. Span arithmetic uses 64 bits before checking the encoded
integer range. Units move only when the existing profitability check finds
smaller workspace operands. Arithmetic order, caller ABI, runtime archives
and FP residency rules are unchanged.

This reduces prefix traffic for existing workspace instructions. It does
not remove additional double spills or loads.

## Measured real-renderer gain

Baseline backend: `6f7b42c8`. Private baseline and candidate compilers use the
same runtime archives and isolated emulator. Paired POV input fingerprints
verify 87 identical non-compiler inputs.

| Complete generic POV box | Baseline | Candidate |
| --- | ---: | ---: |
| Decoded instructions | 1,592,403,106 | 1,592,403,106 |
| Modeled T800 cycles | 5,151,451,525 | 5,091,332,488 |
| Executed prefix bytes | 1,043,357,827 | 983,238,790 |
| Linked ELF bytes | 355,276 | 349,204 |

The reference-exact render saves **60,119,037 prefix bytes and modeled cycles
(1.16703%)**, and the ELF shrinks **6,072 bytes**. All decoded opcode totals,
including FPU operations and workspace load/store counts, remain identical.
The savings occur in actual geometry and shading routines:

| Routine | Fewer modeled cycles |
| --- | ---: |
| Intersect_Boxx | 11,371,360 |
| MInvTransVector | 9,375,467 |
| Intersect_Sphere | 9,340,748 |
| Clip_Colour | 6,749,484 |
| MInverseTransformVector | 5,622,916 |
| do_light | 4,820,592 |
| Colour_At | 2,711,750 |
| __ieee754_pow | 2,490,448 |

Profiling disables emulator instruction combination and uses decoded
single-node counts with the T800 timing table. These are model results,
not native hardware timings or sixteen-node scaling measurements.

Focused T800 timing controls pass: 4,096 simple leaf calls improve
475,430 → 438,563 modeled cycles; compound leaf calls improve
676,512 → 660,128. The retained recurrence, product and dot loops change by
only four to seven cycles over their whole runs; no material loop speedup
is claimed for those particular layouts.

## Validation

* **Doom:** eleven hardened phase hashes and `check 0 0` pass with each
  compiler on T800/T9000. Paired ELFs remain byte-identical per CPU. This
  validates the extracted phase benchmark, not full gameplay.
* **CoreMark:** both compilers on T800/T9000 retain the diagnostic CRC
  sequence `e9f5/e714/1fd7/8e3a/e714`. One iteration is not a published score.
* **Raytracer:** four reference images at one/four nodes with each compiler,
  sixteen exact comparisons. Paired ELFs remain byte-identical.
* **POV-Ray:** all three complete 640×480 references pass with both compilers,
  six exact comparisons. Strict link logs are empty, emulator exits normal,
  and paired renderer statistics match. Glass uses the recovered INMOS RNG;
  buffered RGB rows and existing libc are fixed across both builds.

| Scene | Image bytes | SHA-256, both versions and reference |
| --- | ---: | --- |
| box | 921,615 | `fcaeaccaea31d263bede7a2f8774e2e7af8dcfc6f640a893a3fd1d3b8d1d6db0` |
| basicvue | 921,615 | `d537ba4e52842f8cd3e35f8c59bd3d30ac2c3008b60eafc10f7b6ff0cf17cde7` |
| glass | 921,615 | `79a805d547b93c68fc81c16e2c476ea09eed23aab17e091712fc8f0d1c9ef54c` |

The POV fixture recompiles the generic renderer and fdlibm. Its fixed libc
requires T9000 emulator mode. It is separate from the historical sixteen-node
ICC BTL, which is unchanged.

The new ten-case `test_double_slot_units.c` exercises outgoing prefixes of
one through five words, mixed precision, arrays/one-past pointers, large
frames, signed zero and volatile storage. With eight retained suites it
passes baseline/candidate × four CPUs × two layouts: **144 executions**.
Retained mixed-version ABI tests pass **32 runs**, VLA-return tests **eight**,
and generated expressions **384 independent bit comparisons** (seed 32972,
48 expressions, both compilers on four CPUs). The stored-result dependency
regression passes with both compilers on all four CPUs.

Static decoding of the new fixture verifies consistent, injective slot
mapping, adjacent double words, pinned argument prefixes, unchanged frame
adjustments and non-workspace operations. Seven functions change on each
FPU CPU; all fixture functions stay byte-identical on T414/T425. Ten retained
volatile/packed/memory/scalar controls stay byte-identical on all four CPUs.
The normal UART runner passes complete expected-output oracles for the new
fixture and five retained fixtures.

## Reproduction evidence

| Frozen input | SHA-256 |
| --- | --- |
| Baseline compiler | `8d6ae7997722fba4d7159bd7c2da9bfcac7c4eb6b9dd1a1d0a27826b14ea8b7a` |
| Candidate compiler | `843a6125f65bb2d040367a3f4763791465316b85af63e2ca24dc27a8287f2d10` |
| Baseline backend | `99709e0da77a886e814c02f52dc354c2d1fd500087c25ed6ff43549456af6efe` |
| Candidate backend | `b748bf360d0789d217ec8f1d23bebb935e777c065721bddc3d6991bc2ef9e6f7` |

Private scripts, compilers, inputs, images, strict logs, slot checks, timings
and decoded profiles are retained in
`logs/compiler-coordination/double-slot-units/`. Its `acceptance.json` checks
the six full reference images, exits/statistics, application/regression
receipts, identical paired inputs and exact tested backend bytes.
