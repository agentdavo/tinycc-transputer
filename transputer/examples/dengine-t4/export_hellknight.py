#!/usr/bin/env python3
"""Bake the cloned Hellknight MD5 mesh's bind pose into a compact C header.

This is an offline preview tool, not a complete MD5 animation loader. It
reads the local upstream asset and writes only to the requested run directory.
"""
import argparse
import math
import re
from pathlib import Path

NUM = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?"
JOINT = re.compile(rf'^\s*"[^"]+"\s+-?\d+\s+\(\s*({NUM})\s+({NUM})\s+({NUM})\s*\)\s+\(\s*({NUM})\s+({NUM})\s+({NUM})\s*\)')
VERT = re.compile(rf"^\s*vert\s+(\d+)\s+\(\s*({NUM})\s+({NUM})\s*\)\s+(\d+)\s+(\d+)")
TRI = re.compile(r"^\s*tri\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)")
WEIGHT = re.compile(rf"^\s*weight\s+(\d+)\s+(\d+)\s+({NUM})\s+\(\s*({NUM})\s+({NUM})\s+({NUM})\s*\)")


def rotate(q, v):
    x, y, z, w = q
    tx, ty, tz = 2*(y*v[2]-z*v[1]), 2*(z*v[0]-x*v[2]), 2*(x*v[1]-y*v[0])
    return (v[0]+w*tx+y*tz-z*ty, v[1]+w*ty+z*tx-x*tz, v[2]+w*tz+x*ty-y*tx)


def section(lines, name):
    start = next(i for i, line in enumerate(lines) if line.strip() == name + " {")
    end = next(i for i in range(start+1, len(lines)) if lines[i].strip() == "}")
    return lines[start+1:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mesh", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    lines = args.mesh.read_text().splitlines()
    joints=[]
    for line in section(lines,"joints"):
        m=JOINT.match(line)
        if not m: raise SystemExit("Unrecognized joint: " + line)
        x,y,z,qx,qy,qz=map(float,m.groups())
        joints.append(((x,y,z),(qx,qy,qz,-math.sqrt(max(0.0,1-qx*qx-qy*qy-qz*qz)))))
    vertices=[]; triangles=[]; weights=[]
    for line in section(lines,"mesh"):
        m=VERT.match(line)
        if m:
            idx,u,v,start,count=m.groups()
            if int(idx)!=len(vertices): raise SystemExit("nonsequential vertices")
            vertices.append((float(u),float(v),int(start),int(count))); continue
        m=TRI.match(line)
        if m:
            idx,a,b,c=map(int,m.groups())
            if idx!=len(triangles): raise SystemExit("nonsequential triangles")
            # Match the winding reversal in dEngine's md5model.c.
            triangles.append((c,b,a)); continue
        m=WEIGHT.match(line)
        if m:
            idx,j,bias,x,y,z=m.groups()
            if int(idx)!=len(weights): raise SystemExit("nonsequential weights")
            weights.append((int(j),float(bias),(float(x),float(y),float(z))))
    if len(joints)!=110 or len(vertices)!=1656 or len(triangles)!=2626 or len(weights)!=3206:
        raise SystemExit(f"unexpected mesh counts: {len(joints)}, {len(vertices)}, {len(triangles)}, {len(weights)}")
    points=[]
    for _,_,start,count in vertices:
        out=[0.0,0.0,0.0]
        for joint,bias,pos in weights[start:start+count]:
            origin,q=joints[joint]; rotated=rotate(q,pos)
            for k in range(3): out[k]+=(origin[k]+rotated[k])*bias
        points.append((out[0],out[2],-out[1])) # Z-up model to Y-up view
    lo=[min(p[k] for p in points) for k in range(3)]
    hi=[max(p[k] for p in points) for k in range(3)]
    center=[(lo[k]+hi[k])*0.5 for k in range(3)]
    extent=max(hi[k]-lo[k] for k in range(3))
    if extent<=0: raise SystemExit("empty mesh")
    points=[tuple((p[k]-center[k])*1.7/extent for k in range(3)) for p in points]
    buckets=[[] for _ in range(16)]
    for tri in triangles:
        a,b,c=(points[i] for i in tri)
        u=[b[k]-a[k] for k in range(3)]; v=[c[k]-a[k] for k in range(3)]
        n=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0])
        mag=math.sqrt(sum(q*q for q in n))
        light=max(0.0,(n[0]*-0.3+n[1]*0.7+n[2]*0.64)/mag) if mag else 0.0
        buckets[min(15,int(light*15))].extend(tri)
    out=["/* Generated locally from the cloned dEngine MD5 mesh; bind pose only. */",
         f"#define HK_VERTEX_COUNT {len(points)}", f"#define HK_INDEX_COUNT {len(triangles)*3}",
         "static const short hk_vertices[HK_VERTEX_COUNT][3]={"]
    out += ["{%d,%d,%d}," % tuple(round(x*32767) for x in p) for p in points]
    out += ["};", "static const unsigned short hk_indices[HK_INDEX_COUNT]={"]
    for bucket in buckets:
        for k in range(0,len(bucket),24): out.append(",".join(map(str,bucket[k:k+24]))+",")
    out += ["};", "static const unsigned short hk_group_counts[16]={"+",".join(str(len(b)) for b in buckets)+"};"]
    args.output.write_text("\n".join(out)+"\n")
    print(f"Baked {len(joints)} joints, {len(points)} vertices, {len(triangles)} triangles into {args.output}")


if __name__ == "__main__":
    main()
