#!/usr/bin/env python3
"""Bake the default dEngine demo's OBJ room and diffuse maps for T4.

Generated data stays in the run directory. Applies the matrix from demo.map;
materials and textures are read from the separately cloned checkout.
"""
import argparse
import re
from pathlib import Path
from export_hellknight import read_rgb_png

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
        if len(fields)>=2 and fields[0]=='newmtl': current=fields[1]
        elif len(fields)>=2 and fields[0]=='map_Kd' and current: result[current]=fields[1]
    return result

def export(root,output):
    scene,map_name,matrix,models=scene_data(root); lib=materials(root)
    verts=[]; triangles=[]; textures=[]; tex_index={}
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
                texture=lib[name]
                if texture not in tex_index: tex_index[texture]=len(textures); textures.append(texture)
                material=tex_index[texture]
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
                    triangles.append((face[i+1],face[i],face[0],material))
    out=['/* Generated locally from the original dEngine scene. */',
         f'#define ROOM_VERTEX_COUNT {len(verts)}',f'#define ROOM_TRIANGLE_COUNT {len(triangles)}',
         f'#define ROOM_TEXTURE_COUNT {len(textures)}',
         'typedef struct { float x,y,z,nx,ny,nz,u,v; } room_source_vertex;',
         'typedef struct { unsigned short a,b,c,texture; } room_source_triangle;',
         'static const room_source_vertex room_vertices[ROOM_VERTEX_COUNT]={']
    out.extend('{'+','.join(f'{v:.8f}f' for v in row)+'},' for row in verts)
    out+=['};','static const room_source_triangle room_triangles[ROOM_TRIANGLE_COUNT]={']
    out.extend('{%d,%d,%d,%d},'%row for row in triangles)
    out+=['};','static const unsigned char room_textures[ROOM_TEXTURE_COUNT][16*16*4]={']
    for name in textures:
        pixels,w,h=read_rgb_png(root/name); out.append('{')
        for y in range(16):
            row=[]
            for x in range(16):
                i=((y*h//16)*w+x*w//16)*3; row.extend((*pixels[i:i+3],255))
            out.append(','.join(map(str,row))+',')
        out.append('},')
    out.append('};'); output.write_text('\n'.join(out)+'\n')
    print(f'Exported {scene} / {map_name}: {len(models)} OBJ models, {len(verts)} vertices, {len(triangles)} triangles, {len(textures)} diffuse maps')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('checkout',type=Path); p.add_argument('output',type=Path)
    a=p.parse_args(); export(a.checkout,a.output)
