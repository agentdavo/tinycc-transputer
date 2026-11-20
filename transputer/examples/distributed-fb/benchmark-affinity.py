#!/usr/bin/env python3
"""Paired, interleaved T4 array timings with and without per-node affinity."""
import argparse
import json
import os
import re
import statistics
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
NODES = (1, 2, 4, 8, 16, 32)


def run_one(nodes, pinned, frames, output_root, cpu_list):
    env = os.environ.copy()
    env.pop("T4_CPU_PIN", None)
    env.pop("T4_NODE_CPUS", None)
    env.update(NODES=str(nodes), FRAMES=str(frames), FPS="0", CAPTURE="0", WORK=str(output_root))
    if pinned:
        env["T4_NODE_CPUS"] = cpu_list
    result = subprocess.run(["bash", str(HERE / "run-scale.sh")], env=env,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"{nodes} nodes ({'pinned' if pinned else 'unpinned'}):\n{result.stdout}")
    match = re.search(r"^SCALE OK: .*: (.+)$", result.stdout, re.MULTILINE)
    if not match:
        raise RuntimeError("missing run directory: " + result.stdout)
    run_dir = Path(match.group(1))
    uart = (run_dir / "scale.uart").read_text(errors="replace")
    samples = [(int(a, 16), int(b, 16)) for a, b in
               re.findall(r"SCALE FRAME ([0-9a-f]{8}) ([0-9a-f]{8})", uart)]
    if [n for n, _ in samples] != list(range(frames)):
        raise RuntimeError(f"missing or out-of-order frame timings: {run_dir}")
    log = (run_dir / "scale.out").read_text(errors="replace")
    mapping = [(int(a), int(b)) for a, b in
               re.findall(r"\[CPU\] node (\d+) pinned to logical CPU (\d+)", log)]
    if pinned and sorted(n for n, _ in mapping) != list(range(nodes)):
        raise RuntimeError(f"missing or duplicate affinity reports: {run_dir}")
    if not pinned and mapping:
        raise RuntimeError(f"unpinned run contains affinity reports: {run_dir}")
    return {"directory": str(run_dir), "median_ms": statistics.median(v for _, v in samples[1:]) * 0.064,
            "ticks": [v for _, v in samples], "mapping": sorted(mapping)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="existing or new directory for run data")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--frames", type=int, default=24)
    parser.add_argument("--cpus", default="auto", help="T4_NODE_CPUS value for pinned runs")
    args = parser.parse_args()
    if args.repeats < 1 or not 2 <= args.frames <= 120:
        parser.error("repeats must be positive and frames must be 2–120")
    args.output.mkdir(parents=True, exist_ok=True)
    output = args.output.resolve()
    all_results = {"frames": args.frames, "repeats": args.repeats, "cpu_list": args.cpus, "results": {}}
    for nodes in NODES:
        item = {"pinned": [], "unpinned": []}
        for rep in range(args.repeats):
            for pinned in ((False, True) if rep % 2 == 0 else (True, False)):
                key = "pinned" if pinned else "unpinned"
                sample = run_one(nodes, pinned, args.frames, output, args.cpus)
                item[key].append(sample)
                print(f"{nodes:2d} nodes {key:8s} repeat {rep+1}: {sample['median_ms']:.3f} ms", flush=True)
        all_results["results"][str(nodes)] = item
    (output / "affinity-results.json").write_text(json.dumps(all_results, indent=2) + "\n")
    print("nodes  unpinned median  pinned median  pinned / unpinned")
    for nodes in NODES:
        item = all_results["results"][str(nodes)]
        u = statistics.median(x["median_ms"] for x in item["unpinned"])
        p = statistics.median(x["median_ms"] for x in item["pinned"])
        print(f"{nodes:5d} {u:15.3f} {p:14.3f} {p/u:18.3f}")
    print("Details:", output / "affinity-results.json")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError) as exc:
        print(exc, file=sys.stderr)
        sys.exit(1)
