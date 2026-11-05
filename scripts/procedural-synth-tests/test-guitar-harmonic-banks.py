"""Audit shipped harmonic PCM, source identity, coverage and actual alternates."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
assets = root / 'files/server/optional-assets/fetcher.studio-samples'
tool = Path(sys.argv[1])
catalog = json.loads((assets / 'catalog.json').read_text())
for technique, bounds in (('natural', (52, 83)), ('pinch', (60, 96))):
    ident = 'guitar-harmonics-' + technique + '-v1'
    entry = next(b for b in catalog['banks'] if b['id'] == ident)
    report = json.loads((assets / 'manifests' / (ident + '.json')).read_text())
    data = (assets / entry['path']).read_bytes()
    assert len(data) == entry['bytes'] and hashlib.sha256(data).hexdigest() == entry['sha256'] == report['sha256']
    assert json.loads(subprocess.check_output([str(tool), str(assets / entry['path'])])) == entry['native']
    assert report['license'] == entry['license'] == 'CC-BY-4.0'
    magic, count, frames, rate, reserved = struct.unpack_from('<8s4I', data)
    assert (magic, rate, reserved) == (b'FSBANK2\0', 48000, 0)
    assert len(data) == 24 + count * 20 + frames * 2 and count == len(report['zones'])
    records = [struct.unpack_from('<12BII', data, 24 + 20 * n) for n in range(count)]
    for key in range(128):
        matches = [r for r in records if r[0] <= key <= r[1]]
        assert bool(matches) == (bounds[0] <= key <= bounds[1]), (ident, key)
        if matches:
            assert len(matches) == matches[0][6] and sorted(r[5] for r in matches) == list(range(len(matches)))
            assert len({r[:5] + r[6:12] for r in matches}) == 1, 'Alternates agree on pitch/velocity range'
    for record, zone in zip(records, report['zones']):
        lo, hi, vl, vh, note, alternate, alternates, flags, group, off, pad1, pad2, start, length = record
        assert (note, lo, hi, alternate, alternates) == tuple(zone[k] for k in ('root', 'low', 'high', 'alternate', 'alternates'))
        assert (vl, vh, flags, group, off, pad1, pad2) == (1, 127, 0, 0, 0, 0, 0)
        assert 24000 <= length <= 96000 and length == zone['frames']
        assert abs(zone['annotationTime'] - zone['sourceOnsetFrame'] / 48000) < .1
        assert zone['sourceStartFrame'] == max(0, zone['sourceOnsetFrame'] - 144)
        assert zone['sourceString'] in 'EADGBe' and zone['sourceVelocity'] >= 30
        assert abs(zone['pitchErrorSemitones']) <= .45
        assert abs(zone['measuredPeakMidi'] - note) <= .45, 'Alternate takes cannot jump to another overtone pitch'
        assert (zone['overtone'] == 1 if technique == 'natural' else 2 <= zone['overtone'] <= 8)
        source = next(s for s in report['sources'] if s['id'] == zone['sourceId'])
        assert source['license'] == 'CC-BY-4.0' and source['archive'].startswith(zone['player'])
        for key in ('archiveSha256', 'audioSha256', 'annotationSha256'):
            assert len(source[key]) == 64
        pcm = data[24 + count * 20 + start * 2:24 + count * 20 + (start + length) * 2]
        assert hashlib.sha256(pcm).hexdigest() == zone['pcmSha256'] and pcm[:2] == pcm[-2:] == b'\0\0'
        values = struct.unpack('<' + str(length) + 'h', pcm)
        assert 500 < max(abs(v) for v in values) <= 22938
        assert sum(v*v for v in values) > length * 100, 'No silent captured zones'
    for root_note in {z['root'] for z in report['zones']}:
        takes = [z for z in report['zones'] if z['root'] == root_note]
        assert len({z['player'] for z in takes}) == len(takes)
        assert len({z['pcmSha256'] for z in takes}) == len(takes), 'No duplicated PCM sold as alternate takes'
    print('PASS', ident, count, 'audited recorded zones, gated tails, real alternates, register and source attribution')
notice = (assets / 'licenses/guitar-techs-ATTRIBUTION.txt').read_text(encoding='utf-8')
assert 'Package 79' in notice and '25-Hz' in notice
