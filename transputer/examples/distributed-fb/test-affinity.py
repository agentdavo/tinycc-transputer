#!/usr/bin/env python3
"""Integration checks for T4 per-node CPU affinity and image equivalence."""
import argparse
import os
import re
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent


def run(nodes, spec, output_root, legacy=None, expect_success=True):
    env = os.environ.copy()
    env.pop("T4_NODE_CPUS", None)
    env.pop("T4_CPU_PIN", None)
    env.update(NODES=str(nodes), FRAMES="2", FPS="0", CAPTURE="1", WORK=str(output_root))
    if spec is not None:
        env["T4_NODE_CPUS"] = spec
    if legacy is not None:
        env["T4_CPU_PIN"] = str(legacy)
    result = subprocess.run(["bash", str(HERE / "run-scale.sh")], env=env,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if not expect_success:
        if result.returncode == 0:
            raise RuntimeError("invalid affinity list unexpectedly succeeded")
        match = re.search(r"inspect (.+)/scale\.uart", result.stdout)
        if not match or "invalid or unavailable T4_NODE_CPUS" not in (Path(match.group(1)) / "scale.out").read_text():
            raise RuntimeError("affinity failure did not report its reason: " + result.stdout)
        return None
    if result.returncode:
        raise RuntimeError(result.stdout)
    match = re.search(r"^SCALE OK: .*: (.+)$", result.stdout, re.MULTILINE)
    if not match:
        raise RuntimeError("missing output directory: " + result.stdout)
    directory = Path(match.group(1))
    log = (directory / "scale.out").read_text(errors="replace")
    mapping = [(int(a), int(b)) for a, b in
               re.findall(r"\[CPU\] node (\d+) pinned to logical CPU (\d+)", log)]
    if spec is None:
        if mapping:
            raise RuntimeError("unpinned run unexpectedly pinned nodes")
    elif sorted(n for n, _ in mapping) != list(range(nodes)):
        raise RuntimeError(f"missing or duplicate node mappings: {directory}")
    return directory, dict(mapping)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    root = args.output.resolve()
    allowed = sorted(os.sched_getaffinity(0))
    if not allowed:
        raise RuntimeError("no available CPUs")
    chosen = allowed[:2]
    spec = ",".join(map(str, chosen))
    baseline, _ = run(4, None, root)
    automatic, mapping = run(4, "auto", root)
    if any(cpu not in allowed for cpu in mapping.values()):
        raise RuntimeError("automatic mapping escaped the allowed CPU set")
    explicit, mapping = run(4, spec, root, legacy=allowed[-1])
    if any(mapping[n] != chosen[n % len(chosen)] for n in range(4)):
        raise RuntimeError("explicit mapping or wrapping is incorrect")
    for frame in range(2):
        expected = (baseline / f"screenshot_{frame}.bmp").read_bytes()
        for other in (automatic, explicit):
            if (other / f"screenshot_{frame}.bmp").read_bytes() != expected:
                raise RuntimeError(f"frame {frame} changed with pinning: {other}")
    run(2, "999999", root, expect_success=False)
    print(f"Affinity tests OK: unpinned/auto/explicit images match; mapping {mapping}; invalid list rejected")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError) as exc:
        raise SystemExit(str(exc))
