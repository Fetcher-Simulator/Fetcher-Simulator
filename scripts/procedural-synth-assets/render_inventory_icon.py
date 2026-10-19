"""Blender: render an inventory icon from the actual installed keyboard NIF."""
import argparse
import sys
from pathlib import Path

import bpy
from mathutils import Vector

p = argparse.ArgumentParser()
p.add_argument('--assets', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
args = p.parse_args(sys.argv[sys.argv.index('--') + 1:])
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.ops.preferences.addon_enable(module='io_scene_mw')
bpy.context.preferences.addons['io_scene_mw'].preferences.scale_correction = 1.0
bpy.ops.import_scene.mw(filepath=str(args.assets / 'meshes/fetcher/synth/keyboard-stand.nif'),
    attach_keyframe_data=False, discard_root_transforms=False, use_existing_materials=False,
    ignore_collision_nodes=False, ignore_custom_normals=False, ignore_animations=True)
for obj in bpy.context.scene.objects:
    if obj.name.startswith('Collision'):
        obj.hide_render = True
for image in bpy.data.images:
    if image.source == 'FILE':
        name = Path(image.filepath.replace('\\', '/')).name
        target = args.assets / 'textures/fetcher/synth' / name
        if target.is_file():
            image.filepath = str(target)
            image.reload()
meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH' and not o.hide_render]
points = [o.matrix_world @ Vector(c) for o in meshes for c in o.bound_box]
low = Vector(tuple(min(q[i] for q in points) for i in range(3)))
high = Vector(tuple(max(q[i] for q in points) for i in range(3)))
center = (high + low) / 2
width = max(high - low)
camera_data = bpy.data.cameras.new('InventoryCamera')
camera = bpy.data.objects.new('InventoryCamera', camera_data)
bpy.context.collection.objects.link(camera)
camera.location = center + Vector((0.45, -1.1, 0.65)) * width
camera.rotation_euler = (center - camera.location).to_track_quat('-Z', 'Y').to_euler()
camera_data.type = 'ORTHO'
camera_data.ortho_scale = width * 1.25
scene = bpy.context.scene
scene.camera = camera
for name, offset, energy in [('Key', (0, -1, 1.5), 900000), ('Fill', (-1, 0, 1), 600000)]:
    data = bpy.data.lights.new(name, 'AREA')
    data.energy = energy
    data.size = width
    obj = bpy.data.objects.new(name, data)
    bpy.context.collection.objects.link(obj)
    obj.location = center + Vector(offset) * width
    obj.rotation_euler = (center - obj.location).to_track_quat('-Z', 'Y').to_euler()
scene.render.engine = 'BLENDER_EEVEE_NEXT'
scene.render.resolution_x = scene.render.resolution_y = 256
scene.render.resolution_percentage = 100
scene.render.film_transparent = True
scene.render.image_settings.file_format = 'PNG'
scene.render.image_settings.color_mode = 'RGBA'
scene.view_settings.view_transform = 'Standard'
args.output.parent.mkdir(parents=True, exist_ok=True)
scene.render.filepath = str(args.output)
bpy.ops.render.render(write_still=True)
