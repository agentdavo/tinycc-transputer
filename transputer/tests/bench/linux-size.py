#!/usr/bin/env python3
"""Report ELF load bytes, BTL overhead and the largest rootfs members."""

import argparse
import json
import struct
from pathlib import Path


def load_sizes(data):
    if data[:6] != b"\x7fELF\x01\x01":
        raise ValueError("expected a little-endian ELF32 image")
    phoff, = struct.unpack_from("<I", data, 28)
    entsize, count = struct.unpack_from("<HH", data, 42)
    if entsize < 32 or phoff + entsize * count > len(data):
        raise ValueError("invalid ELF program headers")
    initialized = zero = 0
    for i in range(count):
        kind, offset, _, _, filesz, memsz, _, _ = struct.unpack_from(
            "<8I", data, phoff + i * entsize)
        if kind != 1:
            continue
        if filesz > memsz or offset + filesz > len(data):
            raise ValueError("invalid load segment")
        initialized += filesz
        zero += memsz - filesz
    return initialized, zero


def archive_members(data):
    pos = 0
    while pos + 110 <= len(data):
        hdr = data[pos:pos + 110]
        if hdr[:6] not in (b"070701", b"070702"):
            raise ValueError(f"invalid newc header at {pos}")
        size = int(hdr[54:62], 16)
        nsize = int(hdr[94:102], 16)
        name_end = pos + 110 + nsize
        payload = (name_end + 3) & ~3
        end = (payload + size + 3) & ~3
        if not nsize or end > len(data) or data[name_end - 1]:
            raise ValueError(f"invalid newc record at {pos}")
        name = data[pos + 110:name_end - 1].decode()
        if name == "TRAILER!!!":
            return
        yield name, size
        pos = end
    raise ValueError("newc archive has no trailer")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("btl", type=Path)
    parser.add_argument("rootfs", type=Path)
    args = parser.parse_args()
    initialized, zero = load_sizes(args.elf.read_bytes())
    btl = args.btl.stat().st_size
    raw = args.rootfs.read_bytes()
    entries = sorted(archive_members(raw), key=lambda entry: -entry[1])
    print(json.dumps({
        "btl_bytes": btl,
        "initialized_load_bytes": initialized,
        "zero_fill_bytes": zero,
        "load_bytes": initialized + zero,
        "btl_bytes_minus_load_bytes": btl - initialized - zero,
        "uncompressed_rootfs_bytes": len(raw),
        "largest_rootfs_members": dict(entries[:15]),
    }, indent=2))


if __name__ == "__main__":
    main()
