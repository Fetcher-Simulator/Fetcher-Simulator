"""Audit real slap/pop source identity, register, coverage and natural tails."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import subprocess

r = Path(__file__).resolve().parents[2]
a = r/'files/server/optional-assets/fetcher.studio-samples'
catalog = json.loads((a/'catalog.json').read_text())
identities = set()
for name in ('slap', 'slap-short', 'pop', 'pop-short', 'thump'):
    ident = 'bass-'+name+'-v1'
    entry = next(x for x in catalog['banks'] if x['id'] == ident)
    report = json.loads((a/'manifests'/f'{ident}.json').read_text())
    assert report['license'] == entry['license'] == 'CC-BY-3.0'
    assert 'Bruce H. McCosar' in (a/'licenses'/f'{ident}-CC-BY-3.0.txt').read_text()
    assert report['archiveSha256'] == '6b2af3543dd111b9f62b53544c4a02ae633fb775e20b42591e006a3c0d610d53'
    data = (a/entry['path']).read_bytes()
    assert hashlib.sha256(data).hexdigest() == entry['sha256']
    native = json.loads(subprocess.check_output([str(Path(sys.argv[1]) if len(sys.argv)>1 else r/'build/synth-core-tests/Release/sample-bank-tool.exe'), str(a/entry['path'])]))
    assert native == entry['native']
    magic, count, frames, rate, reserved = struct.unpack_from('<8s4I', data)
    assert magic == b'FSBANK2\0' and count == (2 if name == 'thump' else 6) and rate == 48000 and reserved == 0
    records = [struct.unpack_from('<12BII', data, 24+i*20) for i in range(count)]
    low = 35 if name.startswith('pop') else 23; high = 76 if name.startswith('pop') or name == 'thump' else 64
    for key in range(128):
        matches = [z for z in records if z[0] <= key <= z[1]]
        assert len(matches) == (count if name == 'thump' else 1) if low <= key <= high else not matches
    for z, info in zip(records, report['zones']):
        assert z[2:4] == (1, 127), 'One actual dynamic, velocity scales it'
        assert z[6] == (2 if name == 'thump' else 1) and z[7:12] == (0,)*5
        assert z[4] == info['root'] and info['trimStartFrame'] == max(0, info['onsetFrame']-144)
        if name != 'thump':
            expected = [33,34,35,36,38,39] if name.startswith('slap') else [45,46,47,48,50,51]
            assert info['root'] in expected, 'Concert pitch; pop is one octave higher, not two'
        pcm = data[24+count*20+z[-2]*2:24+count*20+(z[-2]+z[-1])*2]
        assert hashlib.sha256(pcm).hexdigest() == info['pcmSha256'] and pcm[-2:] == b'\0\0'
        assert info['sourceSha256'] not in identities, 'Different techniques must be different recordings'
        identities.add(info['sourceSha256'])
    assert frames*4 == native['pcmBytes'] and len(data) == 24+count*20+frames*2
    print('PASS', ident, 'source identity, concert pitch, coverage, bounded natural tails and CC BY credits')
