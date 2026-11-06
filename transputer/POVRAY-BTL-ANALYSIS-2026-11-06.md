# POV-Ray BTL, 16-node array and compiler targets — 6 November 2026

The supplied `t4/examples/povray/povsrc/build/povray.btl` is a working
**16-node T800 image**, with one cache/host node and fifteen rendering nodes.
It is an INMOS ICC/toolset image. Its instruction profile is useful evidence
for compiler design, but a change to TinyCC cannot change this existing BTL.

## Configuration and building an array

The authoritative inputs for this binary are
[build/povray.cfs](t4/examples/povray/povsrc/build/povray.cfs) and
[build/povray.map](t4/examples/povray/povsrc/build/povray.map).
The similarly named configuration in `machine/transp` is a ten-node template;
it has three workers per chain rather than five.

```text
                           HOST
                            |
                         Root link 0
                          node 0
                link 1 /   | link 2   \ link 3
                      /    |           \
                     1     6           11
                     |     |            |
                     2     7           12
                     |     |            |
                     3     8           13
                     |     |            |
                     4     9           14
                     |     |            |
                     5    10           15
```

| Connection | Link at upstream node | Link at downstream node |
| --- | --- | --- |
| Host to root 0 | Host interface | Root 0 |
| Root 0 to nodes 1 / 6 / 11 | Root 1 / 2 / 3 respectively | Worker 0 |
| Each successive pair within a chain | Worker 1 | Next worker 0 |

These are bidirectional physical links. Worker links 2 and 3 are unused by
this configuration. A mesh or ring is not required. For a physical array,
the board memory map, host boot interface and actual link wiring must match
the configured target; emulator success alone does not validate a board's
electrical, reset or memory-controller setup.

This collected image reserves **4 MiB per processor**, or 64 MiB across the
array. This is distributed memory, not one shared 64 MiB address space.
The original README discusses smaller worker memories for other builds;
that does not make this particular image a 1 MiB-worker image. Per renderer,
the map budgets 185,380 bytes of linked code, 25,876 bytes of workspace and
3,880,960 bytes of heap. These are process budgets, not individual function
frames or measured peak usage. The terminal workers have extra `Dummy` code;
their highest mapped allocation leaves only 2,424 bytes below the 4 MiB
boundary. Increasing stacks/heaps requires recollecting a consistent image.

Root 0 runs `Cache` and three `FRead` processes at high priority. Each worker
runs a high-priority `SpMux` and a low-priority POV renderer. The last worker
in each chain also has `Dummy`, which terminates the forwarding protocol.
The file cache avoids fifteen independent copies of host file traffic.
Each worker still parses its scene and keeps its own rendering data.

The renderer assigns `worker_id=0..14`, `number_of_workers=15`, and renders
rows `First_Line + worker_id`, then every fifteenth row. At 480 rows, each
worker gets 32 rows. This is static interleaving, not a dynamic tile queue.
It balances the box scene well in instruction counts, but scenes with
concentrated expensive geometry can distribute work differently.

Changing node count means rebuilding the configuration, worker parameters
and boot image together. The original toolchain path is ICC/TCOFF objects,
`ilink`, `icconf`, then `icollect`. TinyCC ELF objects are not replacements
for the existing `.t8x` objects or their channel/process startup ABI.

## Reproduced render and safe emulator launch

The 640×480 `level1/box.pov` output is 921,615 bytes and is byte-identical to
`pictures.zip:ppm/level1/box.ppm`. Its SHA-256 is
`fcaeaccaea31d263bede7a2f8774e2e7af8dcfc6f640a893a3fd1d3b8d1d6db0`.
Both the regular emulator run and private profiled run produce these bytes.

Use an isolated working directory containing short relative `include` and
`scenes` links/copies, and this single-line `povray.def`:

```text
+linclude -lscenes -iscenes/box.pov -w640 -h480 -v -f +d
```

From that directory, with absolute `T4`, `MAP` and `BTL` paths:

```sh
env -u DISPLAY T4_HEADLESS=1 SPYNET="$MAP" \
  timeout -k 3 90 "$T4" -st -s8 -sn -16 -sm 22 -ss -se -sb "$BTL"
```

Use the threaded launcher. The historical `build/pov` script ends with a
global `pkill -9 -x t4`, which can kill unrelated emulator work.
The historical multiplexer fixes `MAXX=640`, `MAXY=480`, writes full rows
and waits for 480 pixel messages plus exit messages. Small dimensions or
partial renders require a coordinated source rebuild; they can hang this BTL.
The original port also warns that its antialiasing support is broken.

## Executed instruction evidence

The private profiler uses isolated T4 `2853b29`, disables instruction fusion,
and keeps separate counters for every node. This matters because different
nodes reuse the same instruction addresses. It records full decoded lengths
and operands. Counts are logical instructions, excluding prefix bytes as
separate instructions. They are neither elapsed-time speedups nor measured
hardware cycles. A second render reproduced every worker's instruction
total exactly; root counts vary slightly with timer/statistics output.

| Measured area, box scene | Executed instructions |
| --- | ---: |
| Root, all code | 164,458,293 |
| All 15 workers, all code | 1,129,791,030 |
| Workers, mapped renderer/library code | 1,124,326,281 |
| Workers, mapped `SpMux` code | 804,270 |
| Least / most renderer work on one worker | 74,493,006 / 75,228,760 |

The worker range differs by about 1%. This is an instruction-count balance
observation for one scene, not a demonstrated 15× speedup. Blocking time,
instruction latency, external RAM and link contention still need measurement.
Code ranges are taken from each node's map: terminal worker code is relocated
by `0x1058`, so applying node 1's ranges to every node misattributes work.
An earlier launch used the joined option `-sm22`, which T4 passes to guest
arguments instead of changing memory size. The table above uses corrected
4 MiB runs with `-sm 22`; earlier default-memory counts are superseded.

| Instruction, all worker code | Executions | Interpretation |
| --- | ---: | --- |
| `ldl` | 288,930,004 | Workspace reads; 25.57% of worker instructions |
| `stl` | 72,869,160 | Workspace writes, including parameter/result handling |
| `fpldnldb` | 67,932,304 | Double loads into the FPU |
| `fpstnldb` | 33,029,959 | Double stores from the FPU |
| `ldnlp` | 64,039,349 | Address displacement; already useful compact addressing |
| `call` | 21,291,104 | Procedure/runtime call overhead remains significant |
| `move` | 3,179,392 | Existing block copies; not all copies need new lowering |
| Completed double sqrt sequences | 637,023 | Native T800 first/five-step/last sequences |

The immediate pattern below executes **1,123,292 times**:

```text
ldlp slot
fpstnldb
ldlp slot
fpldnldb
```

There are fifteen such sites per worker, or 225 mapped instances. Terminal
relocation gives thirty distinct absolute PCs across the array. Of these
events, 637,023 are in the math-library sqrt return sequence, including the
nonterminal-worker site at `0x80033bd7`. This is a **post-sqrt** store/reload
associated with the existing library's return convention. Removing it needs
proof of the full caller/callee ABI and FPU-stack contract. It is distinct
from TinyCC's arithmetic spill **before** the sqrt builtin. The current
TinyCC experiment targets the latter and does not rewrite the ICC binary.

Root's `Cache` code accounts for 163,300,302 instructions. Four call targets
in the output chain execute 921,600 times each, exactly `640*480*3` RGB bytes.
Source `muxdemux.c` confirms three `fputc` calls per pixel after seeking to
the row's offset. The root blocks in `ProcAltList`; these counts do not
establish a busy-polling cache or prove that root is the critical path.
In a separate reference-matching run, T4's T800 model estimates 382,217,404
root cycles versus 256,063,224–259,279,661 per worker. This supports testing
root output as a possible serial limit, but excludes link waits, memory wait
states and other hardware effects. `ldl` plus `stl` account for 32.0% of worker
instructions and 84,828,256 additional executed prefix bytes.

## Improvement order and acceptance

1. **Keep a single-use FP arithmetic result in FA before builtin sqrt.**
   Reuse the backend's existing resident-value proof; preserve precision,
   exception/rounding operations, aliases and pending return values. This
   directly serves lengths, normalization and intersection discriminants.
2. **Reduce double argument and result staging.** Inspect aligned existing
   parameter storage before copying through integer slots. The measured ICC
   post-sqrt sequence motivates an ABI study, not deleting stores across
   arbitrary calls. Keep seed-329/367 alignment and nested-call coverage.
3. **Pack and buffer output rows.** Preserve `(pixel>>8/16/24)&255` channel
   order and row seeks, pack exactly 1,920 bytes, then use buffered block
   output. Raw `MOVE` from 32-bit packed pixels cannot produce 24-bit RGB.
   This should remove substantial byte-call overhead; measure the actual
   render and root/worker waiting time before claiming a speedup.
4. **Optimize strict-order double vectors and Horner loops.** Reuse proven
   addresses, make workspace selection reflect hot surviving references,
   and use existing fused load/add or load/multiply instructions. These are
   separate arithmetic operations, not a single-round FMA. Matrix output
   uses a temporary to support aliases; preserve it where required.
5. **Revisit scheduling only with harder scenes.** Static rows are already
   balanced for box. A dynamic tile queue needs routing, completion, output
   ordering and termination changes, not just another compiler switch.
   Preserve high-priority channel progress over low-priority rendering.

POV defaults `DBL` to double and tunes polynomial tolerances for IEEE64.
Keep operation order and precision. Approximate sqrt, reciprocal substitution
or reassociation needs a separate numerical contract and image investigation.
`level1`, `level2` and `level3` are scene collections, not quality levels.
Broaden acceptance to reflection/refraction, spheres, texture and polynomial
scenes after the first complete TinyCC image is available.

## TinyCC validation boundary and retained evidence

The generic full-source renderer has also been built with host GCC and
produces the same complete box reference bytes. All 35 generic/display
modules compile with the candidate Transputer compiler. Original vector
macros, sphere intersection and Horner evaluation pass a supplementary
baseline/candidate T800/T9000 executable. Its T800 fixture saves twelve
instructions and thirty modeled cycles, including harness overhead.

The chief then completed a genuine full-source TinyCC image fixture:
[runner and scope](tests/bench/povray/README.md). It rebuilds the generic
renderer and fdlibm, initializes libc and provides a bounded iserver file/
heap adapter. Baseline and candidate produce complete `box` and `basicvue`
images byte-identical to their packaged references. The existing libc
archive contains T9000 code, so these single-node 64 MiB emulator runs use
`-s9`; they do not establish a physical 4 MiB T800 application. The original
16-node runtime is an additional port beyond this numerical compiler gate.

The [sqrt acceptance report](tests/bench/fpsqrt-resident-2026-11-06.md)
records the paired instruction/size results and Doom, CoreMark and ray gates.
The full rebuilt box removes 504,889 double spill/reload pairs, saving
2,019,556 decoded instructions and 5,048,890 modeled T800 cycles. This is
about 0.0881% of that model's total, not a measured hardware speedup.

Procedural scenes need runtime compatibility as well as matching FP code.
Generic host `glass` differs from its packaged reference before any TinyCC
change. Source noise initialization uses `srand(0)`/`rand()`, while the BTL
contains a 55-word additive generator with two wrapping indices. The current
libc supplies a different sequence. Keep that port issue separate from the
accepted deterministic-scene compiler tests; do not relax pixel equality.

Input BTL SHA-256:
`261e056fa3bc03bb9f41454938225703e45f8645d8fff8bc402350eadbf4d831`.
Collector map SHA-256:
`6211f02b66a21b5f84e62b047bb3c86ec271f5570e579e569d5162c40a310901`.
Raw per-node histograms, map-based attribution, profiler source and rerun
scripts are retained in `logs/compiler-coordination/povray-deep/`.
Generic host/target build scripts and original-source math fixtures are in
`logs/compiler-coordination/pov-rebuild-audit/`. These are local audit
artifacts; production sources and the historical BTL were not changed.
