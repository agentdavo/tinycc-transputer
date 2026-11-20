# Transparent shared-memory board checkpoint - 19 November 2026

User-approved scope: transparent CPU loads/stores through an emulated board
with up to eight attachments, validating four T9000 CPUs and atomic operations
as a step towards SMP Linux.

## Commits and implementation

Nested T4 repository:

- `acf398a`: repair the T9000 regression runner's moved concurrency source path.
- `bfc321e`: eight-port shared RAM board, CPU memory-path integration, atomic
  control registers, guest/host tests and protocol documentation.

The board supplies 16 MiB at 0x40000000 and per-port control registers at
0x41000000. `T4_SHARED_BOARD=0:1,1:1,2:1,3:1` attaches four CPU address adapters;
normal byte/word and MOVE accesses reach shared RAM. Word CAS, fetch-add and
exchange serialize with ordinary accesses. SPYNET cable collisions, duplicate
CPU attachments and more than eight ports are rejected. The implementation
uses direct functional transactions, without serial-link timing or packets.

See [board protocol and limitations](../../t4/SHARED_BOARD.md).

## Acceptance

Built T4 from source with Makefile.linux in a fresh Ubuntu scratch directory.
Ran `tests/run_shared_board.sh` on four and eight T9000 CPUs (`-s9 -st`), each
with 32 MiB private RAM plus the shared 16 MiB board. Both printed
`BOARD-ALL-PASS`. Each CPU performed 1000 fetch-adds, 1000 successful CAS
increments and 1000 exchange-lock-protected increments, producing 4000 or
8000 in each respective counter. The same guest verified cross-CPU data
publication, byte/unaligned word accesses, failed CAS, and 24-byte
local-to-shared, shared-to-shared and shared-to-local MOVE copies.

Host checks passed for the last RAM word, unsigned addition wraparound,
invalid commands and targets, eight-port limit, bad/duplicate/colliding
attachments and disconnected-node rejection. Non-threaded board startup was
also rejected explicitly.

With the board disabled, all 21 existing `run_t9000_vcp.sh` tests passed,
covering VCP/C104/control links, protected accesses and stack extension,
ALT/timers, timeslices, boot links and shared-link wakeup. Idle CPU checks also
passed (0.34s/1.5s, 0.36s/0.8s, 0.17s/0.6s). The initial regression attempt
could not build tests referencing the old `lib/transputer-conc.c` location;
`acf398a` fixes the path, and the complete rerun passed.

Evidence: `logs/compiler-coordination/shared-board-2026-11-19/` contains
`build.log`, `validation.log`, `regression.log`, guest BTL, per-node-count UART
logs and the candidate emulator. Test source and runner are committed in T4.

## Remaining work

This validates functional shared-memory transactions on bare-metal T9000
guests. It does not boot an SMP Linux kernel. Follow-up work is shared physical
memory/boot setup, secondary CPU entry, per-CPU scheduler/workspace state,
IPIs, local serialization of atomic command staging, architecture atomics and
barriers, and protected mappings. The board is data-only; invalid memory
accesses terminate the emulator rather than raising a guest bus fault.
Serial-link bandwidth/latency and failure behavior remain unmodelled, so these
results support correctness claims rather than hardware speedup claims.
The independent-node Linux/POV-Ray worker service and 1/2/4-node render scaling
remain pending.
