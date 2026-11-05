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


def section_headers(obj):
    offset, = struct.unpack_from("<I", obj, 32)
    size, count = struct.unpack_from("<HH", obj, 46)
    return [struct.unpack_from("<10I", obj, offset + i * size) for i in range(count)]


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


def check_narrow(cc):
    Path("narrow.S").write_text("""
.text
.global _start
_start: ret
.data
.global fields, delta, anchor
anchor:
.byte 0xa5
fields:
.byte byte_base+1, byte_base-127, negative-1
.short short_base+1, short_base-1
.byte 0x5a
delta:
.byte near_target-.
.short near_target-anchor
""")
    Path("defs.S").write_text("""
.global byte_base, short_base, negative, near_target
.set byte_base, 254
.set short_base, 65534
.set negative, -127
.data
near_target: .byte 0
""")
    for name in ("narrow", "defs"):
        run(cc, "-c", name + ".S", "-o", name + ".o")
    run(cc, "-r", "-nostdlib", "narrow.o", "defs.o", "-o", "narrow-part.o")
    for inputs in (["narrow.o", "defs.o"], ["narrow-part.o"], ["defs.o", "narrow.o"]):
        run(cc, "-nostdlib", *inputs, "-o", "narrow.elf")
        _, fields = image_bytes("narrow.elf", "fields", 8)
        assert fields == bytes.fromhex("ff 7f 80 ff ff fd ff 5a"), fields.hex()
        target, _ = image_bytes("narrow.elf", "near_target", 1)
        anchor, guard = image_bytes("narrow.elf", "anchor", 1)
        pc, delta = image_bytes("narrow.elf", "delta", 3)
        assert guard == b"\xa5" and struct.unpack("<bh", delta) == (target - pc, target - anchor)

    for field, expr, bits in (("byte", "byte_base+2", 8), ("byte", "negative-2", 8),
                              ("short", "short_base+2", 16), ("short", "_start", 16)):
        Path("overflow.S").write_text(f".text\n.global _start\n_start: ret\n.data\n.{field} {expr}\n")
        run(cc, "-nostdlib", "overflow.S", "defs.o", "-o", "bad", error=f"{bits}-bit relocation overflow")
    Path("missing.S").write_text(".text\n.global _start\n_start: ret\n.data\n.byte missing\n")
    run(cc, "-nostdlib", "missing.S", "-o", "bad", error="undefined symbol 'missing'",
        env=dict(os.environ, T4_STRICT_UNRESOLVED_CALLS="1"))
    Path("wide.S").write_text(".data\n.byte missing+128\n")
    run(cc, "-c", "wide.S", "-o", "bad.o", error="addend does not fit in signed 8 bits")
    Path("far.S").write_text(".text\n.global _start\n_start: ret\n.data\n.byte far-.\n.space 160\nfar: .byte 0\n")
    run(cc, "-nostdlib", "far.S", "-o", "bad", error="8-bit relocation overflow")


def check_invalid_records(cc):
    # Make ELF REL records the assembler cannot emit, preserving their symbol.
    Path("badrel.S").write_text(".text\n.global _start\n_start: ret\n.data\n.long _start\n")
    run(cc, "-c", "badrel.S", "-o", "valid.o")
    obj = Path("valid.o").read_bytes()
    rel = next(section[4] for section in section_headers(obj) if section[1] == 9)
    _, info = struct.unpack_from("<II", obj, rel)
    for offset, new_info, error in (
            (0, info & ~255 | 250, "unsupported Transputer relocation 250"),
            (3, info & ~255 | 2, "relocation 2 outside"),
            (0xffffffff, 0, None)):  # R_NONE has no field or symbol.
        damaged = bytearray(obj)
        struct.pack_into("<II", damaged, rel, offset, new_info)
        Path("mutant.o").write_bytes(damaged)
        run(cc, "-nostdlib", "mutant.o", "-o", "mutant", error=error)

    Path("slot.S").write_text(".text\n.global _start\n_start: ldc word\n.data\nword: .long 0\n")
    run(cc, "-c", "slot.S", "-o", "slot.o")
    obj = Path("slot.o").read_bytes()
    text = next(section for section in section_headers(obj) if section[2] & 4 and section[5] == 9)
    for fill, error in ((0x20, "cannot encode relocation"), (0x40, "relocation overflow")):
        damaged = bytearray(obj)
        damaged[text[4]:text[4] + 9] = bytes([fill]) * 9
        Path("mutant.o").write_bytes(damaged)
        run(cc, "-nostdlib", "mutant.o", "-o", "mutant", error=error)


if __name__ == "__main__":
    cc = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory() as tmp:
        os.chdir(tmp)
        check_instructions(cc)
        check_narrow(cc)
        check_invalid_records(cc)
    print("Transputer ELF relocation checks passed.")
