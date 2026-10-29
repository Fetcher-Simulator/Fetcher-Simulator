"""Lower the existing credited Yamaha/X-stand adaptation without flattening keys."""
import bpy,sys,json,hashlib,tempfile
from pathlib import Path
from mathutils import Matrix,Vector
assets=Path(sys.argv[sys.argv.index('--')+1]).resolve()
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.ops.preferences.addon_enable(module='io_scene_mw')
bpy.context.preferences.addons['io_scene_mw'].preferences.scale_correction=1
bpy.context.preferences.addons['io_scene_mw'].preferences.texture_paths.add().name=str(assets/'textures')
from es3 import nif
# The standing NIF may already have pages from a previous pipeline run. The
# holder-dressing step adds them after lowering the keyboard, exactly once.
source=nif.NiStream();source.load(assets/'meshes/fetcher/synth/keyboard-stand.nif')
source.root.children=[c for c in source.root.children if c.name!='Fetcher_Sheet_Music']
with tempfile.TemporaryDirectory(prefix='fetcher-keyboard-') as temp:
    clean=Path(temp)/'keyboard.nif';source.save(clean)
    bpy.ops.import_scene.mw(filepath=str(clean),attach_keyframe_data=False,discard_root_transforms=False,use_existing_materials=False,ignore_collision_nodes=True,ignore_custom_normals=False,ignore_animations=True)
root=bpy.data.objects.new('CC_Yamaha_Seated',None);bpy.context.collection.objects.link(root)
objects=[o for o in bpy.context.scene.objects if o.type=='MESH' and not o.name.startswith('Collision')]
bpy.ops.object.select_all(action='DESELECT')
for o in objects:
    o.data.transform(o.matrix_world);o.parent=root;o.matrix_world=Matrix.Identity(4)
    for vertex in o.data.vertices:vertex.co.z=vertex.co.z*(60/84) if vertex.co.z<=84 else vertex.co.z-24
    o.data.update();o.select_set(True);o.mw.object_flags=2
root.select_set(True);bpy.context.view_layer.objects.active=root
output=assets/'meshes/fetcher/orchestra/keyboard-seated.nif'
bpy.ops.export_scene.mw(filepath=str(output),use_selection=True,export_animations=False,extract_keyframe_data=False,preserve_root_tranforms=False,preserve_material_names=True,vertex_precision=.0001)
from es3 import nif
stream=nif.NiStream();stream.load(output)
for texture in stream.objects_of_type(nif.NiSourceTexture):
    basename=Path(texture.filename.replace('\\','/')).name
    assert (assets/'textures/fetcher/synth'/basename).is_file(),texture.filename
    texture.filename='textures\\fetcher\\synth\\'+basename
stream.save(output)
points=[o.matrix_world@v.co for o in objects for v in o.data.vertices]
low=[min(p[i] for p in points) for i in range(3)];high=[max(p[i] for p in points) for i in range(3)]
report={'model':output.relative_to(assets).as_posix(),'sourceModel':'meshes/fetcher/synth/keyboard-stand.nif',
    'triangles':sum(len(x.triangles) for x in stream.objects_of_type(nif.NiTriShapeData)),
    'bounds':[low,high],'sha256':hashlib.sha256(output.read_bytes()).hexdigest(),
    'changes':'Visual-only seated adaptation; stand lowered 24 units, keyboard body/key thickness retained.'}
assert 80<high[2]<90 and low[2]>=-.01,report # Includes the Yamaha's raised music rest.
(assets/'seated-keyboard-report.json').write_bytes((json.dumps(report,indent=2)+'\n').encode())
print('SEATED_KEYBOARD',json.dumps(report))
