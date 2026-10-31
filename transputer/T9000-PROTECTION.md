# T9000 Protection — Full Plan Status

Reference: `t9000ism.txt` ch.9–10, `t9000hrm.txt` §4–5.

## Model (not a classic page MMU)

| Concept | Role |
|---------|------|
| **L-process** | Trusted; HW-scheduled; optional THDS trap-handler |
| **P-process** | Protected mode under a supervisor L-process |
| **goprot** | Privileged: A=PDS, B=RODS → enter P (OReg **−6**, bytes `60 FA`) |
| **syscall** | Trap to supervisor (OReg **−8**, bytes `60 F8`) |
| **4 regions** | LA bits 31:30 select region; size 2^n (n≥8); phys relocate |
| **PMI** | Physical bank map (`T4_PMI=1`) |

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
| **8 Optimized L/P OS** | **Done** | Process-local maps; leave_p; NULL reject; PMI default ON; trap args; soft_mm clear |
| **9 Split RX/RW bFLT** | **Done (stack Q3)** | `flat_q3.c` publishes WPtr→Q3 via **full Q2 mirror** (`LA = phys + 0x40000000`, RODS 1 GiB). Default on. Hard P + Q3 WPtr reaches `DESKTOP_READY`. Sub-window maps dual-mapped the image and broke path-walk (null i_op gcall after getpid). T4 keeps Q3 relocate after soft `leave_p` so C-call workspace stays valid. |
| **10 Concurrent P** | **Done** | Soft RX/private + RODS rebind. **PMI**: private window + text span published on switch; **text-as-ROM** opt-in `T4_PMI_TEXT_ROM=1`; **foreign private** opt-in `T4_PMI_PRIV=1` (off: C-call needs kernel heap in high RAM). **Trap-first** scaffold in `syscall_stub` (`-DTP_USE_T9K_SYSCALL=1`): args via `0x8000006C`→BSS, ret `0x80000068`; default still C-call until fully validated. |
| **11 Data quarter / trap default** | Partial | Soft RX via `end_code`; stack Q3 mirror. Full data-LA rewrite + trap-only default still experimental. |

---

## Verify

```bash
# Unit + guest
cd transputer/t4
make -f Makefile.linux test-protect
make t4 && ./tests/run_hello_p.sh
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

The unit test `tests/test_t9000_protect.c` predates the `cpu_state.h` refactor
(it declares `IPtr`/`AReg`/... as plain globals, which are now `t4cpu->` macros)
and no longer links; `run_hello_p.sh` is the working check.

---

## Kernel integration (Phase 4)

| API | Role |
|-----|------|
| `t9k_on_exec_finish(regs)` | Fills **per-task** quarter-identity soft-mm; sets `ACTIVE` only if enforce on |
| `t9k_task_soft_mm(task)` | Returns `&task->thread.soft_mm` |
| `t9k_soft_setup_user(...)` | Tight image+stack regions (tracking) |
| `t9k_soft_setup_user_quarter(...)` | Full 1 GiB identity of load (+ stack) quarter — enforce-friendly |
| `t9k_soft_fill_rods(mm, rods)` | Build 4 T9000 RODS words for hard `goprot` |
| `t9k_enter_user_p(...)` | One `t9k_asm_goprot` (hard); soft if no trap |
| `t9k_run_user_until_exit(...)` | Loop: goprot → on syscall trap dispatch `__transputer_syscall` → restart |
| `t9k_asm_goprot` / `t9k_asm_syscall` | `t9000_goprot.S` raw encodings (`60 FA` / `60 F8`) |

**Syscall ABI (P-process trap):**

| PDS field | Content |
|-----------|---------|
| `ps.sAreg` | syscall number (in) / return value (out) |
| `ps.sBreg` | arg1 |
| `ps.sCreg` | arg2 |
| further args | future: user workspace slots |

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

- Default: C-call gate (unchanged, production)
- `TP_USE_T9K_SYSCALL=1`: emit `60 F8` before gate (hard-protect experiments)

**uaccess:** `access_ok` consults soft-mm when `T9K_SOFT_FL_ACTIVE`.

---

## Enable flags

| Env / flag | Effect |
|------------|--------|
| `T4_PROTECT=1` (default) | Emulator region checks when in P. Memory accesses only call `t9k_translate()` while `t9k_prot.active` is set (P-mode, or a live region descriptor), so L-mode code with no RODS loaded pays nothing |
| `T4_PROTECT=0` | Disable checks |
| `T4_PROTECT_DEBUG=1` | Trap logs |
| `T4_PMI=1` | PMI bank map |
| `TP_USE_T9K_SYSCALL=1` | **Default** in `syscall_stub.c`: trap-first under hard_p |
| Fixed `0x80000060` | Gate pointer |
| Fixed `0x80000064` | hard_p flag (1 if `t9k_hard_p`) |
| Fixed `0x80000068` | Trap syscall return value |
| Fixed `0x8000006C` | Pointer to 7×u32 arg staging (kernel BSS) |
| Kernel cmdline `t9k_enforce` | Soft-mm `ACTIVE` on every exec (access_ok enforced) |
| Kernel cmdline `t9k_hard_p` | Enter user via **goprot** (implies enforce) |
| Compile `T9K_SOFT_ENFORCE_DEFAULT=1` | Same as enforce without cmdline |
| Compile `T9K_HARD_P_DEFAULT=1` | Same as hard_p without cmdline |

**Boot selftest:** `t9000_prot: selftest PASS` on UART after `late_initcall`.

---

## Production rollout (ops, not design)

1. Rebuild kernel with `t9000_prot.o` + `t9000_goprot.o` (already in `obj-y`)
2. Boot with `T4_PROTECT=1` (emulator P-mode checks)
3. Cmdline (in `setup.c`): `t9k_enforce t9k_hard_p`
4. Confirm UART:
   - `t9000_prot: HARD P-process (goprot enter) + soft ENFORCE`
   - `t9000_prot: selftest PASS`
5. User entry: `t9k_enter_userspace` → `goprot` with RODS; on trap, re-enter or exit
6. Optional: build apps with `TP_USE_T9K_SYSCALL=1` for trap syscalls (else C-call while in P with identity Q2)
7. Tighten RX/RW splits; retire C-call gate when trap path is default

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
                 ├─ optional syscall insn (60 F8) → trap → L → re-goprot
                 └─ exit() → SPAWN_EXIT → write 0xA000001C leave_p → longjmp L
```

| MMIO | Role |
|------|------|
| `0xA000001C` | `VGA_CTRL_LEAVE_P` → T4 `t9k_leave_p()` (soft leave P) |

**Region limit:** T9000 has **one region per LA quarter**. Soft-mm on exec:
- **Q0** RW no-X full identity — UART/kbd/mouse/SD MMIO under hard P (`soft_ok` still rejects `la < 0x1000`)
- **Q2** RWX window — text/data/FB/VGA (`a0000003` when full GiB)
- Multi-quarter Q3 data remap scaffolded; force-split RAM bFLT deferred (TCC)

**Default boot:** `t9k_enforce t9k_hard_p`. `t9k_asm_goprot` loads args from CALL frame `WPtr[1]/[2]` (T4 CALL saves A/B). Re-goprot after C-call: kernel `leave_p` MMIO + T4 skips PRIV on goprot so `t9k_goprot` can soft-leave/re-enter.

**Verified:** HARD P + Q3 WPtr (full Q2 mirror) + `DESKTOP_READY` (e.g. `/tmp/lp-boot51.uart`). Phase 10 soft RX/private + RODS rebind on switch.

**Trap syscall args:** On cause=3, supervisor takes A/B/C as nr/a1/a2 and optional a3..a6 from user `WPtr` words (LA→phys when SPLIT). C-call gate remains default for TCC apps under P.

### Phase 10 concurrent model

| Layer | Isolation |
|-------|-----------|
| **soft_ok / uaccess** | Per-task `priv_lo..priv_hi` + shared FB/VGA/SHM; writes to `text_lo..text_hi` rejected |
| **HW RODS** | Re-installed on every `__switch_to` from `soft_mm.rods_cache` |
| **HW Q2 width** | Still wide (FB `0x81e00000` + VGA `0xA0000000` share Q2 with text) — instruction fetches can still touch foreign Q2 until PMI/per-bank maps |

```
switch_to(prev → next)
  __current_thread_info = next
  t9k_install_task_rods(next)   // MMIO stage RODS + LOAD_RODS
  GAJW to next kernel stack
```
