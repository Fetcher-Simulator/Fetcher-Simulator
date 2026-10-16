"""Extract a user's legitimate original-format download and preserve license metadata."""
import argparse
import hashlib
import io
import json
import urllib.request
import zipfile
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('archive', type=Path)
p.add_argument('working_root', type=Path)
args = p.parse_args()
root = args.working_root.resolve()
source = root / 'original'
digest = hashlib.sha256(args.archive.read_bytes()).hexdigest()
provenance_path = source / 'provenance.json'
if provenance_path.exists():
    previous = json.loads(provenance_path.read_text(encoding='utf-8'))
    if previous['sourceArchiveSha256'] != digest:
        raise ValueError('Existing original source belongs to a different archive; choose a new working root')
    print('Existing original source and provenance preserved:', source)
    raise SystemExit(0)
api = 'https://api.sketchfab.com/v3/models/be22fbe1ef0044f3bedc6e3edf459593'
with urllib.request.urlopen(api, timeout=30) as response:
    metadata = json.load(response)
if not metadata['license']['url'].rstrip('/').endswith('licenses/by/4.0'):
    raise ValueError('Source license changed; inspect the authoritative metadata before conversion')
source.mkdir(parents=True, exist_ok=True)
for name in ('working', 'output'):
    (root / name).mkdir(exist_ok=True)
with zipfile.ZipFile(args.archive) as outer:
    with zipfile.ZipFile(io.BytesIO(outer.read('source/Casio_VL-Tone.zip'))) as inner:
        for member in inner.infolist():
            if not (source / member.filename).resolve().is_relative_to(source):
                raise ValueError('Unsafe source archive path')
        inner.extractall(source)
provenance = {key: metadata.get(key) for key in ('uid', 'name', 'license', 'publishedAt', 'isDownloadable', 'vertexCount', 'faceCount')}
provenance.update(author={key: metadata['user'][key] for key in ('username', 'displayName', 'profileUrl')},
    sourceUrl='https://sketchfab.com/3d-models/keyboard-casio-vl-tone-be22fbe1ef0044f3bedc6e3edf459593',
    metadataUrl=api, verifiedDate=__import__('datetime').date.today().isoformat(), sourceArchiveSha256=digest)
provenance_path.write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')
print('Verified CC BY 4.0 source:', source)
