#!/usr/bin/env bash
# Differential testing of transputer-tcc against host gcc.
#
# For each seed, gen.py writes a random program with no undefined behaviour.
# Host gcc builds it twice - -O0 under UBSan (any report means the generator
# slipped, so the seed is set aside) and -O2 (must agree) - and its output
# becomes the expected output.  The c-testsuite runner then builds and runs it
# on T4 once per layout in PADS (TP_LAYOUT_PAD), so bugs that depend on where
# code and data land show up too.  Failing programs, their expected output and
# the runner's diff go to $WORK/fail; a failure can drop straight into
# tests/single-exec as a new numbered test.
#
# usage: difftest.sh [COUNT] [FIRST_SEED]        (run in WSL/Linux)
# env:   WORK (~/tccfuzz), PADS ("0 1027"), GEN_FLAGS (e.g. --no-float),
#        TCC_BIN, NETLOAD_BIN, T4_BIN (passed to the runner)
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUNNER="$HERE/../runners/single-exec/tcc-transputer"
COUNT=${1:-50}
FIRST=${2:-1}
WORK=${WORK:-$HOME/tccfuzz}
PADS=${PADS:-0 1027}
mkdir -p "$WORK/fail" "$WORK/genub"
cd "$WORK" || exit 1

pass=0 fail=0 skip=0
for s in $(seq "$FIRST" $((FIRST + COUNT - 1))); do
    p=p$s.c
    python3 "$HERE/gen.py" "$s" ${GEN_FLAGS:-} > "$p"
    if ! gcc -std=gnu99 -O0 -w -fsanitize=undefined,float-cast-overflow \
             -fno-sanitize-recover=all -o h0 "$p" 2>/dev/null ||
       ! timeout 20 ./h0 > "$p.expected" 2>h0.err ||
       ! gcc -std=gnu99 -O2 -w -o h2 "$p" ||
       ! timeout 20 ./h2 > h2.out || ! cmp -s h2.out "$p.expected"; then
        cp "$p" h0.err genub/ 2>/dev/null
        skip=$((skip + 1))
        echo "seed $s: generator UB or host disagreement (genub/$p)"
        continue
    fi
    bad=""
    for pad in $PADS; do
        if ! TP_LAYOUT_PAD=$pad sh "$RUNNER" "$p" > r.out 2>&1; then
            bad="$bad $pad"
            cp r.out "fail/p$s.pad$pad.diff"
        fi
    done
    if [ -n "$bad" ]; then
        cp "$p" "$p.expected" fail/
        fail=$((fail + 1))
        echo "seed $s: MISMATCH at pad$bad"
    else
        pass=$((pass + 1))
        rm -f "$p" "$p.expected"
    fi
done
echo "difftest: $pass pass, $fail fail, $skip set aside (seeds $FIRST..$((FIRST + COUNT - 1)), pads: $PADS)"
[ "$fail" -eq 0 ]
