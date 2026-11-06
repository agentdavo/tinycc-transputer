#!/usr/bin/env python3
"""Compare the complete rendered file, including dimensions, with its oracle."""
import hashlib
import pathlib
import sys
import zipfile

archive, image, scene = sys.argv[1:]
data = pathlib.Path(image).read_bytes()
with zipfile.ZipFile(archive) as refs:
    ref = refs.read(f"ppm/level1/{scene}.ppm")
if data != ref:
    different = sum(a != b for a, b in zip(data, ref)) + abs(len(data) - len(ref))
    raise SystemExit(f"{scene}: reference mismatch ({different} differing bytes)")
print(f"{scene}: reference match, {len(data)} bytes, sha256 {hashlib.sha256(data).hexdigest()}")
