# Loop-weighted workspace slot placement experiment

This experiment used compiler revision `c6de60f9fbbd89329dbaaf84f99e591bbcba06c9` as its baseline, including the seed 329 fix. The trial was confined to a scratch checkout; it was not retained in the compiler.

The trial gave workspace slot accesses inside backward-branch intervals four times the normal weight when choosing and evaluating a slot layout. It accepted a layout only if weighted instruction encoding width improved and total static encoding width did not grow. Existing slot, aggregate, relocation and inline-assembly guards were unchanged. The interval scan was potentially quadratic in function size.

| Workload | Baseline | Trial | Result |
| --- | ---: | ---: | --- |
| Ray tracer, model 3, one node, T4 emulator with T800 timing | 3,028,967,767 | 3,028,839,859 | 127,908 fewer (0.0042%); TransLocal remained 339 cycles per call |
| CoreMark, one iteration, T800 modeled cycles | 5,008,259 | 5,008,719 | 460 more (0.0092%); reference CRCs matched |
| Doom `fill`, T800 modeled cycles | 992,020 | 992,022 | 2 more; phase hash matched |

All other Doom phase counts and hashes matched. The Doom build emitted undefined-symbol warnings for unexercised code, so its results apply to the measured phases only. Both compiler variants passed the seed 329 regression on the T800 emulator. All four ray images, on both one and four nodes, matched the reference PPMs byte for byte for both variants.

The small ray gain did not justify a regression in CoreMark and the added compile-time complexity. This result does not establish the bounds, provenance, overlap or volatile proofs required to replace the TransLocal copy loop with `MOVE`; that compiler rewrite remains separate work.

The baseline and trial compiler SHA-256 hashes were `e2ff6ca52832e82d7060a781c7a97181760303049c277c61bfe338d3462244d7` and `70939db528f84b2cbf984f30b10e480a5d3c75128182cd00f35d71a146953484`. Both used runtime archive `dbb6d39c4e13aec053eea8fbae2d550e1dbc64bc3cfb1b8b6fa5b006f3169e06`. The T4 cycle counts came from an instrumented emulator based on revision `78e77a9`.
