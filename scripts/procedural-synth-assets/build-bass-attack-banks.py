"""Curate captured short/ghost finger-bass attacks from the pinned CC0 archive.

An offline, explicit subset conversion, not an SFZ interpreter. Existing banks
and their fingerprints remain untouched. No dynamics normalization is applied.
"""
import argparse
import array
import concurrent.futures
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import zipfile

spec = importlib.util.spec_from_file_location('band_banks', Path(__file__).with_name('build-band-sample-banks.py'))
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sources', type=Path)
    parser.add_argument('tool', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    archive_name = 'Black_And_Blue_Basses_1002.zip'
    pinned = next(s for s in json.loads(Path(__file__).with_name('sample-sources.json').read_text())['sources']
                  if s['asset'] == archive_name)
    archive = args.sources / archive_name
    assert hashlib.file_digest(archive.open('rb'), 'sha256').hexdigest() == pinned['sha256'], 'Source identity changed'
    maps = args.sources / 'metadata/Black_And_Blue_Basses_1002/Programs/maps'
    output = args.output.resolve()
    catalog_path = output / 'catalog.json'
    catalog = json.loads(catalog_path.read_text())
    reports = []
    definitions = [
        ('bass-short-v1', [('darkblack_stac_map.sfz', 1, 127)], 1.25,
         'Captured Darkblack staccato finger plucks: one recorded dynamic, two real alternates; not palm-muted or picked bass.'),
        ('bass-ghost-v1', [('darkblack_ghost_p_map.sfz', 1, 63), ('darkblack_ghost_f_map.sfz', 64, 127)], .75,
         'Captured Darkblack ghost finger attacks: two recorded dynamics, two real alternates; not slap/pop or a synthesized dead-note envelope.'),
    ]
    with zipfile.ZipFile(archive) as source:
        for ident, layers, cap, description in definitions:
            parsed = [(list(helpers.flat_map((maps / name).read_text())), vl, vh) for name, vl, vh in layers]
            # Use the central map, never the detuned t1/t2 unison copies. The
            # source writes bass one octave above concert pitch, as in v1.
            available = sorted({int(r.get('pitch_keycenter', 60)) - 12 for r in parsed[0][0]})
            assert available == list(range(23, 65)), 'Re-audit changed source range'
            roots = available[::3]
            if roots[-1] != available[-1]:
                roots.append(available[-1])
            bounds = helpers.spans(roots, available[0], available[-1])
            rows = []
            for regions, vl, vh in parsed:
                for root in roots:
                    variants = {r['sample'].replace('\\', '/').removeprefix('../'): r for r in regions
                                if int(r.get('pitch_keycenter', 60)) - 12 == root}
                    chosen = sorted(variants, key=lambda name: int(re.search(r'_rr(\d+)', name)[1]))[:2]
                    assert len(chosen) == 2
                    for alternate, name in enumerate(chosen):
                        assert name.startswith('Samples/darkblack/') and '/t1/' not in name and '/t2/' not in name
                        rows.append(dict(sample=name, root=root, low=bounds[root][0], high=bounds[root][1],
                                         vl=vl, vh=vh, alternate=alternate))

            # Materialize bytes before worker threads; ZipFile access stays serial.
            inputs = {r['sample']: source.read(r['sample']) for r in rows}

            def convert(name):
                raw = inputs[name]
                decoded = array.array('h', subprocess.check_output(
                    ['ffmpeg', '-v', 'error', '-i', 'pipe:0', '-ar', '48000', '-ac', '1', '-f', 's16le', '-'], input=raw))
                if sys.byteorder != 'little':
                    decoded.byteswap()
                onset = next((i for i, value in enumerate(decoded) if abs(value) >= 19), None)
                assert onset is not None, ('Silent source', name)
                start = max(0, onset - 144)
                total = len(decoded)
                decoded = decoded[start:start + round(cap * 48000)]
                for i in range(max(0, len(decoded) - 960), len(decoded)):
                    decoded[i] = round(decoded[i] * min(1, (len(decoded) - 1 - i) / 960))
                if sys.byteorder != 'little':
                    decoded.byteswap()
                return name, (decoded.tobytes(), dict(sourceSha256=hashlib.sha256(raw).hexdigest(),
                    sourceFrames=total, onsetFrame=onset, trimStartFrame=start))

            with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
                converted = dict(pool.map(convert, sorted(inputs)))
            records, pcm, provenance, frames = [], [], [], 0
            for row in rows:
                data, details = converted[row['sample']]
                length = len(data) // 2
                assert 2 <= length <= round(cap * 48000)
                records.append(struct.pack('<12BII', row['low'], row['high'], row['vl'], row['vh'], row['root'],
                    row['alternate'], 2, 0, 0, 0, 0, 0, frames, length))
                pcm.append(data)
                provenance.append({**row, **details, 'frames': length, 'pcmSha256': hashlib.sha256(data).hexdigest()})
                frames += length
            path = output / f'sound/fetcher/banks/{ident}.fsb'
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(struct.pack('<8s4I', b'FSBANK2\0', len(records), frames, 48000, 0)
                             + b''.join(records) + b''.join(pcm))
            native = json.loads(subprocess.check_output([str(args.tool.resolve()), str(path)], text=True))
            report = dict(id=ident, path=path.relative_to(output).as_posix(), bytes=path.stat().st_size,
                sha256=hashlib.file_digest(path.open('rb'), 'sha256').hexdigest(), native=native, license='CC0-1.0',
                source='https://shop.karoryfer.com/pages/free-black-and-blue-basses', archive=archive_name,
                archiveSha256=pinned['sha256'], description=description,
                conversion=f'Mono 48-kHz PCM16; central map roots -12, 3-ms onset preroll at -65 dB, {cap}-s maximum tail, 20-ms terminal fade. Original relative dynamics; no normalization.',
                zones=provenance)
            (output / 'manifests' / f'{ident}.json').write_text(json.dumps(report, indent=2) + '\n')
            (output / 'licenses' / f'{ident}-CC0.txt').write_bytes(source.read('license'))
            reports.append(report)
            print(json.dumps({k: report[k] for k in ('id', 'bytes', 'native')}), flush=True)
    # Idempotent extension: never drop old versions needed by saved projects.
    new_ids = {r['id'] for r in reports}
    catalog['banks'] = [r for r in catalog['banks'] if r['id'] not in new_ids]
    catalog['banks'] += [{k: r[k] for k in ('id', 'path', 'bytes', 'sha256', 'native', 'source', 'description')} for r in reports]
    catalog['decodedBytesAllBanks'] = sum(r['native']['pcmBytes'] for r in catalog['banks'])
    assert catalog['decodedBytesAllBanks'] < 248 * 1024 * 1024, 'Retain at least 8 MiB for ordinary samples'
    catalog_path.write_text(json.dumps(catalog, indent=2) + '\n')
    print(json.dumps({'decodedBytesAllBanks': catalog['decodedBytesAllBanks'], 'banks': len(catalog['banks'])}))


if __name__ == '__main__':
    main()
