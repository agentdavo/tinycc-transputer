# Linux 7 fresh BTL build and boot profile

Linux `5e4e55dbba5d` (7.4.0-rc1) was exported into an empty Ubuntu WSL
directory and rebuilt with the TinyCC `7f76c18f` validation compiler and
`tpos_defconfig`. The Kbuild wrappers selected `-mt9000` and used the
compiler's builtin header directory. The build embedded the retained
`transputer/rootfs/output/rootfs.cpio` (1,638,912 bytes, SHA-256
`78b5042eefba26b57302991090413ddb9fbebfd85a4912a7273135912e1e7cd1`).
The compiler binary SHA-256 was
`c0cc5d43dc0699961abaee9534a458e8b1d6c31cda55e9a0f8e69327eb8a9311`.

`transputer-netload` produced a 5,472,777-byte `vmlinux.btl` (SHA-256
`1e059bc234e78659b504c6ea3507c51a1c766e4a0174c94f9a4f74d9ccb1b8e7`).
The decoder reconstructed the single ELF load segment exactly, including
221,532 zero-fill bytes. The 21,379 chunks deliver 5,451,396 bytes starting
at MemStart and ELF entry `0x80000070`. The embedded initramfs accounts for
about 30% of the BTL. The image uses the emulator's zero-length bootstrap.

The ordinary and instrumented runs used T4 `-s806 -sm 25 -ss` with
`T4_PROTECT=1`. Both passed the protection self-test, entered `/init` via
`goprot`, and reached `DESKTOP_LOOP`. The host stopped each run at that
marker. The boot exercised the existing userspace and T4's hard-P behaviour.

## Boot instruction counts

The profiler recorded 42,435,462 decoded instructions in the kernel address
range, grouped by the linked `System.map`. These are counts from a single
profiled startup, on the retained initramfs with framebuffer text enabled.

| Kernel code | Decoded instructions | Share |
| --- | ---: | ---: |
| `fbt_clear_row` and `fbt_glyph` | 8,892,717 | 21.0% |
| `memset` | 3,464,654 | 8.2% |
| `blake2s_compress_generic` and `chacha_permute` | 3,753,690 | 8.8% |
| bFLT `tp_flat_decode_ldc`, `tp_flat_encode_ldc`, and `tp_flat_ldc_len` | 2,675,124 | 6.3% |

`LDL`, `STL`, and `LDLP` account for 17,963,155 instructions (42.3%).
`fbt_clear_row` alone executes 1.52 million `LDL` for 1.28 million `STNL`.
For a kernel-specific trial, measure wider fills on aligned ordinary memory
and keep short, unaligned and volatile accesses covered. For a backend trial,
the BLAKE2s and ChaCha integer loops give a workload for reducing workspace
traffic beyond the framebuffer path. The bFLT helpers show a separate cost
in decoding and re-encoding prefix chains at process startup.

The compatibility link emitted 35 undefined-symbol relocation warnings,
including 12 call references. Examples include `lock_mm_and_find_vma`,
`do_ipv6_getsockopt`, and section boundaries such as `_stext` and `_etext`.
The desktop boot covers a subset of the linked code. Resolve or account for
these sites before using the image for broader kernel acceptance.
