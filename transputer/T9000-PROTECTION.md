# T9000 Protection — Implemented Paths and Isolation Work

Reference: `t9000ism.txt` ch.9–10, `t9000hrm.txt` §4–5.

Source checked: **6 November 2026**. Kernel hard-P entry and userspace trap
syscalls have separate controls. The default command line in
`linux-7/arch/transputer/kernel/setup.c` contains `t9k_enforce t9k_hard_p`,
but `tests/syscall_stub.c` defaults `TP_USE_T9K_SYSCALL` to **0**. Applications
using that stub therefore use the C-call gate unless explicitly rebuilt with
`-DTP_USE_T9K_SYSCALL=1`. Entering P-mode does not establish complete isolation.

The phase list below records implemented mechanisms and historical test
results, not completion of a secure userspace boundary. The saved
`logs/06-boot.log` records protection selftest PASS and `/init` P-entry;
it is not a fresh acceptance run for the current source.

The [6 November Linux isolation review](LINUX-PROTECTION-REVIEW-2026-11-06.md)
records the original trap-first PID 1 panic. Nested Linux commit `cbf812242ba1`
repairs parent continuation preservation across nested spawn, including maps
and workspace state. Linux `bc2969c2ea63` and root stub `a747d3b2` replace the
shared syscall transport with caller-owned frames and register results,
including the packed mmap helper. The owner reports nested trap-path boots
reaching `DESKTOP_LOOP` and mixed-version compatibility checks. These repairs
do not complete isolation, FP/block-move state preservation or trap policy.

## Model (not a classic page MMU)

| Concept | Role |
|---------|------|
| **L-process** | Trusted; HW-scheduled; optional THDS trap-handler |
| **P-process** | Protected mode under a supervisor L-process |
| **G in port boot comments** | Global-error/null-THDS starting state; ISM §10.1.3 specifies a processor halt on a null trap, not an independently scheduled recovery process |
| **goprot** | Privileged: A=PDS, B=RODS → enter P (OReg **−6**, bytes `60 FA`) |
| **syscall** | Trap to supervisor (OReg **−8**, bytes `60 F8`) |
| **4 regions** | LA bits 31:30 select region; size 2^n (8≤n≤30); aligned physical relocation; translated regions must not overlap |
| **PMI** | Physical bank map (`T4_PMI=1`) |

HRM §5 requires checking all P-process reads, writes and instruction fetches,
including region write/execute permissions and device-only restrictions.
ISM §9.2 calls the instructions forbidden in P-mode **privileged**; the
permitted subset is unprivileged. A trap returns to the supervisor after
`goprot`; software determines the recovery policy.

The local OCR renders the null descriptor as `#8000000`; code uses
`0x80000000`. Verify the printed facsimile before changing the literal.

---

## Phase checklist

| Phase | Status | Deliverables |
|-------|--------|----------------|
| **0 Spec** | Done | This file + ISM/HRM tables |
| **1 T4 L/P** | **Done + guest PASS** | `t4/t9000_protect.c`, mem hooks, `tests/run_hello_p.sh` |
| **2 Soft isolation** | **Done** | `asm/t9000_softprot.h`, `access_ok` → soft regions when ACTIVE |
| **3 Protected ABI** | **Done** | hello_p BTL; asm `goprot`/`syscall`; TCC rebuilt; gen helpers |
| **4 Linux supervisor** | **Done (wired)** | `t9000_prot.c` + `t9000_goprot.S`; exec → soft-mm; run-until-exit loop |
| **5 PMI** | **Done** | `t4/t9000_pmi.c` (`T4_PMI=1`) |
| **6 Soft enforce** | **Done** | Per-task `thread.soft_mm`; quarter-identity maps; cmdline `t9k_enforce`; boot selftest |
| **7 Hard P-process** | **Done** | `t9k_enter_userspace` → `goprot`; trap loop; SPAWN_EXIT leave_p; goprot re-enter leave |
| **8 Process-local maps / compatibility bridge** | Implemented; isolation incomplete | Per-task soft maps, NULL rejection, PMI default ON, trap arguments and soft-mm teardown. C-call compatibility still permits broad kernel/device access under P. |
| **9 Split RX/RW bFLT** | Compatibility only; physical alias violates HRM §5.2.2 | `flat_q3.c` publishes WPtr→Q3 via **full Q2 mirror** (`LA = phys + 0x40000000`, RODS 1 GiB). With the simultaneous Q2 identity descriptor, two logical regions map the same physical GiB: behavior is architecturally undefined. Historical T4 desktop boots do not establish T9000 conformance. Disjoint code/data/workspace mappings remain required. |
| **10 Concurrent P** | Partial: per-context continuation repair and frame ABI implemented | Per-task PDS/RODS and nested-spawn context save/restore replace shared continuation buffers. Syscall arguments are caller-owned and results return in A. Concurrent state, FP/block-move preservation and strict isolation still need review. PMI text-write and foreign-private checks remain opt-in. Trap-first syscall code remains opt-in. |
| **11 Data quarter / trap default** | Partial | Soft RX via `end_code`; stack Q3 mirror. Full data-LA rewrite + trap-only default still experimental. |

---

## Verify

```bash
# Guest protection checks (from the emulator repository)
cd transputer/t4
# Host region/device/mode checks; an output path outside the source tree:
cc -I. -O2 -o /tmp/test-t9000-protect tests/test_t9000_protect.c \
  t9000_protect.c t9000_pmi.c
/tmp/test-t9000-protect
./tests/run_hello_p.sh
# On-target: stack extension, device-only regions, fetch/execute
# permission, move/devmove/move2dall, ps.Eptr (test_t9000_pstack/pmode)
./tests/run_t9000_vcp.sh

# TCC mnemonics present
strings ../../transputer-tcc | grep -xE 'goprot|syscall'

# Kernel objects (after full kernel rebuild)
# arch/transputer/kernel/t9000_prot.o t9000_goprot.o
```

**Expected hello_p:**

```text
[T9K-PROT] goprot ... -> P
[T9K-PROT] P-trap cause=3 ...
[WATCH] 0x80000270: 00000003
[WATCH] 0x80000274: 00000008 00000000
PASS: hello_p goprot→syscall→trap cause 3, RESULT=0x00000003, A=t.Scall B=et.NoError
```

The current host suite `tests/test_t9000_protect.c` uses `cpu_state.h` and a
stub `cpu_t`; the earlier claim that it no longer links is stale. On 6 November,
a direct host build of that test with `t9000_protect.c` and `t9000_pmi.c`
returned **ALL PASS**. This is region/device/range and mode-switch coverage,
not a passing nested Linux P-session or trap-only rootfs test. Guest checks
and the kernel soft-map selftest cover different paths.

---

## Kernel integration (Phase 4)

| API | Role |
|-----|------|
| `t9k_on_exec_finish(regs)` | Fills **per-task** quarter-identity soft-mm; sets `ACTIVE` only if enforce on |
| `t9k_task_soft_mm(task)` | Returns `&task->thread.soft_mm` |
| `t9k_soft_setup_user(...)` | Tight image+stack regions (tracking) |
| `t9k_soft_setup_user_quarter(...)` | Broad quarter-compatible image/stack/graphics maps plus Q0 device access; compatibility, not tight isolation |
| `t9k_soft_fill_rods(mm, rods)` | Build 4 T9000 RODS words for hard `goprot` |
| `t9k_enter_user_p(...)` | One `t9k_asm_goprot` (hard); soft if no trap |
| `t9k_run_user_until_exit(...)` | Loop: goprot → on syscall trap dispatch `__transputer_syscall` → restart |
| `t9k_asm_goprot` / `t9k_asm_syscall` | `t9000_goprot.S` raw encodings (`60 FA` / `60 F8`) |

**Current version-1 syscall ABI (P-process trap):**

The opt-in stub checks the capability tag `0x54504601` at `0x8000006C`.
It passes the syscall number in A and a pointer to six aligned caller-owned
words in B, then executes `syscall`. The supervisor validates the complete
private mapped argument range and copies all six words before dispatch.
It writes the signed result into saved A; the assembly leaf returns it
directly after resumption. Packed mmap uses this transport too. The former
seven-word kernel-BSS staging pointer and global result slot are retired.

| PDS field | Content |
|-----------|---------|
| `ps.sAreg` | Syscall number on entry; signed result on resume |
| `ps.sBreg` | Pointer to six aligned caller-owned argument words |
| caller frame | Six arguments copied before dispatch, after complete-range validation |
| `0x8000006C` | ABI capability tag, not an argument pointer |

**Trap resume (supervisor side).** T4 reports a P-trap two ways, so both
kernel builds work on it:

- **ISM §10.3** (what the SPINE RTL does, read by `CONFIG_TRANSPUTER_SPINE_CHIP`
  kernels via `t9k_map_ism_cause`): the supervisor resumes after `goprot` with
  Areg = trap reason (Table 10.5: `t.Scall` 0x08, `t.Time` 0x06, `t.Break` 0x02,
  `t.Error` 0x00) and Breg = error type (Table 10.10: `et.NoError` 0,
  `et.PrivInstruction` 1, `et.AccessViolation` 4).
- **T4 convention** (read by non-SPINE kernels): the T4 cause code in
  `ps.Cntl` low 16.

Before T4 loaded A/B, a SPINE-configured kernel booted on T4 with
`t9k_hard_p` read leftover A/B, mapped init's first trap syscall to
`T9K_TRAP_ERROR`, and panicked with "Attempted to kill init! exitcode=0x4".

**Userspace dual path** (`tests/syscall_stub.c`):

- Default (`TP_USE_T9K_SYSCALL=0`): C-call gate, including hard-P boots.
- Opt-in (`TP_USE_T9K_SYSCALL=1`): when the version-1 capability tag matches,
  pass the caller frame and execute `60 F8`; return the result in A.
  Without that tag, retain the C-call path.
- The legacy flag at `0x80000064` is forced to zero so older clients fall back
  to the gate instead of using the retired shared-buffer ABI. New clients on
  older kernels likewise use the gate when the version-1 tag is absent.

**uaccess:** `access_ok` consults soft-mm when `T9K_SOFT_FL_ACTIVE`.

---

## Enable flags

| Env / flag | Effect |
|------------|--------|
| `T4_PROTECT=1` (default) | Emulator region checks when in P. Memory accesses only call `t9k_translate()` while `t9k_prot.active` is set (P-mode, or a live region descriptor), so L-mode code with no RODS loaded pays nothing |
| `T4_PROTECT=0` | Disable checks |
| `T4_PROTECT_DEBUG=1` | Trap logs |
| `T4_PMI=1` (default) | Emulator PMI bank checks; not complete task isolation |
| `T4_PMI_TEXT_ROM` / `T4_PMI_PRIV` | Opt-in text-write / foreign-private checks in T4; currently enabled by variable presence, even with value `0`. Leave unset to disable. Shared exceptions still apply. |
| `TP_USE_T9K_SYSCALL=0` (source default) | Userspace C-call gate |
| `TP_USE_T9K_SYSCALL=1` (compile override) | Opt-in trap path when the version-1 capability tag matches; rebuild each application using the stub |
| Fixed `0x80000060` | Gate pointer |
| Fixed `0x80000064` | Legacy trap flag, forced to zero for old-client compatibility |
| Fixed `0x80000068` | Retired global result slot; unused by version 1 |
| Fixed `0x8000006C` | `TP_TRAP_ABI_V1` (`0x54504601`) when hard-P is enabled; zero otherwise |
| Kernel cmdline `t9k_enforce` | Soft-mm `ACTIVE` on every exec (access_ok enforced) |
| Kernel cmdline `t9k_hard_p` | Enter user via **goprot** (implies enforce) |
| Compile `T9K_SOFT_ENFORCE_DEFAULT=1` | Enables enforcement without cmdline; source fallback is `0` |
| Compile `T9K_HARD_P_DEFAULT=1` | Sets hard-P initial state; source fallback is `0`. Exec activates region maps when hard-P is enabled. |

**Boot selftest:** `t9000_prot: selftest PASS` on UART after `late_initcall`.

---

## Bring-up verification and trap-default rollout

1. Rebuild kernel with `t9000_prot.o` + `t9000_goprot.o` (already in `obj-y`)
2. Boot with `T4_PROTECT=1` (emulator P-mode checks)
3. Cmdline (in `setup.c`): `t9k_enforce t9k_hard_p`
4. Confirm UART:
   - `t9000_prot: HARD P-process (goprot enter) + soft ENFORCE`
   - `t9000_prot: selftest PASS`
5. User entry: `t9k_enter_userspace` → `goprot` with RODS; on trap, re-enter or exit
6. For an explicit trap-path trial, rebuild apps with `-DTP_USE_T9K_SYSCALL=1`.
   Rebuild every affected startup/stub object, application and rootfs/initramfs
   image; enabling kernel hard-P does not change already-built application code.
7. Complete the isolation and trap-default acceptance work below before changing
   the source default or retiring the C-call gate.

### Remaining acceptance work

The Linux session owns the detailed source review and rollout order. These are
required review/test areas, not claims that every case currently fails:

1. Extend acceptance of the implemented caller-frame ABI: negative errno,
   64-bit argument packing, return-register clobbers, misaligned/wrapping/bad
   pointers, complete-range checks and old/new kernel-client combinations.
2. Verify per-context PDS/RODS and integer state across concurrent tasks,
   nested spawn/exec and supervisor re-entry. Hardware P-state saving does not
   preserve FP registers; supervisor FP and block-move save/restore is required
   where those resources can be clobbered (ISM ch.13).
3. Handle timeslice, signals/restarts, exit and access/privilege faults with
   explicit policy. The current loop dispatches syscall traps, returns other
   causes, and the entry wrapper calls `do_exit`; it is not a complete scheduler
   or Linux signal/restart implementation for protected userspace.
4. Replace the overlapping Q2/Q3 descriptors with physically disjoint mappings
   (HRM §5.2.2; ISM §9.4). Deny kernel/other-task memory and unauthorized MMIO while
   preserving permitted shared surfaces. Exercise ordinary loads/stores,
   fetch/execute, uaccess read/write checks, and MOVE/DEVMOVE/MOVE2D boundaries.
   Soft uaccess validation alone does not constrain arbitrary P-mode accesses.
   Exercise restartable CALL/AJW/GAJW workspace growth separately from ordinary
   illegal accesses, which are not normally restartable (HRM §5.1).
5. Keep ISM trap reason/error and saved control-word semantics correct on
   SPINE. Identify and remove reliance on T4 cause slots, leave-P/RODS MMIO
   backdoors or permissive privileged-instruction behavior for a hardware path.
6. Run a fully rebuilt trap-first `/init`, libc application, BusyBox, shell and
   spawn/exec/wait workload through the normal boot endpoint. Include repeated
   syscalls, concurrent tasks, bad pointers and deliberate isolation violations.
   Prove hard-P trials do not silently succeed through the C-call/L fallback.
7. Inventory all build paths using `tests/syscall_stub.c`, keep an explicit
   compatibility override, then change the default in a separately validated
   commit. Record compiler/kernel/emulator revisions, flags, images and UART
   evidence. A boot banner or soft-map selftest alone is insufficient.

---

## Opcode note

Do **not** use OReg `0xFA`/`0xF8` for protect — those are xsword/cs on the positive secondary map. Protect uses **negative** OReg via nfix (`60FA`/`60F8`).

## L/P lifecycle (optimized path)

```
L (kernel/init)
  └─ tp_spawn / execve
       └─ t9k_on_exec_finish → soft_mm quarter map + ACTIVE
       └─ t9k_enter_userspace
            └─ goprot (60 FA)  →  P (user)
                 ├─ C-call syscall gate (works under Q2 identity)
                 ├─ opt-in syscall insn (60 F8) → trap → L → re-goprot
                 └─ compatibility exit/spawn handling (T4 leave-P bridge on non-SPINE)
```

| MMIO | Role |
|------|------|
| `0xA000001C` | Non-SPINE T4 `VGA_CTRL_LEAVE_P` → `t9k_leave_p()`; not an architectural T9000 operation |

**Region limit:** T9000 has **one region per LA quarter**. Soft-mm on exec:
- **Q0** RW no-X full identity — UART/kbd/mouse/SD MMIO under hard P (`soft_ok` still rejects `la < 0x1000`)
- **Q2** RWX window — text/data/FB/VGA (`a0000003` when full GiB)
- Multi-quarter Q3 data remap scaffolded; force-split RAM bFLT deferred (TCC)

**Default kernel command line:** `t9k_enforce t9k_hard_p`. The C fallback
initializers for both flags are `0`; other command lines/builds must be checked
separately. `t9k_asm_goprot` loads arguments from CALL frame `WPtr[1]/[2]`.
Non-SPINE compatibility uses T4 leave-P behavior; SPINE reads the ISM
reason/error return and must preserve the saved P control word.

**Verified:** HARD P + Q3 WPtr (full Q2 mirror) + `DESKTOP_READY` (e.g. `/tmp/lp-boot51.uart`). Phase 10 soft RX/private + RODS rebind on switch.

**Trap syscall args:** Version 1 uses A for the number/result and B for the
six-word caller frame. C-call remains the source default for applications using
`tests/syscall_stub.c`.

### Phase 10 concurrent model

| Layer | Isolation |
|-------|-----------|
| **soft_ok / uaccess** | Per-task private/shared and text-write policy exists; callers' read/write direction, translation and complete-range checks still need review |
| **HW RODS** | Re-installed on every `__switch_to` from `soft_mm.rods_cache` |
| **HW Q2 width** | Still wide (FB/VGA share Q2 with text); direct foreign/kernel accesses are not ruled out by per-task soft maps. PMI strict options and shared exceptions need independent validation. |

```
switch_to(prev → next)
  __current_thread_info = next
  t9k_install_task_rods(next)   // MMIO stage RODS + LOAD_RODS
  GAJW to next kernel stack
```
