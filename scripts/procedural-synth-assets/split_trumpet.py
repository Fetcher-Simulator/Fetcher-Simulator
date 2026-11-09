"""Split the credited trumpet into a body and three moving valve buttons.

Run in Blender with io_scene_mw available. Pieces stay in the original NIF
coordinate frame so the standing prop and the pose tuner share an origin.
"""

import hashlib
import json
from pathlib import Path

import bpy

bpy.ops.preferences.addon_enable(module="io_scene_mw")
from es3 import nif

assets = Path("files/server/optional-assets/fetcher.procedural-synth")
models = assets / "meshes/fetcher/orchestra"
source = models / "trumpet.nif"
buttons = {f"CC_trumpet_Knopf{number}_LP__0": f"valve-{number}" for number in range(1, 4)}


def load(path):
    stream = nif.NiStream()
    stream.load(path)
    return stream


def triangles(stream):
    return sum(len(shape.data.triangles) for shape in stream.objects_of_type(nif.NiTriShape))


original = load(source)
assert buttons.keys() <= {shape.name for shape in original.root.children}
total = 0
for part in ("body", *buttons.values()):
    stream = load(source)
    stream.root.children = [shape for shape in stream.root.children if buttons.get(shape.name, "body") == part]
    destination = models / f"trumpet-{part}.nif"
    stream.save(destination)
    count = triangles(load(destination))
    assert count > 0
    total += count
assert total == triangles(original)

manifest = assets / "asset-manifest.json"
document = json.loads(manifest.read_text(encoding="utf-8"))
document["files"] = [
    {"path": file.relative_to(assets).as_posix(), "bytes": file.stat().st_size,
     "sha256": hashlib.sha256(file.read_bytes()).hexdigest()}
    for file in sorted(assets.rglob("*")) if file.is_file() and file != manifest
]
manifest.write_bytes((json.dumps(document, indent=2) + "\n").encode())
