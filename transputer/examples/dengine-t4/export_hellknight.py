#!/usr/bin/env python3
"""Bake the cloned Hellknight MD5 mesh and optional idle poses into a C header.

This is an offline preview tool, not a complete MD5 animation loader. It
reads the local upstream asset and writes only to the requested run directory.
"""
import argparse
import math
import re
import struct
import zlib
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

def quat_mul(a,b):
    x,y,z,w=a; X,Y,Z,W=b
    return (w*X+x*W+y*Z-z*Y,w*Y-x*Z+y*W+z*X,w*Z+x*Y-y*X+z*W,w*W-x*X-y*Y-z*Z)

def animation_skeletons(path):
    lines=path.read_text().splitlines()
    hierarchy=[]
    for line in section(lines,"hierarchy"):
        m=re.match(r'^\s*"[^"]+"\s+(-?\d+)\s+(\d+)\s+(\d+)',line)
        if not m: raise ValueError("bad animation hierarchy")
        hierarchy.append(tuple(map(int,m.groups())))
    base=[]
    for line in section(lines,"baseframe"):
        vals=re.findall(NUM,line)
        if len(vals)!=6: raise ValueError("bad animation baseframe")
        base.append(list(map(float,vals)))
    if len(base)!=110 or len(hierarchy)!=110: raise ValueError("unexpected joint count")
    result=[]
    for frame in range(0,120,15):
        data=[float(s) for line in section(lines,f"frame {frame}") for s in re.findall(NUM,line)]
        if len(data)!=157: raise ValueError("unexpected animation component count")
        world=[]
        for i,(parent,flags,start) in enumerate(hierarchy):
            local=base[i].copy(); index=start
            for component in range(6):
                if flags & (1<<component): local[component]=data[index]; index+=1
            xyz=tuple(local[:3]); q=tuple(local[3:])
            q=(*q,-math.sqrt(max(0.0,1-sum(v*v for v in q))))
            if parent>=0:
                if parent>=i: raise ValueError("invalid joint parent")
                parent_pos,parent_q=world[parent]; rotated=rotate(parent_q,xyz)
                xyz=tuple(parent_pos[k]+rotated[k] for k in range(3)); q=quat_mul(parent_q,q)
            world.append((xyz,q))
        result.append(world)
    return result

def skin(vertices,weights,joints,center,extent):
    points=[]
    for _,_,start,count in vertices:
        out=[0.0,0.0,0.0]
        for joint,bias,pos in weights[start:start+count]:
            origin,q=joints[joint]; rotated=rotate(q,pos)
            for k in range(3): out[k]+=(origin[k]+rotated[k])*bias
        p=(out[0],out[2],-out[1])
        points.append(tuple((p[k]-center[k])*1.7/extent for k in range(3)))
    return points

def vertex_normals(points,triangles):
    normals=[[0.0,0.0,0.0] for _ in points]
    for tri in triangles:
        a,b,c=(points[i] for i in tri)
        u=[b[k]-a[k] for k in range(3)]; v=[c[k]-a[k] for k in range(3)]
        n=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0])
        for index in tri:
            for k in range(3): normals[index][k]+=n[k]
    for n in normals:
        mag=math.sqrt(sum(q*q for q in n)) or 1.0
        yield tuple(round(x/mag*32767) for x in n)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mesh", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--texture", type=Path)
    parser.add_argument("--animation", type=Path)
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
    normals=[[0.0,0.0,0.0] for _ in points]
    buckets=[[] for _ in range(16)]
    for tri in triangles:
        a,b,c=(points[i] for i in tri)
        u=[b[k]-a[k] for k in range(3)]; v=[c[k]-a[k] for k in range(3)]
        n=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0])
        mag=math.sqrt(sum(q*q for q in n))
        for index in tri:
            for k in range(3): normals[index][k]+=n[k]
        light=max(0.0,(n[0]*-0.3+n[1]*0.7+n[2]*0.64)/mag) if mag else 0.0
        buckets[min(15,int(light*15))].extend(tri)
    out=["/* Generated locally from the cloned dEngine MD5 assets. */",
         f"#define HK_VERTEX_COUNT {len(points)}", f"#define HK_INDEX_COUNT {len(triangles)*3}",
         "static const short hk_vertices[HK_VERTEX_COUNT][3]={"]
    out += ["{%d,%d,%d}," % tuple(round(x*32767) for x in p) for p in points]
    out += ["};", "static const short hk_normals[HK_VERTEX_COUNT][3]={"]
    for n in normals:
        mag=math.sqrt(sum(q*q for q in n)) or 1.0
        out.append("{%d,%d,%d}," % tuple(round(x/mag*32767) for x in n))
    out += ["};", "static const float hk_uv[HK_VERTEX_COUNT][2]={"]
    out += ["{%s,%s}," % (repr(v[0]),repr(v[1])) for v in vertices]
    out += ["};", "static const unsigned short hk_indices[HK_INDEX_COUNT]={"]
    for bucket in buckets:
        for k in range(0,len(bucket),24): out.append(",".join(map(str,bucket[k:k+24]))+",")
    out += ["};", "static const unsigned short hk_group_counts[16]={"+",".join(str(len(b)) for b in buckets)+"};"]
    if args.animation:
        frames=[skin(vertices,weights,pose,center,extent) for pose in animation_skeletons(args.animation)]
        if any(abs(x)>1.0 for frame in frames for point in frame for x in point):
            raise SystemExit("animation exceeds normalized mesh bounds")
        out.append("#define HK_ANIMATION_FRAMES 8")
        out.append("static const short hk_animated[HK_ANIMATION_FRAMES][HK_VERTEX_COUNT][3]={")
        for points_frame in frames:
            out.append("{")
            out.extend("{%d,%d,%d}," % tuple(round(x*32767) for x in p) for p in points_frame)
            out.append("},")
        out.append("};")
        out.append("static const short hk_animated_normals[HK_ANIMATION_FRAMES][HK_VERTEX_COUNT][3]={")
        for points_frame in frames:
            out.append("{")
            out.extend("{%d,%d,%d}," % n for n in vertex_normals(points_frame,triangles))
            out.append("},")
        out.append("};")
    if args.texture:
        pixels,w,h=read_rgb_png(args.texture)
        out.append("static const unsigned char hk_diffuse[32*32*4]={")
        for y in range(32):
            row=[]
            for x in range(32):
                i=((y*h//32)*w+x*w//32)*3
                row.extend((*pixels[i:i+3],255))
            out.append(",".join(map(str,row))+",")
        out.append("};")
    args.output.write_text("\n".join(out)+"\n")
    print(f"Baked {len(joints)} joints, {len(points)} vertices, {len(triangles)} triangles into {args.output}")


def read_rgb_png(path):
    data=path.read_bytes()
    if data[:8]!=b"\x89PNG\r\n\x1a\n": raise SystemExit("not a PNG")
    pos=8; compressed=b""; w=h=0
    while pos<len(data):
        size=struct.unpack_from(">I",data,pos)[0]; kind=data[pos+4:pos+8]; chunk=data[pos+8:pos+8+size]; pos+=12+size
        if kind==b"IHDR":
            w,h,bit,color,compression,flt,interlace=struct.unpack(">IIBBBBB",chunk)
            if (bit,color,compression,flt,interlace)!=(8,2,0,0,0): raise SystemExit("expected noninterlaced 8-bit RGB PNG")
        elif kind==b"IDAT": compressed+=chunk
        elif kind==b"IEND": break
    raw=zlib.decompress(compressed); stride=w*3; previous=bytearray(stride); result=bytearray()
    if len(raw)!=(stride+1)*h: raise SystemExit("unexpected PNG size")
    for y in range(h):
        mode=raw[y*(stride+1)]; row=bytearray(raw[y*(stride+1)+1:(y+1)*(stride+1)])
        for i in range(stride):
            a=row[i-3] if i>=3 else 0; b=previous[i]; c=previous[i-3] if i>=3 else 0
            if mode==1: v=a
            elif mode==2: v=b
            elif mode==3: v=(a+b)//2
            elif mode==4:
                p=a+b-c; pa=abs(p-a); pb=abs(p-b); pc=abs(p-c)
                v=a if pa<=pb and pa<=pc else b if pb<=pc else c
            elif mode==0: v=0
            else: raise SystemExit("unknown PNG filter")
            row[i]=(row[i]+v)&255
        result.extend(row); previous=row
    return result,w,h


if __name__ == "__main__":
    main()
