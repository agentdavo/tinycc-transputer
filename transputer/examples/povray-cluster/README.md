# Historical POV-Ray cluster

`run.sh` runs the existing POV-Ray 1.0 BTL on its original 16-node T800
configuration: a root/cache/collector and three chains of five workers. It
creates a separate output directory, supplies a self-contained scene, starts
one threaded emulator network, waits for normal exit, and validates the PPM
header and complete pixel-data length. Its `timeout` controls only that
emulator process; unlike the historical `pov` script, it never uses `pkill`.

Run it on Linux or WSL with the separate T4 checkout at `transputer/t4`.
Build that checkout's `t4` emulator and keep its historical POV-Ray assets
in place. The reference comparison also needs its `pictures.zip` archive.

```sh
bash transputer/examples/povray-cluster/run.sh
# Optional: WORK=/path/to/output TIMEOUT=180 SCENE=/path/to/scene.pov
# Bundled example: LEVEL=level1 SCENE_NAME=colors.pov bash transputer/examples/povray-cluster/run.sh
```

The script prints the image path, SHA-256, elapsed wall time, and host user
and system CPU times. `SCENE` is copied to the isolated run directory; custom
scenes must be self-contained. To render a bundled scene, set `LEVEL` to
`level1`, `level2`, or `level3` and `SCENE_NAME` to a `.pov` filename in
that directory. The launcher copies that level and the common include files
into its isolated directory, using short paths for the historical parser.
Some scenes also require assets in other locations or have unsupported
features. `T4_BIN` can select a different emulator.
The BTL and its network map come from `transputer/t4/examples/povray/povsrc/build`.

Compare a completed render against the bundled reference (requires Python 3):

```sh
python3 transputer/examples/povray-cluster/compare_reference.py /path/to/picture.ppm level2 cluster
```

It reports file hashes, byte equality, and the number of differing RGB pixels;
it exits nonzero if the image differs or cannot be read. Its PPM reader covers
the simple binary P6 headers used by the bundled files.

The BTL's collector has a fixed 640 × 480 image and waits for 480 scanlines;
requesting fewer lines can leave the network waiting forever, even if the
ray tracer finished its portion. The launcher therefore keeps that size.
The source's historical notes also report that antialiasing does not work.

## Example measurement

On 6 November 2026, three runs of `scene.pov` in WSL2 on an Intel Core Ultra
7 265 (20 logical CPUs) produced identical SHA-256
`c6118a84d48a498c6297d42fe97e26655034f393d28abf51ad55fb71219ad04e`:

| Run | Wall seconds | Host user seconds | Host system seconds |
| --- | ---: | ---: | ---: |
| 1 | 3.528 | 14.725 | 1.192 |
| 2 | 3.486 | 15.063 | 1.279 |
| 3 | 3.222 | 13.936 | 1.004 |

The median is 3.486 s, or about 88,100 output pixels/s for this scene,
including emulator startup, scene parsing, cluster communication, and PPM
output. Host CPU time is not transputer time. The 15 render workers divide
the 480 lines round-robin, 32 lines each; scene complexity and host scheduling
affect their workloads. The BTL fixes this network size, so these measurements
do not establish parallel speedup or scaling.

An additional `T4_EST_CYCLES=1` run produced the same image in 4.163 s and
printed 15 unlabeled cycle reports: 266.2–272.7 million estimated cycles each,
4.056 billion in total for the reported nodes. The log does not identify a
sixteenth report, so this is not a complete cluster cycle total or a
per-node attribution. T4's cycle model is an estimate, not cycle-accurate
hardware, and this mode changes guest `ldtimer` semantics. It should be
measured separately from ordinary elapsed time. The emulator binary used had
SHA-256 `2c5972c20329d9730726ebea9ed966263c86f275c22ae4fa7c90f765dc1ce884`.

The bundled `level1/colors.pov` scene, which uses `colors.inc` and many
spheres, completed in a single ordinary run in 20.473 s wall time, 288.386 s
host user time and 1.126 s host system time (roughly 14.1 host CPU cores on
average). Its PPM is byte-identical to `ppm/level1/colors.ppm` in the bundled
`pictures.zip`, with SHA-256
`2211bf594b887d749bf6ceb610337bcf4136abc8cf2434e010d40f3ae9954445`.
This one measurement is not a scaling study; it also illustrates how strongly
render time depends on the scene. The `level1`, `level2`, and `level3`
directories are scene collections, not the three worker pipelines. The
original batch scripts render their scene files sequentially, starting a
complete configured transputer network for each image.

## Level 2 and 3 reference scenes

On the same WSL2 machine and T4 binary, on 6 November 2026, each scene below
was run once, sequentially, using the 16-node BTL. Each output was 640 × 480
and **byte-identical** to its corresponding `pictures.zip` PPM (zero differing
RGB pixels). Mean host cores is `(user + system) / wall`; it describes host
CPU consumption, not transputer utilization or efficiency.

| Scene | Wall s | User s | System s | Mean host cores | Output pixels/s |
| --- | ---: | ---: | ---: | ---: | ---: |
| level2/cluster | 6.188 | 75.181 | 0.675 | 12.26 | 49,644 |
| level2/pawns | 77.657 | 1112.672 | 1.822 | 14.35 | 3,956 |
| level3/chess | 20.940 | 286.147 | 1.383 | 13.73 | 14,670 |
| level3/lamp | 12.673 | 170.619 | 0.810 | 13.53 | 24,241 |

These single runs include boot, parsing, link transfers and PPM writes. They
do not measure physical T800 hardware or scaling across different cluster
sizes. `pawns` took about 12.5 times the wall time of `cluster` for the same
pixel count; it uses detailed procedural wood textures, but this comparison
alone does not identify the cost of any particular rendering feature. The
level labels are scene collections, not a reliable ranking of render time.
