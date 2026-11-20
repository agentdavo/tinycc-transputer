# Four-T9000 shared bootstrap checkpoint - 19 November 2026

Linux CPU0 now owns an optional shared-board boot table, releases three
secondary bootstrap images, checks their distinct shared workspaces and
atomic results, then continues normal Linux boot after they park.
CONFIG_TRANSPUTER_T4_SMP_BOOT defaults off. CONFIG_SMP remains disabled;
secondaries do not yet enter Linux scheduling or its online CPU mask.

## Implementation and reproduction

Linux commit `4e53d2d097c2` adds the versioned shared table, bounded admission
protocol, boot-only atomic wrappers, per-CPU stack checks and early setup hook.
See [Linux bootstrap documentation](../../linux-7/Documentation/arch/transputer/t4-shared-boot.rst).

The secondary entry uses private code and a 16 KiB shared workspace selected
by board port ID. Build it using [build-t4-secondary.sh](../../scripts/build-t4-secondary.sh),
setting TCC and NETLOAD to the kernel toolchain if required. It compiles the
same `t4_smp_boot.c` linked into Linux, with the standalone secondary entry.
Use four T9000 CPUs (`-s9 -st -sn -4 -sm 25`), board ports
`0:1,1:1,2:1,3:1`, and T4_NODE_BTL pointing at secondary.btl. The Linux image
runs on node zero. Workspaces occupy 0x40010000..0x4001bfff; the shared boot
table starts at 0x40000000. These regions are outside the private Linux
allocator. Hardware boot on links and a common kernel image are future work;
this run preloads the secondary image using T4_NODE_BTL.

## Four-node Linux result

A fresh Linux build from the prior committed tree plus the candidate changes
booted with 32 MiB private RAM per node and the 16 MiB shared board. UART:

```
T4 CPU0: workspace=802798ec cookie=12340000 iterations=1000 parked=0
T4 CPU1: workspace=40013a7c cookie=12340001 iterations=1000 parked=1
T4 CPU2: workspace=40017a7c cookie=12340002 iterations=1000 parked=1
T4 CPU3: workspace=4001ba7c cookie=12340003 iterations=1000 parked=1
T4_SHARED_BOOT: PASS cpus=4 add=4000 cas=4000 lock=4000
```

Each CPU verified its stack-local 256-word pattern, and CPU0 checked all
secondary workspace bounds and bottom canaries, identity cookies, iteration
counts, and entry/done/parked acknowledgements. Fetch-add, CAS retry loops,
exchange-protected increments and failed-CAS behavior passed.

The same boot then passed pthread/malloc/stdio stress, epoll, POSIX timers,
protected signal handlers, timeslice delivery, two-thread nested FP/MOVE2D
signal-state tests, and two-context CoreMark with 4000 total iterations and
`Correct operation validated`. CoreMark reported 101.103808 seconds and
39.563297 iterations/sec in this run; the different T9000 mode and concurrent
host testing mean this is not a comparable performance measurement or an
SMP speedup result. Both CoreMark contexts still run on CPU0.

Desktop, Files-open and restored-desktop frames were captured and inspected.
The harness checked restored pixels outside the clock area. The desktop and
Files appeared intact. Linux boot produced no invalid-workspace diagnostics.
Kernel BTL SHA256:
`45b701987e9979051b11bfef5632c108e3e5a414b8eefde994e233eac2b4f7c8`.

Evidence is in `logs/compiler-coordination/shared-linux-boot-2026-11-19/`:
`kernel.log`, `validation.log`, `candidate/play/uart.log`, `emulator.log`, and
three framebuffer captures. `build.sh`, `start.sh` and `play.py` retain the
exact build and run commands. `probe.sh` tests admission without Linux.

## Emulator follow-up and limits

The first admission probe passed but T4's AJW diagnostic incorrectly labelled
shared workspaces invalid. Its address check now accepts only aligned board
RAM on an attached CPU, excluding the control window and disconnected CPUs.
Host boundary tests cover that rule. T4 commit `e62d711` records this fix
and the parked-worker/explicit-timeout test improvements. The final four-
and eight-node board run and isolated admission probe passed; the probe
produced no invalid-workspace diagnostics.

The four/eight-node board tests exposed a separate shutdown limitation: guest
checks complete, but timeout cleanup can need SIGKILL (status 137). Completed
workers now STOPP instead of spinning. The runner records exit status and
accepts forced cleanup only alongside the guest pass marker; it does not
claim clean multi-node shutdown is repaired.

The early atomic command wrapper assumes one active context per CPU and no
installed secondary interrupt handlers. It is not yet a replacement for
Linux arch_atomic operations. Shared mutable kernel data/BSS and allocator
state, CPU-local current/IRQ state, secondary idle tasks, timer/trap state,
IPIs, scheduler online sequencing, SMP locks/bitops/barriers and protected
mappings remain prerequisites for a genuinely SMP kernel. The board still
models functional access without physical serial-link timing.

## Topology clarification

The validated wiring reserves link 1 on every CPU for a single board
attachment, uses links 2/3 for a neighbour chain and node 0 link 0 for the
host. The chain is configured but is not used by the shared bootstrap test.
It does not use all four links on every CPU or establish an optimal topology.
The board currently permits one attachment per CPU and dispatches directly;
serial packets, link timing and multiple attachments per CPU are future work.
A useful later experiment is two board links per CPU on the eight-port board,
but any bandwidth benefit must be measured after routing and timing exist.
