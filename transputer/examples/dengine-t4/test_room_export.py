#!/usr/bin/env python3
"""Check the room exporter against the separately cloned demo assets."""
import tempfile
from pathlib import Path
from export_room import export, scene_data

root=Path(__file__).resolve().parents[1]/'dEngine'
scene,map_name,matrix,models=scene_data(root)
assert scene=='data/scenes/techDemo.scene' and map_name=='data/map/demo.map'
assert matrix==[1,0,0,150, 0,1,0,80, 0,0,1,130, 0,0,0,1]
assert len(models)==29
with tempfile.TemporaryDirectory() as d:
    output=Path(d)/'room.h'; export(root,output); data=output.read_text()
    assert '#define ROOM_VERTEX_COUNT 970' in data
    assert '#define ROOM_TRIANGLE_COUNT 653' in data
    assert '#define ROOM_TEXTURE_COUNT 28' in data
    # First source vertex (14,46,-8), transformed by demo.map.
    assert '{164.00000000f,126.00000000f,122.00000000f,' in data
print('demo room export tests OK')
