"""Run with Blender --background --factory-startup --disable-autoexec --python ... -- SOURCE_DIR REPORT."""
import bpy
import json
import sys
from pathlib import Path
from mathutils import Vector

source, report = map(Path, sys.argv[sys.argv.index('--') + 1:])
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.fbx(filepath=str(source / 'Casio_VL-Tone.fbx'), use_image_search=False)
bpy.context.view_layer.update()
objects = []
for obj in bpy.context.scene.objects:
    data = {'name': obj.name, 'type': obj.type, 'dimensions': list(obj.dimensions),
            'location': list(obj.location), 'rotation': list(obj.rotation_euler)}
    if obj.type == 'MESH':
        obj.data.calc_loop_triangles()
        data.update(vertices=len(obj.data.vertices), triangles=len(obj.data.loop_triangles),
                    uv=[uv.name for uv in obj.data.uv_layers], materials=[m.name if m else None for m in obj.data.materials])
    objects.append(data)
meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
points = [o.matrix_world @ Vector(c) for o in meshes for c in o.bound_box]
low = [min(p[i] for p in points) for i in range(3)]
high = [max(p[i] for p in points) for i in range(3)]
report.write_text(json.dumps({'boundsMin': low, 'boundsMax': high, 'objects': objects,
    'materials': [{'name': m.name, 'diffuse': list(m.diffuse_color), 'images':
        [n.image.filepath for n in m.node_tree.nodes if n.type == 'TEX_IMAGE' and n.image] if m.use_nodes else []}
        for m in bpy.data.materials]}, indent=2))
print('SOURCE_REPORT', report)
