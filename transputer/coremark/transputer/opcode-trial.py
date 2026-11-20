#!/usr/bin/env python3
"""Create diagnostic sources in the output directory for opcode A/B runs."""

import sys
from pathlib import Path


def crcbyte(text):
    start = text.index("crcu8(ee_u8 data, ee_u16 crc)")
    end = text.index("\nee_u16\ncrcu16", start)
    replacement = """crcu8(ee_u8 data, ee_u16 crc)
{
    /* Put the reflected CRC and input in the high bits, then advance
       eight bits with the high-aligned polynomial and zero input. */
    unsigned int state = __bitrevword((unsigned int)data ^ (unsigned int)crc);
    return (ee_u16)__bitrevword(__crcbyte(0, state, 0x80050000u));
}
"""
    return text[:start] + replacement + text[end:]


def matrix_walk(text):
    start = text.index("\nmatrix_mul_matrix(ee_u32")
    loops = text[start:]
    if loops.count("    ee_u32 i, j, k;") != 2:
        raise ValueError("matrix trial expects the two original matrix loops")
    loops = loops.replace("    ee_u32 i, j, k;", "    ee_u32 i, j, k, row = 0;")
    loops = loops.replace("i < N; i++", "i < N; i++, row += N")
    loops = loops.replace(
        "            C[i * N + j] = 0;",
        "            MATRES *out = C + row + j;\n"
        "            MATDAT *a = A + row;\n"
        "            ee_u32 b_index = j;\n"
        "            *out = 0;",
    )
    loops = loops.replace("k < N; k++", "k < N; k++, b_index += N")
    loops = loops.replace("C[i * N + j]", "(*out)")
    loops = loops.replace("A[i * N + k]", "a[k]").replace("B[k * N + j]", "B[b_index]")
    return text[:start] + loops


if __name__ == "__main__":
    trial, source, out = sys.argv[1:]
    name, transform = {
        "crcbyte": ("core_util.c", crcbyte),
        "matrix-walk": ("core_matrix.c", matrix_walk),
    }[trial]
    (Path(out) / name).write_text(transform((Path(source) / name).read_text()))
