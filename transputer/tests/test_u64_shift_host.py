"""Check the word-based shift helpers against Python integer arithmetic."""
import ctypes
from pathlib import Path
import random
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "lib/transputer/transputer-runtime.c"
MASK = (1 << 64) - 1


def main():
    rng = random.Random(472)
    values = [0, 1, MASK, 1 << 63, (1 << 63) - 1, 0x8000000100000001]
    values += [rng.getrandbits(64) for _ in range(5000)]
    with tempfile.TemporaryDirectory() as tmp:
        library = Path(tmp) / "runtime.so"
        subprocess.run(["cc", "-O2", "-shared", "-fPIC", str(SOURCE), "-o", str(library)],
                       check=True)
        runtime = ctypes.CDLL(str(library))
        left, signed_right, right = runtime.__ashldi3, runtime.__ashrdi3, runtime.__lshrdi3
        for fn in (left, signed_right):
            fn.argtypes = [ctypes.c_int64, ctypes.c_int]
            fn.restype = ctypes.c_int64
        right.argtypes = [ctypes.c_uint64, ctypes.c_int]
        right.restype = ctypes.c_uint64

        for bits in values:
            signed = ctypes.c_int64(bits).value
            for count in range(66):
                want_left = ctypes.c_int64((bits << count) & MASK).value
                assert left(signed, count) == want_left
                assert right(bits, count) == bits >> count
                assert signed_right(signed, count) == signed >> count
    print(f"{len(values)} values passed for shift counts 0 through 65")


if __name__ == "__main__":
    main()
