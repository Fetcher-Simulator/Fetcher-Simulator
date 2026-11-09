"""Fit the two converted Sketchfab guitar shells to the electric grip.

Run after converting acoustic and classical. Their scale and outline stay
the same in the string plane. The acoustic uses the credited classical wood
textures in place of flat paint and charcoal materials.
"""

import hashlib
import json
from pathlib import Path

import bpy
import numpy as np

bpy.ops.preferences.addon_enable(module="io_scene_mw")
from es3 import nif

assets = Path("files/server/optional-assets/fetcher.procedural-synth")
report_path = assets / "sketchfab-orchestra-report.json"
report = json.loads(report_path.read_text(encoding="utf-8"))
depth_scale = {"acoustic": .74, "classical": .68}
wood = {"cc_acoustic_1.dds": "cc_classical_3.dds", "cc_acoustic_2.dds": "cc_classical_2.dds"}

for name, factor in depth_scale.items():
    entry = report[name]
    path = assets / entry["model"]
    assert hashlib.sha256(path.read_bytes()).hexdigest() == entry["sha256"]
    if entry.get("depthScale") == factor:
        continue
    assert "depthScale" not in entry, "Reconvert the source before changing its depth scale"
    stream = nif.NiStream()
    stream.load(path)
    for shape in stream.objects_of_type(nif.NiTriShape):
        assert np.allclose(shape.rotation, np.eye(3))
        shape.translation[2] *= factor
        mesh = shape.data
        mesh.vertices[:, 2] *= factor
        if len(mesh.normals):
            mesh.normals[:, 2] /= factor
            mesh.normals /= np.linalg.norm(mesh.normals, axis=1, keepdims=True).clip(min=1e-8)
        mesh.center[:] = (mesh.vertices.min(axis=0) + mesh.vertices.max(axis=0)) / 2
        mesh.radius = np.max(np.linalg.norm(mesh.vertices - mesh.center, axis=1))
    if name == "acoustic":
        for texture in stream.objects_of_type(nif.NiSourceTexture):
            stem = texture.filename.rsplit("\\", 1)[-1]
            if stem in wood:
                texture.filename = "textures\\fetcher\\orchestra\\" + wood[stem]
    stream.save(path)
    entry["depthScale"] = factor
    for corner in entry["bounds"]:
        corner[2] *= factor
    entry["sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()

for path in (report_path, assets / "orchestra-report.json"):
    updated = json.loads(path.read_text(encoding="utf-8"))
    for name in depth_scale:
        updated[name] = report[name]
    path.write_bytes((json.dumps(updated, indent=2) + "\n").encode())
