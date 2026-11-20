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

## Exact-commit acceptance rerun

The exact integration parent `f792e7555da9929dc4a4eff9bffdb8ea0d9378d5` and commit `681cbe5126ac06d86c66d06c791ab744005d68cf` were built in separate snapshots. The source change between them is the indexed floating-point address-reuse rule and its tests/report. This rerun supersedes the earlier Doom warning qualification: it uses the hardened `cabe1fbf` harness, with strict unresolved-call mode and a final link that rejects every diagnostic. Both links completed without diagnostics; the extracted Doom stubs and phase validation restrict the claim to the tested benchmark paths.

| Check | Exact parent | Exact commit |
| --- | ---: | ---: |
| Ray model 3 instructions | 1,175,834,141 | 1,173,588,893 |
| Ray model 3 modeled T800 cycles | 3,028,967,767 | 3,019,986,771 |
| Fixed-seed CoreMark modeled T800 cycles | 5,008,259 | 5,008,259 |

Both variants matched all four checked-in PPM images at one and four nodes. CoreMark seed and result CRCs were `e9f5`, `e714`, `1fd7`, `8e3a`, `e714` for both. All eleven hardened Doom phase instruction counts, modeled cycles and hashes matched, with `check 0 0`; parent and candidate Doom BTLs were byte-identical. The hardened harness's four host tests passed. The candidate's positive/volatile/cast/alias code-generation test, numerical T4 fixture, scheduler branch-target/relocation/stack-depth tests and existing indirect-read scheduler test passed; differential seeds 329 and 367 passed at both layout offsets 0 and 1027. The ray numbers use the instruction-histogram model; CoreMark and Doom use the T4 T800 counter. These remain estimates, not hardware timings.

SHA-256 fingerprints for this rerun:

| Input or artifact | SHA-256 |
| --- | --- |
| Parent compiler | `f331fc27260c7a3f06c3e2ea57885539ff97864aea294b87f6610c9705a17a43` |
| Commit compiler | `db8c4a7b2590f5d58b9f19a202ec663ea838df0c6329f116ff84b36cbb937f33` |
| Parent / commit `transputer-gen.c` | `987a374e1753d38ca7d351d8330806f5760aa64cfc67c88e03999b16f3d16b8c` / `ad9895fecd3371a89c07f2ea15ec80a2b0aa62e0acc240298c2776555c57a1ba` |
| T800 runtime / netloader | `dbb6d39c4e13aec053eea8fbae2d550e1dbc64bc3cfb1b8b6fa5b006f3169e06` / `5f827f450a4993804b6ac99345f8d2dc702ef0226d10e5e233550420692f7a8d` |
| T4 timing counter / histogram build | `2c5972c20329d9730726ebea9ed966263c86f275c22ae4fa7c90f765dc1ce884` / `8197dd23757d2ae364c62a354dc06943b73a6c93da5849903e0a16e8b05639a6` |
| Parent / commit model-3 histogram | `7683a4d1a921a08eded562619a8fbc0ac9a2cef45f3146696beff5bb37c76df2` / `abe62099430bac92bdd574650d6da1a00315c205a377c67c34880125dd159560` |
| Parent / commit one-node ray BTL | `38e34fb2f5e76b3abaad306fdb0e90ff147b248051acd742c2ac95031a3896ed` / `0df06a3e49dcffd8a605e40ffda91affec3584a83e8996e2988e48e75d06a396` |
| Parent / commit four-node ray BTL | `3ce8f0e9dc808b94a1e3058b7522c54a2237fa18acd58de441601a0d740fca1e` / `c415d5d96998b9ecdb08822497989fa48e256152c972af649882e28590ccb925` |
| CoreMark BTL, both | `4abc8c9ea48f7bcc03eca5a6197f1ef9a21d11abc151653cb9e8e37706ef842b` |
| Hardened Doom BTL, both | `ea11150be7a92d6e9d88997582de794fc62d4cf664a19d925d2cf84626ad7377` |
| Doom harness / benchmark source | `3fe8f26f1aed6cca5c4b9d6af3c1ca80c5ae8807b8301f57976a4f2a86f00d74` / `c076040ab2012605bdeb471fb67da234855bee21cabfc92c482d3db5bba13e52` |
| Ray-tracer source / build script | `c8dfed2efaadd5d1d3d36c8635bd8358ba554453c70c4123b8d1ec2abde8d535` / `622730b8b5c90d84b1127466a20ff4578a784ec0ebd5b478fe68ed86bb00f5d3` |

The exact snapshots, executable artifacts, output logs and histograms are retained in `.tmp-rayaccept` for independent inspection. This optimization does not resolve the separate source-loop-to-`MOVE` bounds, alias and escape proofs.
