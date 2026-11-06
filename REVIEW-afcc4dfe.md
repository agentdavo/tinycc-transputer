# Review of commit afcc4dfe ("transputer")

## Checkout audit (2026-11-05)

The current branch contains the earlier work in squash `63256c91`. The
November 2 commit IDs below describe its development history. The finding
numbers and line numbers in the original review refer to the older source.

| Findings | Status in this checkout | Evidence |
|---|---|---|
| #1 | Fixed in the squash | The target header lives in `include/transputer/limits.h`. The Windows header is separately tracked. |
| #2–9 | Fixed in the squash | Relaxation remaps local label symbols. Integer-result builtins and UMULL protect their result slots. The computed-goto, live-return, conversion, pointer-difference and narrow-argument UART regressions passed. |
| #10–11 | Fixed for the reported cases | The peephole limits decoding to function ranges and skips relocated instructions. The linker skips non-allocated debug sections. A fresh `-g` link succeeded. |
| #12 | Common storage fixed. Undefined-symbol policy remains a choice. | A `-g -fcommon` link placed a tentative definition in BSS. The strict linker suite rejects missing strong call targets with `T4_STRICT_UNRESOLVED_CALLS=1`. The default still warns. |
| #13–14 | Fixed for instruction addends and the reported opcode and sizing cases. | `80ab498f` carries instruction addends in a nine-byte ELF REL slot. Tests check positive and negative addends, branch targets and partial links, and execute the result on T4. `rev`, `opr N`, `lddevid`, `start`, and `j 17` have regression coverage. |
| #15–18 | Fixed in the squash | VLAs use an arena. Invalid jump patches report internal errors. The static call-frequency table is gone. Value-returning early exits use their own epilogue. The VLA and timeslice regressions passed. |
| #19 | Fixed for the reported cases | The NaN comparison, signed-zero and conversion UART tests passed with `-mt800` on T4's T800 model (`-s8`). |
| #20 | Fixed in `88d57e3d` | The new test starts and joins a child, checks signed word counts, and checks the void types of the timer and ALT waits. |
| #21 | T414 lowering present | The backend uses software floating point and accounts for missing integer instructions. Soft-float, unsigned-comparison, process, ALT, timed-transfer and VLA tests passed with `-mt414` on `-s414`. |
| #22 | Fixed in `6a8008d9` | Indexed and whole-archive links reported the exact member name for both padded and full-width fields. A native x86-64 binary linked and ran through both archive-loading paths. |
| #23 | Timed transfers and a stopping trampoline were present. Workspace alignment fixed in `ce1da4af`. | `test_xlink` exercises timed transfers on soft channels. `test_conc_alt` starts processes at all four byte alignments. |
| #24 | Fixed in the squash | The scheduler describes XWORD as binary. All 14 checks in `test_xword` printed a pass on the T9000 model. |

MOVE2D coverage now checks rectangular copies with unequal byte strides,
both conditional byte modes, zero width and zero rows, repeated setup, and
side-effecting arguments on T425, T800 and T9000 emulators. A separate target
test checks that all four builtins diagnose use on T414. The instruction
pair requires fresh setup for each transfer and must not be separated by a
deschedule. Source and destination overlap is outside the instruction's
defined behavior.

**Other findings and remaining work**

- **Packed call arguments.** Packed word lvalues can require byte-wise
  reconstruction that uses more than one evaluation-stack position.
  `tp_push_sv_is_pure` now excludes them from direct argument staging and
  inline `memcpy`. The regression covers local and volatile global fields,
  packed pointers, direct and indirect calls, and stacked arguments on T414,
  T425, T800 and T9000. A code-generation check confirms aligned `memcpy`
  still uses MOVE and the packed-length case calls `memcpy`.
- **Linker safety.** `310efe5b` validates boot load slots before writing and
  propagates malformed optional-slot errors. Ordinary programs no longer
  get the missing-kernel-symbol or absent-BSS-pair warnings. The final
  linker now applies symbolic `.byte` and `.short` relocations and rejects
  unsupported relocation types, malformed slots, and overflow. Absolute
  narrow fields accept signed or unsigned results; PC-relative fields use
  signed results. Their implicit REL addends must fit the signed field width.
  Instruction addends use a new relocation type, so objects using them need
  the updated linker. The linker conservatively preserves the layout of
  sections referenced through these addends. Label boundaries clear the
  backend's cached register and peephole history.
- **PC-relative data words.** The assembler and linker handle `.long target - .`
  and `.long target - anchor` using a distinct 32-bit PC-relative relocation.
  The relocation regression checks separate objects, both input orders, and a
  partial link. This also preserves the affected code sections during layout.
- **§2–§4, implementation and maintenance.** The experimental inliner,
  compaction pass and call-frequency table are gone. Builtin dispatch uses
  descriptors. Environment-controlled code generation, shared-file tracing,
  symbol-repair paths, unconditional `.tccsym` output, standalone assembler
  code, and kernel boot policy in the linker remain. The review's line-count
  estimates describe the original commit. `T4_RELOC_SYMINFO=1` now enables
  tracing for all symbols, while another value remains a substring filter.
- **§5, generated code.** Same-section call relaxation, argument staging,
  byte-load mask removal and capped inline memset are implemented. Debug
  and switch-table functions still bypass compiler relaxation, dense switch
  tables are opt-in. The 64-bit division runtime uses 32-bit division for
  32-bit dividends and for larger dividends with divisors up to 65535. Larger
  power-of-two divisors use shifts. Other 32-bit divisors use 32
  iterations after dividing the high word. Other divisors wider than 32 bits
  also use 32 restoring iterations because the high quotient word is zero.
  `test_u64_div_runtime` exercises
  these paths on the T414, T800 and T9000 emulator models, including signed
  limits, negative divisors, zero divisors and divisors with the high bit set.
  `tests/bench/u64_div.sh` measures 100 calls with T4's estimated T800-cycle
  counter. Against the runtime before `c6df9457`, the 32-bit-dividend phase
  fell from 2,943,422 to 63,522 cycles, the divisor-65535 phase from 3,249,922
  to 98,622, and the divisor-0xffffffff phase from 3,083,122 to 1,729,922.
  The wide-divisor fallback increased from 3,044,422 to 3,056,422 cycles.
  These totals include the loop and call overhead and are emulator estimates.
  Adding the power-of-two path reduced the `0x80000000` phase from 1,768,122
  to 168,822 estimated cycles per 100 calls. The `0xffffffff` phase increased
  from 1,729,922 to 1,732,422 cycles and the wide-divisor phase increased
  from 3,056,422 to 3,057,422 cycles.
  The subsequent 64-bit power-of-two path reduced the `0x100000000` phase
  from 3,057,422 to 63,222 estimated cycles per 100 calls. The `0xffffffff`
  phase increased from 1,732,422 to 1,733,222 cycles.
  Shortening the remaining loop reduced the non-power-of-two `0x100000003`
  phase from 3,059,022 to 1,661,022 estimated cycles per 100 calls.
  Replacing the loop's struct-by-value helper calls with direct word operations
  reduced that phase to 562,022 cycles. The `0xffffffff` phase fell from
  1,680,222 to 571,922 cycles. The regression includes borrow and high-bit
  boundaries. `test_u64_div_host.py` checks 100,000 deterministic signed and
  unsigned quotient/remainder pairs against host integer arithmetic.
  Simplifying two-word negation removed an extra struct-by-value helper call.
  Estimated cycles per 100 signed divisions fell from 163,522 to 124,322
  for `-65535`, and from 633,122 to 593,922 for `0x100000003`.
  Directly initializing the division wrappers' word unions avoided additional
  struct-return calls. The corresponding signed phases fell to 110,322 and
  580,222 cycles, and the 32-bit-dividend unsigned phase fell from 60,422 to
  47,022 cycles per 100 calls.
  The shift helpers likewise initialize their word unions directly. Their new
  regression checks zero, 1, 31, 32, 33, 63 and overshift counts on T414, T800
  and T9000. The benchmark's 100 variable logical-right shifts fell from
  24,883 to 17,605 estimated T800 cycles. The logical shift helpers now lower
  to native LSHL/LSHR, reducing that phase to 11,765 cycles. Arithmetic right
  shift still uses word operations. `test_u64_shift_codegen.sh` checks the
  native instructions and absence of recursive helper calls for each part.
  `test_u64_shift_host.py` also
  checks 5,006 values against host arithmetic for every shift count from 0
  through 65.
- **§6, compiler speed.** The compiler limits the relaxer's relocation
  scans to the current function. The linker indexes symbol names once before
  relocation, preserving the repair pass's symbol ordering and handling of
  duplicate local names. Inline-section placement now scans deferred bodies
  only when reachable. The per-call scan of the inline-function list remains.

Verification covered focused compiler, linker and UART regressions. The
kernel boot, full test corpus, physical hardware, and the historical size
and timing measurements were outside this run.

## Historical fix log (2026-11-02)

> **Fix status (2026-11-02).** These are fixed in commits on top of the squashed "transputer" commit:
> - #1 limits.h (folded into bf3c2923)
> - #4 and #5 conversions, with #7 `long double` constants (09b2f8c1)
> - #6 PDIV (deb9b424)
> - #3 builtin `REG_IRET`, plus the `UMULL`/`IRE2` guard from #9 (2af8d144)
> - #2 computed goto after relaxation (871031b0)
> - #8 narrow push sizes (cdbc07bb)
> - #11 `-g` link crash and the `-fcommon` half of #12 (2124b601)
> - #13 and #14 assembler `rev`, `opr N`, sizing loop, `lddevid`/`start` (bc77c982)
>
> Each fix comes with a `transputer/tests/test_*.c` that fails without it.
>
> Second batch:
> - #10 linker peephole (33670ced)
> - #17 call-frequency use-after-free (5007ffd2)
> - hot-path `getenv` (c6040a59)
> - disabled `T4_COMPACT` pass and its O(n²) sort (8acaeda7)
> - dead experimental inliner (6f9b60f7)
> - #16 silent jump/patch failures, now internal errors (08f86635)
> - #19 signed zero and T800 NaN comparisons (a77dcaa5, c734d5c9)
> - #24 `xword` scheduler model (fd9428ef)
> - quadratic relaxer relocation scans (2cbef013)
>
> Third batch, code size:
> - masks after `lb` (0ee5d46d)
> - redundant round-to-nearest selects (74835e6c)
> - `x != K` inversion (6ff4a2ae)
> - inline `memset` cap (c1bc618a)
>
> Corpus effect: 281492 → 280058 bytes at `-mt9000` and 325012 → 318665 at `-mt800`.
>
> Fourth batch: call argument staging with `stl`. Spills (b7cbb929), then stacked words, after the scheduler learned that a call reads them (16e93255). Corpus: 280058 → 274167 at `-mt9000` and 318665 → 312790 at `-mt800`. `ldlp s; stnl 0` 4144 → 66.
>
> Fifth batch: call slots. Calls to an earlier function in the same section get a narrow slot at compile time, with ICF told not to fold their callees (f44e4d4b; corpus 274167 → 268554 at `-mt9000`, 312790 → 307199 at `-mt800`). The linker then shrinks the remaining same-section call slots after ICF, in the functions the compiler lists in `.note.tprelax` (bb8c2d95). linux-7 vmlinux: 5714528 → 5680652 → 5511560 bytes (−3.6%), boots to the desktop. The 8-byte `ldc sym` slots stay: about 8 KB of kernel for a lot of risk.
>

The commit has 29 files and +21,908/−5 lines. Every line was read: I read the shared TinyCC files myself, and five parallel reviewers took the backend, linker, assembler, tools and runtime, about 3,500 lines each.

Each finding carries a marker:
- **[tested]**: reproduced with the prebuilt `transputer-tcc` in the tree. That binary was built 2026-11-01. This squashed commit was made 2026-11-02, so the binary is about a day older, but each code path was re-checked at HEAD.
- **[read]**: confirmed by reading the code at HEAD.
- **[suspected]**: plausible, not confirmed.

Line numbers refer to HEAD (afcc4dfe).

---

## 1. Correctness bugs, ranked

| # | Where | Bug | Status |
|---|---|---|---|
| 1 | `include/limits.h` + `win32/build-tcc.bat:152`, `Makefile:396-397` | The new file is installed for every target. The Windows build copies `include/*.h` over `win32/include`, which replaces mingw's `limits.h` with one whose non-transputer path is `#include_next <limits.h>`. Nothing follows it on the PE search path, so `#include <limits.h>` breaks. Running `build-tcc.bat -clean` (line 58) also deletes the tracked `win32/include/limits.h`. This breaks the rule that other targets must stay byte-identical to upstream. | read |
| 2 | `transputer-gen.c:7398-7407` | `tp_relax_function` slides only relocation offsets. It never moves the label symbols defined inside the function, so after relaxation `&&label` / computed `goto` jumps into the middle of a block. With `-g` relaxation bails out and the bug disappears. The Linux BPF interpreter uses this pattern. | tested |
| 3 | `transputer-gen.c` ~27 builtins (4105-4112, 4134, 4153, 4202 … 5340; f16/bf16 conversions 4672, 4692) | Integer-result builtins write `REG_IRET` without `save_reg(REG_IRET)`. Example: `icall() + __ldtimer()` gives `2*ldtimer`. `gen_cvt_ftoi` (3797) already documents and fixes this exact hazard. | tested |
| 4 | `transputer-gen.c:3840-3846` | float → `long long` builds the high word with a logical `ldc 31; shr`, giving 0 or 1. `(long long)-5.0` becomes 0x1_FFFFFFFB. Use `xdble`. Values outside int32 range are also wrong. | tested + read |
| 5 | `transputer-gen.c:3771-3782` | `gen_cvt_itof` always uses `fpi32tor*`. So `(double)0x80000000u` is negative, and `(double)(long long)x` uses only the low word. float → `unsigned` overflows at 2^31 or above. | tested |
| 6 | `transputer-gen.c:9964-9970` | `TOK_PDIV` shares unsigned `ldiv` with `TOK_UDIV`. A negative pointer difference with a non-power-of-two element size is wrong: `&v[0]-&v[2]` with a 12-byte element gives 357913939. tccgen folds PDIV by a power of two to `SAR`, which confirms PDIV is signed. | read |
| 7 | `transputer-gen.c:8117-8120`, `2287-2293` | `VT_LDOUBLE` constants are read via `c.d`. This target does not define `TCC_USING_DOUBLE_FOR_LDOUBLE`, so the value is in `c.ld` (80-bit on an x86 host). `f(1.5L)` passes garbage. | read |
| 8 | `transputer-gen.c:8813`, `8205` | `tp_push_sv` sizes lvalues as `BYTE?1:SHORT?2:4`. `_Bool` and the f16/bf16 types are therefore loaded as whole words, and are also treated as "pure", so a byte-offset `_Bool` argument reads its neighbours. | read |
| 9 | `transputer-gen.c:10194` | `TOK_UMULL` stores to `REG_IRE2` without saving it. If the high word of a live call result is still in IRE2 (for example `get_u64() + x*y`), it is lost. | read; trigger suspected |
| 10 | `transputer-link.c:72-189` | The linker peephole runs before relocation and rewrites `ldc <placeholder>; op` and `ldnlp <placeholder>` to `pfix 0` runs. The relocation prefix-scan then walks into the next instruction: `ldl 1; ldc sym; sum; stl 2` linked to a wild `stl 0x80000090`. It also treats `.long` data in `.text` as code: `.long 0` became `0x20202020`. | tested (asm input) |
| 11 | `transputer-link.c:2938-2945, 3009` | Relocations are applied to non-ALLOC sections such as `.stab`, whose `sh_addr` is 0. `transputer-tcc -g` link → SIGSEGV. | tested |
| 12 | `transputer-link.c:2976-2999, 2954` | Undefined symbols are only a warning and are patched to address 0, so the link "succeeds". `-fcommon` tentatives get address = alignment, so a store goes to absolute address 4. Both happen because the hand-off happens before `resolve_common_syms`/`tcc_add_runtime`. | tested |
| 13 | `transputer-asm.c:119, 1012` | `{"rev", -0x00}`: −0 == 0, so `rev` assembles as `j` (function 0). Also, `opr N` ignores N (`opr 5` → `rev`), `sym+off` operands lose the addend (1040-1047), and `lddevid`/`start` encode as 0x7C/0xFF where they should be 0x17C/0x1FF (328, 830). | read |
| 14 | `transputer-asm.c:1051-1056` | The inline-asm jump sizing loop can oscillate forever. `__asm__("j 17")` at offset 0 hangs tcc. | read |
| 15 | `transputer-gen.c:3675-3719` | VLAs move Wptr with `gajw`, which re-bases every local and pseudo-register. The code is known broken (c-testsuite 00207 hangs) but compiles without a diagnostic. It should be `tcc_error` until fixed. | read |
| 16 | `transputer-gen.c:3493-3495, 3437, 3597-3619` | Jump patching fails silently: an out-of-range chain does `break`; `gjmp_append`'s `n1 < 0` test is on a `uint32_t`; and a re-encoded J/CJ is never checked against the placeholder size. Also `Tp_OPR` (1553-1563) prints "ERROR" and emits nothing, with exit status 0. | read |
| 17 | `transputer-gen.c:952-973` | `track_call_frequency` keeps `funcname`/`get_tok_str` pointers in a static table that is never reset. Identifiers are freed per file, so `tcc -c a.c b.c` calls `strcmp` on freed memory. It runs on every call (8646) and is debug-only. | read |
| 18 | `transputer-gen.c:8785-8796` | The return value is loaded into A/B and then `gjmp(rsym)` runs for every non-final `return`. `j` is a timeslice point, and A/B/C are not preserved across a deschedule. | suspected |
| 19 | `transputer-gen.c:10605-10618` | On T800, float `<=`/`>=` are lowered as `!(a>b)`, so they are true for NaN. `-x` is `0.0 - x` (10439), so `-(+0.0)` = +0.0. `fpldzerodb` is used for −0.0 constants (2287). | read |
| 20 | `transputer-gen.c:4250, 4253, 4444, 4105, 4131` | `__startp` has its operands swapped. `__endp`/`__wcnt` take no argument but the instructions consume A. `__tin`/`__altwt` "return" a value the instructions never define. | read vs ISM + emulator |
| 21 | `-mt414` (libtcc.c:2086) | Accepted, but GTU (10261…) and every FPU op are emitted unconditionally. The project's own emulator flags `gtu` as bad on T414. | read |
| 22 | `tccelf.c:3967, 4022` | `memcpy(archive_member_name, hdr.ar_name, 31)` copies from a 16-byte field. It reads `ar_date`/`ar_uid`, so names are wrong. The block is duplicated. | read |
| 23 | `lib/transputer-conc.c` vs `include/conc.h:145-164` | `ChanInTimeFail`/`ChanOutTimeFail` are declared and documented but never defined, so any use fails to link. `PSetup` leaves `wptr[0]` (the return address) uninitialised and does not align `ws`. | read |
| 24 | `transputer-gen.c:6010` (scheduler) | `tps_effect` models `xword` as unary although it is binary, so S4/S5 can move a value across the xword that consumes it. | suspected |

Further risks noted by the reviewers:
- The 16-bit read-modify-write store is not atomic with respect to other processes or MMIO (`transputer-gen.c:3168-3266`).
- The eval-stack depth model drifts: `load()` does LDL without `abcreg_push`, and `abcreg_pop` clamps at 0 (1485).
- The DUP-tag and rollback-peephole state is not cleared at `gind()` labels.
- Linker addends are ignored for IPREL32/LDC_ABS (REL target).
- `patch_ldc_absolute` has no bounds check (`transputer-link.c:417-429`).
- Unknown relocation types are dropped silently (`transputer-link.c:1724`).
- `elf2flt` guesses relocations from byte patterns, including in rodata.

---

## 2. Code quality

### 2.1 Debug scaffolding in production paths
This is the dominant quality issue.
- The commit reads **68 distinct environment variables** (`T4_*`, `TP_*`, `TCC_*`) from 78 `getenv` sites, and has **294 `fprintf(stderr)` calls**.
- Some of these variables change the generated code: `TP_NO_RELAX`, `TP_NO_SCHED`, `TP_NO_SLOTMAP`, `TP_NO_LDNLP`, `TP_NO_CMPFLIP`, `TP_NO_EQC0`, `TP_POOL`, `T4_SWITCH_JT`, `T4_ICF`, `T4_COMPACT`, `TCC_FRAME_POLICY`, `TCC_TRANSPUTER_WSUB_OPT`/`BSUB_OPT`. The output therefore depends on the build environment and is not visible on the command line. Turn these into `-f`/`-m` options or delete them.
- The shared files carry target debugging that belongs in the backend or nowhere:
  - `tccelf.c:3363-3880`: about 150 lines of reloc and symbol-merge logging, including a transputer instruction decoder written twice. One bug: `T4_RELOC_SYMINFO` is used both as the on-switch and as the filter, so `T4_RELOC_SYMINFO=1` shows only symbols containing "1".
  - `tccgen.c:744-860`: `greloca` logging (`T4_RELOC_*`, `T4_SYM_TRACE`, `T4_IPREL_*`).
  - `tccgen.c:95-133`: `TCC_TRACE_FUNC` and the inliner knobs.
  - `tccgen.c:1399-1426`: per-`gvtst` trace prints.
- The linker writes an unrequested `<output>.tccsym` file on every link and prints kernel-specific warnings ("start_kernel symbol not found", "Could not patch BSS load points") when linking ordinary programs (`transputer-link.c:3375, 3149, 762`).

### 2.2 Guards that hide corruption instead of fixing it
Four layers of "invalid symbol index" handling warn and carry on rather than stopping:
- `tccgen.c:577-583`: `elfsym`
- `tccgen.c:701`: `put_extern_sym2` re-creates the symbol
- `tccgen.c:842-873`: `greloca`, with magic limits `sym->c > 1000000` and `sym->v < 100000`; a negative `sym->c` silently drops the relocation
- `tccelf.c:883-889`: `update_relocs`

Each lets a corrupt relocation through and produces wrong code with at most a warning. Related workarounds:
- `gv()` silently forces `VT_SYM` onto a sym constant that lacks it (`tccgen.c:2402-2418`).
- The linker's symbol "repair" re-binds every R32/IPREL32 relocation by name (`transputer-link.c:1444-1541`). Its own comment records two miscompiles it caused.

Once the root causes are found, these should become `tcc_error`, or `assert` in debug builds.

### 2.3 Dead or unreachable code
- **Experimental inliner** (`tccgen.c:7838-7940`), gated by `T4_INLINE_EXPERIMENTAL`. It compares `inline_sym->v == s->v`, but `s` is the function *type* ref (`s = vtop->type.ref`, 7746), whose `v` is the anonymous `SYM_FIELD`, so it can never match. If it did match, `s->c = 1` would clobber the type ref, and `tccpp_putfile` (7876) is never undone. It only inlines `return <literal>;` anyway. Delete it.
- **`gvtst`** (`tccgen.c:1399`): the `vtop->r != VT_CMP` branch is unreachable, because `gvtst_set` always leaves `VT_CMP`. The constant fast path in `gvtst_set` (1339) duplicates what `gen_op(TOK_NE)` already folds.
- **`tcc_write_elf_file`** (`tccelf.c:2866`): its transputer intercept is unreachable, because `elf_output_file` (3067) returns first. The usual hook is `tcc_output_file()`, as PE and Mach-O do.
- **`transputer-gen.c`**:
  - The compaction subsystem (7691-7966, about 275 lines) is disabled, known to be buggy, and superseded by relax. Its `padding_log` is stale after relax, so `T4_COMPACT=1` would corrupt code.
  - About 295 lines of `gen_opf` (10630-10909) are unreachable, because every float op returns earlier.
  - About 110 lines of gv2-path constant detection in `gen_opi` (9744-9843) can never fire after `gv2()`.
  - The commented-out `gen_builtin_memset` (5523-5622).
  - Five `__attribute__((unused))` functions (1674-1705), and 10 `if (0) fprintf` sites.
  - The "recursion detector" (524, 5641) and the "Wptr corruption check" comment with no check behind it (7641).
- **`transputer-link.c`**: PLT/GOT code copied from AArch64 (1979-2042) that writes past a 16-byte entry; `relocate_init` (3388) with no caller.
- **`transputer-asm.c`**: the standalone "tasm" (53-946, about 670 lines) has no caller and is broken in about eight ways. The `TP_FPU_RUNTIME` host-libm block (1202-1238) is never enabled.
- **`transputer-netload.c`**: the multi-node deploy mode (281-549) does not work, and there is no networking.
- **Host tools**: none of `elf2flt`, `mksd` and `netload` has a Makefile rule.
- **`Makefile:271-273`**: the `transputer-*.o : DEFINES += -DTCC_TARGET_TRANSPUTER` rules do nothing. Those files are `#include`d, never compiled on their own, and `config.h` already defines the target.
- **`.gitignore`**: the final line `transputer/` makes about 25 earlier `transputer/**` lines redundant. Broad globals such as `*.bin`, `*.img`, `*.orig` and `*.cmd` affect upstream too.
- **`elf.h`**: `R_TRANSPUTER_GOT*/PLT*/TLS*/SYM_DIFF/ALIGN` are copied from RISC-V and never applied. `R_TRANSPUTER_NUM` (35) sits below three later relocations (36-39). `EM_TRANSPUTER 805` sits above upstream's "pick large random numbers" note.

### 2.4 Layering over upstream mob
The fencing discipline is good overall: tccgen.c alone has 94 `#ifdef TCC_TARGET_TRANSPUTER` blocks, with upstream kept in `#else`. Exceptions and rough edges:
- `include/limits.h` (bug #1 above). Ship it in a transputer-only sysinclude directory instead.
- **No-op fences.** Four empty `#ifdef/#endif` or `#ifndef/#endif` pairs (tccgen.c:4177, 7330, 9812, 9976). Several fences change only a comment (`save_reg_upstack`, the `gen_opl` "avoid differnt registers" comment, the `TCC_TARGET_TRANSPUTER` line in tcc.h:161-163). They add diff without effect.
- **Fences that interleave braces.** The pointer-add fast path (`tccgen.c:3887-3990`) opens `if (!tp_ptradd_fast) {` in one fenced block and closes it in another, and the BCHECK branch is mis-indented. `gvtst_set` splits one `else` across two adjacent `#ifdef` blocks.
- **Token list.** The 160-line intrinsic token list in `tcctok.h` would sit better in a `transputer-tok.h` included the way `i386-tok.h`/`arm-tok.h` are.
- **Generic features fenced to transputer only.** About half of the tccgen.c diff adds target-independent features: `__auto_type`/C23 `auto`, `transparent_union`, `noinline`, `__builtin_object_size`/`clz`/`strlen`/`strcmp`/overflow, packed enums, forward aliases, `.incbin`, `.byte sym`. These are reasonable upstream candidates in their own right. Kept in this commit they hide inside a 22k-line squash.
- **`tccgen.c:10662`, `10925`**: functions placed in any section numbered above `bss` get text flags. This replaces upstream's `ad.new_section` test with a section-number heuristic.

### 2.5 Style against CodingStyle
- **Tabs**: transputer-gen.c (17 lines, 1676-1704), transputer-link.c (76 lines of XIP code), transputer-elf2flt.c (279 lines).
- **C99 features not on the allowed list**: `for (int i …)` in transputer-gen.c:954 and in six places in link.c; `%zu`; `<stdbool.h>` in transputer-asm.c:34.
- **asm.c housekeeping**: transputer-asm.c re-`#define`s `malloc`/`free` (41-50) and includes `tcc.h` twice.
- **Whitespace and characters**: 208 added lines have trailing whitespace. There are many non-ASCII `→`/`—`, including inside runtime format strings.
- **Linkage**: about 120 `gen_builtin_*` functions plus `tp_emit_1op/2op/3op` have external linkage rather than `static`/`ST_FUNC`, with prototypes repeated in tcc.h and in local forward declarations.
- **Commit message**: the whole 22k-line commit message is one word ("transputer").

---

## 3. Comments

**Strengths.** The best comments explain *why* precisely:
- transputer-gen.c: the ADC-trap rationale (241-258), the halfword-alignment proof (1707-1735), the literal-pool design (1825-1855), the memcpy purity/aliasing note (5390-5406), the scheduler header (5893-5953), and call staging (8187-8274).
- tccgen.c: the 64-bit compare rewrite (2900-2950), return-with-cleanups (9011-9040), loop rotation (8886, 9085), `VT_UNALIGNED` (7667-7700).
- `include/conc.h` and the `transputer-tvx.h` format notes.

**Stale or wrong** (all read):
- `tcc.h:810-813` says `tp_t9000` "Default 0 == plain T805 codegen", but `libtcc.c:900` sets it to 1. transputer-gen.c's header (1-6) still says "T414/T800".
- `tcctok.h:447`: "Deliberately NOT spelled _Float16 … VT_BTYPE has no room". The next DEF adds `_Float16`, and tcc.h widened `VT_BTYPE` for it.
- **Placeholder sizes** have changed several times and the comments lag behind:
  - "5 bytes / ~1M words" vs code 3 bytes / 4095 (gen 1757, 5669).
  - "±65K" for jumps (3320).
  - ICF claims every placeholder is 8 bytes; calls are 6 (link 2071).
  - elf2flt says "9-byte placeholder"; gen uses 5.
- "MINT is a single-byte opcode … saves 7 bytes": MINT is `opr 0x42`, 2 bytes (gen 1507).
- **Rounding-mode resets** ("Restore round-to-nearest", gen 3774, 3830, 3879): the mode resets after every FP instruction on both chips, so these instructions and comments are dead.
- "CALL saves A/B/C at [0..2]": it is [1..3] (5681). "RET restores A/B/C" (3975): it does not.
- `gfunc_call`'s header (8555-8571) says stacked args are "not implemented yet" and that the target is in A; both are wrong. It also points to stale tccgen.c line numbers and to a `STL-LDL-PLAN.md` that is not in the repo (9868).
- The `__startp` comment (4246) documents the swapped operands as if they were correct.
- The float → `long long` path is called "conservative" (3841).
- `transputer-enc.h:55-57` contradicts its own header and code. Its "classic transputer encoder" emits NFIX first, where the INMOS form (and `mksd`) emits PFIX…NFIX, so the tree has two different encodings of negatives.
- `transputer-conc.c:183-194, 233-235` disagree with the code that follows them and with gen.c on what lives in Wptr[0].

**Too chatty.** Changelog material belongs in commit messages, not code:
- Dated war stories: "CRITICAL FIX (2026-01-17)", "BUGFIX (2026-01-16)" in `tccgen.c:10533`, "NEW MINIX-STYLE … (2026-01-15)".
- Benchmark numbers that will go stale: "47.5k instructions across the kernel", "17,808 REV;STNL sites", "1,506 cycles out of 7.9M".
- Think-aloud left in: "Wait — actually STL *does* pop AReg… Skip it." (link 98-101), "Actually LDC 1 = 1 byte… NOT a win" (link 166-170).
- The same 64-bit store bug told twice (gen 2983-3003 and 3280-3292).
- An arXiv essay in `transputer-tvx.h:350-378`.

**Too sparse.** The comments are thin exactly where the latent bugs are:
- the linker peephole's interaction with the relocation prefix-scan;
- which peepholes may roll back across a label;
- the implicit Wptr[0] writes by `outbyte`/ALT that copy-propagation must respect;
- a warning that a call between `__alt()` and `__altwt()` clobbers the ALT state.

---

## 4. Source density

| Area | Lines | Removable without behaviour change | Main items |
|---|---|---|---|
| transputer-gen.c | 10,912 | ~2,100 (≈20%) | compaction (275), dead `gen_opf` (295), dead gv2 const paths (110), builtin wrappers → table-driven (≈700: 119 wrappers, 80 prototypes, ~120 `case` labels in tccgen.c could all key off one descriptor table with an `opcode`+`kind`; the dot/gemv family is 4 identical bodies; the f16/bf16 converters are pairwise identical), LDC_ABS placeholder emission pasted 8× (70), 4× `gen_ptradd_*` → 1 (100), the 16-bit RMW store's twin halves (40-85), the jump-length iteration duplicated in `gjmp_addr`/`gsym_addr` (30), debug scaffolding (≈250) |
| transputer-link.c | 3,407 | ~1,100; down to 700-900 total if Linux boot policy moves out | 4 prefix decoders + 2 fixed-width LDC writers, ~10 copies of "symbol address = sh_addr + st_value", a `find_sym_by_name` that re-implements `find_elf_sym`, 5 near-identical layout walks, dead PLT/GOT (170), tracing (≈400). About 1,100 lines are kernel policy (`start_kernel`, initcall ordinals, `sched_class` order, `__fp_init_bootmem_node`, a debug window at `base+0x1dc1d0`) that belongs in a linker script or kernel tool, not the compiler. |
| transputer-asm.c + headers | 2,331 | ~1,000 | dead tasm (670), host-libm block (55), `g()` tracing (65), one X-macro opcode table replacing `instructions[]` plus the duplicate `TP_FPU_*`/`TP_FPENTRY_*`/`TP_ENC_*` defines (≈280, which also fixes the `rev`/`lddevid` drift); `transputer-tvx.h` is about 80% emulator-only code (the compiler uses 4 functions) |
| tools + runtime | 2,005 | ~500 | netload's non-working deploy mode (270); prefix encoders re-implemented in mksd/elf2flt (one `tp_encode_fixed()` in enc.h would serve all); duplicate typedefs/comments in conc.c |
| shared files | ~3,500 | ~450 | tccelf.c logging (≈150), greloca/gv/gvtst tracing (≈120), dead inliner (≈100), the `tccdefs.h` va_arg block identical to the i386 fallback it precedes (12), no-op fences |
| **Total** | **21,908** | **≈5,000 (about a quarter)** | |

`tp_sched_function` is one 760-line function (6101-6860), and "next non-padding instruction" is hand-coded 7 times even though `tps_next` exists. Splitting it into its annotate, liveness, copy-prop and peephole passes would make it reviewable.

---

## 5. Generated code: density and speed

Ranked by likely payoff.

1. **Relaxation coverage.** Functions with a switch jump table, `-g`, more than 64 KB of code, mid-function AJW (computed goto, indirect calls) or an LDLP escape to an FPU store are never relaxed or scheduled (gen 7078-7085, 6242). They keep:
   - 3-byte local accesses
   - 5-byte forward jumps
   - 5-byte AJW
   - a 32-byte loop-pool pad executed as no-ops

   The in-code note estimates relaxation is worth about 32% of `.text`. The relocation fix in bug #2 would also let computed-goto functions relax safely.
2. **Switch jump tables are off by default** (`T4_SWITCH_JT`, tccgen.c:9306), because they were "implicated in" an unresolved maple_tree bug. Dense switches fall back to compare chains. Fixing the root cause gives smaller and faster switches. The table's LDC slot reserves 9 bytes for an operand that is always 2 (gen 870-878).
3. **The linker never shrinks code.** Calls (6 bytes), `ldc sym` (8) and inline-asm symbols (9) are fixed slots padded with *executed* `pfix 0`. The linker peephole and the LDPI rewrite replace instructions with `pfix 0` instead of deleting them: zero size gain and one wasted cycle each. Real relaxation needs codegen to emit relocations or a side table for intra-function branches. Estimated at 300-500 KB of kernel text (suspected).
4. **The `gen_opi` gv2 fallback** (gen 9739) handles `p->f`, `*p`, globals and char/short. It spills both operands, reloads them, and never folds constants into `adc`/`eqc`/`ldnl`. Example: `p->f + 4` takes 8 instructions where `ldnl k; adc 4` does it. Three-operand ops are excluded from the direct path, so a 64-bit add is about 20 instructions where 9 with `lsum` would do. Extend the direct path to any operand that passes `tp_push_sv_is_pure`.
5. **Redundant masks.** `lb` zero-extends, so `ldc #FF; and` after it wastes 4 bytes per unsigned-char load (gen 2399, 2507, 8848, 8920). The byte-pair halfword compose adds a needless `ldc #FFFF; and`, 6 bytes (2427, 2539, 8878, 8958).
6. **Scratch slots 15-19** (16-bit loads, float constants, the RMW store) raise `func_max_slot_used`, so locals start at slot 17 or later and lose 1-byte `ldl`/`stl`. Pseudo-registers sit at 7-14 while 0-6 are often idle. A scratch-free halfword load: `dup; lb; rev; adc 1; lb; ldc 8; shl; or`.
7. **64-bit runtime.** `lib/transputer-runtime.c`'s `u64_udivmod` divides one bit per iteration, with out-of-line struct-by-value calls, at an estimated 6-10k cycles per divide. Add a `divisor.hi == 0` fast path using `ldiv`. `__ashldi3`/`__lshrdi3` should each be one `lshl`/`lshr`. `__ashrdi3` is still a libcall from gen (10333).
8. **Calls.**
   - An indirect call costs 12-18 instructions where about 9 would do.
   - Staging stores use `ldlp s; stnl 0` instead of `stl s`.
   - Void callees still capture the return value with `stl IRET`.
   - Helper calls via `func_old_type` run the 6-instruction capture-all.
   - A struct by value costs about 8 instructions per word (one MOVE per word).
   - The prologue copies stacked arguments that are already in memory.
9. **Return value.** Keep it in slots 7/8 and load it once in the epilogue. This also fixes suspected bug #18 and saves bytes in multi-return functions.
10. **The -mt800 16-bit store** is about 50 instructions / 181 bytes (gen 3168-3266). The two-`sb` form is about 15 bytes but is blocked by a documented frame bug.
11. **Small but common:**
    - `x != K` emits `eqc K; eqc 0` (10128).
    - `ULT`/`UGE` keep a `rev` that a vstack swap removes.
    - FPU results always round-trip through workspace.
    - The rounding-mode instructions are dead, wasting 4 bytes per conversion on T800.
    - Negative displacements are never folded into `ldnl`/`stnl` (gen 458, 2805), which hurts `container_of`.
    - Constant addresses skip the literal pool (2472, 2902), so MMIO polling loops reload an 8-byte `ldc` each pass.
    - Values just above MinInt cost 8 bytes; `mint; adc k` takes 3-4.
    - Inline `memset` has no size cap: `memset(buf,0,256)` becomes 187 bytes.
    - A 4-byte aligned struct copy uses MOVE (6 bytes) where `ldnl/stnl` takes 4.
    - `ggoto` is 10 instructions where `ldl t; gcall` takes 3 bytes.
    - On T800, `x<<2` → `dup;bsub;dup;bsub` and `x*3` are larger than `ldc;shl`/`ldc;prod`. This is fine if T9000 grouping is the target.
12. **Already a good win, keep it:** for/while loop rotation in tccgen.c (8886, 9085) removes the unconditional jump per iteration, the loop literal pool hoists constants, and WSUB/WSUBDB/BSUB/SSUB indexed addressing is used.

---

## 6. Compiler and linker speed

1. **`getenv` in hot paths, uncached:**
   - `transputer-asm.c:1144-1146` calls `getenv("T4_CALL_EMIT_FILTER")` on **every non-prefix byte** the compiler emits whenever the variable is unset, which is the normal case.
   - `tccgen.c:3905, 3929, 3967`: 2-3 `getenv` calls per pointer `+`/`-`.
   - `tccgen.c:2410`: per sym constant in `gv()`.
   - `tccgen.c:781`: per IPREL32 relocation.
   - `transputer-gen.c:8574`: per `gfunc_call`.
   - `transputer-link.c:1418, 1637, 1745`: per relocation.

   Read each once at `transputer_init`, or remove them.
2. **`tp_relax_function` is quadratic in translation-unit size.** It scans the whole section's relocation table three times per function (gen 7143-7171, 7373-7385, 7398-7407). Measured: 3× the input took 9.5× the relax time. Record the relocation index at the prologue and scan from there.
3. **Linker symbol "repair" is O(R×S) and on by default** (link 1469-1515): a full-symtab `strcmp` scan for every R32/IPREL32. That is about 10^10 operations on a kernel link (suspected tens of seconds), for a pass that should be a no-op. About 100 more linear `find_sym_by_name` scans should use the hashed `find_elf_sym`.
4. **Inline-section placement** (tccgen.c:1586-1775):
   - `note_inline_call_section` scans every inline function on every call (O(calls × inlines); kernel headers bring thousands of static inlines).
   - `resolve_inline_sections` re-lexes *every* inline body in the TU, used or not.

   Store an `InlineFunc*` index by token (the `by_token` table already exists), and walk only reachable bodies from a worklist.
5. **Per-function O(n²) structures:**
   - The jump-metadata list is scanned linearly per J/CJ, giving O(J²).
   - One `tcc_malloc` per local access, jump and switch slot.
   - The 4096-entry `padding_log` bubble sort runs every function even with compaction off (gen 7726).
   - `tp_hist_record` shifts 12 entries per instruction.
6. **`track_call_frequency`**: 100 × 2 `strcmp` per call site for a debug message, and also a use-after-free (bug #17).

---

## 7. Suggested order

1. **Cross-target regression:** move `include/limits.h` into a transputer-only sysinclude path (bug #1).
2. **Silent wrong code reachable from plain C:** bugs 2-9, 11, 12, 15 and 16. Turn the warn-and-continue paths (§2.2, jump patching, `Tp_OPR`, linker undefined/overflow) into `tcc_error`.
3. **Assembler table bugs:** fix `rev`, `opr N`, the addends, and the `lddevid`/`start` encodings.
4. **Debug cleanup:**
   - Remove the hot-path `getenv` calls, `track_call_frequency` and the experimental inliner.
   - Move every codegen-affecting environment variable to a command-line flag.
   - Strip the target logging from `tccelf.c`/`tccgen.c`.
5. **Delete dead code:** compaction, dead `gen_opf`/`gen_opi` paths, the standalone tasm, PLT/GOT, and the netload deploy mode (about 2,000 lines).
6. **Make the fixed size win reachable:** let functions with switch tables or `-g` relax, re-enable jump tables, remove the redundant masks after `lb`, keep scratch slots out of 0-15, add the gv2 direct path for pure operands, and add `ldiv`/`lshl`/`lshr` to the 64-bit runtime.
7. **Make it table-driven:** the builtin dispatch, and one opcode X-macro table shared by gen, asm and the headers.
8. **Structural, longer term:** move Linux boot policy out of `transputer-link.c`, use the standard `relocate()` hook instead of a private final linker, and add linker relaxation.
