# Shared-board SMP lock tests

Run `bash transputer/tests/smp-locks/run.sh` from a TinyCC workspace with T4
shared-board support. TCC, NETLOAD, T4_BIN and SMP_TEST_OUT can override the
compiler, BTL packer, emulator and artifact directory.

This compiles the production Linux `arch/transputer/kernel/smp_locks.c` and
boots its contended spinlock, read/write lock, CAS and subword-exchange tests
on four T9000 nodes. Readers verify a two-word invariant updated by writers.
Adjacent bytes and halfwords share physical words, exercising masked CAS
retries. Private atomic values and both initial IRQ-mask states are checked.

The minimal include shims supply types, compiler barriers, BUG reporting and
an emulated local IRQ flag for this freestanding test. They do not emulate
real interrupt delivery. The production kernel supplies its own IRQ helpers;
the separately validated SMP-enabled Linux boot tests those paths on CPU0.
Secondaries remain outside the Linux scheduler in the current kernel port.

Success requires LOCK-PASS and no LOCK-BUG/LOCK-FAIL marker. Workers park
with STOPP; timeout cleanup is expected and may need a final kill. An exit
code alone never counts as a passing test.
