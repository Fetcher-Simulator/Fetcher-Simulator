"""Build stage-only drum kit meshes from the credited static drumkit.nif.

Run in Blender with io_scene_mw enabled. The original inventory/world mesh
stays intact. All pieces retain their original kit coordinates and materials.
"""

import hashlib
import json
from pathlib import Path

import bpy
import numpy as np

bpy.ops.preferences.addon_enable(module="io_scene_mw")
from es3 import nif

assets = Path("files/server/optional-assets/fetcher.procedural-synth")
models = assets / "meshes/fetcher/orchestra"
source = models / "drumkit.nif"
parts = {
    "ride": ("Circle.003_Metal_0",),
    "crash": ("Circle.005_Metal_0",),
    "hat": ("Circle.006_Metal_0",),
    "hat-pedal": ("Cube.017_Metal_0",),
    "kick-pedal": ("Cube.013_Metal_0",),
    "beater": (
        "Cylinder.048_Metal_0", "Cylinder.049_Beige_0",
        "Cylinder.049_Black_0", "Cylinder.049_Metal_0",
    ),
    "kick-head": ("Cylinder.002_Black Glossy.001_0", "Cylinder.002_Black transp_0"),
}
ride_stand = {
    "Circle.003_Black_0", "Cylinder.033_Metal_0", "Cylinder.035_Black_0",
    "Cylinder.035_Metal_0", "Cylinder.036_Black_0", "Cylinder.036_Metal_0",
    "Cylinder.037_Black_0", "Cylinder.037_Metal_0", "Cylinder.058_Metal_0",
}
ride_shift = np.array([-14, -4, 0], dtype=np.float32)
moving = {name for group in parts.values() for name in group}


def load_source():
    stream = nif.NiStream()
    stream.load(source)
    return stream


def shape_name(shape):
    return shape.name.removeprefix("CC_drumkit_")


def triangles(stream):
    return sum(len(shape.data.triangles) for shape in stream.objects_of_type(nif.NiTriShape))


original = load_source()
assert moving | ride_stand <= {shape_name(shape) for shape in original.root.children}
total = 0
for part in ["stage", *parts]:
    stream = load_source()
    if part == "stage":
        stream.root.children = [shape for shape in stream.root.children if shape_name(shape) not in moving]
    else:
        stream.root.children = [shape for shape in stream.root.children if shape_name(shape) in parts[part]]
    for shape in stream.root.children:
        if part == "ride" or shape_name(shape) in ride_stand:
            shape.translation += ride_shift
    destination = models / f"drumkit-{part}.nif"
    stream.save(destination)
    verified = nif.NiStream()
    verified.load(destination)
    count = triangles(verified)
    assert count > 0
    total += count
assert total == triangles(original), "Each source triangle must belong to exactly one stage mesh"

manifest = assets / "asset-manifest.json"
document = json.loads(manifest.read_text(encoding="utf-8"))
document["files"] = [
    {"path": file.relative_to(assets).as_posix(), "bytes": file.stat().st_size,
     "sha256": hashlib.sha256(file.read_bytes()).hexdigest()}
    for file in sorted(assets.rglob("*")) if file.is_file() and file != manifest
]
manifest.write_bytes((json.dumps(document, indent=2) + "\n").encode())
