# Loop-pool surviving-reference ranking trial, 2026-11-06

**Rejected:** a bounded trial ranked pool candidates by the number of distinct
recorded LDC sites that survived code generation, rather than committing the
first candidates to the available slots. It found a real, scheduled-code
admission bias in a constructed loop, but the matched Doom, CoreMark and
raytracer workloads produced byte-identical ELFs and no measured benefit.
Static site counts also cannot establish dynamic loop frequencies, so the
trial was not merged.

The baseline is source commit `3783a69a`. The baseline compiler SHA-256 was
`bb3af9bbf03257dae0e08e9fdcdd8045d65c767b21f16247f6aa658da31cb983`;
the trial compiler was
`d1cf834cf8d098edfe350ad782c0cec657b3148af72353628a2550a0287828fc`.
Both used the same runtime archives and T4 binary described in
[the integer-to-FPU report](itof-direct-2026-11-06.md). T4's T800 timing
model is an estimate, not a hardware measurement.

## Reproducer

Compile the following function with `-mt800 -c`, once with `TP_POOL=32` and
once with `TP_POOL=64`, plus `TP_POOL_DEBUG=1`. Disassemble `bias` with
`python3 transputer/scripts/tpdis.py object.o bias`. Normal scheduling is on.
The volatile gate prevents the six early branch bodies from being folded
away; the seventh constant has four static use sites. This constructs a
static preference, not a measurement of branch frequency.

```c
volatile unsigned gate;
__attribute__((noinline)) unsigned bias(unsigned n, unsigned x) {
    unsigned i;
    for (i = 0; i < n; ++i) {
        if (gate == 1) x ^= 0x123;
        if (gate == 2) x ^= 0x234;
        if (gate == 3) x ^= 0x345;
        if (gate == 4) x ^= 0x456;
        if (gate == 5) x ^= 0x567;
        if (gate == 6) x ^= 0x678;
        x ^= 0x789; x += 0x789; x ^= 0x789; x += 0x789;
    }
    return x;
}
```

With reservation 32, the baseline reported five initializers and five
rewritten sites; four in-loop `ldc 0x789` instructions remained. With
reservation 64, six initializers and six sites were selected, and the same
four loads remained. The trial reported five initializers/eight surviving
sites at 32 bytes, and six/nine at 64 bytes: `0x789` was pooled and an early
one-use constant was left unpooled. The capacity and reservation were not
increased. The trial retained the existing preallocated slot lifetime,
preheader dominance checks, and VLA/`loc` rewind guard; it introduced no
speculative program-memory loads.

Both compilers passed the constructed loop (including a zero-trip case and
all seven gate values), `test_loop_pool`, `test_loop_pool_goto` (goto into a
loop and Duff's device), and `test_vla` on T414, T425, T800, T805, T9000 and
T9000TVX. Their UART output matched. The T805 used the T800 emulator model;
the T9000TVX run used the T9000 model and did not exercise TVX instructions.

## Matched workloads

Doom's T800 phase cycles and hashes were unchanged, including the built-in
`check 0 0`. The fixed-seed, one-iteration CoreMark diagnostic was unchanged:
5,008,259 estimated cycles for T800 and 3,319,286 for T9000-generated code
on the T800 timing model, with seed/list/matrix/state CRCs
`e9f5/e714/1fd7/8e3a`. These are not official CoreMark scores.

The one-node 256×256 model-3 raytracer produced the same reference image,
SHA-256 `672e73749ea0ee27c3e452fe0dbd558dbc23cdae61741b399557f7f1a26fdbb2`,
and 3,654,480,420 modeled cycles and 1,175,888,819 modeled instructions on
both sides. The Doom ELF, both CoreMark ELFs, and the raytracer ELF were each
byte-identical to their corresponding baseline. The Doom harness emitted
unresolved-symbol warnings for code outside the exercised phases, as noted in
the integer-to-FPU report.

A future policy needs evidence about surviving *dynamic* use, preheader and
frame cost, and a way to coordinate with the other loop slot/cache analyses.
It must preserve zero-trip behavior, goto/Duff dominance, VLA slot lifetime,
and protected-mode fault behavior; counting static references alone is not
sufficient to claim a workload win.
