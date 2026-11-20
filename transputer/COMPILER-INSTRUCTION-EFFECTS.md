# Transputer instruction effects — design

The scheduler, workspace-slot remapper and FPU history optimizations need
overlapping facts about instructions. A shared description can provide those
facts while each transformation continues to impose its own safety rules.
This is a design for incremental migration, not a new optimization pass.

## Description

An effect describes one decoded instruction and its operands in context.
It should contain:

* Minimum integer and FPU stack depths, maximum additional depth during
  execution or expansion, and the net stack changes. A sequence combines
  peak demand as well as net change.
* Workspace words read and written as intervals, including accesses through
  a known workspace-derived address. An access records its byte width,
  alignment and whether its address is known. Implicit call-frame reads and
  writes are explicit effects.
* Other memory reads and writes, with separate unknown-alias and volatile or
  device properties. A known workspace access does not make an unrelated
  indirect access safe.
* Control effects, including branch targets, relocations and instruction
  boundaries, and whether the instruction may trap, deschedule or access
  device, scheduler, channel or processor state.
* A conservative unknown result for instructions or address expressions the
  model cannot describe. Unknown effects stop transformations that need the
  missing fact.

Effects must describe the sequence actually emitted for the selected target.
For example, an operand that uses byte loads, a helper call or a scratch slot
has the combined requirements of those operations. Relocatable symbols retain
their symbol, addend and relocation provenance.

## Consumer rules

Each client asks for the properties it needs. Stack scheduling needs proven
depth and compatible ordering. Slot remapping needs complete explicit and
implicit workspace footprints. FPU movement additionally needs FPU-stack
effects and proof that integer-stack, memory and control effects commute.
No single “pure” flag is sufficient for all three decisions.

An instruction with a zero net stack change can still need more stack space
than is available. Likewise, a read-only operation may trap or access a
device. A transformation must explicitly prove both its stack capacity and
its ordering requirements.

## Regressions the model must prevent

* Packed argument staging (commit 0fd07e87) showed that treating an operand as
  a simple push based only on net stack change misses the peak depth and
  scratch use of its bytewise expansion. Sequence effects must include both
  before a call stager can keep other arguments on the stack.
* Workspace copy propagation (commit 5a5ef711) showed that LDLP s; LDNL d
  reads word s + d, even when a direct-load-only use scan sees no reader of
  that word. The footprint must include the indirect read or mark it unknown.

## Migration and verification

Start with read-only effect descriptions for a narrow, well-tested subset and
compare them against each current consumer. Add counterexamples for stack
peaks, indirect and partial workspace reads, calls, volatile/device access,
traps, branch targets and relocations. Unknowns must keep existing barriers.
Migrate one consumer at a time with byte and relocation comparisons, target
regressions and the workload checks required for any changed code generation.
