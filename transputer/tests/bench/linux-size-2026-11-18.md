# TransputerOS Linux 7 size checkpoint, 18 November 2026

The baseline was Linux `862f42ce8a46` with the full 1,638,912-byte rootfs.
Measurements use a fixed TinyCC toolchain, build stamp and T4 T806 emulator.
Run `python3 transputer/tests/bench/linux-size.py ELF BTL ROOTFS.cpio` to
measure initialized load bytes, zero fill, BTL overhead and rootfs members.
The baseline BTL contained 5,237,920 initialized bytes, 221,540 zero-fill
bytes and 21,412 bytes of BTL framing.

| Build | BTL bytes | Comparison |
| --- | ---: | --- |
| Full rootfs, uncompressed baseline | 5,480,872 | Baseline |
| Slim rootfs, uncompressed | 5,251,625 | 229,247 fewer than baseline |
| Slim rootfs, gzip | 4,442,384 | 809,241 fewer than uncompressed slim |
| Slim rootfs, LZ4 | 4,513,253 | 70,869 more than gzip |
| Slim gzip, socket busy polling disabled | 4,425,871 | 16,513 fewer than slim gzip |
| Slim gzip, symbolic errno names also disabled | 4,424,393 | 1,478 fewer than previous row |
| Full gzip, with both config changes | 4,651,906 | 828,966 fewer than baseline |
| Full gzip, networked default with 32 KiB printk ring | 4,244,812 | 1,236,060 fewer than baseline |
| Slim gzip, offline profile before the log buffer change | 3,890,059 | 534,334 fewer than networked slim, same log buffer |
| Slim gzip, networked, 32 KiB printk ring | 4,017,300 | 407,093 fewer than networked slim with a 128 KiB ring |
| Slim gzip, offline, 32 KiB printk ring | 3,482,964 | 534,336 fewer than networked slim with the same ring |
| Slim gzip, networked, corrected bootstrap and omitted final zero tail, 128 KiB ring | 4,201,976 | 222,417 fewer than the same ELF's zero-filled BTL |
| Slim gzip, offline, 32 KiB ring, omitted final zero tail | 3,364,304 | 2,116,568 fewer than baseline |

The slim staging option, `TPOS_SLIM_PACKAGES=1`, omits the two duplicate Doom
and CoreMark package archives when the corresponding executables are staged.
It reduces the raw rootfs to 1,410,560 bytes. Keep the original packages on SD
when offline reinstall is needed. The offline test mounted an ext2 SD card
containing those packages and opened its read/write test file successfully.
It retained local sockets. Apply `tpos_offline.config` after loading
`tpos_defconfig` using the same `ARCH=transputer` and toolchain arguments.

Gzip needs the native alignment of `__initramfs_size` after the compressed
stream. LZ4 also needs the Transputer `memmove` inline under TinyCC. Single
host runs reached the desktop in 1.42 s for uncompressed slim, 1.90 s for gzip
and 1.25 s for LZ4. These timings include emulator startup and polling.
The kernel printed 20,928 KiB, 21,716 KiB and 21,648 KiB available,
respectively. Peak target memory was not measured.

## Effective Kconfig

Keep `EXPERT` to expose other size controls. Its Kconfig rule selects
`DEBUG_KERNEL`, so setting that symbol to off in the defconfig had no effect.
`DEBUG_MISC`, debug info and debugfs are already disabled. An expert
Transputer prompt now lets the defconfig turn off `NET_RX_BUSY_POLL`, which
previously had a hidden on default. Turning off `SYMBOLIC_ERRNAME` keeps
numeric errno values and drops names. A 32 KiB printk ring reduces the
reported data and metadata allocation from 540,672 to 135,168 bytes and
retains fewer old messages under sustained logging. UART still reported the
protection self-test, BusyBox directory probes and desktop startup.
Removing repeated RAM and framebuffer settings left both effective configs
byte-for-byte identical.

## Compiler and linker measurements

Across seven Linux objects, the existing Transputer inliner produced 210,781
text bytes with its 32-token cap. A 24-token cap produced 211,043 bytes,
a 48-token cap produced 211,952, and disabling inlining produced 219,395.
TinyCC's `-Os` currently
has no Transputer-specific size policy. A useful next test is to price
inline expansion after folding constant arguments and accounting for argument
staging and call bytes. Measure the linked BTL as ICF changes the outcome.

Relinks of the same LZ4 object set produced 4,513,253 BTL bytes as built,
4,814,140 with ICF off and 4,615,347 with call relaxation off. The ordinary
link audited all 81,346 call relocations with zero mismatches. The probe
changed one option per relink. Generic dead-section elimination still
needs explicit roots for the entry and assembly references, initcalls,
exception tables and their linker-generated boundaries.

## Boot and userspace checks

The networked, offline and reduced-tail images reached `DESKTOP_LOOP` and
the validated desktop captures matched the baseline 800×600 framebuffer
byte for byte. The earlier log ring and the new ring both passed the
BusyBox directory probes and the protection self-test. The final networked
and offline strict links checked 81,341 and 67,716 call relocations
respectively, with zero mismatches. Host TAP networking and a physical SPINE
boot were outside this validation.

The dirty-RAM tests filled the exact linked BSS range with `0xa5` before
loading the BTL. The original clear loop inverted the `CJ` condition and
stalled at early startup. The rebuilt kernel with the corrected condition
cleared that memory and reached the same desktop. Use
`transputer-netload vmlinux vmlinux.btl --omit-zero-tail` with a bootstrap
that initializes its omitted tail, such as this corrected Linux kernel.
The existing two-argument invocation still includes the zero-fill bytes.

Anonymous `mmap2` uses `alloc_pages_exact`. The current `munmap` returns
success without freeing. A two-page probe through the real syscall trap
returned success from `munmap` and received a distinct, zeroed page on the
next allocation. Reclaim needs exact extent ownership and lifetime
tracking across exec, nested spawn, cached BusyBox and shared mappings. The
current allocation bitmap is global and the spawn scratch table records
buddy orders rather than exact lengths. Both require changes before freeing
pages on a syscall or process exit.

With only `/init` rebuilt using `TP_USE_T9K_SYSCALL=1`, the emulator recorded
155 P-to-L syscall traps and the system reached the same desktop. A second
trial also relinked BusyBox with that stub. The original uClibc archive
failed the strict link with duplicate DNS resolver symbols. A scratch
archive retained the resolver object that already exported all the symbols
of the two redundant archive members. BusyBox then linked and passed its
directory probes. Its syscall site accounted for 128 additional hardware
traps. A trial with a newly linked window manager recorded two further
traps at the window manager's syscall site and produced the reference
desktop pixels. Running `/bin/tpsh -c 'echo TRAP_TPSH_OK'` returned zero and
recorded six traps at the shell's syscall site. The original uClibc archive
and the syscall default are unchanged. Installed application packages
still need independent trials.
