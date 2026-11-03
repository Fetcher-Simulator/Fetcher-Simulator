"""Check shipped bass attack data through the production bank reader."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
assets = root / 'files/server/optional-assets/fetcher.studio-samples'
tool = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'build/synth-core-tests/Release/sample-bank-tool.exe'
catalog = json.loads((assets / 'catalog.json').read_text())
assert len(catalog['banks']) == 10
assert sum(r['native']['pcmBytes'] for r in catalog['banks']) == catalog['decodedBytesAllBanks'] < 252 * 1024 * 1024
assert 256 * 1024 * 1024 - catalog['decodedBytesAllBanks'] > 4 * 1024 * 1024
for entry in catalog['banks']:
    data = (assets / entry['path']).read_bytes()
    assert len(data) == entry['bytes'] and hashlib.sha256(data).hexdigest() == entry['sha256']
    native = json.loads(subprocess.check_output([str(tool), str(assets / entry['path'])], text=True))
    assert native == entry['native'], entry['id']
    if entry['id'].startswith('guitar-techs-'):
        report = json.loads((assets / 'manifests' / (entry['id'] + '.json')).read_text())
        assert entry['license'] == report['license'] == 'CC-BY-4.0'
        assert len(report['zones']) == 13 and report['source'].startswith('https://zenodo.org/')
        count = native['zones']; offset = 24 + count * 20
        records = [struct.unpack_from('<12BII', data, 24 + i * 20) for i in range(count)]
        for key in range(128):
            assert sum(r[0] <= key <= r[1] for r in records) == (1 if 35 <= key <= 86 else 0)
        for r, z in zip(records, report['zones']):
            assert r[2:8] == (1, 127, z['root'], 0, 1, 0), 'One actual recorded dynamic/performance, gated'
            assert z['sourceStartFrame'] == max(0, z['sourceOnsetFrame'] - 144)
            assert abs(z['sourceOnsetFrame'] / 48000 - z['annotationTime']) < .1, 'No late vibration or previous ringing note used as onset'
            pcm = data[offset + r[-2] * 2:offset + (r[-2] + r[-1]) * 2]
            assert hashlib.sha256(pcm).hexdigest() == z['pcmSha256'] and pcm[-2:] == b'\0\0'
        print('PASS', entry['id'], 'recorded open/palm identity, bounded cuts, original annotations, coverage and CC BY notice')
    if entry['id'] not in ('bass-short-v1', 'bass-ghost-v1'):
        continue
    report = json.loads((assets / 'manifests' / (entry['id'] + '.json')).read_text())
    magic, count, frames, rate, reserved = struct.unpack_from('<8s4I', data)
    assert magic == b'FSBANK2\0' and count == len(report['zones']) and rate == 48000 and reserved == 0
    assert len(data) == 24 + count * 20 + frames * 2
    records = [struct.unpack_from('<12BII', data, 24 + i * 20) for i in range(count)]
    for key in range(128):
        for velocity in (1, 63, 64, 127):
            matches = [r for r in records if r[0] <= key <= r[1] and r[2] <= velocity <= r[3]]
            assert len(matches) == (2 if 23 <= key <= 64 else 0), (entry['id'], key, velocity)
    source_hashes = set()
    for record, zone in zip(records, report['zones']):
        lo, hi, vl, vh, note, alternate, alternates, flags, group, off, pad1, pad2, start, length = record
        assert (note, lo, hi, vl, vh, alternate, alternates) == (zone['root'], zone['low'], zone['high'], zone['vl'], zone['vh'], zone['alternate'], 2)
        assert flags == group == off == pad1 == pad2 == 0, 'Gated melodic bank, no drum one-shots'
        assert 't1/' not in zone['sample'] and 't2/' not in zone['sample']
        assert zone['trimStartFrame'] == max(0, zone['onsetFrame'] - 144)
        assert length == zone['frames'] and length <= (60000 if entry['id'] == 'bass-short-v1' else 36000)
        pcm = data[24 + count * 20 + start * 2:24 + count * 20 + (start + length) * 2]
        assert hashlib.sha256(pcm).hexdigest() == zone['pcmSha256'] and pcm[-2:] == b'\0\0'
        assert zone['sourceSha256'] not in source_hashes, 'Alternates/layers must be distinct recordings'
        source_hashes.add(zone['sourceSha256'])
    print('PASS', entry['id'], count, 'bounded zones, actual alternate sources, coverage, trim, gated tails and identities')
print('PASS all ten production bank identities and shared PCM budget')
