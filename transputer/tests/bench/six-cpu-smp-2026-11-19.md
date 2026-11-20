# Six-CPU SMP checkpoint â€” 19 November 2026

## Validated emulator step

T4 commit `8894797` adds opt-in six data links, preserves the two separate
control links, and provides common kernel RAM plus deferred secondary entry.
The fixture uses 15 mesh cables and one board attachment per CPU on link 5.
This is a custom T9000 extension; physical T9000s have four data links.

Candidate emulator SHA256:
`476e179295f5b0b5638267b80842c602850d985cd844ecc30bcd0a64326fd11b`.

Evidence in `logs/compiler-coordination/smp-six-2026-11-19/`:

- `mesh-final/`: all 15 cables, 30 checked transfers, 2051 bytes per payload.
- `mesh-regression/`: existing control network, synchronous acknowledgement,
  VCP/event/loopback checks pass in default four-data-link mode.
- `shared-mesh/`: all six CPUs execute one image, read shared initialized
  data, and reserve/write disjoint blocks through atomic allocation.
- Default four-link mode also passes the six-CPU shared-image fixture using
  a chain topology.
- `control-six/`: dedicated control-link network and three-CPU chain pass
  with six data links enabled, including remote boot/reset and error recovery.

Reproduce common-image validation with `transputer/tests/test_shared_kernel.sh`
and explicit `TCC`, `NETLOAD`, `T4_BIN`, `SHARED_KERNEL_TEST_OUT` paths.
The script requires the nested Linux and T4 trees for startup and fixtures.

## Compiler correction

Root commit `d40c3ccd` fixes constant-address lvalues being classified as
immediate integers. In the observed kernel failure, comparison with the CPU-ID
register loaded its value but compared against its address. The target test
fails on the old compiler and passes on the corrected compiler; all 40 EEVDF
comparison checks also pass. Evidence is in
`logs/compiler-coordination/smp-kernel-2026-11-19/lvalue-*` and `eevdf-lvalue.*`.

## Linux integration in progress

Linux commit `e2bc53cc6bef` corrects the SELTH wrapper's calling convention
for T4 shared-kernel builds. `test_thds_call_abi.sh` selects two descriptors
and restores the original descriptor; the old wrapper fails and the fixed
wrapper passes. Physical SPINE's separate register convention is preserved.

Linux commit `9e23f0080071` supplies the six-CPU kernel milestone. The
six-CPU configuration now selects generic SMP idle-thread allocation.
Without it Linux passed a null idle task to the architecture's secondary
startup routine, producing an invalid workspace. CPU-local current pointers,
IRQ masks, timers and THDS state, plus polled IPI delivery, are implemented
and undergoing integration validation.

The diagnostic integration boot now reports ``smp: Brought up 1 node, 6 CPUs``
and passes the synchronous cross-CPU callback/identity/per-CPU counter test.
The direct secondary entry must clear ``TP_THREAD_STATUS_NEW`` inherited from
``fork_idle()``; otherwise the first idle resume wrongly enters ``ret_from_fork``.

The clean image also passes the protected-memory selftest and all 768
allocation/write/check/free cycles from six bound kernel workers. BTL SHA256:
`777091956d7c32d4d28c89921ef6f641f071af6374bc9523d5438f40c6f46555`.
Evidence is `logs/compiler-coordination/six-core-linux-2026-11-19/fixed/play/`.

The first protected userspace launch then aborts in T4: the emulator's
host-global protection state incorrectly applies CPU0's P-process mode to
other CPUs, and a trap attempts a cross-thread longjmp. This failure was fixed by T4 commit `dafaf7d`, which makes protection and
PMI state CPU-local. Its regression covers 4,000 host trap returns and
six-node protected FP/MOVE/MOVE2D execution. T4 commit `69a362e` also fixes
background link polling to read hardware channel metadata physically, while
keeping direct protected guest access forbidden. The final application run reached protected libc workers but exceeded its
900-second host deadline; CoreMark and desktop are not yet established.

The initial policy pins userspace to CPU0 while allowing kernel workers on
other CPUs. IPIs are delivered at polling boundaries, not asynchronously in
arbitrary kernel loops. The board serializes functional memory accesses; it
does not model calibrated link bandwidth, latency or cache coherence timing.


## Idle wakeups and secondary timers

T4 commit `3dfda6f` implements the shared-board idle hint. The pending-IPI
check and timed condition wait share the publisher mutex; ordinary stores,
CAS, add and exchange wake the target. The hint leaves the pending IPI
unchanged. A one-millisecond timeout keeps timer polling progressing; this
is a host wait deadline, not a guaranteed guest scheduling latency.

`transputer/tests/test_shared_idle.sh` passes against the combined emulator:
128 host publication races; measured empty timeout 1,086,513 ns and an
already-pending return 187 ns. These measurements describe this run only.
All five secondary guests pass timeout, running-hardware-timer and IPI-wake
checks. The fixture exposed that secondary direct C entry bypasses head.S
and must explicitly start its hardware timers with STTIMER.

Evidence: `logs/compiler-coordination/smp-kernel-2026-11-19/idle-hint/suite/`.
Combined emulator SHA256:
`63b084899fccf119e5c50340848af51893e7e818a32263dc9c0abdc3bf5f3558`.

Linux commit `873408993b88` includes explicit secondary timer startup, the idle
hint, a common six-worker start barrier and online CPU enumeration in
/proc/cpuinfo. BTL SHA256:
`1499b00c0c06538ecf419be1fe2e98a0a9d4398f4196dc15cfbe1f81ae0b305e`.
It has passed six online CPUs, callbacks, the six-worker start barrier,
768 allocator iterations, protection selftest and initial protected pthread
checks. The longer run exceeded its 900-second host deadline during the
128-round, three-worker libc stress test while syscall activity continued.
No kernel panic or protection fault was reported. Full libc, timer/signal,
CoreMark and desktop completion are unvalidated for this image. The guest
/proc/cpuinfo assertion was not reached. Logs are preserved under
`logs/compiler-coordination/six-core-linux-2026-11-19/final/play/` and
`validation-final.log`.

The final test initramfs changes only the unique equal-length /init string
/proc/meminfo to /proc/cpuinfo to exercise online CPU enumeration; bFLT code,
relocations and length are unchanged. The timeout is not a passing result.
Next investigate timer catch-up and shared-memory lock contention before
rerunning the complete application validation; do not weaken its assertions.

## Reference ideas

INMOS Technical Note 51 is useful for timer wraparound, phase-preserving
periodic deadlines, bounded polling and separating timer resolution from
scheduling latency. Use original implementations and tests; no manual code
is imported. Its older-processor timing figures do not establish timing for
this custom six-CPU emulator. The next timing tests should cover wraparound,
late periodic deadlines and wakeup latency under contended shared access.


Read-only follow-up found a likely slowdown mechanism in time.c: each CPU
replays up to 16 missed 100 Hz ticks per poll and retains the remaining
backlog. Six CPUs can demand 600 full tick handlers per host second; when
serialized emulation takes longer than the interval, idle CPUs can repeatedly
replay ticks before reaching the idle hint. This is a hypothesis from code
review, not yet a measured profile. Instrument per-CPU missed ticks, capped
batches, handler duration and idle-hint counts next. Consider idle-secondary
catch-up coalescing with correct idle accounting, or proper oneshot/NO_HZ
support; do not discard CPU0 timekeeping or active-task accounting.


## Timer slowdown resolved: full integration pass

Linux commit `d4c654aa713c` supplies the shared 1 MHz clock as Linux's initial
clocksource, preserves its conversion scale at registration, and uses one-shot
clockevents with CONFIG_NO_HZ_IDLE. Only armed ONESHOT devices dispatch.
Stopped, detached and shutdown states cannot enter the legacy periodic loop.
Generic Linux code owns timekeeping and idle accounting; the experimental
manual idle-tick coalescing approach was not retained.

T4 commit `d9a8fd1` adds bounded adaptive idle waits: values 0/1 preserve the
old 1 ms behavior, 2..50000 request microseconds, larger values clamp to 50 ms.
The mutex-protected pending-IPI check and condition wait prevent lost wakes.
CPU0 keeps 1 ms waits for peripheral input. APs request at most 50 ms and
shorten that request for an armed timer deadline. Host scheduling and long
kernel sections can still delay dispatch; timer delivery remains polled.

The host test passes default/maximum/clamped waits, an already-pending IPI,
and 128 races across store/CAS/add/exchange. The kernel target selftest passes
wraparound, exact expiry, consume-once, callback rearming, disarming and
per-device CPU selection. Legacy non-SMP timer/process objects compile too.

### Measured behavior

Counters were read out of band, with no hot-path printk. The baseline sample
covers 60.36--90.64 seconds; the final sample covers 60.39--90.74 seconds.
They show the execution behavior during each boot, not matched application
phases or calibrated hardware performance. Handler time counts completed
batches; snapshots briefly pause the emulator.

| CPU | Baseline completed timer events | Baseline handler seconds | Final completed timer events | Final idle-wait seconds |
| --- | ---: | ---: | ---: | ---: |
| 1 | 3348 | 29.290 | 0 | 27.084 |
| 2 | 2870 | 27.243 | 0 | 29.398 |
| 3 | 2064 | 28.798 | 0 | 29.438 |
| 4 | 2080 | 28.890 | 0 | 29.437 |
| 5 | 288 | 5.142 | 0 | 29.468 |

Baseline AP backlog grew into hundreds/thousands of ticks and repeatedly hit
the 16-tick cap. The final AP counters remain stopped while CPU0 continues
servicing active-task timers. Adaptive waits also remove most of the shared
memory traffic previously spent returning to a one-millisecond idle wait.

### One-boot validation

The complete six-CPU, 32 MiB run passes:

- Timer deadline, atomic/futex, protection and cross-CPU callback selftests.
- Six bound workers released together, with all 768 allocations verified.
- Static pthread and concurrent malloc/stdio stress.
- epoll, POSIX timers and protected signal delivery during CPU-bound work.
- Distinct FP/MOVE2D state in two threads, including nested handlers and returns.
- All six /proc/cpuinfo entries.
- Two-context CoreMark: 4,000 iterations, correct CRC validation,
  387.063040 seconds, 10.334234 iterations/second in this diagnostic run.
- Desktop rendering, Files opening, and return to the desktop; framebuffer
  equality outside the clock region plus visual inspection of all three images.

Userspace remains pinned to CPU0. The CoreMark result is not a six-core speedup.
The full run completes in roughly seven minutes; the old periodic candidate
was still in libc stress when its 900-second deadline expired.

Root commit `363cbf29` fixes the integration reader to consume proc files
through EOF and adds TP_TEST_SMP_CPUINFO. The prior 160-byte read truncated
CPU enumeration. A rebuilt fixture initially linked a pre-signal syscall stub;
that artifact failed sigaction and was corrected to use the current protected
stub before the passing run. No kernel workaround or assertion relaxation was
used. Evidence for that fixture-only failure is in `fixture-stale-stub/`.

Final evidence directory:
`logs/compiler-coordination/six-core-linux-2026-11-19/`.

- `timer-baseline/measure.jsonl`: periodic baseline counters.
- `validation-full.log`: final counters and integration assertions (exit 0).
- `timer-full/play/uart.log`: complete guest results.
- `timer-full/play/{desktop-before,files-open,desktop-restored}/spine.bmp`:
  visually inspected framebuffers.
- `build-init.sh`, `build-full.sh`, `run-full.sh`: exact local reproduction scripts.
- `init-full/`: rebuilt integration init program with the current syscall stub.

Final T4 SHA256:
`2b9bc8ef28a0873a1f1673c3c27f43b032a5f1205f1edff1968a6bb4e43a2675`.
Final BTL SHA256:
`e51d9d34f26a201b1411013dc95ea28f4ba44a0b3660da6b2c1a698297e156e6`.

A parameterized copy of the passing harness is committed as
`transputer/tests/test_smp_timer_desktop.py`; run it under xvfb-run with
--t4, --kernel, --btl, --map, --net and --output paths. Its initramfs must contain
the libc/event-time tests, TP_TEST_SMP_CPUINFO, two-context CoreMark and fbwm.


## Protected userspace on all six CPUs

The next milestone removes the CPU0 affinity restriction. T4 commit `24d1495`
makes RODS and PMI staging CPU-owned, including same-host-thread CPU rebinding;
4,000 concurrent staged installs/trap returns and reset isolation pass. T4
`bf375a1` adds opt-in fault-only diagnostics (`T4_PROTECT_FAULTS=1`).

Linux now keeps synchronous launch continuations, stack snapshots and BusyBox
image-cache ownership with the Linux task. New threads start with no launcher
state. Shared-kernel launches use the Linux-owned path instead of hardware RUNP;
launching while the calling image has multiple threads returns EBUSY. Inactive
kernel tasks clear stale RODS/PMI mappings at the scheduler boundary. Affinity
and getcpu syscalls are available through the protected frame ABI.

An unpinned libc run exposed a CPU0 ABI publication bug: setup_arch published
before t9k_hard_p was parsed, and exec on another CPU did not refresh CPU0's
private low-RAM publication word. A later migration to CPU0 selected the legacy
C-call gate and faulted on the shared-board CPU-ID register. CPU0 now republishes
in smp_prepare_cpus; secondary CPUs publish their own gate before coming online.

Test commit `90e7cba5` visits CPUs 0 through 5 twice, checking affinity readback,
getcpu and errno preservation. Recurring signal workers run on CPUs 4/5 and
nested signal workers on CPUs 2/3, with distinct FP/MOVE2D images. Under SMP,
periodic deliveries could outpace the snapshot loop; diagnostics showed zero
unfilled slots rather than changed FP values. An acknowledgment now requires a
complete snapshot before the next counted delivery. All 16 snapshots and both
nested returns still undergo exact comparisons. The userspace-ack run passes
libc, migration, epoll, timers and all these signal-state checks; it was stopped
after starting CoreMark to replace the benchmark clock.

SMP CoreMark uses CLOCK_MONOTONIC because LDTIMER epochs belong to individual
CPUs and its main thread can migrate. The test pins each worker to a distinct
CPU, checks affinity and CPU identity, starts workers through a barrier, and
verifies thread-targeted periodic timer signals. These are diagnostic timings
including timer and syscall overhead, not calibrated T9000 hardware scores.

Fresh standalone CoreMark images are separate from Linux bFLT applications:

| Image | Iterations | Seconds | Iterations/second | Validation |
| --- | ---: | ---: | ---: | --- |
| coremark-t800.btl | 1100 | 14.887744 | 73.886279 | Correct operation validated |
| coremark-t9000.btl | 2000 | 17.372736 | 115.122914 | Correct operation validated |

Both live in `logs/compiler-coordination/coremark-btl-2026-11-19/`, beside ELF,
map and UART logs. Root commit `7af727e9` selects the correct emulator CPU and
rejects unsuccessful runs. T800 SHA256:
`ddf9734d3e81a8b5bdf04ee87f7f65536cb719c64c676a285c900d262bea3ead`.
T9000 SHA256:
`49116dbca689a8fc902f434c1dea8b551c6f3fbc1c895d6a512c1a5a19716f93`.


The 500-iteration-per-worker SMP attempt was stopped after read-only RAM
inspection confirmed both workers had passed their barrier, were receiving
signals with no identity/CPU errors, and had advancing CRCs. CPU0 tick handling
consumed most of that worker's elapsed time. A temporary host reference map
confirmed the sampled CRC sequence represented progress, not a deadlock.
Final integration uses 100 iterations per worker, retaining CoreMark's normal
CRC and minimum-ten-second validation; expected final CRC is 988c per worker.
The build script's general multi-thread default remains 500. The interrupted
500-iteration run is not a completed benchmark result.


### Bounded integration results

In the `userspace-100` 32 MiB boot, two pinned CoreMark workers completed 100
iterations each in 145.178816 seconds (1.377611 iterations/second total).
CPU0/TID77 received 576 timer signals; CPU1/TID78 received 557. Both reported
bad=0/error=0, all standard CRC checks passed, and final CRCs were 988c.
The synchronous child returned successfully and the same boot then launched
the six-worker image.


### Read-only shared-board controller path

Six-worker progress snapshots showed all workers ready, distinct TIDs, zero
signal errors and advancing CRCs, but high contention. Controller magic,
CPU identity/count and clock epoch are immutable after initialization, before
CPU threads start. These reads now bypass the RAM mutex. Guest RAM, atomic
operations and idle/IPI publication keep the original locking protocol.

The six-thread host regression checks identity, monotonic time, ordinary RAM
and exact shared atomic totals. Median times for 100,000 loops per thread:

| Workload | Before ms | After ms | Ratio |
| --- | ---: | ---: | ---: |
| Controller reads only | 129.396 | 8.032 | 16.1x |
| RAM/atomic work every 64 loops | 155.685 | 22.909 | 6.8x |
| RAM/atomic work every loop | 259.235 | 171.690 | 1.51x |

The existing idle regression also passes all 128 publication races, bounded
50 ms waits and immediate return for an already-pending IPI. Host evidence is
in `readonly-host-results.txt` and `test-board-readonly.sh` in the six-core
report directory.

With the identical frozen Linux image and 100 iterations per worker, the
candidate's two-core guest run passes in 90.400192 seconds, or 2.212385
iterations/second: about 60.6% higher throughput than the completed baseline
pair. CPU0/TID77 received 295 signals and CPU1/TID78 received 353, both with
zero errors and final CRC 988c. These are individual diagnostic runs, not a
statistical hardware benchmark. The interrupted six-core baseline is not
reported as a completed performance result.


The same fast-read boot completed six workers: 600 total iterations in
470.825600 seconds (1.274357 iterations/second). CPUs 0..5 used TIDs 79..84,
received 1825/1828/1807/1701/1718/1653 timer signals, and all reported
bad=0/error=0 with final CRC 988c. Six-worker throughput remains below the
pair in this serialized functional emulator; this is a correctness milestone,
not evidence of hardware SMP scaling.

Validated commits: Linux `968d83e4ab17` (protected task/syscall state),
T4 `0662c43` (controller reads), root `893fe6fb` (CoreMark SMP stress mode).

That boot reached the desktop and launched Files, but did not pass the full
GUI harness: the capture occurred during Files' first clear, and the quit
marker timed out. TFILES_START precedes rendering. Investigation also found
an actual input ownership race: Linux's unopened input devices drained the
same UART FIFOs as legacy framebuffer applications, and the emulator FIFO
indices were unsynchronized across host CPU threads.

The follow-up gives each Linux input device its own timer, armed on first
open and synchronously stopped on last close. T4 keyboard/mouse FIFOs use
separate mutexes; 512,000 ordered bytes, overflow/empty behavior, escape strings
and competing consumers pass host tests. The GUI harness now waits for a
stable full Files frame (large title and pane regions), rather than accepting
any changed desktop pixels. Its predicate accepts the earlier complete Files
capture and rejects the failed partial clear. A guest evdev key-read/close
probe precedes the later raw-UART desktop checks to test ownership transfer.


The first input follow-up failed its new evdev close check. Bounded diagnostics
proved that input delivery and event copying completed, then the close callback
called an unresolved `del_timer_sync` symbol. Linux 7 exports
`timer_delete_sync`; the driver now uses that API. The validation build rejects
undefined/unresolved-symbol warnings before boot. All temporary diagnostics
were removed, including the generic evdev edits. The corrected candidate has
ELF SHA256 `7edf91151d2e397b5242ecc78d8626782f37f714626341cc92514936f6f55b44`
and BTL SHA256 `d1b4f0f943c4679a1400534c0c15318ec78730fbb1b3472c410f8cfa48bdcbc2`.
Evidence is under `six-core-linux-2026-11-19/userspace-input-fixed/` and
`validation-userspace-input-fixed.log`.


### Completed protected userspace integration

The corrected `userspace-input-fixed` boot passed the complete harness (exit0)
with six online Linux CPUs sharing 32MiB. One uninterrupted boot passed:

- pthread creation, isolated errno, `/dev/null` FD_CLOEXEC semantics and the
  multi-thread synchronous-spawn rejection;
- concurrent malloc/stdio stress, affinity migration through CPUs0..5 twice,
  epoll, POSIX timers and timeslice signal delivery;
- recurring and nested FP/MOVE2D handler state checks in two protected threads;
- an evdev key press and synchronous device close, followed later by raw UART
  input in Files and the desktop;
- both pinned CoreMark configurations with100 iterations per worker and all
  standard CRCs plus final CRC988c;
- full Files rendering, quit to desktop, clock-masked pixel equality and
  desktop exit back to the launcher (all six `/proc/cpuinfo` entries).

| Workers | Total iterations | Shared monotonic seconds | Iterations/sec |
| --- | ---: | ---: | ---: |
| 2 (CPUs0,1) | 200 | 115.150272 | 1.736861 |
| 6 (CPUs0..5) | 600 | 781.122880 | 0.768125 |

Two-worker TIDs77/78 received452/424 signals. Six-worker TIDs79..84 received
3053/3036/3027/3010/2937/2966 signals. Every worker reported bad=0/error=0.
The host was heavily loaded during this run. Together with the serialized
shared-memory emulator and significant timer-handler time, this means these
numbers establish correctness, not physical T9000 scaling or a reproducible
performance regression. Six-worker throughput is lower, not a speedup.

Saved framebuffer SHA256 values (raw XRGB, clock included):

- desktop-before: `0859bf0f8ef7b7fa269f0d67644dbb77f6f57f5fa8aca5967ff782c9c1ac616e`
- files-open: `edb20409225a07a78423736a3696a7903fde3ff64cd00b1e867312c7c2f2c303`
- desktop-restored: `c174e6448dea504e5c4515e495af5b88f27dfffb8a317c81ab0afce63a5008dd`

Files and restored desktop BMPs were visually inspected. The raw desktop hashes
differ because the clock advances; the harness compares all other pixels.
The running harness had the older final text saying "two-worker"; its explicit
assertions and UART verify both configurations. That status text is corrected.

Validated follow-up commits: Linux `748d5f9e0d7f` (input-device ownership),
T4 `160a6a4` (synchronized input FIFOs). Frozen emulator SHA256:
`2f9be89f6b9aff2b08f14fd431fcc9e2b2687e70476c0c7bda7ba0c0b1c67cd4`.
The final kernel and emulator contain no temporary diagnostic instrumentation.
This validates the opt-in extended T4 SMP platform; it does not establish
strict memory isolation, a full POSIX/NPTL port, or hardware SMP support on an
unmodified physical T9000.


## Shared RAM contention reduction

T4 commit `89509e4` partitions common kernel RAM locking into 256 stripes
indexed by 4 KiB page. Board RAM, control registers, IPI publication and idle
waits retain their existing mutex. Unaligned words spanning pages lock both
stripes in index order. Atomic commands lock their target stripe while holding
the board mutex; ordinary kernel accesses never take the board mutex. No RAM
access or instruction fetch bypasses synchronization. Linux and the kernel BTL
are unchanged from the completed protected-userspace integration above.

Opt-in `T4_BOARD_PROFILE=1` records per-host-thread counts and samples mutex
acquisition time every 1,024 acquisitions. A bounded baseline run completed
2-worker CoreMark (86.435776 seconds, 2.313857 iterations/sec), then collected
six-worker contention samples before deliberate interruption. It is not a
completed six-worker baseline score. The earlier full six-worker runs remain
the completed pre-change evidence; their timings vary with host load.

Evidence: `logs/compiler-coordination/smp-contention-2026-11-19/` contains
build/run scripts, frozen baseline source, binaries, UART/framebuffer logs,
raw timer counters, host tests and extracted profile-window JSON. In selected
steady six-worker windows:

- Ordinary RAM accounted for 99.85–99.87% of baseline mutex acquisitions.
- Sampled mean mutex acquisition time ranged from 480–863 ns across CPUs with
  the global lock, versus 132–215 ns with partitioned locks. Sampling includes
  clock overhead and host scheduling; it is not exact CPU utilization.
- Average timer-handler duration in baseline seconds 244.92–275.50 was
  18.40/15.86/23.36/17.26/17.83/18.58 ms for CPUs 0..5. Candidate seconds
  155.13–185.90 measured 8.88/5.13/4.60/4.72/4.42/4.67 ms. Both windows were
  within six-worker execution. Tick rate, timer-signal interval and kernel
  algorithms were unchanged.

The candidate completed the full harness in one shared 32 MiB boot with all
six CPUs online, libc stress, migration, epoll, POSIX timer signals, recurring
and nested FP/MOVE2D preservation, evdev close and raw-input desktop/Files.

| Workers | Total iterations | Seconds | Iterations/sec |
| --- | ---: | ---: | ---: |
| 2 | 200 | 47.731264 | 4.190126 |
| 6 | 600 | 213.194752 | 2.814328 |

The paired two-worker profile improved throughput by about 81%. The new
six-worker result is also faster than the earlier completed global-lock runs
(470.825600 and 781.122880 seconds), but those were not controlled repeated
benchmarks. Six-worker throughput still trails two-worker throughput.

Two-worker TIDs 76/77 received 188/169 signals. Six-worker TIDs 78..83 received
832/791/805/780/801/804 signals. Every worker reported bad=0/error=0 and all
standard CRCs plus final CRC988c passed. The GUI harness exited successfully;
Files and restored-desktop images were inspected. Clock-masked desktop pixels
match. Raw framebuffer hashes:

- desktop-before: `0859bf0f8ef7b7fa269f0d67644dbb77f6f57f5fa8aca5967ff782c9c1ac616e`
- files-open: `edb20409225a07a78423736a3696a7903fde3ff64cd00b1e867312c7c2f2c303`
- desktop-restored: `a70d763806f0d7d7a04d3818b170f637e3359cb99e6966b35c2d7f012cb9e0e7`

Host regressions pass with profiling disabled: concurrent ordinary RAM and
atomic operations, exact shared-counter totals, cross-page words including
stripe-index wrap, byte writes, failed CAS, invalid commands and 128 IPI
wake races. The deliberately contentious host microbenchmarks are correctness
stressors, not representative speedups: their atomic-heavy or shared-word
traffic can be neutral or slower with the additional stripe lock.

Frozen baseline emulator SHA256:
`fc5e69703de0336c682823b2b7041971d88acaecac52967d63eda9ab7da7d509`.
Validated candidate emulator SHA256:
`5f9911d9d6f8cd1fb835fa742254ff1dbde1d4c7ecf4be082e7043ac479228e9`.
Kernel BTL SHA256:
`d1b4f0f943c4679a1400534c0c15318ec78730fbb1b3472c410f8cfa48bdcbc2`.
