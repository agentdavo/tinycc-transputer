#!/usr/bin/env python3
"""Regenerate transputer/t4/tvx_iq.h from a llama.cpp checkout.

Usage: extract_iq_tables.py <path/to/llama.cpp> > ../../t4/tvx_iq.h
The codebooks are copied verbatim so the emulator decodes the i-quants
byte-identically to llama.cpp."""
import re, sys
src = open(sys.argv[1] + '/ggml/src/ggml-common.h').read()
want = [('uint8_t','kmask_iq2xs'),('uint8_t','ksigns_iq2xs'),
        ('uint64_t','iq2xxs_grid'),('uint32_t','iq3xxs_grid'),('uint64_t','iq1s_grid')]
print('#ifndef TVX_IQ_H\n#define TVX_IQ_H\n\n#include <stdint.h>\n')
for ty, name in want:
    m = re.search(r'GGML_TABLE_BEGIN\(' + ty + r',\s*' + name + r',\s*([A-Za-z0-9_]+)\)(.*?)GGML_TABLE_END\(\)', src, re.S)
    n = m.group(1)
    if n == 'NGRID_IQ1S': n = '2048'
    print('static const %s tvx_%s[%s] = {%s};\n' % (ty, name, n, m.group(2)))
print('#endif /* TVX_IQ_H */')
