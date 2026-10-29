"""Append the credited sheet booklet to existing holders without re-exporting them.

Run in Blender with io_scene_mw. Existing geometry, collision and materials are
preserved byte-for-byte at the NIF object level. Re-running replaces our named
sheet subtree, so conversion never accumulates duplicate pages.
"""
import bpy,sys,json,math,hashlib
from pathlib import Path
from mathutils import Matrix
import numpy as np
assets=Path(sys.argv[sys.argv.index('--')+1]).resolve()
bpy.ops.preferences.addon_enable(module='io_scene_mw')
from es3 import nif
sheet=assets/'meshes/fetcher/orchestra/sheet-music.nif';tag='Fetcher_Sheet_Music'
report={}
for path,position,angle,yaw in [
    ('meshes/fetcher/synth/keyboard-stand.nif',(0,6.7,92),70,0),
    ('meshes/fetcher/orchestra/keyboard-seated.nif',(0,6.7,68),70,0),
    ('meshes/fetcher/orchestra/music-stand.nif',(0,.6,64.6),-70,180),
]:
    target=assets/path;stream=nif.NiStream();stream.load(target)
    assert isinstance(stream.root,nif.NiNode)
    stream.root.children=[c for c in stream.root.children if c.name!=tag]
    original=sum(len(s.triangles) for s in stream.objects_of_type(nif.NiTriShapeData))
    paper=nif.NiStream();paper.load(sheet)
    rotation=np.array(Matrix.Rotation(math.radians(angle),3,'X')@Matrix.Rotation(math.radians(yaw),3,'Z'),dtype=np.float32)
    node=nif.NiNode(name=tag,flags=2,translation=np.array(position,dtype=np.float32),rotation=rotation,children=paper.roots)
    stream.root.children.append(node);stream.save(target)
    # Validate the serialized subtree, not just the in-memory graph.
    check=nif.NiStream();check.load(target)
    sheets=[o for o in check.objects_of_type(nif.NiNode) if o.name==tag]
    triangles=sum(len(s.triangles) for s in check.objects_of_type(nif.NiTriShapeData))
    assert len(sheets)==1 and triangles==original+212,(path,triangles,original)
    points=[]
    def visit(o,parent=np.eye(4)):
        if isinstance(o,nif.RootCollisionNode):return
        matrix=parent@o.matrix
        if isinstance(o,nif.NiTriShape):points.extend(o.data.vertices@matrix[:3,:3].T+matrix[:3,3])
        for child in getattr(o,'children',[]):visit(child,matrix)
    visit(check.root)
    report[path]={'baseTriangles':original,'sheetTriangles':212,'triangles':triangles,
        'position':position,'tiltDegrees':angle,'yawDegrees':yaw,'bounds':[np.min(points,axis=0).tolist(),np.max(points,axis=0).tolist()],
        'sha256':hashlib.sha256(target.read_bytes()).hexdigest()}
    if target.stem=='keyboard-seated':
        p=assets/'seated-keyboard-report.json';data=json.loads(p.read_text())
        for key in ['triangles','sha256','bounds','sheetTriangles']:data[key]=report[path][key]
        p.write_bytes((json.dumps(data,indent=2)+'\n').encode())
(assets/'music-holder-report.json').write_bytes((json.dumps(report,indent=2)+'\n').encode())
print('MUSIC_HOLDERS',json.dumps(report))
