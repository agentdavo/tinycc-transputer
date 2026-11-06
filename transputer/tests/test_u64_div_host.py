"""Differential test of the 32-bit-word runtime against Python integers."""
import ctypes
from pathlib import Path
import random
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "lib/transputer/transputer-runtime.c"
MASK = (1 << 64) - 1


def main():
    rng = random.Random(897)
    divisors = [1 << i for i in range(64)]
    divisors += [0xffffffff, 0x100000001, 0x7fffffffffffffff,
                 0x8000000000000001, MASK]
    with tempfile.TemporaryDirectory() as tmp:
        library = Path(tmp) / "runtime.so"
        subprocess.run(["cc", "-O2", "-shared", "-fPIC", str(SOURCE), "-o", str(library)],
                       check=True)
        runtime = ctypes.CDLL(str(library))
        udiv, umod = runtime.__udivdi3, runtime.__umoddi3
        sdiv, smod = runtime.__divdi3, runtime.__moddi3
        for fn in (udiv, umod):
            fn.argtypes = [ctypes.c_uint64, ctypes.c_uint64]
            fn.restype = ctypes.c_uint64
        for fn in (sdiv, smod):
            fn.argtypes = [ctypes.c_int64, ctypes.c_int64]
            fn.restype = ctypes.c_int64

        for i in range(100000):
            dividend = rng.getrandbits(64)
            divisor = rng.choice(divisors) if i % 2 else (rng.getrandbits(64) or 1)
            assert (udiv(dividend, divisor), umod(dividend, divisor)) == divmod(dividend, divisor)

            a = ctypes.c_int64(dividend).value
            b = ctypes.c_int64(divisor).value
            if a == -(1 << 63) and b == -1:
                continue
            quotient = abs(a) // abs(b)
            if (a < 0) != (b < 0):
                quotient = -quotient
            assert (sdiv(a, b), smod(a, b)) == (quotient, a - quotient * b)
        # Exercise every divisor in the base-65536 path, plus the adjacent
        # restoring-division path, with full-width dividends.
        for divisor in range(1, 65540):
            dividend = rng.getrandbits(64) | (1 << 63)
            assert (udiv(dividend, divisor), umod(dividend, divisor)) == divmod(dividend, divisor)
        for i in range(10000):
            divisor = rng.randrange(1 << 32, 1 << 64)
            for dividend in (divisor - 1, divisor, min(divisor + 1, MASK)):
                assert (udiv(dividend, divisor), umod(dividend, divisor)) == divmod(dividend, divisor)
        for fn in (udiv, umod, sdiv, smod):
            assert fn(123, 0) == 0
    print("100,000 signed and unsigned pairs, 65,539 small divisors, and 30,000 wide boundaries passed")


if __name__ == "__main__":
    main()
