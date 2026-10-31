#!/usr/bin/env bash
# Run ffn_cube.c -DC104 (scheme 1) on 8 T9000s and three IMS C104s, as one
# threaded T4 network, and print node 0's phase timeline next to the
# crossbar model and the hypercube's measured times (cube.sh 1).
#
# Each node's Link1-3 goes to its own 8-port C104 (routers 8, 9, 10), which
# routes on the first header byte - the destination node - and deletes it.
#
# usage: bash transputer/tests/net/c104.sh     (from the checkout root)
# env:   TCC_DIR, T4_BIN, WORK, NET (2), PROFILE (tvx3), LINKMBPS (9),
#        LINKLAT (20), C104LAT (20), VXFER (1; 0: a process per transfer),
#        SPLIT (3: virtual links per peer and plane), DIRECT (0; 1: one-step
#        all-gather to all seven peers), OVERLAP (0; 1: with DIRECT, compute
#        in NCH (4) chunks, each exchanged while the next is computed),
#        KEEP (1: QGEMMs on an unchanged vector set the keep-activations bit)
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
R=${TCC_DIR:-$ROOT}
T=$ROOT/transputer/tests
T4=${T4_BIN:-$ROOT/transputer/t4/t4}
WORK=${WORK:-$(mktemp -d)}
N=8
mkdir -p "$WORK" && cd "$WORK" || exit 1

{
    echo "Connect HOST to processor 0 link 0"
    for p in 1 2 3; do
        r=$((N + p - 1))
        echo "router $r links $N"
        for n in $(seq 0 $((N - 1))); do
            echo "Connect processor $n link $p to router $r link $n"
            echo "router $r interval $n $((n + 1)) $n"
        done
        echo "router $r delete 0-$((N - 1))"
    done
} > c104.map
for n in $(seq 0 $((N - 1))); do
    v=0; [ $n -eq 0 ] && v=1
    "$R/transputer-tcc" -mt9000tvx -static -nostdlib -I"$R/include" -I"$T" -DNODE=$n -DVERIFY=$v \
        -DSCHEME=1 -DC104=1 -DVXFER=${VXFER:-1} -DSPLIT=${SPLIT:-3} -DDIRECT=${DIRECT:-0} -DOVERLAP=${OVERLAP:-0} -DNCH=${NCH:-4} -DKEEP=${KEEP:-1} -o n$n.elf "$T/test_boot.S" "$HERE/ffn_cube.c" "$R/lib/transputer-conc.c" \
        "$R/transputer-libtcc1.a" 2>/dev/null &&
    "$R/transputer-netload" n$n.elf n$n.btl >/dev/null 2>&1 || { echo "node $n: build failed"; exit 1; }
done
export SPYNET=c104.map T4_NODE_BTL=n%d.btl
export T4_T9000=1 T4_T9K_MARKS=1 T4_T9K_NET=${NET:-2} T4_T9K_PROFILE=${PROFILE:-tvx3}
export T4_T9K_LINKMBPS=${LINKMBPS:-9} T4_T9K_LINKLAT=${LINKLAT:-20} T4_T9K_C104LAT=${C104LAT:-20}
rm -f c104.uart
env -u DISPLAY T4_UART_CONSOLE=0 T4_UART_LOG=c104.uart \
    timeout 900 "$T4" -s806 -sm 26 -ss -st -sn -$N -sb n0.btl > c104.out 2>&1 &
PID=$!
for i in $(seq 1 1800); do
    grep -qa 'T9000-MARK\] n0 VERIFY' c104.out 2>/dev/null && break
    kill -0 $PID 2>/dev/null || break
    sleep 0.5
done
kill $PID 2>/dev/null

mark() { grep -a "T9000-MARK\] n0 $1 " c104.out | head -n 1 | sed 's/.*cycles=\([0-9]*\).*/\1/'; }
s=$(mark Sstart); gu=$(mark gateup); hh=$(mark h); dn=$(mark down); dd=$(mark done)
if [ -n "$s" ] && [ -n "$dd" ]; then
    awk -v s=$s -v gu=$gu -v hh=$hh -v dn=$dn -v dd=$dd -v mb=$T4_T9K_LINKMBPS \
        -v lat=$T4_T9K_LINKLAT -v rl=$T4_T9K_C104LAT 'BEGIN {
        r = mb / 50                             # bytes per cycle
        hop = 2 * lat + rl                      # node -> C104 -> node
        # two crossbar steps: one piece per plane, then 4/3 of a piece per
        # plane (ffn_cube.c allgather_c104).  "link rate": bytes alone.
        p_h = (1 + 4 / 3) * 288 * 4 / r + 2 * hop
        p_y = (1 + 4 / 3) * 640 * 4 / r + 2 * hop
        # "VCP": each virtual link waits for every 32-byte packet to be
        # acknowledged - data (2-byte header + EOM/EOP) one way, a 3-byte
        # acknowledge back, both through the switch
        pkt = (35 + 3) / r + 2 * hop
        v_h = (int((288 * 4 + 31) / 32) + int((4 * 288 * 4 / 3 + 31) / 32)) * pkt
        v_y = (int((640 * 4 + 31) / 32) + int((4 * 640 * 4 / 3 + 31) / 32)) * pkt
        printf "scheme 1D on 8 nodes and 3 C104s, links %d MB/s; node 0, cycles:\n", mb
        printf "  %-30s %9s %9s %9s %9s\n", "phase", "measured", "VCP", "link rate", "hypercube"
        printf "  %-30s %9d\n", "gate+up compute", gu - s
        printf "  %-30s %9d %9d %9d %9d\n", "SwiGLU + all-gather h", hh - gu, v_h, p_h, 45171
        printf "  %-30s %9d\n", "quantise h + down compute", dn - hh
        printf "  %-30s %9d %9d %9d %9d\n", "all-gather y (8 nodes)", dd - dn, v_y, p_y, 99766
        printf "  %-30s %9d  = %.2f ms (hypercube 6.73 ms)\n", "FFN total", dd - s, (dd - s) / 50000
    }'
fi
grep -a 'T9000-MARK\] n0 VERIFY' c104.out | sed 's/.*n0 \(VERIFY [A-Z]*\).*/\1/' || true
grep -aq 'T9000-MARK\] n0 VERIFY' c104.out || { echo "no verdict"; grep -a -E "^-[EW]-" c104.out | sort | uniq -c | head; }
