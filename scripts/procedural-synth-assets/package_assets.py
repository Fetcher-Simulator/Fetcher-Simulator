"""Build the credited, hash-manifested runtime bundle after Blender conversion."""
import argparse
import hashlib
import json
import struct
import zipfile
from pathlib import Path

def write_utf8(path, text):
    # Manifested files must have identical bytes on Windows and Unix.
    path.write_bytes(text.encode('utf-8'))

p = argparse.ArgumentParser()
p.add_argument('--source', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--working', type=Path, required=True)
p.add_argument('--zip', type=Path, required=True)
args = p.parse_args()
metadata = json.loads((args.source / 'provenance.json').read_text(encoding='utf-8'))
report = json.loads((args.working / 'conversion-report.json').read_text(encoding='utf-8'))
if report.get('roundTripImport') != 'PASS' or report.get('serializedTriangles') != 6908:
    raise ValueError('Run a successful NIF conversion/validation first')
model = 'meshes/fetcher/synth/keyboard.nif'
if not (args.output / model).read_bytes().startswith(b'NetImmerse File Format, Version 4.0.0.2'):
    raise ValueError('Unexpected NIF version')
for name, size, compression in [('keyboard.dds', 2048, b'DXT1'), ('keyboard_n.dds', 1024, b'DXT5')]:
    data = (args.output / 'textures/fetcher/synth' / name).read_bytes()
    height, width = struct.unpack_from('<II', data, 12)
    mips = struct.unpack_from('<I', data, 28)[0]
    if data[:4] != b'DDS ' or (width, height) != (size, size) or data[84:88] != compression or mips < 10:
        raise ValueError('Invalid runtime DDS texture: ' + name)
credit = '''Keyboard (Casio VL-Tone)
Original author: AleixoAlonso
Author: https://sketchfab.com/AleixoAlonso
Source: https://sketchfab.com/3d-models/keyboard-casio-vl-tone-be22fbe1ef0044f3bedc6e3edf459593
License: Creative Commons Attribution 4.0 International (CC BY 4.0)
License URL: https://creativecommons.org/licenses/by/4.0/
License confirmed using the model's public Sketchfab API metadata on 2026-10-16.

Adaptation for OpenMW:
- FBX transformed to Morrowind NIF 4.0.0.2 using Blender 4.5 and io_scene_mw.
- Enlarged to 48 game units across for in-world readability; centered horizontally,
  with the bottom at local Z=0. Separate key/button geometry is retained.
- Original UV atlas and 6,896 visual triangles retained. Added a 12-triangle box collider.
- Original 2K color texture converted to mipmapped DXT1 DDS. Original normal map
  resized to 1K mipmapped DXT5 DDS, optionally detected by OpenMW's normal-map setting.
- PBR materials adapted to two conventional NIF materials; LCD glass approximated
  with alpha blending. Metalness/roughness are not used by this legacy material setup.

The model and derived textures remain under CC BY 4.0. Preserve this credit,
the license link/text and the indication of changes when redistributing the assets.
No endorsement by AleixoAlonso or Casio is implied. The engine and Lua code are
separate works under their own repository licenses.
'''
write_utf8(args.output / 'ATTRIBUTION.txt', credit)
write_utf8(args.output / 'LICENSE-CC-BY-4.0.txt', '''The keyboard model and its adapted textures are licensed under
Creative Commons Attribution 4.0 International (CC BY 4.0).

Original author: AleixoAlonso. See ATTRIBUTION.txt for source and changes.
License: https://creativecommons.org/licenses/by/4.0/
Full legal terms: https://creativecommons.org/licenses/by/4.0/legalcode.en

This notice supplies the license URI; it is not a reproduction of the legal text.
''')
metadata.pop('description', None)
write_utf8(args.output / 'provenance.json', json.dumps(metadata, indent=2) + '\n')
report.pop('roundTripTextureFiles', None)
report['file'] = model
write_utf8(args.output / 'conversion-report.json', json.dumps(report, indent=2) + '\n')
manifest = {'version': 1, 'packageId': 'fetcher.procedural-synth.assets', 'model': model, 'files': []}
for path in sorted(args.output.rglob('*')):
    if not path.is_file() or path.name == 'asset-manifest.json':
        continue
    data = path.read_bytes()
    manifest['files'].append({'path': path.relative_to(args.output).as_posix(), 'bytes': len(data),
                              'sha256': hashlib.sha256(data).hexdigest()})
write_utf8(args.output / 'asset-manifest.json', json.dumps(manifest, indent=2) + '\n')
args.zip.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(args.zip, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for path in sorted(args.output.rglob('*')):
        if path.is_file():
            z.write(path, path.relative_to(args.output).as_posix())
print('ASSET_BUNDLE_PASS', len(manifest['files']), 'verified files;', args.zip, args.zip.stat().st_size, 'bytes')
