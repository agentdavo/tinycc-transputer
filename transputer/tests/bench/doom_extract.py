#!/usr/bin/env python3
"""Replace unexercised Doom functions with fail-fast benchmark stubs."""
import re
import sys
from pathlib import Path

REMOVED = {
    "r_draw.c": ("R_FillBackScreen",),
    "r_segs.c": ("R_RenderMaskedSegRange", "R_StoreWallRange"),
    "r_plane.c": ("R_ClearPlanes", "R_FindPlane", "R_CheckPlane", "R_DrawPlanes"),
    "r_things.c": ("R_InstallSpriteLump", "R_InitSpriteDefs", "R_DrawVisSprite",
                   "R_ProjectSprite", "R_AddSprites", "R_DrawPSprite",
                   "R_DrawPlayerSprites", "R_DrawSprite", "R_DrawMasked"),
}
LEXEME = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)


def masked(source):
    return LEXEME.sub(lambda m: re.sub(r'[^\n]', ' ', m.group()), source)


def matching(text, start, left, right):
    depth = 0
    for i in range(start, len(text)):
        if text[i] == left:
            depth += 1
        elif text[i] == right:
            depth -= 1
            if not depth:
                return i
    raise ValueError("unbalanced " + left)


def stub_functions(source, names):
    text = masked(source)
    replacements = []
    for name in names:
        matches = []
        for match in re.finditer(r'\b' + re.escape(name) + r'\s*\(', text):
            paren = text.index('(', match.start())
            end = matching(text, paren, '(', ')') + 1
            while end < len(text) and text[end].isspace():
                end += 1
            if end < len(text) and text[end] == '{':
                matches.append((end, matching(text, end, '{', '}') + 1))
        if len(matches) != 1:
            raise ValueError(f"expected one definition of {name}, found {len(matches)}")
        start, end = matches[0]
        replacements.append((start, end, '{ bench_unreachable("' + name + '"); }'))
    for start, end, replacement in sorted(replacements, reverse=True):
        source = source[:start] + replacement + source[end:]
    return source


def main():
    root, out = map(Path, sys.argv[1:])
    out.mkdir(parents=True, exist_ok=True)
    for name, functions in REMOVED.items():
        (out / name).write_text(stub_functions((root / name).read_text(), functions))


if __name__ == '__main__':
    main()
