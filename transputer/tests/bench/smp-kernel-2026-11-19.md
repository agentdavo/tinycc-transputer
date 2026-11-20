# CONFIG_SMP build checkpoint - 19 November 2026

The Transputer Linux 7 port now has an experimental SMP-enabled build
configuration, architecture atomic/lock/futex support, and the required
TinyCC per-CPU/linker-boundary support. This remains a **one-online-CPU Linux
baseline**: secondary bootstrap CPUs are not yet scheduler CPUs.

## Configuration and implementation

`arch/transputer/configs/tpos_smp_defconfig` selects:

```
CONFIG_SMP=y
CONFIG_NR_CPUS=4
CONFIG_TRANSPUTER_T4_SMP_BOOT=y
```

The ordinary tpos_defconfig keeps the UP default. The new configuration builds
SMP scheduler/RCU/per-CPU paths. Linux reports only CPU0 present/online until
shared kernel memory and an IPI transport exist. __cpu_up returns
-EOPNOTSUPP; remote IPI operations are deliberately not silent no-ops.

Linux commit `025cabea69d7` records the kernel implementation and configuration.

Kernel changes add exchange/CAS for 8/16/32-bit values, spinlocks and reader/
writer locks, and futex atomic operations that translate protected addresses
and check write access. Shared board addresses use masked word-CAS retries;
CPU-private RAM uses local IRQ exclusion. This distinction must change when
a shared high-address kernel RAM window is introduced. Local IRQ state is
still global in the one-CPU Linux baseline. Barriers rely on the emulator's
synchronous serialized board transactions; no asynchronous hardware ordering
is claimed. Spinlocks poll before retrying CAS to reduce command traffic.

TinyCC commit `5c5c983d` groups and page-aligns per-CPU template sections,
materializes zero-fill inputs, and defines __per_cpu_start/end/load plus
__lock_text_start/end. The emulator fixture checks boundaries, alignment,
initialized values, zero-fill data and lock-function containment.

## Validation

The image links with strict unresolved-call checking and no undefined-symbol
warnings. The initial build exposed missing futex hooks and lock-text bounds;
the successful rebuild includes real implementations and linker definitions.

Four T9000 nodes run with 32 MiB private RAM each and the shared 16 MiB board.
Linux CPU0 releases the three bootstrap CPUs, verifies their distinct shared
workspaces, and checks 4000 fetch-add, CAS and exchange-locked increments.
The secondaries then park. The SMP-enabled kernel reports:

```
Transputer SMP atomic/futex selftest: PASS
Transputer SMP build: CPU0 only; shared kernel map/IPIs pending
smp: Brought up 1 node, 1 CPU
Transputer SMP: 1 Linux CPU online
```

Its embedded per-CPU unit initializes successfully. The generic NOMMU per-CPU
allocator also prints `max_distance=0x8000 too large for vmalloc space 0x0`;
this is followed by successful embedded allocation, not an abort.

The same boot runs protected pthread/malloc/stdio tests, epoll, POSIX timers,
nested two-thread FP/MOVE2D signal tests, two-context CoreMark (4000 total
iterations, correct operation validated), and the desktop/Files workflow.
The framebuffer harness checks restored pixels outside the clock area.
CoreMark contexts run on CPU0; this does not measure SMP speedup.

Separately, the production smp_locks.c passes four-node contention tests for
spinlocks, read/write two-word consistency, CAS counters, adjacent byte and
halfword exchange, private atomics and restoration of both initial IRQ-mask
states. Freestanding type/BUG/IRQ shims isolate the algorithm; this is not a
multi-CPU interrupt-delivery test. Reproduce with
`transputer/tests/smp-locks/run.sh`; see its README for tool overrides.

Reproduce linker checks with `transputer/tests/test_percpu_layout.sh`.
Evidence and exact build/run scripts live in
`logs/compiler-coordination/smp-kernel-2026-11-19/`: `kernel-resume.log`,
`candidate/.config`, `candidate/vmlinux.btl`, `validation.log`,
`candidate/play/uart.log`, framebuffer captures, `locks-final/` and
`linker-final/`. The kernel build is kept in persistent Ubuntu storage
because temporary build directories did not survive separate WSL invocations.

Final BTL SHA256:
`fdb06cc9b801ee206cfc8cdd46346b034b5affddc782de0eed7f2d5fedc89387`.
The final run reported CoreMark 81.222592 seconds; host load differed between
runs, so this is a correctness result rather than a performance comparison.
Both the initial and final SMP-enabled images reached the desktop; acceptance
above refers to the final image built from tpos_smp_defconfig.

## Remaining work

This completes the requested CONFIG_SMP build prerequisite, not four-core
Linux execution. Next are a common kernel image/data/BSS and allocator map,
CPU-local current/IRQ/trap state, secondary idle-task entry, IPIs and CPU-online
sequencing, followed by cross-CPU scheduler/thread tests. The shared board
still dispatches directly without serial-link timing or packets. Optimal
link topology and multi-link bandwidth remain unmeasured.
