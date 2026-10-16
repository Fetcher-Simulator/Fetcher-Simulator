"""Blender 4.5 + installed io_scene_mw conversion of AleixoAlonso's Casio source.

Usage: blender --background --factory-startup --disable-autoexec --python THIS --
       --source SOURCE --output ASSET_DATA_ROOT --working WORKING_ROOT
Original source/archive and the provenance.json remain outside the runtime bundle.
"""
import argparse
import bpy
import json
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path
from mathutils import Matrix, Vector

p = argparse.ArgumentParser()
p.add_argument('--source', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--working', type=Path, required=True)
p.add_argument('--width', type=float, default=48.0)
p.add_argument('--magick', default=shutil.which('magick'))
args = p.parse_args(sys.argv[sys.argv.index('--') + 1:])
if not args.magick or not 10 <= args.width <= 100:
    raise ValueError('ImageMagick and a 10..100-unit target width are required')
provenance = json.loads((args.source / 'provenance.json').read_text(encoding='utf-8'))
if provenance['uid'] != 'be22fbe1ef0044f3bedc6e3edf459593' or not provenance['license']['url'].rstrip('/').endswith('licenses/by/4.0'):
    raise ValueError('Expected the verified AleixoAlonso source under CC BY 4.0')
texture_dir = args.output / 'textures/fetcher/synth'
mesh_path = args.output / 'meshes/fetcher/synth/keyboard.nif'
for directory in (texture_dir, mesh_path.parent, args.working):
    directory.mkdir(parents=True, exist_ok=True)
textures = args.source / 'Textures/#INT0002_Textures_2k'
diffuse = texture_dir / 'keyboard.dds'
normal = texture_dir / 'keyboard_n.dds'
subprocess.run([args.magick, str(textures / '#INT0002_Textures_COL_2k.png'), '-alpha', 'off', '-strip',
                '-define', 'dds:compression=dxt1', '-define', 'dds:mipmaps=11', str(diffuse)], check=True)
subprocess.run([args.magick, str(textures / '#INT0002_Textures_NRML_2k.png'), '-resize', '1024x1024',
                '-alpha', 'off', '-strip', '-define', 'dds:compression=dxt5', '-define', 'dds:mipmaps=10', str(normal)], check=True)

bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.ops.preferences.addon_enable(module='io_scene_mw')
# Explicit file units avoid depending on saved user add-on preferences.
bpy.context.preferences.addons['io_scene_mw'].preferences.scale_correction = 1.0
bpy.context.preferences.addons['io_scene_mw'].preferences.texture_paths.add().name = str(args.output / 'textures')
from io_scene_mw import nif_shader
from es3 import nif

bpy.ops.import_scene.fbx(filepath=str(args.source / 'Casio_VL-Tone.fbx'), use_image_search=False)
bpy.context.view_layer.update()
source_objects = [o for o in bpy.context.scene.objects if o.type == 'MESH']
points = [o.matrix_world @ Vector(c) for o in source_objects for c in o.bound_box]
low = Vector(tuple(min(p[i] for p in points) for i in range(3)))
high = Vector(tuple(max(p[i] for p in points) for i in range(3)))
scale = args.width / (high.x - low.x)
transform = Matrix.Scale(scale, 4) @ Matrix.Translation(Vector((-(low.x + high.x)/2, -(low.y + high.y)/2, -low.z)))

body = nif_shader.create_material('Casio VL-Tone atlas').mw
body.diffuse_color[:] = (1, 1, 1, 1)
body.ambient_color[:] = (1, 1, 1, 1)
body.specular_color[:] = (0.12, 0.12, 0.12, 1)
body.emissive_color[:] = (0, 0, 0, 1)
body.shine = 14
body.base_texture.image = bpy.data.images.load(str(diffuse), check_existing=True)
body.base_texture.layer = 'UVMap'
glass = nif_shader.create_material('Casio LCD cover').mw
glass.diffuse_color[:] = (0.8, 0.9, 0.8, 1)
glass.ambient_color[:] = (0.25, 0.3, 0.25, 1)
glass.specular_color[:] = (0.6, 0.6, 0.6, 1)
glass.alpha = 0.12
glass.use_alpha_blend = True
glass.alpha_flags = 237
glass.shine = 60
root = bpy.data.objects.new('Fetcher_Casio_VL_Tone', None)
bpy.context.collection.objects.link(root)
visuals = []
source_parts = []
for source in source_objects:
    mesh = source.data.copy()
    mesh.transform(transform @ source.matrix_world)
    mesh.update()
    source_parts.append(source.name)
    for index, original in enumerate(list(mesh.materials)):
        mesh.materials[index] = glass.material if original and 'Glass' in original.name else body.material
    obj = bpy.data.objects.new(source.name.replace('#INT0002_', 'FS_Casio_'), mesh)
    bpy.context.collection.objects.link(obj)
    obj.parent = root
    obj.mw.object_flags = 2
    visuals.append(obj)
for source in source_objects:
    bpy.data.objects.remove(source, do_unlink=True)

height = (high.z - low.z) * scale
depth = (high.y - low.y) * scale
collision = bpy.data.objects.new('Collision', None)
bpy.context.collection.objects.link(collision)
collision.parent = root
collision.mw.object_flags = 2
bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, height * 0.5))
box = bpy.context.object
box.name = 'Collision_Keyboard_Box'
box.dimensions = (args.width, depth, height)
bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
box.parent = collision
box.mw.object_flags = 2
box.display_type = 'WIRE'
box.hide_render = True
bpy.ops.object.select_all(action='DESELECT')
for obj in [root, collision, box] + visuals:
    obj.select_set(True)
bpy.context.view_layer.objects.active = root
bpy.ops.wm.save_as_mainfile(filepath=str(args.working / 'keyboard-prepared.blend'))
result = bpy.ops.export_scene.mw(filepath=str(mesh_path), use_selection=True, export_animations=False,
    extract_keyframe_data=False, preserve_root_tranforms=False, preserve_material_names=True, vertex_precision=0.0001)
if 'FINISHED' not in result:
    raise RuntimeError(f'NIF export failed: {result}')
stream = nif.NiStream()
stream.load(mesh_path)
for texture in stream.objects_of_type(nif.NiSourceTexture):
    if texture.filename:
        basename = Path(str(texture.filename).replace('\\', '/')).name
        texture.filename = 'textures\\fetcher\\synth\\' + basename
stream.save(mesh_path)
objects = list(stream.objects())
report = {
    'blender': bpy.app.version_string, 'sourceParts': source_parts, 'sourceTriangles': 6896,
    'targetDimensions': [args.width, depth, height], 'sourceScaleFactor': scale,
    'file': str(mesh_path), 'bytes': mesh_path.stat().st_size,
    'blocks': dict(Counter(type(o).__name__ for o in objects)),
    'serializedTriangles': sum(len(o.triangles) for o in objects if isinstance(o, nif.NiTriShapeData)),
    'textures': [t.filename for t in stream.objects_of_type(nif.NiSourceTexture) if t.filename],
}
(args.working / 'conversion-report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')

# Validate actual serialized bytes through a fresh importer, then render that import.
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
result = bpy.ops.import_scene.mw(filepath=str(mesh_path), attach_keyframe_data=False, discard_root_transforms=False,
    use_existing_materials=False, ignore_collision_nodes=False, ignore_custom_normals=False, ignore_animations=True)
if 'FINISHED' not in result:
    raise RuntimeError('Exported NIF could not be imported')
render_objects = [o for o in bpy.context.scene.objects if o.type == 'MESH' and not o.name.startswith('Collision')]
for obj in bpy.context.scene.objects:
    if obj.name.startswith('Collision'):
        obj.hide_render = True
points = [o.matrix_world @ Vector(c) for o in render_objects for c in o.bound_box]
import_low = Vector(tuple(min(p[i] for p in points) for i in range(3)))
import_high = Vector(tuple(max(p[i] for p in points) for i in range(3)))
report['roundTripDimensions'] = list(import_high - import_low)
if any(abs(actual - expected) > 0.02 for actual, expected in zip(report['roundTripDimensions'], report['targetDimensions'])):
    raise RuntimeError('Unexpected NIF round-trip bounds')
if report['blocks'].get('RootCollisionNode') != 1:
    raise RuntimeError('Expected one simple collision root')
if report['serializedTriangles'] != 6896 + 12:
    raise RuntimeError('Unexpected triangle loss or duplication during NIF export')
for relative in report['textures']:
    if not (args.output / relative.replace('\\', '/')).is_file():
        raise RuntimeError('Missing exported texture: ' + relative)
report['roundTripImport'] = 'PASS'
report['roundTripTextureFiles'] = sorted({bpy.path.abspath(image.filepath) for image in bpy.data.images
    if image.source == 'FILE' and image.filepath and Path(bpy.path.abspath(image.filepath)).is_file()})
(args.working / 'conversion-report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
center = (import_high + import_low) * 0.5
camera_data = bpy.data.cameras.new('PreviewCamera')
camera = bpy.data.objects.new('PreviewCamera', camera_data)
bpy.context.collection.objects.link(camera)
camera.location = center + Vector((args.width * 0.45, -args.width * 0.85, args.width * 0.9))
camera.rotation_euler = (center - camera.location).to_track_quat('-Z', 'Y').to_euler()
camera_data.type = 'ORTHO'
camera_data.ortho_scale = args.width * 1.2
bpy.context.scene.camera = camera
for name, offset, energy, size in [('Key', (0,-0.5,1), 8000,0.8), ('Fill',(-1,0.2,0.7),4000,1)]:
    data = bpy.data.lights.new(name, 'AREA')
    data.energy = energy
    data.shape = 'DISK'
    data.size = args.width * size
    lamp = bpy.data.objects.new(name, data)
    bpy.context.collection.objects.link(lamp)
    lamp.location = center + Vector(offset) * args.width
    lamp.rotation_euler = (center - lamp.location).to_track_quat('-Z','Y').to_euler()
scene = bpy.context.scene
scene.render.engine = 'BLENDER_EEVEE_NEXT'
scene.render.resolution_x, scene.render.resolution_y, scene.render.resolution_percentage = 1200, 700, 100
scene.render.image_settings.file_format = 'PNG'
scene.render.filepath = str(args.working / 'keyboard-nif-preview.png')
scene.world.color = (0.08, 0.09, 0.12)
scene.view_settings.view_transform = 'Standard'
bpy.ops.render.render(write_still=True)
print('KEYBOARD_EXPORT_PASS', mesh_path)
