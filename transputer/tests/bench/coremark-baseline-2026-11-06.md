# CoreMark compiler baseline, 2026-11-06

This is a one-iteration diagnostic baseline, not a valid CoreMark score. It
uses CoreMark's 2K performance seeds `(0, 0, 0x66)`, one iteration, and all
three workloads. `T4_EST_CYCLES=1` makes `ldtimer` return estimated T800
cycles, not timer ticks: the benchmark's printed seconds and iterations per
second are therefore not meaningful. The T9000 row is a T800-timing estimate
for T9000-generated instructions, not a T9000 cycle model. Memory wait
states, scheduling, and actual silicon timings are not measured.

| Compiler target | Emulator | Estimated cycles in timed region | PC samples |
|---|---|---:|---:|
| T414 | `-s414` | 5,034,850 | 1,968 |
| T425 | `-s425` | 5,008,258 | 1,967 |
| T800 | `-s8` | 5,008,259 | 1,630 |
| T9000 | `-s9` | 3,319,286 | 1,158 |

Every run reported seed CRC `e9f5`, list CRC `e714`, matrix CRC `1fd7`, and
state CRC `8e3a`, matching the benchmark's reference values. A run of one
iteration does not satisfy CoreMark's official duration requirement, despite
the program printing its validation message when the artificial cycle clock
is enabled.

For the PC-sampling pass, `T4_IPTR_SAMPLE=1000` sampled one address per 1,000
emulated instructions. Samples were mapped to the nearest preceding text
symbol in the corresponding `.elf.tccsym`. These counts are approximate
instruction-hotspot samples, include startup/reporting, and do not attribute
nested calls or estimate component-exclusive cycles.

| Function | T414 | T425 | T800 | T9000 |
|---|---:|---:|---:|---:|
| `crcu8` | 421 | 421 | 383 | 279 |
| `core_state_transition` | 256 | 287 | 190 | 211 |
| `matrix_mul_matrix_bitextract` | 248 | 246 | 206 | 134 |
| `matrix_mul_matrix` | 212 | 212 | 171 | 104 |
| `core_list_find` | 210 | 212 | 177 | 79 |

One candidate for investigation is the narrow local load/store traffic in
`crcu8`: the T414 build loads and stores byte locals repeatedly, and its
promoted right shift uses `lb; xdble; ldc 1; lshr`. A possible range-aware
shortening would require proving the value remains nonnegative after integer
promotion and preserving the T414 register semantics. This is an audit lead,
not a measured optimization or a proposed source change to CoreMark.

The source checkout identified itself as `e29bca5a-dirty` when built; because
other work was active, the exact binary hashes below identify this run more
reliably than the revision alone. The runtime archives were existing builds;
their precise source revisions were not established.

| Artifact | SHA-256 |
|---|---|
| Cross compiler | `0d3dbd4f763e564c7f63c8d25ac7fa4b2320dd913f329115160828e1f32ad133` |
| T414 runtime | `c1f955193dbb0a3ad0a9fa4e05c8210459714239a181439ed9993f5b0a7bc79d` |
| T425 runtime | `8b62fa1bcc539005c70b261cf2900d41a966bf37fe861b19231c732dee6a50af` |
| T800 runtime | `dbb6d39c4e13aec053eea8fbae2d550e1dbc64bc3cfb1b8b6fa5b006f3169e06` |
| T9000 runtime | `6750477284c50b500a90ec79e18f8918247d0dafd2c14849aca1ebb8cb72c818` |
| T4 emulator | `2c5972c20329d9730726ebea9ed966263c86f275c22ae4fa7c90f765dc1ce884` |

To repeat with a built compiler and its matching archives, choose each CPU
in turn and use the existing build script without altering benchmark sources:

```sh
CPU=t414 ITERATIONS=1 RUN_COREMARK=0 TCC="$TCC" BTL="$OUT/t414.btl" \
  bash transputer/coremark/transputer/build.sh
T4_EST_CYCLES=1 T4_UART_CONSOLE=0 T4_UART_LOG="$OUT/t414.uart" \
  timeout -k 3 20 "$T4" -s414 -sm 26 -ss -sb "$OUT/t414.btl"
T4_IPTR_SAMPLE=1000 T4_UART_CONSOLE=0 T4_UART_LOG="$OUT/t414-sample.uart" \
  timeout -k 3 20 "$T4" -s414 -sm 26 -ss -sb "$OUT/t414.btl" \
  > "$OUT/t414-samples.log" 2>&1
```

Use `CPU=t425`, `t800`, or `t9000` with `-s425`, `-s8`, or `-s9`, respectively.
The build writes the ELF, symbol map, exact compiler commands, and (where
applicable) instruction census beside each BTL. Run the compiler, emulator,
and runtime archives matching the recorded hashes to reproduce these figures.
