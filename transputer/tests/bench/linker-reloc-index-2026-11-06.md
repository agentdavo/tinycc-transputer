# Relocation index link profile

The baseline source was `1dbe953a`, after the no-candidate relaxation fast
path. A `-pg -fno-inline` diagnostic compiler linked the existing Linux
`vmlinux.o` 20 times. Its sampled profile attributed 0.34 of 1.21 CPU seconds
to `tp_resolve_reloc_symbol`, called 150,518 times per link. Those samples are
for identifying work, not the timings below. The input's type-1 and type-4
relocations contained about 134,000 entries and 58,700 distinct symbol/type
pairs. Name hashing was repeated during relocation repair even though the
index had already hashed every symbol.

The new index retains each symbol's bucket and skips the scan when that
bucket contains only the referenced symbol. Such a scan cannot select a
different definition. Relocation repair policy and ordering are unchanged;
debug mode still runs the original scan so its diagnostics remain intact.
An instrumented candidate took the shortcut 47,055 times for this link.
The extra array costs one `unsigned` per symbol: about 444 KiB for this
113,712-symbol Linux object, plus allocator overhead.

Both compilers were built with GCC `-O2` from the same source snapshot; the
candidate changed only the linker index. Linux input and outputs were copied
to a private Linux temporary directory. Three batches of 30 alternating
baseline/candidate links (`-mt9000 -nostdlib`) had median wall times:

| Batch | Baseline | Candidate | Change |
|---|---:|---:|---:|
| 1 | 0.13243 s | 0.13043 s | -1.51% |
| 2 | 0.13247 s | 0.13021 s | -1.70% |
| 3 | 0.13196 s | 0.13093 s | -0.78% |

These are short links and the absolute saving is roughly 1–2 ms, not an
end-to-end kernel build measurement. On a 160-marked-section synthetic link,
24 alternating runs had medians 0.00392 and 0.00415 s; on the existing
48-unmarked-section, 48 MiB case, five had medians 0.2569 and 0.2606 s.
Neither is evidence of a gain on those stress cases. A hash-only candidate
without the single-symbol shortcut also showed no Linux improvement.

Linux, both synthetic links, and matched Doom, CoreMark and raytracer links
produced byte-identical ELFs and diagnostics. The workload comparisons used
the same compiled objects and output path; relocation listings matched too.
The Linux and marked-section links also matched with `T4_RELOC_FIX_DEBUG=1`
and `T4_STRICT_RELOCS=1`. A partial-link output and its relocations matched.
`test_link_reloc_cache.sh` compares fast and debug paths on six targets with
unique and duplicate local names, function pointers and a partial link.
The ELF relocation suite and T800 link-relaxation, duplicate-local,
extern/static, cross-file and assembler-addend UART tests passed for both
compilers. These checks do not establish that all possible objects are safe.

| Artifact | SHA-256 |
|---|---|
| Baseline compiler | `4e8687db6dff2676b70fd5e83a05fbe4e67641a3a7d07e2520836e96a60fdd56` |
| Candidate compiler | `3c3f6f7817e558103f3fbe52daef83f7adec10ceb02baa642c3aa527d6e0e863` |
| Linux input | `ae9a0bb5d55d887587f7530f9566cae91d98a2fed5dd07c97a2e09c186290454` |
| Marked-section stress input | `b03bead8f91f744ffaa620f96adfe52f123e03b821bb49ec22e88db9b991689d` |
| Doom ELF | `bab2d36c097c3c612e44138cf8fb2b5856307d61b35aa25357ea8342f1074d2e` |
| CoreMark ELF | `7cadd1b51f46d4da334fac8ffa7a67db6cfcc7e490adcbd5346888e6da7588bc` |
| Raytracer ELF | `d87e4f93473ba25f37267478edb4e7d13467e9391aec2427c55f92abf58275ba` |
