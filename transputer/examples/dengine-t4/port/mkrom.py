#!/usr/bin/env python3
"""Pack the files dEngine's techDemo scene opens into one read-only image.

The engine reads data/config.cfg, follows it to the scene, map, models and
material libraries, and loads the textures of the materials its models use.
This script follows the same references (it does not run the engine), so the
image holds exactly the files the scene needs for the selected material
quality, plus the shaders, font and camera path the renderer and platform
loop open.  Files are stored unmodified; nothing is decoded or resized here.

Image layout (little endian):
  "DROM", u32 version (1), u32 count, u32 image size
  count x {u32 name offset, u32 name length, u32 data offset, u32 size},
  sorted by name; then the names; then the data, each file 8-byte aligned
  and followed by a NUL that its size does not count.
"""
import argparse
import re
import struct
import sys
from pathlib import Path


def tokens(text):
    """Tokens as lexer.c reads them: it also treats , ( ) * : as white space."""
    out = []
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    for line in text.splitlines():
        line = line.split('//')[0]
        if line.lstrip().startswith('#'):
            continue
        out.extend(re.split(r'[ \t\r,()*:]+', line.strip()))
    return [t for t in out if t]


def block_items(toks, keyword):
    """Items inside `keyword { ... }` blocks of a dEngine lexer file."""
    items = []
    i = 0
    while i < len(toks):
        if toks[i] == keyword and i + 1 < len(toks) and toks[i + 1] == '{':
            i += 2
            while i < len(toks) and toks[i] != '}':
                items.append(toks[i])
                i += 1
        i += 1
    return items


def parse_mtl(text):
    materials = {}
    current = None
    for line in text.splitlines():
        parts = line.split()
        if not parts:
            continue
        if parts[0] == 'newmtl' and len(parts) > 1:
            current = parts[1].replace('"', '')
            materials.setdefault(current, [])
        elif current and parts[0] in ('map_Kd', 'BumpKd', 'SpecKd') and len(parts) > 1:
            materials[current].append(parts[1])
    return materials


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dengine', type=Path, help='dEngine checkout')
    ap.add_argument('output', type=Path)
    ap.add_argument('--quality', choices=('high', 'low', 'both'), default='both',
                    help='material libraries whose textures to include')
    ap.add_argument('--list', type=Path, help='write the packed file list here')
    args = ap.parse_args()
    root = args.dengine
    files = set()
    missing = set()

    def need(rel):
        rel = rel.replace('\\', '/')
        while rel.startswith('./') or rel.startswith('/'):
            rel = rel[2:] if rel.startswith('./') else rel[1:]
        if (root / rel).is_file():
            files.add(rel)
            return True
        missing.add(rel)
        return False

    def read(rel):
        return (root / rel).read_text(errors='replace')

    need('data/config.cfg')
    scenes = block_items(tokens(read('data/config.cfg')), 'scene')
    maps, objs, md5s, anims = [], [], [], []
    for scene in scenes:
        if not need(scene):
            continue
        toks = tokens(read(scene))
        for i, t in enumerate(toks):
            if t == 'filename' and i + 1 < len(toks):
                maps.append(toks[i + 1])
    for m in maps:
        if not need(m):
            continue
        toks = tokens(read(m))
        i = 0
        while i < len(toks):
            if toks[i] == 'entities' and i + 2 < len(toks):
                kind = toks[i + 1]
                j = i + 3
                while j < len(toks) and toks[j] != '}':
                    if toks[j] == 'model' and j + 1 < len(toks):
                        (objs if kind == 'OBJ' else md5s).append(toks[j + 1])
                        j += 1
                    elif toks[j] == 'anim' and j + 1 < len(toks):
                        anims.append(toks[j + 1])
                        j += 1
                    j += 1
                i = j
            i += 1

    used_materials = set()
    for o in objs:
        if need(o):
            for line in read(o).splitlines():
                parts = line.split()
                if len(parts) > 1 and parts[0] == 'usemtl':
                    used_materials.add(parts[1])
    for m in md5s:
        if need(m):
            for name in re.findall(r'shader\s+"([^"]*)"', read(m)):
                used_materials.add(name)
    for a in anims:
        need(a)

    need('data/materials.lbr')
    lbr = tokens(read('data/materials.lbr'))
    libraries = []
    if args.quality in ('high', 'both'):
        libraries += block_items(lbr, 'highQuality')
    if args.quality in ('low', 'both'):
        libraries += block_items(lbr, 'lowQuality')
    # MATLIB_LoadLibraries opens both lists' files only for the selected
    # quality, but the libraries are small: pack all of them.
    for lib in block_items(lbr, 'highQuality') + block_items(lbr, 'lowQuality'):
        need(lib)
    for lib in libraries:
        if (root / lib).is_file():
            for name, textures in parse_mtl(read(lib)).items():
                if name in used_materials:
                    for t in textures:
                        need(t)

    # Opened by the renderer and the platform loop, not by the scene files.
    for shader in ('v_uber', 'f_uber', 'v_shadowMapGenerator', 'f_shadowMapGenerator', 'v_text', 'f_text'):
        need('data/shaders/%s.glsl' % shader)
    need('data/font/font1.png')
    need('data/font/font1.dat')
    need('data/cameraPath/fps.cp')

    names = sorted(files)
    header = 16 + 16 * len(names)
    name_blob = b''
    name_offsets = []
    for n in names:
        name_offsets.append(header + len(name_blob))
        name_blob += n.encode() + b'\0'
    offset = (header + len(name_blob) + 7) & ~7
    entries, data = [], bytearray()
    for n, no in zip(names, name_offsets):
        payload = (root / n).read_bytes()
        pad = (-(offset + len(data))) & 7
        data += b'\0' * pad
        entries.append((no, len(n), offset + len(data), len(payload)))
        data += payload + b'\0'
    data += b'\0' * ((-(offset + len(data))) & 7)
    total = offset + len(data)
    image = bytearray(b'DROM' + struct.pack('<III', 1, len(names), total))
    for e in entries:
        image += struct.pack('<IIII', *e)
    image += name_blob
    image += b'\0' * (offset - len(image))
    image += data
    assert len(image) == total
    args.output.write_bytes(image)
    if args.list:
        args.list.write_text(''.join('%8d %s\n' % (e[3], n) for n, e in zip(names, entries)))
    print('ROM %s: %d files, %d bytes (%s quality)' % (args.output, len(names), total, args.quality))
    if missing:
        print('missing from the checkout (the engine will report them): ' + ', '.join(sorted(missing)), file=sys.stderr)


if __name__ == '__main__':
    main()
