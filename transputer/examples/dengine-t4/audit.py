#!/usr/bin/env python3
"""List GL call names in dEngine's programmable renderer and T4's subset."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
source = ROOT / "transputer/examples/dEngine/src/renderer_progr.c"
header = ROOT / "transputer/examples/distributed-fb/gles2_subset.h"
if not source.is_file():
    raise SystemExit("Clone dEngine as described in README.md first")
text = source.read_text(errors="replace")
text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
calls = set(re.findall(r"\b(gl[A-Z][A-Za-z0-9_]*)\s*\(", text))
api = header.read_text()
named = {name for name in calls if re.search(r"\b" + name + r"\s*\(", api)}
print(f"Programmable renderer: {len(calls)} distinct direct GL call names")
print(f"Named in T4 subset ({len(named)}):", ", ".join(sorted(named)))
print(f"Not named in T4 subset ({len(calls - named)}):", ", ".join(sorted(calls - named)))
print("Name presence does not imply complete semantics; macros and indirect calls are not counted.")
