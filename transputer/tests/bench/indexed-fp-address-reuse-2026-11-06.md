# Indexed floating-point address reuse

Compiler baseline: `e75ac2a2a55a7ad35f640b8c1e7dfb9b68d21789`. A model-three, 256x256, one-node instruction histogram showed three sites in `ellipsoidSect` that computed the same indexed workspace address twice. Together these sites ran 2,245,248 times. After the scheduler carries the store address, the original sequence is:

```
ldl index; ldlp array; wsub; ldl index; ldlp array; fpldnlsni
```

The new rule uses `dup; fpldnlsn` in place of the final three instructions. The emitter records proof only for a direct, nonvolatile local integer with an active matching symbol whose address has not been taken. A cast can erase a volatile qualifier, so the declared symbol is checked as well as the expression. The scheduler checks that both original reads have this proof, the intervening staging store does not overwrite the index, and neither instruction interval crosses a branch target or pinned relocation. The rule is disabled for inline assembly and T414.

| Workload | Baseline | Candidate | Correctness |
| --- | ---: | ---: | --- |
| Ray model 3, one node, modeled cycles | 3,028,967,767 | 3,019,986,771 | Reference image matched; 8,980,996 fewer cycles (0.2965%) |
| Ray model 3, executed instructions | 1,175,834,141 | 1,173,588,893 | 2,245,248 fewer instructions |
| CoreMark, one iteration, modeled cycles | 5,008,259 | 5,008,259 | Reference CRCs matched |
| Doom measured phases | unchanged | unchanged | All phase hashes and instruction/cycle counts matched |

All four reference PPMs matched byte for byte at one and four nodes. The histogram uses the T800 timing table and an instrumented T4 based on revision `78e77a9`; these are modeled cycles, not a hardware measurement. The Doom benchmark emitted undefined-symbol warnings for unexercised code, limiting its conclusion to the measured phases. One-iteration CoreMark is a diagnostic, not a published score.

The positive and volatile-index, volatile-cast and volatile-alias fixtures were checked in disassembly and on T4. A host scheduler test exercises the proof, interior branch-target and pinned-relocation barriers. The `00182` pointer regression passed, as did 20 differential seeds (300–319) and seeds 329 and 367, each at layouts 0 and 1027. Clean candidate ray, CoreMark and Doom BTLs matched the measured prototype's binaries.
