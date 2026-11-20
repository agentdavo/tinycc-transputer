#!/usr/bin/env python3
"""Compare a launcher PPM with the corresponding bundled reference."""
import argparse
import hashlib
import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
ARCHIVE = ROOT / "transputer/t4/examples/povray/pictures.zip"


def ppm(data):
    # The bundled files use simple P6 headers without comments.
    match = re.match(rb"P6\s+(\d+)\s+(\d+)\s+(\d+)\s", data)
    if not match:
        raise ValueError("expected a simple P6 PPM header")
    width, height, maximum = map(int, match.groups())
    pixels = data[match.end():]
    if maximum != 255 or len(pixels) != width * height * 3:
        raise ValueError("unsupported or incomplete PPM")
    return (width, height), pixels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("level", choices=("level1", "level2", "level3"))
    parser.add_argument("scene", help="scene name, with or without .pov")
    args = parser.parse_args()
    name = Path(args.scene).stem
    reference_name = f"ppm/{args.level}/{name}.ppm"
    try:
        actual = args.image.read_bytes()
        with zipfile.ZipFile(ARCHIVE) as archive:
            expected = archive.read(reference_name)
        actual_size, actual_pixels = ppm(actual)
        expected_size, expected_pixels = ppm(expected)
    except (OSError, KeyError, ValueError, zipfile.BadZipFile) as exc:
        print(f"Comparison failed: {exc}", file=sys.stderr)
        return 2
    print(f"Reference: {reference_name}")
    print(f"SHA-256: {hashlib.sha256(actual).hexdigest()}")
    print(f"Byte-identical: {actual == expected}")
    if actual_size != expected_size:
        print(f"Dimensions differ: {actual_size} vs {expected_size}")
        return 1
    differences = sum(actual_pixels[i:i+3] != expected_pixels[i:i+3]
                      for i in range(0, len(actual_pixels), 3))
    print(f"Different RGB pixels: {differences} / {actual_size[0] * actual_size[1]}")
    return 1 if differences else 0


if __name__ == "__main__":
    sys.exit(main())
