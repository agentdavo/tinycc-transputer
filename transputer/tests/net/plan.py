#!/usr/bin/env python3
"""plan.py - per-token decode latency of DeepSeek-V4.1-Flash (the IQ1_M file:
routed experts IQ1_S, dense weights Q5_K/Q8_0) on up to 1024 T9000-class
transputers with TVX, for several arrangements, link speeds and wire formats.

Everything per node is in T9000-model cycles at 50 MHz, calibrated on T4:
  - one routed IQ1_S expert (gate, up, SwiGLU, down) = 560.5K cycles
    (tests/net/moe_net.c, profile tvx3: 16 B/cycle weight read, 64 integer
    MAC lanes, 8 FP lanes, 16 KB activation buffer);
  - all dense matrices of one layer = 7.91M cycles (tests/bench/layer.sh);
  - a link moves bytes at its payload rate; cut-through forwarding adds one
    chunk per extra hop (tests/net/sim.sh, NET=2).
validate() replays the measured 7-node tree with the same constants.

Modelled, not measured: sparse attention compute (64 heads x top-512 tokens x
512-wide latent, int8 KV on the integer lanes), the DSA indexer scan, router
software, and C104 router latency.  The VCP's per-packet acknowledges through
the C104s (vcp_rate()) and the cost of starting a collective step
(step_lat(), msglat.sh) are measured.

usage: python3 plan.py [--ctx 4096] [--top 3]
"""
import argparse, math, random

MHZ = 50
LAYERS, ACTIVE, N_EXPERTS = 40, 6, 384
C_DENSE = 7.80e6          # one layer's dense matrices, one node   (measured,
                          #   tvx4, hc_fn in K chunks: bench/layer.sh)
C_EXPERT = 0.459e6        # one routed IQ1_S expert, one node: 0.5605M
                          #   measured at tvx3, x 0.819 for tvx4's ternary
                          #   lanes (layer.sh routed tensors 3.36M -> 2.75M)
C_HEAD = 28.4e6           # output head: 455 MB of Q5_K at 16 B/cycle
INT_LANES = 64
EXPERT_BYTES = 116e9      # routed experts in the IQ1_M file
DENSE_BYTES = 6.0e9       # everything else (dense, head, embeddings, Engram wkv)
KV_PER_TOKEN = 40e3       # <= one 512-wide bf16 latent per layer per token
RAM_MAX = 4 << 30         # 32-bit address space
LINKS = 4
O_MSG = 40                # cycles to start a collective step: one __vxfer
                          #   (tests/net/msglat.sh: 139 per step, 99 wire)
O_MSG_BS = 150            # the same on byte-stream links: a process each
POSTED = True             # VXFER outputs do not wait for the last ack
HOP = 25                  # cycles per C104 router hop (~0.5 us)
LINK_LAT = 20             # cycles per link (T4_T9K_LINKLAT)
VL_IN_FLIGHT = 3          # virtual links busy per link at once: ffn_cube.c
                          #   SPLIT=3, MemStart moved up for 18 of them
OVERLAP = True            # a stage's exchanges run beside its compute, in
                          #   chunks (ffn_cube.c OVERLAP: 5.17 -> 4.24 ms)
NCH_CHOICES = (1, 2, 4, 8)
O_CHUNK = 330             # cycles per extra chunk: the handoff to the
                          #   exchanging process and its VXFER; QGEMM's
                          #   keep-activations bit (tvx.h TVX_KEEP_ACTS)
                          #   spares the activation re-read
DIRECT = True             # a collective as one step to every peer at once
HOL = 1.04                # ... at this many times the link time, plus
P_PKT = 11.3              #   cycles per 32-byte packet on a link: the
                          #   acknowledges and packet handling of so many
                          #   messages at once (ffn_cube.c DIRECT y gather:
                          #   43.4K cycles at 9 MB/s, 6.4K at 90)
CHUNK = 1024              # forwarding granularity in software (bytes)
D, FF = 5120, 2304

def attn_cycles(ctx):
    """Sparse attention core plus the DSA indexer scan, one layer, one node.
    Indexer: 32 heads x 128 dims over the whole context, on ~8 of 40 layers."""
    core = 64 * min(512, ctx) * 512 * 2 / INT_LANES
    indexer = 32 * 128 * ctx / INT_LANES * (8 / LAYERS)
    return core + indexer

def xfer(nbytes, r, links=1):
    return nbytes / (r * links)

def vcp_rate(r):
    """Bytes per cycle a link gives through the C104s.  The T9000 VCP sends
    a virtual link's next 32-byte packet only when the last one is
    acknowledged: 2-byte header, data and end token one way, a 3-byte
    acknowledge back, each crossing a switch - so the virtual links in
    flight on a link share that round trip.  Measured on T4: one virtual
    link per cable through a C104 at 9 MB/s, 1.86-2.05x the bare link time
    (tests/net/c104.sh); three on one direct cable, 1.06x (vlbw.sh).  At
    90 MB/s c104.sh's all-gather moves 0.25 B/cycle a cable with one per
    peer (model 0.21) and 0.62 with three (model 0.64); six get no more."""
    rt = 38 / r + 2 * (2 * LINK_LAT + HOP)
    return min(r * 32 / 38, VL_IN_FLIGHT * 32 / rt)

def path(hops=3):
    """Cycles for a packet's head from node to node: `hops` C104s between
    hops + 1 links."""
    return (hops + 1) * LINK_LAT + hops * HOP

def step_lat(hops=3):
    """Fixed cost of one collective step through the C104s: starting it,
    then the head's path - and back again for the last acknowledge unless
    the outputs are posted.  msglat.sh, 4 bytes through one C104: 139
    cycles a step posted, 213 with a process per output."""
    return O_MSG + path(hops) * (1 if POSTED else 2)

RT_PATH = 1.4             # a stop-and-wait round trip: two packet times
                          #   and this many paths (direct_bw)

def direct_bw(nbytes, peers, rr, hops=3):
    """A DIRECT collective's link time: `nbytes` from a node to `peers` at
    once, split over the links, one virtual link per peer on each - at HOL
    times the link time plus P_PKT a packet, unless so few virtual links
    share a link that its stop-and-wait shows: then each takes a round trip
    a packet, two packet times (the acknowledge waits behind data packets)
    plus RT_PATH paths (it goes back at the header's arrival).  Fitted on
    ffn2d.sh, 3 x 3 grid, 2 peers, 1 hop: gate/up and y reduce-scatters
    of 4096 and 4552 bytes measured 10.7-11.5K and 12.1-13.4K cycles at
    9 MB/s over four runs (wire term 9.9K and 11.0K; this 10.9K and 12.1K),
    2.8K and 3.0K
    at 90 MB/s (wire 1.2K and 1.3K; this 2.8K and 3.1K).  ffn_cube's 7
    peers stay on the wire term at 9 and 90 MB/s."""
    if peers < 1 or nbytes <= 0:
        return 0.0
    pkts = nbytes / 32 / LINKS
    wire = nbytes / (LINKS * rr * 32 / 38) * HOL + pkts * P_PKT
    return max(wire, pkts / peers * (2 * 38 / rr + RT_PATH * path(hops)))

def stage_c104(comp, col, row, rr, a, b):
    """One matmul stage over the C104s: `comp` cycles of compute and its
    collectives - the input all-gathered in a column of a nodes (`col`
    bytes a node), partial sums reduce-scattered in a row of b (`row`) -
    over links of rr bytes/cycle.  Recursive doubling takes log2 steps at
    vcp_rate(); DIRECT sends to every peer at once, one step per
    collective with every virtual link in flight (direct_bw()).  With
    OVERLAP the stage goes in nch chunks, each one's exchange beside the
    next one's compute, for O_CHUNK per extra chunk.  Returns (time,
    exposed comm)."""
    best = None
    for direct in ((False, True) if DIRECT else (False,)):
        if direct:
            steps = (a > 1) + (b > 1)
            bw = direct_bw(col, a - 1, rr) + direct_bw(row, b - 1, rr)
        else:
            steps = math.ceil(math.log2(a)) + math.ceil(math.log2(b))
            bw = xfer(col + row, vcp_rate(rr), LINKS)
        for nch in (NCH_CHOICES if OVERLAP else (1,)):
            comm = bw + nch * steps * step_lat()
            t = (max(comp, comm) + min(comp, comm) / nch +
                 (nch - 1) * O_CHUNK)
            if best is None or t < best:
                best = t
    return best, best - comp

def allreduce_c104(vbytes, n, rr):
    """All-reduce over the C104s, links of rr bytes/cycle: the cheaper of
    reduce-scatter + all-gather by recursive doubling (2 log2 n steps, for
    big vectors) and radix-k doubling - each step one VXFER swapping the
    whole vector with the k-1 peers of a digit, ceil(log_k n) steps, every
    virtual link in flight; beyond one peer, single-packet messages meet
    less contention than DIRECT's (1.2x) but cost 35 cycles a peer
    (allred.sh, 8 nodes: radix 2 963/407 cycles at 9/90 MB/s, model
    933/363; radix 8 2084/490, model 2119/523).  The 96-byte hc mixes of a
    layer took 40% of its time at 1024 nodes as 20 steps; radix 2 with the
    whole vector takes 10."""
    best = allreduce(vbytes, n, vcp_rate(rr), LINKS, step_lat())
    if n <= 1 or not DIRECT:
        return best
    for k in (2, 4, 8, 16):
        steps = math.ceil(math.log(n) / math.log(k) - 1e-9)
        per = step_lat() + (k - 1) * vbytes / (LINKS * rr * 32 / 38) * (
            1.2 if k > 2 else 1.0) + (k - 2) * 35
        best = min(best, steps * per)
    return best

def allgather(vbytes, n, r, links, lat):
    """Every node ends with the whole vector; recursive doubling."""
    if n <= 1:
        return 0.0
    return xfer(vbytes * (1 - 1 / n), r, links) + math.ceil(math.log2(n)) * lat

def allreduce(vbytes, n, r, links, lat):
    if n <= 1:
        return 0.0
    return 2 * xfer(vbytes * (1 - 1 / n), r, links) + 2 * math.ceil(math.log2(n)) * lat

_maxload = {}
def exp_maxload(groups, balls=ACTIVE, trials=20000):
    """Expected busiest group when 6 experts land in `groups` groups."""
    if groups not in _maxload:
        rnd = random.Random(1)
        s = 0
        for _ in range(trials):
            c = [0] * groups
            for e in rnd.sample(range(N_EXPERTS), balls):
                c[e % groups] += 1
            s += max(c)
        _maxload[groups] = s / trials
    return _maxload[groups]

def ram_ok(gb):
    return gb * 1e9 <= RAM_MAX

# ---- arrangements ------------------------------------------------------

def full_1d(n, mbps, wb, ctx):
    """Every matrix of every layer row-split over all n nodes; C104 network
    (4 planes, any node to any in 3 router hops).  Each matmul's input is
    all-gathered to every node."""
    r = vcp_rate(mbps / MHZ)
    lat = step_lat()
    comp = (C_DENSE + ACTIVE * C_EXPERT + attn_cycles(ctx)) / n
    gathers = [D, 1280 + 512, 8192, D, (ACTIVE + 1) * FF]   # x, q_a+kv, out_a, x2, h
    comm = sum(allgather(v * wb, n, r, LINKS, lat) for v in gathers)
    comm += allreduce_c104(384 * 4, n, mbps / MHZ)          # router logits
    comm += 4 * allreduce_c104(24 * 4, n, mbps / MHZ)       # hc mixes, norms
    if n > 64:                                               # a head over n/64 nodes
        comm += allgather(512 * wb, n // 64, r, LINKS, lat)
    head = C_HEAD / n + allreduce_c104(64, n, mbps / MHZ)
    kv = KV_PER_TOKEN * ctx / max(1, n // 64)
    ram = (EXPERT_BYTES + DENSE_BYTES) / n + kv
    return LAYERS * (comp + comm) + head, comp, comm, ram / 1e9, "1D shard, C104"

def full_2d(n, mbps, wb, ctx, net):
    """Every matrix split over a logical a x b grid of all n nodes: each
    node's slice of the input is all-gathered within its column of a nodes,
    and partial sums are reduce-scattered within its row of b nodes, so a
    node moves k/b + m/a values per matmul instead of a whole vector.  b is
    capped so a node never splits a 256-weight quant block.
      net="c104":  any-to-any over 4 router planes, recursive doubling.
      net="torus": a sqrt(n) x sqrt(n) torus of direct links and no routers,
                   neighbour-to-neighbour rings using 2 links per dimension,
                   byte-stream, so no VCP acknowledges."""
    r = vcp_rate(mbps / MHZ) if net == "c104" else mbps / MHZ
    side = int(round(math.sqrt(n)))
    mats = (C_DENSE + ACTIVE * C_EXPERT) / n                 # weight-read bound
    comp = mats + attn_cycles(ctx) / n
    stages = [(D, 1280 + 512), (1280, 32768), (32768, 8192), (8192, D),
              (D, 384), (D, (ACTIVE + 1) * 2 * FF), ((ACTIVE + 1) * FF, D)]
    km = sum(k * m for k, m in stages)
    t = comp - mats                                          # attention core
    for k, m in stages:
        b = max(1, min(side, k // 256))
        a = max(1, n // b)
        col = (k / b) * (1 - 1 / a) * wb                     # input pieces
        row = (m / a) * (1 - 1 / b) * max(wb, 2)             # partial sums
        cs = mats * k * m / km                               # its matrices' share
        if net == "c104":
            t += stage_c104(cs, col, row, mbps / MHZ, a, b)[0]
        else:
            t += cs + xfer(col + row, r, 2) + ((a - 1) + (b - 1)) * (O_MSG_BS + LINK_LAT)
    # The hc mixes and norms reduce over the hidden state, and between
    # stages each row of b nodes holds all of it (the column pieces): a
    # row all-reduce, the same in every row, not one over all n.
    bx = max(1, min(side, D // 256))
    if net == "c104":
        t += 4 * allreduce_c104(24 * 4, bx, mbps / MHZ)
    else:
        t += 4 * allreduce(24 * 4, bx, r, 2, O_MSG_BS + LINK_LAT)
    head = C_HEAD / n
    kv = KV_PER_TOKEN * ctx / max(1, n // 64)
    ram = (EXPERT_BYTES + DENSE_BYTES) / n + kv
    return (LAYERS * t + head, comp, t - comp, ram / 1e9,
            "2D shard, " + ("C104" if net == "c104" else "torus, no routers"))

def role_split(n, mbps, wb, ctx, p_frac, stripe):
    """Dense nodes (1D over C104) plus expert nodes holding whole experts
    striped `stripe` ways; the 6 active experts run on their groups."""
    r = vcp_rate(mbps / MHZ)
    lat = step_lat()
    p = max(1, int(n * p_frac))
    e = n - p
    if e < stripe or e // stripe < 1:
        return None
    groups = e // stripe
    if not ram_ok(EXPERT_BYTES / e / 1e9 + 0.05):
        return None
    dense = (C_DENSE + attn_cycles(ctx)) / p
    expert = exp_maxload(groups) * C_EXPERT / stripe
    gathers = [D, 1280 + 512, 8192, D, FF]                   # dense side
    comm = sum(allgather(v * wb, p, r, LINKS, lat) for v in gathers)
    comm += allreduce_c104(384 * 4, p, mbps / MHZ) + 4 * allreduce_c104(96, p, mbps / MHZ)
    dispatch = xfer(D * 1.14, r, LINKS) + math.ceil(math.log(max(e, 2), 4)) * (lat + CHUNK / r)
    within = allgather(FF * wb, stripe, r, LINKS, lat)
    combine = xfer(D * wb, r, LINKS) + lat
    comp = dense + expert
    comm += dispatch + within + combine
    head = C_HEAD / p
    kv = KV_PER_TOKEN * ctx / max(1, p // 64)
    ram = max(EXPERT_BYTES / e, DENSE_BYTES / p + kv)
    return (LAYERS * (dense + expert + comm) + head, comp, comm, ram / 1e9,
            "role split %d dense + %d expert, stripe %d" % (p, e, stripe))

def validate():
    """The measured 7-node tree (sim.sh, CHUNK=1 REDUCE=1): 724.9K cycles."""
    r = 9 / MHZ
    bcast = xfer(20 * 292, r) + CHUNK / r                    # two hops, cut-through
    ret = xfer(D * 4, r) + CHUNK / r                         # f32 outputs, reduced
    pred = 1300 + bcast + 0.5605e6 + ret + 6 * 1800       # its expert ran at tvx3
    print("validation: 7-node MoE layer predicted %.0fK cycles, measured 724.9K (%+.1f%%)"
          % (pred / 1e3, 100 * (pred / 724.9e3 - 1)))

def upgrades(ctx):
    """The best arrangement after each step, at 256 and 1024 nodes: the
    software and emulator ones first, then hardware.  Compute is tvx4's."""
    keys = ("POSTED", "HOP", "VL_IN_FLIGHT", "OVERLAP", "DIRECT")
    g = globals()
    base = {k: g[k] for k in keys}
    old = dict(OVERLAP=False, DIRECT=False)
    rows = [("T9000: a vout per transfer, one VL per peer", 9,
             dict(old, POSTED=False, VL_IN_FLIGHT=1)),
            ("VXFER, outputs posted", 9, dict(old, VL_IN_FLIGHT=1)),
            ("+ 3 VLs per peer and cable", 9, old),
            ("+ direct collectives where they win", 9, dict(old, DIRECT=True)),
            ("+ exchanges beside compute, chunked (now)", 9, {}),
            ("links 4x (400 Mbit/s)", 36, {}),
            ("links 4x, 8 VLs in flight", 36, dict(VL_IN_FLIGHT=8)),
            ("links 4x, 8 VLs, router hop 25 -> 5 cycles", 36,
             dict(VL_IN_FLIGHT=8, HOP=5)),
            ("links 10x (1 Gbit/s), 8 VLs, hop 5", 90,
             dict(VL_IN_FLIGHT=8, HOP=5))]
    print("\n== upgrades, best arrangement, Q8 activations on the wire, tokens/s")
    print("%-52s %8s %8s" % ("", "256", "1024"))
    for name, mbps, over in rows:
        g.update(base)
        g.update(over)
        out = []
        for n in (256, 1024):
            out.append(max(MHZ * 1e6 / f[0] for f in (
                full_1d(n, mbps, 1.0625, ctx),
                full_2d(n, mbps, 1.0625, ctx, "c104"))))
        print("%-52s %8.2f %8.2f" % (name, out[0], out[1]))
    g.update(base)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ctx", type=int, default=4096)
    ap.add_argument("--top", type=int, default=3)
    a = ap.parse_args()
    validate()
    upgrades(a.ctx)
    print("context %d tokens; tokens/s at 50 MHz; RAM per node in GB (32-bit cap 4.3)" % a.ctx)
    wires = [("f32", 4), ("bf16", 2), ("q8", 1.0625)]
    for mbps in (9, 36, 90):
        print("\n== links %d MB/s each (%s), 4 per node" % (mbps,
              {9: "T9000 DS-link, 100 Mbit/s", 36: "4x DS-link",
               90: "1 Gbit/s serial"}[mbps]))
        print("%5s  %-44s %5s %7s %8s %8s %6s" % ("nodes", "arrangement", "wire",
              "tok/s", "comp/lyr", "comm/lyr", "RAM"))
        for n in (64, 128, 256, 512, 1024):
            res = []
            for wname, wb in wires:
                for t in (full_1d(n, mbps, wb, a.ctx),
                          full_2d(n, mbps, wb, a.ctx, "c104"),
                          full_2d(n, mbps, wb, a.ctx, "torus")):
                    if ram_ok(t[3]):
                        res.append((t, wname))
                for pf in (0.125, 0.25, 0.5):
                    for s in (1, 2, 4, 8, 16):
                        t = role_split(n, mbps, wb, a.ctx, pf, s)
                        if t and ram_ok(t[3]):
                            res.append((t, wname))
            res.sort(key=lambda x: x[0][0])
            for i, (t, wname) in enumerate(res[:a.top]):
                print("%5s  %-44s %5s %7.2f %7.0fK %7.0fK %6.2f" % (
                      n if i == 0 else "", t[4], wname, MHZ * 1e6 / t[0],
                      t[1] / 1e3, t[2] / 1e3, t[3]))

if __name__ == "__main__":
    main()
