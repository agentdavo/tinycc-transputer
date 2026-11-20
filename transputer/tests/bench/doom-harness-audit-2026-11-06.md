# Doom benchmark link audit

The previous benchmark linked whole Doom source files and warned about
unresolved references. The T800 object's text relocations put those references
in functions outside the exercised routines: `R_FillBackScreen`,
`R_RenderMaskedSegRange`, `R_StoreWallRange`, plane setup/drawing functions,
and sprite setup, projection and drawing functions. The measured
`R_RenderSegLoop` and `R_MapPlane` call `FixedMul`; the linked assembly supplies
it. The harness controls the callbacks used by the measured wall, masked
column and plane paths. This is a fixed-input benchmark, not a complete Doom
link or a proof about other inputs.

The harness now generates copies of four Doom source files, replacing the
unexercised function bodies with stubs that report an error and stop. The
measured functions remain the original source. The extraction refuses missing
or duplicate definitions. The remaining undefined symbols in the T800
benchmark object were `FixedMul`, `FixedDiv` and `__divdi3`; the assembly and
runtime archive resolve them. The final link uses strict unresolved-call mode
and rejects any linker diagnostic, including unresolved data references. Each
emulator run must start and halt, finish every phase, report `check 0 0`, and
match both the other run and the recorded phase hashes.

The focused host tests cover extraction failures and malformed or differing
results. A negative T800 run deliberately entered `R_FillBackScreen` and was
rejected, and a compiler wrapper injecting a linker warning was rejected.
The usual compile uses `-w`, as before; the strict diagnostic check applies to
the link, not warnings suppressed during compilation.

Before and after builds used the same `5afc5a61` compiler snapshot (SHA-256
`47d708f6a3c7418bd907f9b74dff48d42f1b29a8572d9cee3d7324c4fc6cec6f`),
netloader `5f827f450a4993804b6ac99345f8d2dc702ef0226d10e5e233550420692f7a8d`,
and T4 `2c5972c20329d9730726ebea9ed966263c86f275c22ae4fa7c90f765dc1ce884`.
Each pair used the same `-mt` target and the script's existing flags, source
inputs and uClibc headers. Runtime archive SHA-256 values were:

| Target | SHA-256 |
|---|---|
| T414 | `c1f955193dbb0a3ad0a9fa4e05c8210459714239a181439ed9993f5b0a7bc79d` |
| T425 | `8b62fa1bcc539005c70b261cf2900d41a966bf37fe861b19231c732dee6a50af` |
| T800 | `dbb6d39c4e13aec053eea8fbae2d550e1dbc64bc3cfb1b8b6fa5b006f3169e06` |
| T9000 | `6750477284c50b500a90ec79e18f8918247d0dafd2c14849aca1ebb8cb72c818` |

All eleven phase hashes matched before and after on all four targets. Changes
in code layout shifted the T800-estimated cycle count by 320 for `segloop` and
`masked` on each target, and also for `mapplane` on T9000. Those changes are
not evidence of a compiler speedup. The T9000 numbers use T4's T800 estimate,
not its T9000 group model; none of these figures are hardware measurements.
