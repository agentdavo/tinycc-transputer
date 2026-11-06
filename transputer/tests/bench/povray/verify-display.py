import pathlib
import sys

expected = bytearray(b"P6\n4 3\n255\n")
for y in range(3):
    for x in range(4):
        rgb = (x * 37, y * 91, x + y * 4)
        if (x, y) == (1, 0):
            rgb = (255, 128, 0)
        if (x, y) == (2, 2):
            rgb = (1, 2, 3)
        expected.extend(rgb)
if pathlib.Path(sys.argv[1]).read_bytes() != expected:
    raise SystemExit("display row/revisit mismatch")
print("display: out-of-order rows/pixels, revisits and repeated close pass")
