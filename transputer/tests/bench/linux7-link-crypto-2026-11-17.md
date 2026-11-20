# Linux 7 strict link and crypto workspace profile

The Linux 7.4-rc1 `tpos_defconfig` image builds with
`T4_STRICT_UNRESOLVED_CALLS=1`. The linker uses actual input sections for
the kernel bounds and lays out init records together. Linux removes references
to disabled features at preprocessing time and supplies the Transputer
`cpuinfo` iterator and TinyCC's out-of-line diagnostic guards.

The compatibility build recorded in the [earlier boot profile](linux7-fresh-2026-11-17.md)
reported 35 undefined symbols. The strict-link baseline and optimized build
reported zero. The optimized call audit checked 81,427 relocations with zero
mismatches. Both builds used the retained initramfs and reached `DESKTOP_LOOP`
in T4's `-s806` mode with hard-P enabled and the protection self-test passing.
Kbuild still emitted its three existing modpost section-mismatch warnings.

## Crypto profile

BLAKE2s expands the ten round permutations as constant workspace offsets.
ChaCha copies its state into a local word array with `MOVE`, then copies the
result back and explicitly wipes the temporary. Both use TinyCC's paired-word
rotate lowering within their round functions. The standard C paths remain
available with compilers that lack the new builtins.

The two sets of kernel objects were also linked into the same bare-metal
known-answer workload. It exercises empty, short, block-boundary, and keyed
multi-block BLAKE2s inputs, and ChaCha12, ChaCha20, and HChaCha20. Both sets
passed on T4 in T9000 mode. The final sources also passed the same vectors
when compiled without the new builtins. The rotate regression passed on the T414, T425,
T800, and T9000 at two text layouts.

| Routine | `LDL` + `STL` + `LDLP` | Decoded instructions | Modeled T800 cycles |
| --- | ---: | ---: | ---: |
| BLAKE2s compression | 42,266 → 25,938 (−38.6%) | 79,500 → 45,206 (−43.1%) | 269,494 → 123,660 (−54.1%) |
| ChaCha permutation | 11,171 → 5,154 (−53.9%) | 24,554 → 10,131 (−58.7%) | 71,025 → 29,060 (−59.1%) |

In the profiled desktop boot the two functions executed 836,915 fewer
workspace instructions combined. The paired boot ran 43,565,772 → 42,226,699
decoded kernel instructions (−3.1%). The estimates use emulator opcode
histograms and the T800 timing model.

Unrolling increases the BLAKE2s compression routine from 1,392 to 6,146
bytes in its object. The ChaCha permutation shrinks from 1,219 to 692 bytes.
The final BTL is 5,472,584 bytes, 193 bytes smaller than the strict baseline,
and reconstructs its ELF load segment exactly. Its SHA-256 is
`3581c69b7f142945a8540183f1d0cf2d7c92c98e5d5832dfed652339253ff96a`.
