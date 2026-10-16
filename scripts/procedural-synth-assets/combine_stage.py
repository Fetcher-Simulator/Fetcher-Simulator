"""Blender: combine the verified Yamaha OBJ and X-stand into one Morrowind NIF."""
import argparse
import json
import math
from pathlib import Path
import subprocess
import sys

import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, str(Path(__file__).parent))
from read_static_fbx import import_stand

p = argparse.ArgumentParser()
p.add_argument('--source', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--working', type=Path, required=True)
args = p.parse_args(sys.argv[sys.argv.index('--') + 1:])
args.working.mkdir(parents=True, exist_ok=True)
texture_dir = args.output / 'textures/fetcher/synth'
mesh_path = args.output / 'meshes/fetcher/synth/keyboard-stand.nif'
texture_dir.mkdir(parents=True, exist_ok=True)
mesh_path.parent.mkdir(parents=True, exist_ok=True)
for name, uid in [('yamaha-keyboard', 'ed15fc0df2d244a583e2ac0929b57045'),
                  ('keyboard-x-stand', 'd76fd9cba4234dd1891581c2df6b7b00')]:
    meta = json.loads((args.source / name / 'provenance.json').read_text())
    if meta['uid'] != uid or not meta['license']['url'].rstrip('/').endswith('licenses/by/4.0'):
        raise ValueError('Expected verified CC BY 4.0 source: ' + name)

def dds(source, name, size, normal=False):
    target = texture_dir / (name + '.dds')
    subprocess.run(['magick', str(source), '-resize', f'{size}x{size}', '-alpha', 'off', '-strip',
                    '-define', 'dds:compression=' + ('dxt5' if normal else 'dxt1'),
                    '-define', 'dds:mipmaps=' + str(int(math.log2(size))), str(target)], check=True)
    return target

yt = args.source / 'yamaha-keyboard/textures'
st = args.source / 'keyboard-x-stand/textures'
body_texture = dds(yt / 'Dek_Base_Color.png', 'yamaha_body', 2048)
keys_texture = dds(yt / 'lambert1_Base_Color.png', 'yamaha_keys', 1024)
buttons_texture = dds(yt / 'Keyboard_tex.png', 'yamaha_trim', 1024)
stand_texture = dds(st / 'Albedo_xStand.png', 'xstand', 2048)
dds(yt / 'Dek_Normal_OpenGL.png', 'yamaha_body_n', 1024, True)
dds(st / 'normal_xStand.jpg', 'xstand_n', 1024, True)

bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.ops.preferences.addon_enable(module='io_scene_mw')
bpy.context.preferences.addons['io_scene_mw'].preferences.scale_correction = 1.0
bpy.context.preferences.addons['io_scene_mw'].preferences.texture_paths.add().name = str(args.output / 'textures')
from io_scene_mw import nif_shader
from es3 import nif

def material(name, texture=None, color=(1,1,1,1)):
    m = nif_shader.create_material(name).mw
    m.diffuse_color[:] = color
    m.ambient_color[:] = (1,1,1,1)
    m.emissive_color[:] = (0,0,0,1)
    m.specular_color[:] = (0.12,0.12,0.12,1)
    m.shine = 16
    if texture:
        m.base_texture.image = bpy.data.images.load(str(texture), check_existing=True)
        m.base_texture.layer = 'UVMap'
    return m.material

mats = {'aiStandardSurface5SG': material('Yamaha body', body_texture),
        'aiStandardSurface1SG': material('Yamaha keys', keys_texture),
        'aiStandardSurface2SG': material('Yamaha trim', buttons_texture),
        'initialShadingGroup': material('Yamaha dark plastic', color=(0.035,0.035,0.035,1))}
stand_mat = material('Michael Perez X-stand', stand_texture)
root = bpy.data.objects.new('Fetcher_Yamaha_Keyboard_With_Stand', None)
bpy.context.collection.objects.link(root)
visuals = []

def bounds(objects, transform=Matrix.Identity(4)):
    points = [transform @ o.matrix_world @ Vector(c) for o in objects for c in o.bound_box]
    return (Vector(tuple(min(p[i] for p in points) for i in range(3))),
            Vector(tuple(max(p[i] for p in points) for i in range(3))))

rotate = Matrix.Rotation(math.pi/2, 4, 'Z')
bpy.ops.wm.obj_import(filepath=str(args.source / 'yamaha-keyboard/source/exportpaino/export piano.obj'))
keyboard = [o for o in bpy.context.scene.objects if o.type == 'MESH']
keyboard_rotate = Matrix.Rotation(-math.pi/2, 4, 'Z')
lo, hi = bounds(keyboard, keyboard_rotate)
keyboard_scale = 104 / (hi.x-lo.x)
keyboard_transform = Matrix.Translation(Vector((0,0,84))) @ Matrix.Scale(keyboard_scale,4) @ Matrix.Translation(
    Vector((-(lo.x+hi.x)/2, -(lo.y+hi.y)/2, -lo.z))) @ keyboard_rotate
for obj in keyboard:
    obj.data.transform(keyboard_transform @ obj.matrix_world)
    obj.matrix_world = Matrix.Identity(4)
    obj.name = 'Yamaha_Keyboard'
    for i, original in enumerate(list(obj.data.materials)):
        obj.data.materials[i] = mats[original.name]
    obj.parent = root
    obj.mw.object_flags = 2
    visuals.append(obj)
key_points = [obj.data.vertices[index].co for obj in keyboard for poly in obj.data.polygons
              if obj.data.materials[poly.material_index].name == mats['aiStandardSurface1SG'].name
              for index in poly.vertices]
key_lo = [min(p[i] for p in key_points) for i in range(3)]
key_hi = [max(p[i] for p in key_points) for i in range(3)]

stand = import_stand(args.source / 'keyboard-x-stand/source/X_Stand_2.fbx')
lo, hi = bounds(stand, rotate)
stand_scale = Matrix.Diagonal(Vector((66/(hi.x-lo.x),25/(hi.y-lo.y),84/(hi.z-lo.z),1)))
stand_transform = stand_scale @ Matrix.Translation(Vector((-(lo.x+hi.x)/2, -(lo.y+hi.y)/2,-lo.z))) @ rotate
for obj in stand:
    obj.data.transform(stand_transform @ obj.matrix_world)
    obj.matrix_world = Matrix.Identity(4)
    obj.data.materials.clear()
    obj.data.materials.append(stand_mat)
    obj.parent = root
    obj.mw.object_flags = 2
    visuals.append(obj)

collision = bpy.data.objects.new('Collision', None)
bpy.context.collection.objects.link(collision)
collision.parent = root
collision.mw.object_flags = 2
colliders = []

def box(name, location, dimensions, rotation=None):
    bpy.ops.mesh.primitive_cube_add(size=1, location=location)
    o = bpy.context.object
    o.name = name
    o.dimensions = dimensions
    if rotation:
        o.rotation_euler = rotation
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    o.parent = collision
    o.mw.object_flags = 2
    o.hide_render = True
    o.display_type = 'WIRE'
    colliders.append(o)

box('Collision_keyboard_body', (0,0,89), (104,30,10))
for x in [-30,30]:
    box('Collision_stand_foot', (x,0,1.5), (3,25,3))
for y in [-7,7]:
    for sign in [-1,1]:
        a,b = Vector((-30*sign,y,3)),Vector((25*sign,y,82))
        direction = b-a
        box('Collision_stand_crossbar', (a+b)*0.5, (2,2,direction.length), direction.to_track_quat('Z','Y').to_euler())

for o in visuals:
    o.data.calc_loop_triangles()
triangles = sum(len(o.data.loop_triangles) for o in visuals)
source_area = sum((o.data.vertices[t.vertices[1]].co-o.data.vertices[t.vertices[0]].co).cross(
    o.data.vertices[t.vertices[2]].co-o.data.vertices[t.vertices[0]].co).length*0.5
    for o in visuals for t in o.data.loop_triangles)
if triangles != 10208+1982:
    raise ValueError('Visual geometry count changed')
bpy.ops.object.select_all(action='DESELECT')
for obj in [root,collision]+visuals+colliders:
    obj.select_set(True)
bpy.context.view_layer.objects.active = root
bpy.ops.wm.save_as_mainfile(filepath=str(args.working / 'keyboard-stand.blend'))
bpy.ops.export_scene.mw(filepath=str(mesh_path), use_selection=True, export_animations=False,
    extract_keyframe_data=False, preserve_root_tranforms=False, preserve_material_names=True, vertex_precision=0.0001)
stream = nif.NiStream()
stream.load(mesh_path)
for texture in stream.objects_of_type(nif.NiSourceTexture):
    texture.filename = 'textures\\fetcher\\synth\\' + Path(texture.filename.replace('\\','/')).name
stream.save(mesh_path)
expected_triangles = triangles+12*len(colliders)
serialized_triangles = sum(len(d.triangles) for d in stream.objects_of_type(nif.NiTriShapeData))
removed = expected_triangles-serialized_triangles
serialized_area = 0
for shape in stream.objects_of_type(nif.NiTriShape):
    if shape.name.startswith('Collision'):
        continue
    for triangle in shape.data.triangles:
        a,b,c = [Vector(shape.data.vertices[i]) for i in triangle]
        serialized_area += (b-a).cross(c-a).length*0.5
if not 0 <= removed <= 3 or abs(serialized_area-source_area)>source_area*0.00001:
    raise ValueError('NIF export lost visual geometry or surface area')
for texture in stream.objects_of_type(nif.NiSourceTexture):
    if not (args.output / texture.filename.replace('\\','/')).is_file():
        raise ValueError('Unresolved NIF texture')

bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.mw(filepath=str(mesh_path), attach_keyframe_data=False, discard_root_transforms=False,
    use_existing_materials=False, ignore_collision_nodes=False, ignore_custom_normals=False, ignore_animations=True)
for o in bpy.context.scene.objects:
    if o.name.startswith('Collision'):
        o.hide_render = True
meshes = [o for o in bpy.context.scene.objects if o.type=='MESH' and not o.hide_render]
lo,hi = bounds(meshes)
if abs(lo.z)>0.02 or abs((hi-lo).x-104)>0.02:
    raise ValueError('NIF origin/scale validation failed')
report = dict(model='meshes/fetcher/synth/keyboard-stand.nif', visualTriangles=triangles-removed, sourceTriangles=triangles,
              removedDegenerateTriangles=removed, sourceArea=source_area, serializedArea=serialized_area,
              serializedTriangles=serialized_triangles, collisionBoxes=len(colliders),
              dimensions=list(hi-lo), keyboardBottom=84, keyBounds=[key_lo,key_hi],
              roundTripImport='PASS', origin='floor, horizontally centered',
              textures=[t.filename for t in stream.objects_of_type(nif.NiSourceTexture)])
(args.working / 'stage-report.json').write_text(json.dumps(report,indent=2))
center=(lo+hi)*0.5
camera_data=bpy.data.cameras.new('PreviewCamera')
camera=bpy.data.objects.new('PreviewCamera',camera_data)
bpy.context.collection.objects.link(camera)
camera.location=center+Vector((130,-190,110))
camera.rotation_euler=(center-camera.location).to_track_quat('-Z','Y').to_euler()
camera_data.type='ORTHO';camera_data.ortho_scale=155
scene=bpy.context.scene;scene.camera=camera
for name,offset,energy in [('Key',(40,-80,160),90000),('Fill',(-120,20,100),60000)]:
    data=bpy.data.lights.new(name,'AREA');data.energy=energy;data.size=120
    light=bpy.data.objects.new(name,data);bpy.context.collection.objects.link(light)
    light.location=center+Vector(offset)
    light.rotation_euler=(center-light.location).to_track_quat('-Z','Y').to_euler()
scene.world.color=(0.12,0.12,0.12)
scene.render.engine='BLENDER_EEVEE_NEXT'
scene.render.resolution_x=1100;scene.render.resolution_y=1100;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.view_settings.view_transform='Standard'
scene.render.filepath=str(args.working/'keyboard-stand-preview.png')
bpy.ops.render.render(write_still=True)
print('COMBINED_STAGE_PASS',json.dumps(report))
