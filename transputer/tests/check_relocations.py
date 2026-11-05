"""Check relocation fields in separately assembled, partially linked ELF32 inputs."""
from pathlib import Path
import os
import struct
import subprocess
import sys
import tempfile


def run(*args, error=None, env=None):
    result = subprocess.run(args, capture_output=True, text=True, env=env)
    if error is None:
        assert result.returncode == 0, (args, result.stderr)
    else:
        assert result.returncode != 0 and error in result.stderr, (args, result.stderr)
    return result.stderr


def image_bytes(name, symbol, size):
    elf = Path(name).read_bytes()
    symbols = Path(name + ".tccsym").read_text().splitlines()
    addr = next(int(line.split()[0], 16) for line in symbols if line.split()[2] == symbol)
    phoff, = struct.unpack_from("<I", elf, 28)
    entsize, count = struct.unpack_from("<HH", elf, 42)
    for i in range(count):
        kind, offset, base, _, filesz = struct.unpack_from("<5I", elf, phoff + i * entsize)
        if kind == 1 and base <= addr and addr + size <= base + filesz:
            start = offset + addr - base
            return addr, elf[start:start + size]
    raise AssertionError(f"{symbol} outside load segments")


def operand(slot):
    value = 0
    for byte in slot[:-1]:
        value |= byte & 15
        assert byte >> 4 in (2, 6), slot.hex()
        if byte >> 4 == 6:
            value = ~value
        value = (value << 4) & 0xffffffff
    return value | (slot[-1] & 15)


def check_instructions(cc):
    cases = [
        ("ldc", 4, 4), ("adc", 8, -0x123), ("eqc", 12, 0x12345678),
        ("j", 0, -1), ("cj", 10, 7), ("call", 9, -16),
        ("ldnl", 3, -0x80000000),
    ]
    source = ".text\n.global _start\n_start:\n"
    for i, (mnemonic, _, addend) in enumerate(cases):
        source += f".global site{i}\nsite{i}: {mnemonic} target{addend:+d}\n"
    Path("sites.S").write_text(source)
    Path("callee.S").write_text(".text\n.global target\ntarget: ret\n")
    for name in ("sites", "callee"):
        run(cc, "-c", name + ".S", "-o", name + ".o")
    run(cc, "-r", "-nostdlib", "callee.o", "sites.o", "-o", "partial.o")
    for inputs in (["sites.o", "callee.o"], ["partial.o"]):
        log = run(cc, "-nostdlib", *inputs, "-o", "out", env=dict(os.environ, T4_CALL_AUDIT="1"))
        assert "CALL_AUDIT_MISMATCH" not in log, log
        target, _ = image_bytes("out", "target", 1)
        for i, (_, opcode, addend) in enumerate(cases):
            pc, slot = image_bytes("out", f"site{i}", 9)
            expected = target + addend
            if opcode in (0, 9, 10):
                expected -= pc + len(slot)
            assert slot[-1] >> 4 == opcode and operand(slot) == expected & 0xffffffff, slot.hex()


if __name__ == "__main__":
    cc = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory() as tmp:
        os.chdir(tmp)
        check_instructions(cc)
    print("Transputer ELF relocation checks passed.")
