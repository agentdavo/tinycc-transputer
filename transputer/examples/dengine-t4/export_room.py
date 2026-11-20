#!/usr/bin/env python3
"""Bake the default dEngine demo's OBJ room and diffuse maps for T4.

Generated data stays in the run directory. Applies the matrix from demo.map;
materials and textures are read from the separately cloned checkout.
"""
import argparse
import re
import math
from pathlib import Path
from export_hellknight import read_rgba_png

def strip_comments(s): return re.sub(r'//[^\n]*|#[^\n]*','',s)

def scene_data(root):
    config=strip_comments((root/'data/config.cfg').read_text())
    scene=re.search(r'\bscene\s*\{\s*(\S+)',config).group(1)
    content=strip_comments((root/scene).read_text())
    name=re.search(r'\bfilename\s*:\s*(\S+)',content).group(1)
    text=strip_comments((root/name).read_text())
    block=re.search(r'\bmatrix\s*\{([^}]*)\}\s*entities\s+OBJ\s*\{([^}]*)\}',text,re.S)
    if not block: raise ValueError('no matrix and OBJ entities in map')
    matrix=[float(n) for n in block.group(1).split()]
    if len(matrix)!=16 or matrix[12:]!=[0,0,0,1]: raise ValueError('unsupported map matrix')
    models=re.findall(r'\bmodel\s+(\S+)',block.group(2))
    if not models: raise ValueError('empty room')
    return scene,name,matrix,models

def materials(root):
    text=(root/'data/materials/level1.mtl').read_text()
    result={}; current=None
    for line in text.splitlines():
        fields=line.split()
        if len(fields)>=2 and fields[0]=='newmtl': current=fields[1]; result[current]={}
        elif len(fields)>=2 and current and fields[0] in ('map_Kd','BumpKd','SpecKd','hasAlpha'): result[current][fields[0]]=fields[1]
    return result

def export(root,output,texture_size=16):
    if not 1<=texture_size<=512: raise ValueError('texture size must be 1..512')
    scene,map_name,matrix,models=scene_data(root); lib=materials(root)
    verts=[]; triangles=[]; textures=[]; tex_index={}; mats=[]; mat_index={}
    def texture_id(path):
        if not path: return 65535
        if path not in tex_index: tex_index[path]=len(textures); textures.append(path)
        return tex_index[path]
    for model in models:
        pos=[]; normal=[]; uv=[]; dedup={}; material=None
        for line in (root/model).read_text().splitlines():
            f=line.split()
            if not f or f[0].startswith('#'): continue
            if f[0]=='v': pos.append(tuple(map(float,f[1:4])))
            elif f[0]=='vn': normal.append(tuple(map(float,f[1:4])))
            elif f[0]=='vt': uv.append(tuple(map(float,f[1:3])))
            elif f[0]=='usemtl':
                name=f[1]
                if name not in lib: raise ValueError(f'missing material {name}')
                if name not in mat_index:
                    entry=lib[name]; mat_index[name]=len(mats)
                    mats.append((texture_id(entry.get('map_Kd')),texture_id(entry.get('BumpKd')),texture_id(entry.get('SpecKd')),int(entry.get('hasAlpha','0'))))
                material=mat_index[name]
            elif f[0]=='f':
                if material is None: raise ValueError('face without material')
                face=[]
                for ref in f[1:]:
                    if ref not in dedup:
                        ix=[int(x) for x in ref.split('/')]
                        if len(ix)!=3 or min(ix)<1: raise ValueError('unsupported OBJ index')
                        p=pos[ix[0]-1]; t=uv[ix[1]-1]; n=normal[ix[2]-1]
                        p=tuple(sum(matrix[k*4+j]*p[j] for j in range(3))+matrix[k*4+3] for k in range(3))
                        # Normals for the actual room matrix (translation only).
                        if matrix[:12:4]!=[1,0,0] or matrix[1:12:4]!=[0,1,0] or matrix[2:12:4]!=[0,0,1]:
                            raise ValueError('non-translation normal transform not implemented')
                        dedup[ref]=len(verts); verts.append((*p,*n,*t))
                    face.append(dedup[ref])
                for i in range(1,len(face)-1):
                    # dEngine's OBJ loader reverses its input winding.
                    a,b,c=face[i+1],face[i],face[0]
                    va,vb,vc=verts[a],verts[b],verts[c]
                    u=[vb[k]-va[k] for k in range(3)]; v=[vc[k]-va[k] for k in range(3)]
                    s1=vb[6]-va[6]; t1=vb[7]-va[7]; s2=vc[6]-va[6]; t2=vc[7]-va[7]; det=s1*t2-s2*t1
                    tangent=[(u[k]*t2-v[k]*t1)/det for k in range(3)] if abs(det)>1e-10 else [1,0,0]
                    mag=math.sqrt(sum(x*x for x in tangent)) or 1; tangent=[x/mag for x in tangent]
                    triangles.append((a,b,c,material,*tangent))
    out=['/* Generated locally from the original dEngine scene. */',
         f'#define ROOM_VERTEX_COUNT {len(verts)}',f'#define ROOM_TRIANGLE_COUNT {len(triangles)}',
         f'#define ROOM_TEXTURE_COUNT {len(textures)}',
         f'#define ROOM_TEXTURE_SIZE {texture_size}',
         f'#define ROOM_MATERIAL_COUNT {len(mats)}',
         'typedef struct { float x,y,z,nx,ny,nz,u,v; } room_source_vertex;',
         'typedef struct { unsigned short a,b,c,material; float tx,ty,tz; } room_source_triangle;',
         'typedef struct { unsigned short base,bump,specular,alpha; } room_source_material;',
         'static const room_source_vertex room_vertices[ROOM_VERTEX_COUNT]={']
    out.extend('{'+','.join(f'{v:.8f}f' for v in row)+'},' for row in verts)
    out+=['};','static const room_source_triangle room_triangles[ROOM_TRIANGLE_COUNT]={']
    out.extend('{%d,%d,%d,%d,%.8ff,%.8ff,%.8ff},'%row for row in triangles)
    out+=['};','static const room_source_material room_materials[ROOM_MATERIAL_COUNT]={']
    out.extend('{%d,%d,%d,%d},'%row for row in mats)
    out+=['};','static const unsigned char room_textures[ROOM_TEXTURE_COUNT][ROOM_TEXTURE_SIZE*ROOM_TEXTURE_SIZE*4]={']
    for name in textures:
        pixels,w,h=read_rgba_png(root/name); out.append('{')
        for y in range(texture_size):
            row=[]
            for x in range(texture_size):
                i=((y*h//texture_size)*w+x*w//texture_size)*4; row.extend(pixels[i:i+4])
            out.append(','.join(map(str,row))+',')
        out.append('},')
    out.append('};'); output.write_text('\n'.join(out)+'\n')
    print(f'Exported {scene} / {map_name}: {len(models)} OBJ models, {len(verts)} vertices, {len(triangles)} triangles, {len(mats)} materials, {len(textures)} maps')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('checkout',type=Path); p.add_argument('output',type=Path);p.add_argument('--texture-size',type=int,default=16)
    a=p.parse_args(); export(a.checkout,a.output,a.texture_size)
