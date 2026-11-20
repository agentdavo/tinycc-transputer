#!/usr/bin/env python3
"""Summarize per-frame assembly time measured by the T800 low-priority timer."""
import re
import statistics
import sys
from pathlib import Path

text = Path(sys.argv[1]).read_bytes().decode("ascii", errors="replace")
frames = [(int(a, 16), int(b, 16)) for a, b in re.findall(r"SCALE FRAME ([0-9a-f]{8}) ([0-9a-f]{8})", text)]
if not frames or [n for n, _ in frames] != list(range(len(frames))):
    raise SystemExit("missing or out-of-order frame timings")
ticks = [t for _, t in frames]
steady = ticks[1:] if len(ticks) > 1 else ticks
print("assembly ms (each frame):", ", ".join(f"{t * 0.064:.3f}" for t in ticks))
print(f"median assembly ms (excluding first frame if possible): {statistics.median(steady) * 0.064:.3f}")
