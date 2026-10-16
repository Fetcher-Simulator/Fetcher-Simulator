"""Read this static, identity-transform ASCII FBX without executing source code.

Blender's built-in importer accepts binary FBX only. This deliberately restricted
reader preserves polygon UVs/normals and refuses transforms or mappings it cannot
represent. It is not a general FBX animation/rig importer.
"""
import re
import bpy
from mathutils import Matrix, Vector


def import_stand(path):
    text = path.read_text(encoding='utf-8')
    if not text.startswith('; FBX 7.5.0'):
        raise ValueError('Expected the original static ASCII FBX 7.5 stand')
    models = dict(re.findall(r'\n\tModel: (\d+), "Model::([^"]+)", "Mesh" \{', text))
    connections = re.findall(r'C: "OO",(\d+),(\d+)', text)
    owners = dict(connections)
    for match in re.finditer(r'P: "(?:Lcl|Geometric) (Translation|Rotation|Scaling)"[^\n]*', text):
        values = [float(x) for x in match[0].split(',')[-3:]]
        if values != ([1, 1, 1] if match[1] == 'Scaling' else [0, 0, 0]):
            raise ValueError('Nonidentity FBX transforms require a full importer')
    rotation = Matrix.Rotation(1.5707963267948966, 3, 'X')  # FBX Y-up to Blender Z-up.

    def array(block, name, kind=float):
        match = re.search(r'\b' + name + r': \*(\d+) \{\s*a:([^}]*)\}', block)
        if not match:
            raise ValueError('Missing FBX array: ' + name)
        result = [kind(x.strip()) for x in match[2].split(',') if x.strip()]
        if len(result) != int(match[1]):
            raise ValueError('FBX array length mismatch: ' + name)
        return result

    objects = []
    for match in re.finditer(r'\n\tGeometry: (\d+), "[^"]*", "Mesh" \{(.*?)\n\t\}', text, re.S):
        block = match[2]
        vertices = array(block, 'Vertices')
        vertices = [rotation @ Vector(vertices[i:i+3]) for i in range(0, len(vertices), 3)]
        indices = array(block, 'PolygonVertexIndex', int)
        faces, face = [], []
        for index in indices:
            face.append(-index-1 if index < 0 else index)
            if index < 0:
                faces.append(face); face = []
        if face:
            raise ValueError('Unterminated FBX polygon')
        uv_block = re.search(r'LayerElementUV: 0 \{(.*?)\n\t\t\}', block, re.S)
        if not uv_block or '"ByPolygonVertex"' not in uv_block[1] or '"IndexToDirect"' not in uv_block[1]:
            raise ValueError('Unsupported FBX UV mapping')
        uv = array(uv_block[1], 'UV')
        uv_indices = array(uv_block[1], 'UVIndex', int)
        name = models[owners[match[1]]]
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata(vertices, [], faces)
        mesh.update()
        layer = mesh.uv_layers.new(name='UVMap')
        if len(mesh.loops) != len(uv_indices):
            raise ValueError('FBX UV loop count mismatch')
        for loop in mesh.loops:
            index = uv_indices[loop.index]
            layer.data[loop.index].uv = uv[index*2:index*2+2]
        normals_block = re.search(r'LayerElementNormal: 0 \{(.*?)\n\t\t\}', block, re.S)
        if normals_block and '"ByPolygonVertex"' in normals_block[1] and '"Direct"' in normals_block[1]:
            raw = array(normals_block[1], 'Normals')
            normals = [rotation @ Vector(raw[i:i+3]) for i in range(0, len(raw), 3)]
            if len(normals) != len(mesh.loops):
                raise ValueError('FBX normal loop count mismatch')
            for polygon in mesh.polygons:
                polygon.use_smooth = True
            mesh.normals_split_custom_set(normals)
        obj = bpy.data.objects.new(name, mesh)
        bpy.context.collection.objects.link(obj)
        objects.append(obj)
    if not objects:
        raise ValueError('No static meshes in the FBX')
    return objects
