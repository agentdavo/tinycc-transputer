# Bare-metal floating-point instruction diagnostic

This suite complements `transputer/c-testsuite` (C compiler tests) and the
individual regressions under `transputer/tests`. It uses generated raw opcode
probes, integer-only checking/reporting, fixed IEEE bit-pattern expectations,
and no libc. It does not borrow the historic proprietary FP library stored
in `transputer-fpu.S`.

The base T805 build has 227 cases; the T9000 build has 551. Each prints `BEGIN`
before calling the probe, then `PASS` or `FAIL` with raw result words. A
mismatch also prints the output word index, actual bits and expected bits.
Case IDs and totals are hexadecimal. No input or iserver protocol is required
once the executable payload has arrived.

Full builds now also run **78,848 software bfloat16 checks** after the native
FP probes: all 65,536 widening patterns and 13,312 SoftFloat-derived narrowing
vectors. This extension uses ordinary T805/T9000 software conversions, never
TVX instructions. Require the final **`FPU-ALL PASS`**, as well as the native
`FPU-SUPER PASS` and `BF16 PASS` summaries. The UART validator checks both
phases and rejects logs truncated before bfloat16 completes. Use `FPU_BF16=0`
for a deliberately native-FP-only image; SoftFloat-only seed profiles also
omit the extension. Each generated `bf16-manifest.json` declares its presence.

## Files and boot protocol

Use `fpu-super-t805-link.btl` or `fpu-super-t9000-link.btl` for a length-prefixed
link-0 boot path. The stream is:

```
A7 [167 executable bootstrap bytes] [contiguous application payload]
```

There is no leading `00`, no chunk framing in the payload, no 16-bit length,
and no extra message after the application starts. Send the whole file in
order through link 0 with normal link handshaking. The bootstrap moves itself
to 0x8003f000, sets workspace to 0x8003e000, and receives the fixed payload size
embedded in its instructions into 0x80000070. It then enters that address.

UART startup markers are `B` (loader waiting for payload), `L` (payload loaded)
and `S` (application startup). `H` means the test driver returned and parked;
it does not by itself mean PASS. Read the `FPU-SUPER PASS/FAIL` summary.

Requirements: at least **256 KiB RAM** mapped at 0x80000000, already usable
external memory, a configured link-0 boot path, and the project UART peripheral.
The loader does not configure a physical board's DRAM interface. It is tested
through T4's executable-bootstrap/link receive path, not on physical silicon.

The `*-direct.btl` variants retain the existing T4-specific `00` + length-
prefixed chunks + terminal `00` format. Do not stream these to a ROM boot port.
The accompanying `.json` records loader size, RAM requirement and SHA256.

UART uses 32-bit accesses: data 0x10000000, status 0x10000004. Status bit0 is
TX ready; bit1 is RX available. Normal test output and loader B/L poll TX ready.
The inherited startup S/H markers are direct stores. These are project MMIO
registers, not built-in T805 peripherals.

## Instruction coverage and limits

`coverage.json`, `cases.h` and `probes.S` are generated separately for each CPU.
They record every test's input words, expected output words and named operations.
The T805 manifest covers 54 named FP operations/sequences, plus the `fpentry`
dispatcher used by its micro-operations. The T9000 manifest covers 56 named
operations in the initial profile; the latest profile also names trap/control
operations. The profiles intentionally differ; T9000 direct encodings are not
sent to the T805.

Covered groups include:

- Single/double loads, stores, indexed loads and load-add/load-multiply forms.
- Add/subtract/multiply/divide, zero loads, duplicate/reverse and comparisons.
- NaN/finite/order predicates, signed zero, subnormals, infinities, cancellation,
  division by zero and invalid-operation flags.
- Signed/unsigned integer conversion, precision conversion, integer rounding
  in all four modes, range checks and integer stores.
- T805 `fpentry` operations: square-root first/step/last sequences (two steps
  for single precision, five for double), exponent scaling, absolute value,
  multiply/divide by two, rounding modes, error controls and `fpunoround`.
- Remainder, including large exponent separation; T9000 `fprem`, `fprange`,
  `fpge`, `fplg`, `fpadddbsn` and direct versions of the arithmetic controls.
- Diagnostic `fpsttest`/`fpldtest` roundtrips, and T9000 `fpstall`/`fpldall`
  mixed-precision data, rounding-mode and error-flag preservation.

This is broad instruction smoke/edge testing, **not exhaustive IEEE compliance
or validation of every microcode intermediate state**. In particular:

- Full T9000 builds run `audit_opcodes.py`: all 50 architectural FP opcodes
  and six compatibility/diagnostic operations must have named probes and
  emitted raw instructions. A separate assembly object verifies each named
  mnemonic against independent expected opcode bytes. `opcode-audit.json`
  lists every opcode and its probes. Use a rebuilt compiler with the
  `fpstall`/`fpldall` assembler entries. SoftFloat-only subset builds skip
  this full-inventory requirement.

- Remainder uses the documented conditional step sequence. T4 completes its
  calculation in `fpremfirst`, so it can skip `fpremstep`; opcode presence is
  not proof that real hardware's iterative implementation has been validated.
- `fpunoround` currently has an exact-value probe, not exhaustive guard-bit tests.
- `fpsttest`/`fpldtest` are diagnostic hooks. The normal compiler opcode census
  flags them intentionally; their inclusion here is deliberate, not accidental
  T9000 leakage into T805 code. A core omitting them will stop at their BEGIN.
- Old preliminary T9000 tables naming opcode DD `fpnoround` differ from the
  later instruction-set manual's `fpadddbsn`; this suite targets the latter.
- Scheduling/preemption and trap-state preservation remain separate OS tests.

## Build and run

From a Linux/WSL shell, with the Transputer compiler and netloader built:

```sh
TCC=/path/to/transputer-tcc NETLOAD=/path/to/transputer-netload \
FPU_SUPER_OUT=/absolute/output bash transputer/tests/fpu-super/build.sh

T4_BIN=/path/to/t4 FPU_SUPER_OUT=/absolute/output \
bash transputer/tests/fpu-super/run.sh
```

Do **not** add T4 `-se`: the `fpchkerr` case deliberately transfers an FP error
to the integer Error flag before clearing it. `run.sh` uses 256 KiB RAM and
checks both loading formats. It returns failure for failed or incomplete tests;
known emulator defects are not whitelisted. `FPU_TIMEOUT` overrides its default
120-second per-image limit.

For a UART log from your own core:

```sh
python3 transputer/tests/fpu-super/validate_uart.py \
    /absolute/output/t805/coverage.json captured-uart.txt
```

Exit0 means all cases passed, exit1 means complete execution with test failures,
and exit2 means missing, duplicated, misordered or incomplete results.

## Validation checkpoint — 19 November 2026

Artifacts and UART logs are in
`logs/compiler-coordination/fpu-super-2026-11-19/`.
Both loading formats completed with the same results on T4:

| Profile | Complete | Passed | Failed |
| --- | ---: | ---: | ---: |
| T805 (`-s8`) | 105 | 105 | 0 |
| T9000 (`-s9`) | 119 | 115 | 4 |

The four T9000 failures are `fpgt-sn-nan`, `fpgt-db-nan`,
`fpeq-nan-self-sn` and `fpeq-nan-self-db`. The pre-fix emulator used the older
comparison behavior for these shared instructions. The T9000 manual requires
false with a NaN operand; the test expectations remain strict. No emulator
changes were made to conceal these failures. T805 uses `fpordered` to mask
its NaN greater-than comparison, as required by its programming model.

The validator also rejected deliberately truncated output and a PASS record
whose result bits were altered. The link images
begin with A7 and were exercised using actual emulated link input, not direct
payload preloading. T4 exits255 after STOPP with no runnable processes; the
UART summary, not that exit status alone, decides success.

References used for semantics (no instruction implementations copied):

- [INMOS T805 engineering data, 1989 databook](https://www.bitsavers.org/components/inmos/transputer/72-TRN-203-01_The_Transputer_Databook_Second_Edition_1989.pdf)
- [INMOS T9000 Instruction Set Manual, especially section11.9.1 and11.12.3](https://www.transputer.net/iset/72-trn-240-01/t9000ism.pdf)

## Additional instruction-table cross-check

The user-supplied IMS T9000 ISA tables (19 November 2026) list 50 FP
mnemonics across arithmetic, memory, comparison, conversion, stack, rounding,
and whole-state save/restore. All 50 appear in the T9000 coverage manifest.
The table also agrees with the later `2D FD` encoding of `fpadddbsn`.
This is a named-instruction coverage check, not exhaustive operand coverage.

Its cycle counts and zero-cycle grouped rounding controls are useful compiler
scheduling guidance, but this suite does not validate cycle accuracy or
instruction grouping. Rounding controls must be placed according to their
one-operation lifetime; `fpldall` restores saved rounding state. The table's
P-process and exception annotations are relevant to separate protected-mode
and trap tests; these images run bare metal.

## Per-probe timing (19 November 2026)

Version 2 prints `TIME <hex-case-id> ticks=<8-hex-digits>` after every result.
The default images read the architectural `ldtimer` instruction. At high
priority the timer unit is 1 microsecond; at low priority it is 64 microseconds.
This startup uses an even workspace descriptor (high priority). T4 advances
normal timers from host elapsed time, so very short probes often report zero
and host scheduling can introduce noise. These are elapsed ticks, not CPU cycles.

Each generated probe resets FP state, reads `ldtimer`, stores the start value
in io[22], executes the test sequence, then reads `ldtimer` and stores io[23].
The unsigned subtraction handles one counter wrap. The interval includes the
start-value recording (`ldl 1; stnl 22`), the test's operand loads and result
stores, and the ending timer read. It excludes FP reset, call/return, result
checking, and all UART polling/writes. It is a sequence measurement, not an
isolated opcode latency. UART hardware may still transmit already-written
bytes concurrently; there is no UART drain register in this interface.

T4 has an existing diagnostic override of `ldtimer`:

| Build `FPU_TIMING` | Run `T4_EST_CYCLES` | UART unit | Meaning |
| --- | --- | --- | --- |
| `ticks` (default) | `0` | `ticks` | Architectural timer |
| `instructions` | `2` | `instructions` | Dispatched instruction count |
| `t800_model_cycles` | `1` | `t800_model_cycles` | T800 timing estimate; T805 image only |

Build each mode into a separate `FPU_SUPER_OUT` directory. `run.sh` reads its
`timing-mode` file and sets the corresponding emulator override explicitly.
When running manually, match the override to the compiled label: the guest
cannot detect this emulator setting. Use the default ticks images on hardware.
The validator requires exactly one correctly labelled timing record per case.

The instruction counter counts each dispatched primary/secondary instruction
once. Operand `pfix`/`nfix` bytes are tracked separately, not added to that
instruction count. The cycle estimate adds their costs. `fpentry` counts as
one dispatched instruction, with its selected micro-operation cost included
in the cycle estimate. Loop iterations count each time they execute; an
iterative FP opcode's internal microsteps are not separate dispatched instructions.

The model is implemented in `transputer/t4/t800_cycles.h`, selected in
`main.c`, and returned by `ldtimer` in `p.c`. It excludes memory wait states,
channel waits and accurate concurrency/pipeline timing. It is not a T9000
cycle model. The supplied Scala pipeline-stage table describes a separate
implementation and does not establish cycle accuracy for T4.

Timing artifacts: `logs/compiler-coordination/fpu-timing-2026-11-19/`,
`fpu-timing-instructions-2026-11-19/` and `fpu-timing-cycles-2026-11-19/`.
All ten image runs completed at 256 KiB: both boot formats for both CPUs in
tick/instruction modes, plus both T805 cycle-model formats. Every case emitted
one timing record. Results remain 105/105 T805 and 115/119 T9000, with the same
four documented NaN failures. Instruction and estimated-cycle deltas match
between direct/link boot for every case. The first single load/store probe
reports 8 dispatched instructions and 19 T800 model cycles, including the
recording overhead. Missing, duplicated, wrong-unit and truncated timing logs
were rejected. Normal timer deltas are deliberately not required to match.

## T9000 unordered-comparison repair — 19 November 2026

T4 now masks unordered binary comparison results only in T9000 mode, after
executing the existing comparison helper. This keeps operand consumption,
rounding reset and existing error effects while preserving T800/T805 raw
bit-pattern ordering and equality. This repair concerns comparison results;
it does not redesign IEEE exception reporting.

The suite adds 24 raw-opcode comparisons per profile: single/double precision,
positive/negative quiet/signaling NaNs, NaN on either side of greater-than,
and self-equality. T805 explicitly expects its legacy results, without an
`fpordered` mask; T9000 expects false. The original four failing cases remain.

Artifacts and the rebuilt emulator are under
`logs/compiler-coordination/fpu-nan-fix-2026-11-19/`. Its `validate.sh` records
the build and run procedure. The `ticks`, `instructions` and
`t800_model_cycles` subdirectories contain BTLs, coverage and UART logs;
the last mode applies only to T805. These supersede the historical failing
checkpoints above when used with the repaired emulator.

Validated T4 commit: `338fc17`. All ten runs pass: 129/129 T805 and
143/143 T9000, with complete timing records. Direct/link instruction counts
and T805 model-cycle counts agree for every probe. The original 105 T805
results and instruction counts are unchanged from the pre-fix checkpoint.

## Rounding and exception boundaries — 19 November 2026

The suite now covers 219 T805 and 234 T9000 cases. The added boundaries use
explicit input/output bit patterns, with no target-side FP oracle:

- Adjacent representable operands immediately below, at and above half an ULP
  at positive and negative 1.0, in nearest, plus, minus and zero modes.
- Positive/negative maximum-finite times two, checking infinity versus maximum
  finite results under all four rounding modes and aggregate FP_Error.
- Positive/negative smallest-subnormal divided by two, checking directed
  rounding, signed zero and absence of aggregate FP_Error.
- Exact minimum-normal to subnormal transitions.
- Repeated fptesterr, checking error detection followed by clearing.
- One-instruction rounding lifetime after arithmetic and loads, and T9000
  rounding restored by fpldall then consumed by the next FP operation.

The pre-fix T4 passed the arithmetic/flag boundaries but failed six T9000
rounding-lifetime cases. Its persistent rounding override contradicted ISM
11.6. The repair removes that override; fpstall saves the selected mode then
resets it, while fpldall restores it without consuming it. Both profiles keep
per-probe UART timing, and the original NaN regression cases remain enabled.

Here “exception flags” means the aggregate FP_Error observed with fptesterr
and its clear-on-test behaviour. Underflow/inexact alone must not set this
aggregate flag. T9000's separate IEEE exception flags, trap enables and full
architectural FPstatusReg encoding are not validated by these probes; T4's
existing state-vector encoding remains an emulator-specific representation.
Do not interpret a passing result as full T9000 IEEE trap/status conformance.

Pre-fix evidence: `logs/compiler-coordination/fpu-boundaries-2026-11-19/`.
Repaired build and runs: `logs/compiler-coordination/fpu-boundaries-fixed-2026-11-19/`.
The latter contains the build/run script, emulator, and separate `ticks`,
`instructions` and T805-only `t800_model_cycles` artifacts.

Semantics reference: [T9000 ISM, sections 11.2.3 and 11.6](https://www.transputer.net/iset/72-trn-240-01/t9000ism.pdf).

Validated T4 commit: `f0c28c3`. All ten runs passed at 256 KiB RAM:
219/219 T805 in ticks, instructions and T800 model-cycle modes; 234/234
T9000 in ticks and instruction modes, each via direct and link boot. Every
case emitted a timing record. Deterministic timings agree between boot formats.

## Separate IEEE flags and FP traps — 19 November 2026

The latest T9000 profile has 318 cases (T805 remains 219). It now tests the
architectural StatusReg error flags and enable bits through ldflags/stflags:

| Condition | Flag bit | Enable bit | Handler error type |
| --- | ---: | ---: | ---: |
| Aggregate FPError | 2 | 3 | 7 |
| Invalid operation | 8 | 9 | 8 |
| Divide by zero | 10 | 11 | 9 |
| Overflow | 12 | 13 | 10 |
| Underflow | 14 | 15 | 11 |
| Inexact | 16 | 17 | 12 |

New probes check flag masks and clearing, all five IEEE conditions, the
aggregate compatibility flag, quiet/signaling NaN arithmetic and comparison
rules, and integer range checks. Load-only NaNs must leave flags clear.
Underflow tests distinguish exact tiny results (trap if enabled, no flag when
masked) from tiny inexact results. fptesterr clears aggregate FPError while
leaving the individual IEEE flags intact.

Local trap probes install a THDS, select enables, execute the faulting opcode,
check Areg/Breg, saved controls and Eptr, inspect the restored operands and
return with tret. They check aggregate-over-specific precedence,
overflow/underflow-over-inexact precedence, and inexact-only trapping. Taken
traps must not set flags for the trapping operation. The Eptr check includes
the prefix byte and verifies the continuation points after the opcode.

Compound probes check load/add rollback after the completed memory load,
fprtoi32 traps separately during rounding and range checking (including
preservation of an earlier inexact flag), and fpadddbsn traps in its addition
and conversion stages. Handler setup uses ldth to save the old handler; it
does not rely on selth returning a defined integer-stack value.

Two protected-process probes use a separate workspace at 0x8003c000 and
identity RODS. They verify invalid-operation and exact-tiny underflow traps
return to the supervisor with the expected error type, unmodified controls,
and restored FP operands. run.sh explicitly enables T4_PROTECT=1. Hardware
runs need actual T9000 protection support; T805 does not execute these probes.

Timing still excludes UART reporting. A trapping probe's measured interval
includes trap entry, handler checks and return, so its count is not the
latency of the faulting arithmetic instruction alone.

Artifacts: `logs/compiler-coordination/fpu-ieee-checkpoint-2026-11-19/`.
The earlier directories in this document are historical checkpoints.

The new ldflags/stflags layout supersedes T4's private two-bit encoding.
FP traps save architectural controls in PDS.Cntl and deliver the architectural
reason/type in A/B. At that checkpoint, non-FP traps still used the legacy cause slot and
fpstall/fpldall used a private encoding. The architectural-state checkpoint
below supersedes both limitations. These tests are
not exhaustive IEEE arithmetic, full OS integration or physical-silicon
validation. Native exception detection is supplemented by explicit T9000
NaN/range rules and relies on the tested host floating-point environment.

Final validation: all ten 256 KiB runs passed (219/219 T805, 318/318 T9000),
with a timing record for every case. Instruction and model-cycle counts agree
between boot formats. All original 219 T805 results and instruction counts
match the preceding boundary-test checkpoint. The region/protection unit
tests, 4,000 concurrent per-CPU install/return checks and fault-reporting
regression also passed. No Linux boot or physical-hardware validation was run.

Validated emulator commit: `5521f9a` in the nested T4 repository.

## Architectural FP state and Linux ABI — 19 November 2026

The seven-word image now follows ISM tables 11.22–11.26. Word zero contains
rounding (bits 0–1: zero, nearest, plus, minus) and the three register types
(bits 2–3, 4–5, 6–7: 0 single, 1 double). Reserved bits are zero. Single values
occupy the lower-addressed word of their slot. FPError and IEEE exception
flags are separate StatusReg state; fpldall does not restore or clear them.
The FP shadow image uses the same format and preserves interrupted rounding.

The suite adds 38 probes: all 32 rounding/type combinations, four externally
constructed load images, a set-error independence check and a syscall status
check. The previous cleared-error test now checks the architectural behavior.

Linux now reads trap reason/type from A/B for every P trap and preserves
PDS.Cntl as StatusReg. Its assembly wrapper saves supervisor flags separately;
sigreturn restores only P-visible flags/enables and rejects reserved FP image
bits/types. Rebuild T4 and Linux together: kernels using the old PDS cause slot
are incompatible with this trap ABI. Run protected tests with T4_PROTECT=1.

Artifacts and reproducible build scripts are under
`logs/compiler-coordination/fpu-architectural-state-2026-11-19/`.
The 32 MiB Linux boot passed libc/stdio/pthread stress, epoll, POSIX timers,
different caller/handler sticky flags across syscalls and sigreturn, timeslice
FP/MOVE2D state, two-thread nested signals, two-context CoreMark (2,000
iterations), and desktop/Files open-close checks. Restored desktop pixels
match with the clock masked. This validates emulation, not physical silicon.

## Optional SoftFloat oracle profile

The supplied `transputer/softfloat-3e` directory contains an arithmetic library,
not a TestFloat test runner. `softfloat_vectors.py` builds a host-only oracle
from that tree and generates 472 independent vectors: binary32/binary64
arithmetic, rounding to integral values, float-width conversions, signed
integer conversions in both directions, and unsigned integer-to-double.
All four rounding modes are covered.
Cases include halfway rounding, cancellation/signed zero, subnormals, overflow,
underflow and inexact results. T9000 probes check the separate IEEE flags;
T805 probes check result bits using the appropriate sqrt instruction sequence.

Run from WSL with the usual TCC, NETLOAD and T4_BIN environment variables:

```sh
# Regenerate the checked-in reference data when intentionally updating the oracle.
python3 transputer/tests/fpu-super/softfloat_vectors.py \
  --source transputer/softfloat-3e \
  --out transputer/tests/fpu-super/softfloat-vectors.json

# Run just the independent arithmetic vectors, with normal UART timing.
export FPU_SOFTFLOAT=only FPU_TIMING=instructions FPU_SUPER_OUT="$PWD/logs/fpu-softfloat"
bash transputer/tests/fpu-super/build.sh
bash transputer/tests/fpu-super/run.sh
```

`FPU_SOFTFLOAT=1` appends these vectors to the architectural suite; unset/0
keeps the base suite. The JSON includes a hash of the source/build inputs,
records the adapter hash and random seed, uses 8086-SSE specialization and
detects tininess after rounding. The supplied
source tree is left untouched and is not vendored by this change. Ordinary
BTL builds use the committed numerical vectors and do not need SoftFloat.
Its license remains in `transputer/softfloat-3e/COPYING.txt`.

SoftFloat is an arithmetic oracle, not a T9000 instruction/trap emulator.
NaN payload conventions, legacy T805 comparisons and T9000 trap rollback
remain in the architecture-specific probes. This finite profile does not
claim exhaustive IEEE coverage. Guest UART and host oracle work stay outside
the measured guest instruction sequence. The default per-image timeout is
600 seconds for the expanded combined suite's UART stream on a busy host.

Initial 80-vector checkpoint implementation commits: nested T4 `e3a0fe9`, nested Linux
`4726b24b92be`. The SoftFloat-only instruction profile passed 80/80 cases
on each CPU in both boot formats (320 executions), with matching direct/link
instruction counts. Combined profiles compile to 299 T805 and 436 T9000
cases; their link payloads are 82,120 and 121,804 bytes respectively. Execution
validation ran the architectural and SoftFloat profiles separately.

Final matrix: all 14 image runs passed, totaling 3,058 successful probe
executions. The ten architectural runs cover both boot formats in tick and
instruction modes for both CPUs, plus T805 model-cycle timing. The four
SoftFloat runs use instruction timing. All deterministic direct/link timings
agree, and all 219 original T805 instruction timings match the prior checkpoint.
The expanded instruction runs initially exceeded the old 120-second UART
limit without mismatches; rerunning with the 300-second limit completed.
Machine-readable results and artifact hashes are in the checkpoint directory's
`validation.json`. Host region/protection, 4,000 concurrent CPU-state checks
and fault-reporting tests also passed.

## Expanded SoftFloat super BTLs — 19 November 2026

The 408-vector corpus contains the original 80 arithmetic cases, 216 additional
boundary/conversion cases and 112 seeded random cases. Boundary cases include
both signs immediately below, at and above halfway; odd/even significand ties;
normal/subnormal transitions; directed rounding; and integer endpoints.
Float-to-integer inputs have representable rounded results, leaving invalid
conversion conventions to the existing architecture-specific tests.

Random arithmetic inputs span finite exponent/significand fields. Random
float-width and float-to-integer conversions are included. Regeneration with
`--seed 0x9000805` reproduced every result and flag; change `--seed` to explore
a different corpus. Oracle generation uses integer bit construction and
SoftFloat, never host floating-point arithmetic to obtain expected values.

The rebuilt combined profiles contain 627 T805 and 764 T9000 cases. Build them
with `FPU_SOFTFLOAT=1 FPU_TIMING=instructions`, then run the normal runner.
The larger T9000 image requires the updated loader bound: its 215,536-byte
payload still leaves a full guard page below the protected workspace. The
174,908-byte T805 payload also fits the same 256 KiB layout. A boundary check
accepts the exact safe payload limit and rejects one additional byte.

Artifacts are in `logs/compiler-coordination/fpu-softfloat-expanded-2026-11-19/`.
The `super-instructions/` directory contains both CPUs' `*-direct.btl` and
`*-link.btl`, UART transcripts, coverage manifests and result JSON files.
Direct BTLs use T4's chunked loader; link BTLs contain the executable one-byte
length bootstrap for streaming through link 0. Timing records count emulated
instructions, not hardware clock cycles, and exclude UART printing.

Linux validation reuses the unchanged committed kernel (`4726b24b92be`) and
T4 (`e3a0fe9`) in a fresh 32 MiB boot. It covers libc threading, epoll, POSIX
timers, distinct caller/handler exception flags, nested FP/MOVE2D signal
handlers in two threads, two-context CoreMark and desktop/Files restoration.

Final validation passed all four combined image runs: 627/627 T805 and
764/764 T9000 in both direct and link boot, totaling 2,782 probe executions.
Every probe emitted instruction timing, direct/link counts agree, and all
original 219 T805 and 356 T9000 counts match the architectural-state checkpoint.
Linux and the desktop pixel-restoration checks passed; the captured restored
desktop was also visually inspected. No T4 arithmetic defect was found, so
the emulator and kernel sources remain unchanged. The early 344-vector
link-run log truncated during host disk exhaustion is superseded by these
complete combined runs. `validation.json` records the results and BTL hashes.

## Multiple seeds and exceptional conversions — 19 November 2026

The default corpus now adds 64 exceptional-conversion cases to the preceding
408 vectors: 32 quiet/signalling NaN width conversions (both signs, all four
rounding modes), 24 invalid float-to-int32 checks, and eight positive/negative
double-to-single overflow conversions. The combined super BTLs contain
699 T805 and 836 T9000 probes after the eight new per-CPU NaN-absolute-value
regressions; they retain the 256 KiB memory layout.

Conversion coverage includes:

| Conversion | Guest instructions / check |
| --- | --- |
| Signed int32 to single/double | fpi32tor32 / fpi32tor64 |
| Unsigned int32 to double | fpb32tor64 |
| Single to double, double to single | fpr32tor64 / fpr64tor32 (T805 entry equivalents) |
| Floating-point to valid int32 | fprtoi32 followed by fpstnli32 |
| NaN/out-of-range to int32 | fprtoi32 error flags, without an undefined integer store |
| Floating-point rounding to an integral value | fpint, result stays in floating-point format |

The raw SoftFloat answer stays in the reference JSON. The guest generator
applies T9000 ISM 11.11 and 11.14 separately: narrowing any NaN yields
R64ToR32NaN (`0x7fc20000`); quiet NaN conversions set FPError, while signalling
NaNs also set FPInvalidOp. Invalid int32 conversion checks verify flags rather
than SoftFloat's integer-indefinite value, because storing NaN as an integer
is undefined on the Transputer. T805 NaN tests check classification and FPError
without assuming T9000 payload/quieting rules. Finite results remain bit-exact.

Two additional checked-in random-only corpora contain 112 cases each, using
seeds `329` and `0xdeadbeef`. Together with the default `0x9000805` corpus,
these provide 336 seeded random cases and 696 total oracle vectors.

```sh
python3 transputer/tests/fpu-super/softfloat_vectors.py \
  --source transputer/softfloat-3e --seed 329 --random-only \
  --out transputer/tests/fpu-super/softfloat-seed-329.json

export FPU_SOFTFLOAT=only FPU_TIMING=instructions
export FPU_SOFTFLOAT_VECTORS="$PWD/transputer/tests/fpu-super/softfloat-seed-329.json"
export FPU_SUPER_OUT="$PWD/logs/fpu-seed-329"
bash transputer/tests/fpu-super/build.sh
bash transputer/tests/fpu-super/run.sh
```

Unset `FPU_SOFTFLOAT_VECTORS` and use `FPU_SOFTFLOAT=1` for the full default
super BTLs. UART still emits BEGIN, PASS/FAIL, result bits, per-probe TIME and
the final suite summary. Instruction counts exclude UART printing and are
not claims about physical CPU cycles.

Artifacts: `logs/compiler-coordination/fpu-softfloat-nan-seeds-2026-11-19/`.
`final/` holds the rebuilt full images; `final-seed-329/` and
`final-seed-0xdeadbeef/` hold the additional seed images tested on the fixed
emulator. `exceptions/` holds the initial focused conversion run.
Each directory includes direct/link BTLs, UART logs and result JSON.

The user's RTL report independently found 22 failures in the preceding
627-case T805 image on branch tip `ea62487`: 13 directed-overflow/multiply
results, eight subnormal widenings, and one tiny negative narrowing. The user
reported 627/627 after applying T-FPU-3c/D20 to a scratch RTL copy. This is
reported external evidence, not a claim that the RTL fixes have landed.
The previous 627-case artifacts remain under `fpu-softfloat-expanded-2026-11-19`.

That review also exposed a T4 bug: the legacy absolute-value helper cleared
the sign of negative NaNs on T9000. ISM 11.7 explicitly preserves it. The
T9000 result finalizer now restores the NaN operand before quieting it; T805
continues to clear its sign and set FPError. Eight probes per CPU cover both
precisions, both signs and quiet/signalling NaNs. The old emulator fails all
four negative T9000 cases; the corrected emulator passes all eight. T805
passes on both emulators. Evidence is in `fpabs-regression/`.

TinyCC's hardware-FPU emission already selects direct fpabs for T9000 and
fpentry/fpuabs for T805 (`tp_gen_fpu_unary`); no compiler encoding change is
needed. The test images were rebuilt with the existing compiler. This check
does not claim that soft-float fallback code models every hardware FP flag.

Final runs stage UART files on the WSL filesystem, then copy complete logs
and BTLs into the checkpoint directory. This avoids the observed cost of
byte-at-a-time UART logging onto the Windows-mounted filesystem; measured
guest instruction counts are unaffected.

Final validation on T4 commit `58a9971`: all 12 full/seed image runs pass,
totaling 3,966 probes with UART instruction timing. The full images pass
699/699 T805 and 836/836 T9000 in both boot formats; each extra seed passes
112/112 on both CPUs in both formats. Deterministic direct/link counts agree,
and the original 219 T805 / 356 T9000 baseline timings remain unchanged.
The fixed emulator also passes a fresh 32 MiB Linux boot with the unchanged
kernel `4726b24b92be`: timer signals, nested two-thread FP/MOVE2D state,
two-context CoreMark, desktop and Files restoration. The restored pixels
match with the clock masked and were visually checked. `validation.json`
records counts, regression outcomes, hashes and the emulator revision.

## NaN propagation and conversion traps — 19 November 2026

The T9000 profile adds 30 probes: all eight rows of ISM Table 11.31 in
both precisions, six subtraction/multiplication/division propagation probes,
and eight enabled conversion traps. Distinct NaN signs and payloads expose
operand selection errors. Conversion traps cover signalling and quiet NaNs,
overflow, inexact narrowing, inexact underflow and exact tiny results with
underflow trapping enabled. They check the original operand precision and
bits, saved controls, exception type and successful `tret` continuation.

Eight two-NaN probes failed on the previous T4. T9000 now selects the
signalling operand first, or FA when neither/both signal, before quieting.
This follows Table 11.31 independently of host arithmetic. `fpadddbsn` keeps
its special R64ToR32NaN rule. The T805 path is unchanged.

Identical trap handlers share one body. Expected-value tables omit the two
timestamp slots, which have no fixed oracle. Both images still fit the
256 KiB layout without weakening the bootstrap workspace guard. Timing
remains per-probe instruction counts, excluding UART output, not physical
T9000 cycles. TinyCC hardware opcode emission needs no change.

Evidence and rebuilt BTLs are in
`logs/compiler-coordination/fpu-nan-traps-2026-11-19/`: `before-fix/` records
the eight failures; `final/` contains the 699-case T805 and 866-case T9000
images. Additional random profiles are in `final-seed-329/` and
`final-seed-0xdeadbeef/`. T9000 is the architectural target; T805 remains a
compatibility regression. The external RTL default-mode decision is separate
and is not changed by this checkpoint.

Validated on nested T4 `6754ff5`: all 12 image runs pass, totaling 4,026
probes with complete UART timing. Direct/link counts agree and the original
219 T805 / 356 T9000 baseline instruction counts are unchanged. A fresh
32 MiB boot of unchanged Linux `4726b24b92be` passes libc/threading, epoll,
POSIX timers, protected signal flags, nested two-thread FP/MOVE2D handlers,
two-context CoreMark and desktop/Files restoration. Restored pixels match
with the clock excluded and were visually inspected. `validation.json`
records results and hashes.

## Compound and remainder edges — 19 November 2026

Thirty-five more T9000 probes exercise `fpadddbsn`, `fprem` and `fprange`.
They check propagated NaN priority, generated remainder NaN codes, infinite
divisors, positive/negative quotient overflow and the largest valid quotient
in both precisions. Trap cases cover NaN operands, zero divisors, quotient
overflow and tiny exact remainder results. The tiny remainder case checks
original operands; this is allowed by ISM 11.13, which also permits a
partially reduced dividend on an interruptible `fprem` underflow trap.

The previous T4 fails 12 probes: generic host remainder NaNs lack the
T9000 cause code, quotient overflow was unchecked, and the double range
quotient correction incorrectly computed `0 * infinity`. T4 now assigns
the architectural NaN codes and invalid flag, checks quotient magnitude
against `2^24-1`/`2^53-1`, and skips the correction for non-finite operands.
The new `fpadddbsn` cases pass without an emulator change. No TinyCC
backend change was needed.

Each generated CPU directory now includes `trap-cases.json` with numeric
IDs and names for probes requiring a handler. This supports external RTL
staging (such as T-FPU-T9a versus M7); the T4 runner still requires every
case to pass. No external expected-failure policy is applied automatically.

Input tables omit timestamp slots just as expected-value tables do; the
probes initialize both counters themselves. Full images retain the fixed
256 KiB layout and all UART timing. New artifacts are under
`logs/compiler-coordination/fpu-compound-edges-2026-11-19/`, with full
images in `final/`, old-emulator failures in `before-fix/`, and the two
additional random-seed profiles in `final-seed-329/` and
`final-seed-0xdeadbeef/`.

Validated on T4 `1d88eaf`: full images pass 699/699 T805 and 901/901 T9000
in both boot formats. All 12 full/seed runs pass, totaling 4,096 timed
probes. Direct/link timings agree and original baseline instruction counts
are unchanged. A fresh 32 MiB boot of unchanged Linux `4726b24b92be` passes
libc/threading, epoll, timers, protected/nested two-thread FP/MOVE2D signals,
two-context CoreMark and desktop/Files restoration. Restored pixels match
outside the clock and were visually checked. `validation.json` records
results, image hashes and the T4 revision.

## FP opcode audit and compound rounding — 19 November 2026

Twenty-six additional `fpadddbsn` probes cover positive/negative midpoint
rounding and tiny results in all four modes, signed-zero cancellation,
double-rounding counterexamples, and trap-stage operand/control/rounding
preservation. A handler snapshots the architectural FP image before any
FP stores consume the restored rounding mode. Addition-stage traps retain
the selected mode; conversion-stage traps observe the nearest mode after
the addition stage, as required by the compound-instruction rule.

All new cases pass on unchanged T4 `1d88eaf`. The opcode audit did uncover
a TinyCC assembler omission: `fpstall` and `fpldall` lacked mnemonic entries
although raw-byte tests worked. These now emit `60 ff` and `60 fe`. The
old compiler rejects the audit at `fpstall`; the rebuilt compiler passes
all 56 byte comparisons. Compiler-generated arithmetic is unchanged.

The audit inventory comprises:

| Group | Instructions |
| --- | --- |
| Memory and stack | fpldnldbi, fpstnldb, fpldnlsni, fpstnlsn, fpldnldb, fpldnlsn, fpldzerosn, fpldzerodb, fpdup, fprev, fpstall, fpldall |
| Arithmetic and compound memory | fpadd, fpsub, fpmul, fpdiv, fprange, fprem, fpldnladddb, fpldnlmuldb, fpldnladdsn, fpldnlmulsn, fpdivby2, fpmulby2, fpsqrt, fpexpdec32, fpexpinc32, fpabs, fpadddbsn |
| Predicates | fpnan, fpordered, fpnotfinite, fpgt, fpeq, fpge, fplg |
| Conversion/range | fpi32tor32, fpi32tor64, fpb32tor64, fprtoi32, fpstnli32, fpint, fpr32tor64, fpr64tor32, fpchki32, fpchki64 |
| Rounding | fprn, fprp, fprm, fprz |
| Compatibility/diagnostic | fpsttest, fpchkerr, fpldtest, fptesterr, fpseterr, fpclrerr |

Legacy `fpentry`, `fpremfirst`, `fpremstep` and `fpu*` micro-operations belong
to the T805 profile, not additional architectural T9000 opcodes. Opcode
coverage is complete for this documented inventory; edge coverage is not
an exhaustive IEEE or hardware conformance claim.

Full rebuilt images pass 699/699 T805 and 927/927 T9000 in both formats.
All 12 full/seed runs total 4,148 passing probes with UART instruction
timing, unchanged original baseline instruction counts and matching
direct/link timings. A fresh 32 MiB boot of unchanged Linux `4726b24b92be`
passes libc/threading, epoll/timers, protected/nested two-thread FP/MOVE2D
signals, two-context CoreMark, and desktop/Files restoration. Pixels match
outside the clock and were visually checked. Artifacts, opcode/trap
manifests and `validation.json` are under
`logs/compiler-coordination/fpu-round-audit-2026-11-19/`.

Initial-value tables now omit the two pointers assigned by the driver,
as well as the two timestamps assigned by probes. Coverage JSON retains
the complete 24-word logical input shape. This keeps both full images
inside the fixed 256 KiB layout without dropping probes.

## Cross-opcode edge coverage — 19 November 2026

Ninety-six new T9000 cases extend the inventory audit with:

- 42 rounding-consumption probes, setting round-plus immediately before
  each target instruction and snapshotting the architectural rounding word
  before a subsequent FP operation. Selectors, save/restore and the two
  rounding-dependent integer operations retain their dedicated tests.
- Eight indexed/non-indexed load/store and stack probes preserving negative
  zero and signalling NaN bits while retaining an unrelated sticky flag.
- 24 quiet/signalling NaN probes across square root, integer rounding,
  multiply/divide-by-two and exponent scaling, checking payloads and flags.
- Four classification probes proving signalling NaNs remain unmodified and
  do not raise flags; eight signed integer range-boundary probes.
- Ten enabled unary traps checking original operands, saved controls,
  exception reason and successful return in both precisions.

Expected values are now stored densely in mask-bit order rather than as
22-word arrays. The logical coverage JSON and UART word numbering are
unchanged. A deliberately corrupted expected value for logical slot 21
produces exactly the intended failure in both boot formats; see `mutation/`.
With the additional cases, the T9000 payload shrinks from 238,468 to 189,820
bytes and T805 from 178,556 to 124,556 bytes. The 256 KiB guard is unchanged.

All cases pass on unchanged T4 `1d88eaf`; no new emulator or compiler code
fix was needed. Full images pass 699/699 T805 and 1,023/1,023 T9000 in both
boot formats. All 12 full/seed runs total 4,340 passing timed probes. The
56-opcode audit passes; original baseline instruction counts and direct/link
timing agreement are preserved. The fresh 32 MiB Linux `4726b24b92be` boot
passes libc/threading, epoll/timers, protected/nested FP/MOVE2D signals,
two-context CoreMark and desktop/Files restoration. Restored pixels match
outside the clock and were visually inspected.

Evidence, rebuilt images and `validation.json` are under
`logs/compiler-coordination/fpu-opcode-edges-2026-11-19/`. The deliberately
failing mutation images are separate from passing images in `final/`.

The local SoftFloat tree also contains bfloat16 references. Standard T9000
has no bfloat16 opcodes: TinyCC widens `__bf16` using a shift and narrows
through `__truncsfbf2`. Optional TVX uses its own extension instructions.
Future bfloat16 tests should separately cover software conversions and TVX,
including exhaustive widening and narrowing boundaries; this checkpoint
does not claim to validate those paths.

## Integrated software bfloat16 — 19 November 2026

The full rebuilt images pass 699 T805 / 1,023 T9000 native probes plus 78,848
software bfloat16 checks each, through both direct and link boot. UART reports
per-probe native timing and 308 bfloat16 batch timings in the selected unit.
The shared bfloat16 test code checks exact TinyCC NaN policies separately
from the supplied SoftFloat fork's canonical-NaN policy; flags from SoftFloat
are reference information, not an implemented software conversion interface.

Native input vectors now use a nonzero-word mask and packed values. The
driver explicitly zeroes omitted words before every probe. Logical JSON
inputs and output checks remain unchanged. Bfloat16 reference inputs and
16-bit expected results are stored in separate arrays. These changes keep
the complete images inside the original 256 KiB guard: **160,740 bytes T805**
and **205,804 bytes T9000**. No tests were removed to fit the extension.

The 56-opcode audit still passes. The standalone binary16 and bfloat16
software/TVX suites pass 56 image runs, while four integrated super-image
runs and four binary16 language runs also pass. Artifacts and hashes are in
`logs/compiler-coordination/f16-widen-super-2026-11-19/validation.json`.
Use the images under its `super/` directory for the updated super tests.

## Indirect and packed narrowing — 20 November 2026

TVX binary16 and bfloat16 narrowing now load binary32 bits through the
existing integer load paths. Packed fields use byte assembly; saved indirect
addresses retain the canonical spill convention. Volatile reads are not
reused. Constants retain the established staging fallback.

Each format passes 528,384 conversion checks in 28 software/TVX image runs,
through direct and link boot. All 65,536 widening patterns are tested; each
of 13,312 narrowing vectors is checked through workspace, volatile indirect,
and packed fields at offsets 1, 2 and 3. Exact NaN sign/payload policies are
checked separately from the supplied SoftFloat fork's canonical NaNs.
Narrowing tests cover nearest-even boundaries and seed-329 random inputs;
this is not an exhaustive test of all 2^32 narrowing inputs or every TVX op.

For either format, the measured TVX indirect workload falls from 493,064 to
453,128 instructions (three saved per conversion). Packed offset 1/2/3
workloads fall from 879,112/919,048/905,736 to 799,240/839,176/825,864
(six saved per conversion). Software, widening and workspace timings are
unchanged. UART output and input preparation remain outside measurement;
loop/call/result staging are included. These are T4 instruction counts,
not hardware cycles. No emulator semantic changes were required.

The super builder now also emits a distinct T9000+TVX image, containing
1,023 native FP probes and 132,096 bfloat16 checks with 516 batch timings.
T805 and standard T9000 retain 699/1,023 native probes and 78,848 software
bfloat16 checks. All six super boot runs and sixteen additional language/
context runs pass: 78 current-compiler image runs overall.

Artifacts: `logs/compiler-coordination/tvx-narrow-memory-2026-11-20/`.
`comparison.json` records the old/new timings; `super/` contains all three
image pairs and UART validation results. Use `fpu-super-t9000tvx-link.btl`
for ROM link boot with TVX, or its `-direct.btl` counterpart for T4's direct
loader. The TVX payload is 212,216 bytes and fits the 256 KiB test RAM.

## TVX bfloat16 arithmetic — 20 November 2026

Added 1,116 independently checked arithmetic probes to the T9000+TVX super
image: 528 packed pairs each for BF16ADD2 (#1CE) and BF16MUL2 (#1CD), plus
30 BF16DOT (#1C9) vectors tested at aligned and unaligned addresses.
Coverage includes both lanes, signed zeros, subnormals, half-ULP ties,
overflow, infinities, signalling/quiet NaNs, cancellation, sequential
(non-fused) accumulation, zero/odd lengths, and random seed 329.

`generate_tvx_bf16.py` builds a host SoftFloat oracle from the supplied source
without changing it. Binary32 additions/multiplications are independently
computed; packed results are narrowed with integer nearest-even rounding.
The checked-in header and JSON record inputs, expected results and hashes.
The super builder verifies the header hash. Arithmetic NaNs are checked as
quiet NaNs: payload/sign selection currently differs between host TVX and
the target software FPU. This does not weaken the existing exact-bit NaN
conversion tests. Arithmetic exception flags and trap behaviour are not
claimed by these tests.

Every arithmetic check emits a TVX-TIME UART record, measured before output
and verification. The validated runs use `instructions`, not silicon cycles;
a compound TVX operation counts as one guest instruction, so these readings
must not be treated as the hardware execution cost of a dot product. Input
preparation is outside the interval; calls and result staging are included.

All 94 image runs pass: 56 binary16/bfloat16 conversion runs (528,384 checks
per format), six super runs, four standalone arithmetic runs comparing
software and TVX, sixteen language/context runs, and twelve mixed-operation
regressions (SIMD, vector/GEMVBF, Q8DOTBF, block rotation, vector conversion,
and QGEMM retained activations). The opcode audit confirms the three TVX
instructions in compiled wrappers, and their absence in software wrappers.
Four deliberately corrupted UART logs are rejected by the new validator.
No TinyCC lowering or T4 arithmetic defect was found in this coverage.

The TVX super payload is 225,964 bytes, still within the 256 KiB test RAM.
It passes 1,023 native FP probes, 132,096 bfloat16 conversion checks and
1,116 arithmetic checks per boot. T805 and non-TVX T9000 images retain their
previous hashes. Artifacts, UART logs and hashes:
`logs/compiler-coordination/tvx-bf16-arithmetic-2026-11-20/validation.json`.

Reproduce the standalone arithmetic checks with `TCC`, `NETLOAD`, `T4_BIN`
and `TVX_ARITH_OUT` set, then run `bash transputer/tests/fpu-super/run_tvx_bf16.sh`.
The normal super build includes the arithmetic probes in its TVX variant.
This checkpoint covers the core bfloat16 arithmetic instructions; mixed
quantization/graphics kernels have regression coverage, not an exhaustive
SoftFloat edge audit of every opcode or every input combination.

## Exact TVX bfloat16 NaNs — 20 November 2026

T4 commit `5057020` and the matching software runtime now use the same
integer-bit NaN selection before performing BF16ADD2, BF16MUL2 and BF16DOT
arithmetic. The first NaN in logical operand order wins; preserve its sign
and payload, then set the quiet bit. Invalid operations without a NaN
operand return positive canonical `0x7fc00000` (bfloat16 `0x7fc0`).

Dot products apply this rule at each multiply and sequential add, then at
`acc + sum`. Thus an incoming NaN accumulator wins over a NaN sum, including
all low binary32 payload bits. This is a TVX result contract, not a change
to native T9000 NaN tables or a promise of matching exception flags/traps.
Mixed quantization/graphics kernels are outside this rule's current scope.

The generator uses an independent integer NaN-policy overlay around the
SoftFloat finite arithmetic oracle. The super and standalone validators
now require exact bits, with no NaN-class exception. Coverage grows to
1,036 packed pairs (each add and multiply) and 58 dot vectors (each aligned
and unaligned): **2,188 arithmetic checks**. Every one of the 254 bfloat16
NaN encodings is exercised in both lanes/operand positions. Dot vectors
also test empty input, competing NaNs, generated-invalid results and
binary32 accumulator payloads below bfloat16 precision.

All **94 image runs pass**, including both conversion suites, all six super
boot runs, four standalone arithmetic runs, sixteen language/context runs
and twelve mixed-operation regressions. Software and TVX arithmetic outputs
are bit-identical. The previous T4 fails 54 checks with the same new image;
the validator rejects deliberate sign, payload and quiet-bit mutations.
The initial standalone launch was retried after the compiler build finished.

The T9000+TVX super payload is 237,732 bytes within the 256 KiB guard. It
runs 1,023 native FP probes, 132,096 conversion checks and 2,188 arithmetic
checks per boot. T805 and standard T9000 super hashes remain unchanged.
UART timings are instruction counts excluding output/checking, not hardware
cycles; compound TVX opcodes still count as one guest instruction.

Artifacts and exact hashes:
`logs/compiler-coordination/tvx-bf16-nan-2026-11-20/validation.json`.
Use `super/fpu-super-t9000tvx-link.btl` for ROM link boot, or the corresponding
`-direct.btl` for T4's direct loader. T4's detailed contract is recorded in
`transputer/t4/TVX-BF16-NAN.md`; the vendored TVX headers pass the sync check.

## TVX bfloat16 flags and traps — 20 November 2026

T4 `cd542c9` extends BF16ADD2, BF16MUL2 and BF16DOT to the existing T9000
precise FP checkpoint. Flags accumulate across lanes/stages and bfloat16
narrowing. Nontrapping completion publishes sticky flags; trapping restores
original integer/FP operands and status, with the existing exception priority.
The shared checked helpers preserve the established exact NaN result policy.

The software runtime now saves status, masks intermediate traps, accumulates
flags and uses architectural scalar FP operations to deliver an enabled
exception before returning. It preserves existing sticky flags and handler
changes to enables. Repeated retries drain competing exceptions in priority
order. TinyCC adds T9000-only `__ldflags()` / `__stflags(value)` builtins and
`__T9000__` for this runtime path. The status builtin is rejected for T805;
older runtime targets keep their native fallback exception behaviour.

Validation uses nearest-even arithmetic: invalid, overflow, tiny/inexact
underflow and inexact, including narrowing exceptions, with FPError for
NaN/infinity operands. Exact tiny results alone do not signal underflow.
All 2,188 arithmetic cases now require exact result AND status bits against
independent SoftFloat finite flags plus the explicit TVX NaN/FPError policy.
NaN sign/payload checks remain exact. Twenty-three additional generated
scenarios test enabled traps, aggregate priority, competing lane exceptions,
retry operands, clearing one enable at a time, sticky flags and fptesterr.
The software fallback passes the same scenarios in separate timed images.

All **96 image runs pass**: 56 conversion runs, six super runs, four arithmetic
runs, two software trap runs, sixteen language/context runs and twelve mixed
TVX regressions. The prior T4 fails 2,090 checks with the new arithmetic image.
Flag mutations are rejected by the UART validator. The 50 architectural plus
six compatibility FP opcode audit and vendored-header sync check pass.
T805 and non-TVX T9000 super hashes remain unchanged.

The TVX super now has 1,023 native FP probes plus 23 exception/status probes,
132,096 bfloat16 conversions and 2,188 arithmetic result/flag checks. Its
236,156-byte payload fits the original 256 KiB guard. Repetitive NaN inputs
are generated procedurally, with full independent JSON expectations retained;
flags in stored packed vectors are compressed. No cases or guard space were
removed. UART timings exclude output/checking and are instruction counts,
not the latency of compound TVX operations in hardware.

Important ABI scope: a software trap occurs at an internal scalar raise
instruction, whereas native TVX traps restore opcode operands. Retrying after
changing enables is tested on both paths; a handler that skips or substitutes
an operation does not have an identical saved-register ABI. These are new
L-process trap tests. The existing protected route is reused in T4, but Linux
and new P-process trap stress tests were not run in this checkpoint.

Artifacts: `logs/compiler-coordination/tvx-bf16-flags-2026-11-20/validation.json`.
Updated images are under `super/`; use `fpu-super-t9000tvx-link.btl` for ROM
link boot and `-direct.btl` for T4's direct loader. Reproduce software trap
checks with TCC, NETLOAD, T4_BIN and TVX_TRAP_OUT set, then run
`bash transputer/tests/fpu-super/run_tvx_traps.sh`.

## Protected-process TVX traps — 20 November 2026

Twenty P-process scenarios now run independently on software and TVX paths,
through direct and link boot: **80 timed checks pass**. The probes verify
exception type, zero error-trap reason, P-mode in saved STATUS, no premature
sticky-flag publication, native integer operands, and retry from PDS.Eptr.
They include aggregate priority and successive invalid/overflow/inexact
traps with one enable cleared per retry. A pre-existing divide-by-zero sticky
flag survives throughout. Successful completion returns through syscall
(reason 8) with the expected result and flags in both the P process and PDS.

The supervisor saves FP context with fpstall, overwrites the live FP stack,
then restores it with fpldall before goprot re-entry. Deliberately replacing
fpldall with POP makes the dot/inexact case fail, proving that restoration
is exercised. Each protected process has its own test workspace. Identity
region descriptors allow the test harness memory; these are protected trap
and retry tests, not a new test of inter-process address-space isolation.
Software traps remain at the internal scalar raise site; retry, not skipping
that instruction, is the supported common contract.

A fresh **single 32 MiB Linux boot** on T4 `cd542c9` passed pthread/libc,
epoll/POSIX timers, timer-handler flags, CPU-bound timeslice delivery, two
signal workers, nested two-thread FP/MOVE2D handlers, two-context CoreMark
and the desktop. CoreMark validated both contexts' CRCs (60.332553 iterations/
second for this run, not a performance comparison). Files opened and closed,
and restored desktop pixels match the initial frame outside the clock.
The restored frame was also visually inspected.

This boot reused the validated kernel image from Linux `4726b24b92be`;
Linux and T4 implementation changes were unnecessary. Protected TVX probes
ran as standalone BTLs before the Linux regression boot, not as new Linux
applications. No kernel rebuild or Linux TVX signal stress is claimed.

Artifacts, UART logs, framebuffer captures and image hashes:
`logs/compiler-coordination/tvx-protected-2026-11-20/validation.json`.
The new protected images are `software/link.btl` and `tvx/link.btl`, with
matching `direct.btl` images. UART timings use instruction counts and include
supervisor save/restore/retry work, excluding UART output and C verification.

Reproduce with TCC, NETLOAD, T4_BIN and TVX_TRAP_OUT set, then run
`TVX_PROTECTED=1 TVX_TRAP_BACKEND=software bash transputer/tests/fpu-super/run_tvx_traps.sh`
and repeat with `TVX_TRAP_BACKEND=tvx` and a separate output directory.
Leaving TVX_PROTECTED unset retains the existing L-process trap suite;
its 23 cases also pass again through both boot formats.

## Linux TVX SIGFPE retry — 20 November 2026

Linux commit: `3d6e776a8f96`.

Linux now delivers precise T9000 FP error traps as SIGFPE, with the matching
FPE_FLTINV/DIV/OVF/UND/RES code and the faulting PDS.Eptr in both si_addr and
the saved instruction pointer. Aggregate FPError maps to FPE_FLTINV. Other
trap types keep their existing handling. Signal handlers start with FP trap
enables masked; sigreturn restores the saved enables unless the handler
edits its ucontext. FP and block-move register images retain the existing ABI.

The baseline kernel terminated both protected workers with cause 4 at their
first enabled FP trap. The fixed kernel completes **144 retries per backend**
(288 total) from overlapping software/native TVX pthreads. Nine cases cover
packed add/multiply and dot, NaN payloads, aggregate/invalid, overflow,
underflow and inexact. Tests verify exact results, pre-existing sticky flags,
no premature flags, si_code/si_addr, protected STATUS and successful retry
after clearing enables. Handlers do FP work and change flags; both sigreturns
must preserve the interrupted operation. A start barrier and per-round yields
keep the workers overlapping. The final run delivered 10 timer signals to the
software worker and nine to native TVX, with zero errors.

The rebuilt kernel and initramfs passed one **32 MiB T4 -s9 boot** with libc
stress, epoll/POSIX timers, CPU-bound and nested two-thread FP/MOVE2D timer
handlers, two-context CoreMark (both CRCs valid), and the desktop. CoreMark
reported 62.539337 iterations/second; this is a validation result, not a speedup
claim. Files opened/closed and restored pixels matched outside the clock;
framebuffer captures were visually checked. This is one emulated CPU with two
protected pthreads, not an SMP throughput test. TVX/timers overlap; CoreMark
and desktop follow in the same boot.

Mode matters: the older desktop runner used `-s806`. Its Linux smoke results
do not establish T9000 FP trap behavior. This checkpoint explicitly uses `-s9`.
T4 and TinyCC required no implementation changes; the fix is in Linux's
protected trap/signal path. Software traps still occur at the internal scalar
raise instruction, so retry is the common contract, not instruction skipping.

Build the guest with `transputer/tests/build_protected_tvx.sh`, setting TCC,
ELF2FLT, UCLIBC, LIBTCC1, PTHREAD_TEST_DIR and OUT. PTHREAD_TEST_DIR supplies
the tested startup/syscall objects, libc stress object, static_pthread main
built with TP_TEST_EVENT_TIME and TP_TEST_DESKTOP_INTEGRATION, and libpthread.
The builder compiles the same operation wrapper with -mt9000 and -mt9000tvx,
links the current software TVX runtime, and emits OUT/init and OUT/probe.elf.
Install that init in a rootfs containing two-context CoreMark and fbwm, then
rebuild Linux. TP_TEST_TVX enables this test in protected_event_time.c.

Artifacts and reproducible build/boot scripts:
`logs/compiler-coordination/tvx-linux-2026-11-20/validation.json`.
The boot image is `candidate/vmlinux.btl`; UART and framebuffer captures are
under `candidate/play`. Kernel checkpatch reports zero errors/warnings. The
build retains three existing modpost section-mismatch warnings from baseline.
The kernel contribution and tests were developed with Codex assistance.
