#!/usr/bin/env python3
"""Check the room exporter against the separately cloned demo assets."""
import tempfile
from pathlib import Path
from export_room import export, scene_data, materials
from export_hellknight import read_rgba_png

root=Path(__file__).resolve().parents[1]/'dEngine'
scene,map_name,matrix,models=scene_data(root)
assert scene=='data/scenes/techDemo.scene' and map_name=='data/map/demo.map'
assert matrix==[1,0,0,150, 0,1,0,80, 0,0,1,130, 0,0,0,1]
assert len(models)==29
lib=materials(root)
assert lib['textures/base_door/jumbodoor']['SpecKd'].endswith('jumbodoor_s.png')
assert lib['textures/base_wall/sopanel20']['hasAlpha']=='1'
alpha,w,h=read_rgba_png(root/lib['textures/base_wall/sopanel20']['map_Kd'])
assert len(alpha)==w*h*4 and any(alpha[i]!=255 for i in range(3,len(alpha),4))
with tempfile.TemporaryDirectory() as d:
    output=Path(d)/'room.h'; export(root,output); data=output.read_text()
    assert '#define ROOM_VERTEX_COUNT 970' in data
    assert '#define ROOM_TRIANGLE_COUNT 653' in data
    assert '#define ROOM_TEXTURE_COUNT 51' in data
    assert '#define ROOM_MATERIAL_COUNT 29' in data
    assert '#define ROOM_TEXTURE_SIZE 16' in data
    # First source vertex (14,46,-8), transformed by demo.map.
    assert '{164.00000000f,126.00000000f,122.00000000f,' in data
    export(root,output,8);assert '#define ROOM_TEXTURE_SIZE 8' in output.read_text()
    try: export(root,output,0)
    except ValueError: pass
    else: raise AssertionError('invalid texture size accepted')
print('demo room export tests OK')
