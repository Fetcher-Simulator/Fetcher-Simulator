"""Blender/es3 validation of the actual NIF/DDS files and attribution hashes."""
import hashlib,json,sys,struct
from pathlib import Path
import bpy,numpy as np
root=Path(sys.argv[sys.argv.index('--')+1]).resolve()
bpy.ops.preferences.addon_enable(module='io_scene_mw')
from es3 import nif
manifest=json.loads((root/'asset-manifest.json').read_text());paths={x['path'] for x in manifest['files']}
for f in manifest['files']:
    p=root/f['path'];assert p.is_file() and p.stat().st_size==f['bytes'] and hashlib.sha256(p.read_bytes()).hexdigest()==f['sha256'],f['path']
report=json.loads((root/'sketchfab-orchestra-report.json').read_text());models={};renderModels={};referenced=set()
for p in sorted((root/'meshes').rglob('*.nif')):
    assert p.read_bytes().startswith(b'NetImmerse File Format, Version 4.0.0.2'),p
    stream=nif.NiStream();stream.load(p)
    shapes=list(stream.objects_of_type(nif.NiTriShapeData));triangles=sum(len(x.triangles) for x in shapes)
    assert 50<=triangles<=60000,p
    for shape in shapes:assert np.isfinite(shape.vertices).all(),p
    for texture in stream.objects_of_type(nif.NiSourceTexture):
        path=texture.filename.replace('\\','/');assert not path.startswith('/') and ':' not in path and '..' not in Path(path).parts and path in paths,(p,path)
        referenced.add(path)
    models[p.stem]=triangles
    collision=set()
    def collect(node):
        for child in getattr(node,'children',[]):
            collision.add(id(child));collect(child)
    for node in stream.objects_of_type(nif.RootCollisionNode):collect(node)
    renderModels[p.stem]=sum(len(x.data.triangles) for x in stream.objects_of_type(nif.NiTriShape) if id(x) not in collision)
    if p.stem in report:assert report[p.stem]['triangles']==triangles,p
assert models['violin']+models['bow']==14614,'violin/bow split must retain every source triangle exactly once'
print('NIF_TRIANGLES',json.dumps(models))
# The standing model also has collision boxes; seated attachments are visual only.
assert renderModels['keyboard-seated']==renderModels['keyboard-stand']==12189+212,'Yamaha render geometry includes one booklet'
for model in ['meshes/fetcher/synth/keyboard-stand.nif','meshes/fetcher/orchestra/keyboard-seated.nif','meshes/fetcher/orchestra/music-stand.nif']:
    s=nif.NiStream();s.load(root/model)
    assert len([o for o in s.objects_of_type(nif.NiNode) if o.name=='Fetcher_Sheet_Music'])==1,model
assert 'chair' not in models and models['stool']==1036,'compact stool replaces the blue chair'
for p in root.rglob('*.dds'):
    data=p.read_bytes();assert data[:4]==b'DDS ' and struct.unpack_from('<I',data,28)[0]>=2,p
credits=json.loads((root/'sketchfab-models.json').read_text());notice=(root/'ATTRIBUTION.txt').read_text(encoding='utf-8')
for item in credits:assert item['source'] in notice and item['author'] in notice and item['licenseUrl'] in notice,item['id']
print('CC_ASSETS_PASS',json.dumps({'models':models,'verifiedFiles':len(paths),'textureReferences':len(referenced),'creditedSources':len(credits)}))
