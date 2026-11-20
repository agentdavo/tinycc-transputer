#!/usr/bin/env python3
"""Inspect the default dEngine scene and verify its directly named assets."""
import re
from pathlib import Path

ROOT=Path(__file__).resolve().parents[3]/"transputer/examples/dEngine"

def read(relative):
    path=ROOT/relative
    if not path.is_file(): raise SystemExit(f"Missing scene asset: {relative}")
    return path.read_text(errors="replace")

def clean(text):
    return re.sub(r"//[^\n]*|#[^\n]*", "", text)

def main():
    config=clean(read("data/config.cfg"))
    m=re.search(r"\bscene\s*\{\s*(\S+)",config)
    if not m: raise SystemExit("No configured scene")
    scene=m.group(1); scene_text=clean(read(scene))
    m=re.search(r"\bfilename\s*:\s*(\S+)",scene_text)
    if not m: raise SystemExit("No scene map")
    map_path=m.group(1); map_text=clean(read(map_path))
    refs=re.findall(r"\b(model|anim)\s+(\S+)",map_text)
    obj=triangles=vertices=uvs=normals=0
    for kind,path in refs:
        text=read(path)
        if kind=="model" and path.lower().endswith(".obj"):
            obj+=1
            for line in text.splitlines():
                fields=line.split()
                if not fields: continue
                if fields[0]=="v": vertices+=1
                elif fields[0]=="vt": uvs+=1
                elif fields[0]=="vn": normals+=1
                elif fields[0]=="f": triangles+=max(0,len(fields)-3)
    print(f"Default scene: {scene}\nMap: {map_path}")
    print(f"OBJ models: {obj}; vertices: {vertices}; UVs: {uvs}; normals: {normals}; triangulated faces: {triangles}")
    for kind,path in refs:
        if not path.lower().endswith(".obj"): print(f"{kind}: {path}")
    print(f"Verified {len(refs)} directly named map assets; materials and textures are not audited.")

if __name__=="__main__": main()
