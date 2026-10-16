import bpy
import json
import sys
from pathlib import Path

source, report = map(Path, sys.argv[sys.argv.index('--') + 1:])
bpy.ops.wm.open_mainfile(filepath=str(source / 'Casio_VL-Tone.blend'), load_ui=False, use_scripts=False)
out = []
for m in bpy.data.materials:
    if not m.name.startswith('#INT0002'):
        continue
    item = {'name': m.name, 'diffuse': list(m.diffuse_color), 'nodes': []}
    if m.use_nodes:
        for n in m.node_tree.nodes:
            inputs = {}
            for i in n.inputs:
                if i.is_linked or not hasattr(i, 'default_value'):
                    continue
                value = i.default_value
                try:
                    value = list(value)
                except TypeError:
                    pass
                if isinstance(value, (str, int, float, list)):
                    inputs[i.name] = value
            item['nodes'].append({'name': n.name, 'type': n.type, 'inputs': inputs})
    out.append(item)
report.write_text(json.dumps(out, indent=2))
print('MATERIAL_REPORT', report)
