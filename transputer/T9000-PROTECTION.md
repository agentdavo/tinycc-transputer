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
do not complete isolation or trap policy. Linux `e3181b5f0775` additionally
preserves FP and MOVE2D state across the P boundary and synchronous nested
spawn; its scope and remaining validation are described below.

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

### Live descriptor audit

The Linux owner inspected the desktop's emitted descriptors after
`bc2969c2ea63`. Inclusive ranges below describe that compatibility boot:

| Region | Descriptor | Logical range | Physical range | Permissions |
| --- | --- | --- | --- | --- |
| Q0 | `20000001` | `00000000–3FFFFFFF` | `00000000–3FFFFFFF` | RW, including devices |
| Q1 | `80000000` | Disabled by the implementation | — | — |
| Q2 | `A0000003` | `80000000–BFFFFFFF` | `80000000–BFFFFFFF` | RWX |
| Q3 | `A0000001` | `C0000000–FFFFFFFF` | `80000000–BFFFFFFF` | RW |

Q2 and Q3 alias the full physical GiB. A conforming replacement must reserve
disjoint aligned backing for code, data/heap and workspace, then update the
loader, relocations, entry/argv/GOT pointers, capability discovery and device
access policy together. Software checks on syscall frames do not remove the
alias available to ordinary P instructions.

Separate standalone tests on T4 `2e44501`, using disjoint regions, passed
CALL/AJW/GAJW workspace-extension restart, scalar access/execute/device checks,
and privileged-instruction rejection. A test supervisor also preserved FP and
block-move state across intentional clobber when it explicitly saved/restored
them; omitting restoration was detected. This does not establish equivalent
state handling in Linux.

Those tests exposed two independent emulator defects: `FPLDNLSN` read zero
instead of relocated `0x3FA00000`, and `FPSTNLSN` to RX memory failed to trap.
Nested T4 commits `fe000b2` (loads) and `2853b29` (stores) repair these paths
through guest word accessors. Multiword preflight prevents a denied second
word from partially writing memory or consuming an FP operand. The committed
`t4/tests/test_t9000_pmode.c` covers direct/indexed remapping, device-only/RX
violations, both widths, state loads/stores and framebuffer access. Removing
only the store preflight fails its first-word canary.

Host/guest protection and workspace-extension checks passed; the unchanged
Linux frame-ABI image still reaches `DESKTOP_LOOP`, with parent resumption and
nested child status 23. This is T4 compatibility evidence. It does not repair
the overlapping Linux regions or add Linux FP/block-state preservation.
The standard debug build has a pre-existing `Instruction` macro/field
collision; a private build with only that field renamed passes the debug
guest suite. The retained audit/reproducer and validation record are local
captures in `logs/p-isa-audit/`.

### Linux FP and block-move continuation state

Linux `e3181b5f0775` adds a native seven-word FP image and three-word block
image to each continuation's `t9k_soft_mm`. The `goprot` wrapper saves the
supervisor's images on its workspace, restores the selected P images, and
captures P state immediately on trap return before restoring supervisor state.
The trap reason/type are saved before either state-store operation uses the
integer stack. The existing synchronous-spawn snapshot copies these images
with the parent PDS/maps, so nested children cannot replace a suspended
parent's FP or MOVE2D state. Fresh entry obtains a clean processor-generated
FP image instead of assuming an emulator-specific status encoding.

The committed fixture in `linux-7/arch/transputer/tools/pstate-test/` passes
at three levels on T4 `2853b29`, including mixed FP widths/values, rounding,
sticky error state and all three block registers. Returns are 37 from the
grandchild, 23 from the child and zero from the shell, followed by
`DESKTOP_LOOP`. Omitting FP or block saves independently fails the tests;
an instrumented supervisor passes 200 round trips. Ordinary trap-first and
legacy desktop boots also pass. Independent source review found no blocker
in frame offsets, return-value handling, image order or updated call sites.

These are T4 round-trip results, not native status-bit compatibility or
SPINE/physical T9000 validation. T4 uses a local FP status/error encoding;
independent preservation of native exception flags needs hardware/PDS/THDS
checks. A review also identified that T4's `fpstall` does not apply the ISM
round-to-nearest postcondition; test that separately before changing it.
FPUNOROUND transient state, asynchronous scheduling, timeslice/signal policy
and non-restartable memory-semantics traps remain separate acceptance work.

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
| **10 Concurrent P** | Partial: continuation/frame ABI and FP/block boundary switching implemented | `e3181b5f0775` preserves per-continuation FP/block images and supervisor state; three-level nested and supervisor controls pass on T4. Asynchronous scheduling, native flags, RTL and strict isolation remain open. PMI strict options and trap-first syscalls remain opt-in. |
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
6. For an explicit trap-path trial, export `TP_USE_T9K_SYSCALL=1` before
   building init, BusyBox, the Transputer shell, the framebuffer manager,
   the package tools, native TinyCC, CoreMark or T9000 Doom. Their build scripts pass
   `-DTP_USE_T9K_SYSCALL=1` when compiling the userspace syscall stub.
   Rebuild the rootfs and boot image with the resulting binaries.
7. Complete the isolation and trap-default acceptance work below before changing
   the source default or retiring the C-call gate.

The initial 18 November 2026 T4 trial verified hardware syscall traps from a rebuilt
`/init`, BusyBox, Transputer shell and framebuffer window manager. The desktop
framebuffer matched the reference. BusyBox needed a scratch uClibc archive with redundant
DNS resolver objects removed for strict linking. See the
[Linux 7 size checkpoint](tests/bench/linux-size-2026-11-18.md) for the
measurements and outstanding userspace checks.

If a strict app link reports duplicate uClibc DNS resolver symbols, rebuild
the T9000 static archive in a clean directory. In a Linux checkout with TinyCC
and staged Transputer UAPI headers, use the following commands from the
project root.

```sh
export WORK="$(mktemp -d)"
KERNEL_HEADERS="$PWD/transputer/linux-7-headers/include" \
  bash transputer/scripts/build-uclibc-part.sh t9000
export UCLIBC="$WORK/uClibc-ng-t9000"
export TP_USE_T9K_SYSCALL=1
```

The archive, startup objects and generated headers are in `$UCLIBC`.

The follow-up trial linked BusyBox directly with a fresh, unmodified uClibc
archive. Two rebuilds of that archive matched byte for byte. The normal T4
desktop boot passed at 32 MiB, including the protection self-test, BusyBox
probes and reference framebuffer comparison. A separate 64 MiB trial ran
the shell, 1,000 CoreMark iterations and a 12-frame Doom run from an ext2 SD
card before starting the framebuffer manager. CoreMark validated its CRCs,
Doom exited successfully and the desktop framebuffer matched again.

The original 32 MiB sequence completed both benchmarks, then failed to
allocate the framebuffer manager's contiguous 4 MiB block. The kernel
reported 1,156 KiB free. Reclaiming the private anonymous heap of a
completed synchronous child raised that to 5,512 KiB, with the memory
still fragmented. Doom now advises the kernel to drop clean WAD cache
pages and closes the file descriptors on normal exit. With both changes,
the 32 MiB image completes both benchmarks and reaches the same
reference framebuffer. Reclamation is scoped to private anonymous
allocations from synchronous soft-spawn children. `munmap` now frees a
whole recorded extent in the current spawn scope immediately, with the
length rounded up to a page. It rejects a partial overlap with an owned
extent. Untracked mappings still use the compatibility path.

The 32 MiB probe passed through the hardware trap and the C-call gate.
For each path, a parent and nested child each performed eight 4 MiB
map/unmap cycles, checked that fresh pages were zeroed and verified that
partial unmaps left their data intact. The parent also verified its live
mapping after the nested child returned.

Soft and hardware spawn now drop the temporary image pin when exec fails
while the caller's address space is still current. Four missing-file
attempts on each path previously raised the observed `mm_users` count
from 1 to 9. With the fix it stayed at 1 through all eight calls, which
still returned `ENOENT`.

A successful synchronous spawn now restores the saved parent address space
before dropping the child's image reference. BusyBox keeps a separate cache
reference, uses its own address space on re-entry and avoids restoring its
snapshot while that image is suspended in a parent frame. A real exec,
top-level exit or task teardown releases the cache and its buffers. The
hardware exit path switches to a permanent kernel workspace before waking
the parent, which can then reclaim the child without freeing its running
workspace.

On the 32 MiB emulator, 80 consecutive fresh shell spawns interleaved with
ten cached BusyBox applets passed and reached the reference desktop. The
prior kernel panicked on entry to the 16th shell. Four raw hardware spawns
returned successfully and reused the same child image address. The
direct exec and top-level exit probes each measured one cache reference
immediately before its release.
With Linux 7 commit `de424d474bb2`, private anonymous mappings from
direct exec and other launches outside a soft spawn belong to the Linux
address space. They are released on a matching unmap or when that address
space is destroyed. Soft-spawn
children retain their separate mapping scope. Partial unmaps of recorded
extents are rejected.

The 32 MiB boot running CoreMark, Doom and native TinyCC previously
completed all three workloads but failed to start the desktop. The
framebuffer manager needed a contiguous 4 MiB block, and the kernel
reported 17,352 KiB free in smaller blocks.

Linux fbwm chooses the smallest available slot that fits and maps
`width × height × 4` bytes for the client pixels. The kernel rounds up
to whole pages. Window destruction and reset pass the recorded mapping
length to `tp_munmap_owned`. Both the trap and C-call gate route this
request to the same kernel ownership check. On a kernel without the
syscall, fbwm maps full slot capacity and retains it for reuse, so a
larger window can reopen in the same slot. Externally supplied pixels
remain borrowed.

When a client recreates an owned surface after resizing, fbwm keeps
the existing mapping if the new dimensions fit the slot and use the
same number of pages. It updates the stride and clears the new pixel
area to the window background. A page-count change uses the normal
release and allocation path, and close releases the original extent
even after repeated resizes. Window dragging keeps its existing
geometry-only behavior.

A 93×61 surface requires 24 KiB of backed pages, down from 300 KiB.
A 513×349 surface requires 700 KiB, down from 1,000 KiB. The host
geometry tests verify the exact length passed to both mapping calls
and preserve the reference frame.

The bFLT is 36,404 bytes, up 144 bytes from exact-size mapping and down
from 112,572 bytes with `malloc` and `free`. Its image plus stack is
328,544 bytes before any surface is created. CoreMark, Doom, native
TinyCC and the desktop passed in one 32 MiB boot. The desktop pixels
and native TinyCC ELF object matched their references byte for byte.
The resize probe alternated between 93×61 and 94×61 40 times without
mapping or unmapping pages. It checked the stride and pixels after
each resize, confirmed that close freed the mapping, and verified
that growing to 320×240 mapped a new extent. The host tests also
checked that shrinking across a page boundary released the extra
pages.
Direct legacy exec from SD previously halted during a file read on
the caller's bFLT workspace. Linux 7 commit `ddc9907b5e4e` pins the
caller's address space while top-level exec loads the new image.
On successful replacement, it switches to the task's kernel workspace
before releasing the old image and entering the new program. A failed
lookup returns to the original workspace and drops the temporary
reference.

Both direct SD exec transports passed in separate 32 MiB boots. Each
ran CoreMark, Doom and native TinyCC, recovered from `ENOENT` for a
missing SD executable, then launched `/sd/usr/bin/fbwm`. Both resize
probes passed. The native TinyCC objects and desktop framebuffers
matched their references byte for byte.

The package tools passed strict linking with the opt-in. The current
syscall default remains the C-call gate.

Native TinyCC now links the Transputer compiler runtime and uses a local
binary64 implementation of `ldexpl`. It uses the normal bFLT startup
objects to receive its command-line arguments. The 32 MiB T4 test
compiled an object onto the SD card through the trap path. The extracted
ELF contained the expected binary64 words for a normal hexadecimal
constant, the least positive subnormal, the tie that rounds to zero,
overflow to infinity and a finite constant near the top of the range.
The Linux dispatcher handles the generic 32-bit `fcntl64` number used by
uClibc `fdopen`. The legacy `fcntl` number remains available.

TinyCC now propagates object-output stream errors, including failures
when the stream flushes on close. A host test imposed a 128-byte output
limit. Before the fix both a small buffered object and a large object
were truncated with a zero exit status. Both now fail with the write
diagnostic. With the limit removed their bytes match the earlier output.
An injected `fdopen` failure also verified that TinyCC closes the file
descriptor and reports the original error.

### Remaining acceptance work

The Linux session owns the detailed source review and rollout order. These are
required review/test areas, not claims that every case currently fails:

1. Extend acceptance of the implemented caller-frame ABI: negative errno,
   64-bit argument packing, return-register clobbers, misaligned/wrapping/bad
   pointers, complete-range checks and old/new kernel-client combinations.
2. Verify per-context PDS/RODS and integer state across concurrent tasks,
   asynchronous scheduling and supervisor re-entry. The nested FP/block
   save/restore fix `e3181b5f0775` passes the T4 fixture; verify native exception
   flags, PDS/THDS interactions, RTL and scheduling paths separately. Hardware
   P-state saving alone does not preserve FP/block registers (ISM ch.13).
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
