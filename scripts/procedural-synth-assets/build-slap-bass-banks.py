"""Build genuine CC BY 3.0 slap/pop recordings; never relabel finger bass.

This deliberately small archive has one performance/dynamic per pitch. Short
notes and muted thumps are separate recordings, not extra invented RR layers.
"""
import argparse
import array
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import zipfile

ARCHIVE = 'bmccosar_-_BMC_slap_bass_samples.zip'
SHA256 = '6b2af3543dd111b9f62b53544c4a02ae633fb775e20b42591e006a3c0d610d53'
SOURCE = 'https://ccmixter.org/files/bmccosar/13742'
LICENSE = 'https://creativecommons.org/licenses/by/3.0/'
ATTRIBUTION = ('BMC slap bass samples by Bruce H. McCosar (2008), CC BY 3.0.\n'
               + SOURCE + '\n' + LICENSE + '\n'
               'Fetcher adaptation: mono 48-kHz PCM16, onset trim, terminal fade,\n'
               'concert-pitch keyboard mapping and bounded native sample banks.\n'
               'No endorsement implied. One recorded dynamic/performance per pitch;\n'
               'the two muted thumps are actual different recordings. No normalization.\n')


def bounds(roots, low, high):
    return {r: (low if i == 0 else (roots[i-1]+r)//2+1,
                high if i == len(roots)-1 else (r+roots[i+1])//2)
            for i, r in enumerate(roots)}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('sources', type=Path); p.add_argument('tool', type=Path); p.add_argument('output', type=Path)
    a = p.parse_args(); archive = a.sources / ARCHIVE; output = a.output.resolve()
    assert hashlib.file_digest(archive.open('rb'), 'sha256').hexdigest() == SHA256
    catalog = json.loads((output/'catalog.json').read_text())
    pitches = [('A', 33), ('Bb', 34), ('B', 35), ('C', 36), ('D', 38), ('Eb', 39)]
    reports = []
    with zipfile.ZipFile(archive) as z:
        for technique, short in [('slap', False), ('slap', True), ('pop', False), ('pop', True), ('thump', False)]:
            ident = 'bass-' + technique + ('-short' if short else '') + '-v1'
            suffix = 'short' if short else 'long'
            roots = [note + (12 if technique == 'pop' else 0) for _, note in pitches]
            spans = bounds(roots, 35 if technique == 'pop' else 23, 76 if technique == 'pop' else 64)
            if technique == 'thump':
                rows = [('thump1.flac', 40, 23, 76, 0, 2), ('thump2.flac', 40, 23, 76, 1, 2)]
            else:
                rows = [(name.lower()+'pop_'+suffix+'.flac' if technique == 'pop' else name+'_'+suffix+'.flac',
                         root, *spans[root], 0, 1) for (name, _), root in zip(pitches, roots)]
            records, chunks, zones, frames = [], [], [], 0
            for name, root, low, high, alternate, alternates in rows:
                raw = z.read(name)
                pcm = array.array('h', subprocess.check_output(['ffmpeg', '-v', 'error', '-i', 'pipe:0',
                        '-ar', '48000', '-ac', '1', '-f', 's16le', '-'], input=raw))
                if sys.byteorder != 'little': pcm.byteswap()
                onset = next(i for i, value in enumerate(pcm) if abs(value) >= 19)
                start = max(0, onset - 144); source_frames = len(pcm)
                pcm = pcm[start:start + 4*48000]
                for i in range(max(0, len(pcm)-480), len(pcm)):
                    pcm[i] = round(pcm[i]*min(1, (len(pcm)-1-i)/480))
                if sys.byteorder != 'little': pcm.byteswap()
                data = pcm.tobytes(); length = len(data)//2
                records.append(struct.pack('<12BII', low, high, 1, 127, root, alternate, alternates,
                                           0, 0, 0, 0, 0, frames, length))
                chunks.append(data); frames += length
                zones.append(dict(sample=name, root=root, low=low, high=high, alternate=alternate,
                    sourceSha256=hashlib.sha256(raw).hexdigest(), sourceFrames=source_frames,
                    onsetFrame=onset, trimStartFrame=start, frames=length,
                    pcmSha256=hashlib.sha256(data).hexdigest()))
            path = output/'sound/fetcher/banks'/f'{ident}.fsb'
            path.write_bytes(struct.pack('<8s4I', b'FSBANK2\0', len(records), frames, 48000, 0)
                             + b''.join(records) + b''.join(chunks))
            native = json.loads(subprocess.check_output([str(a.tool.resolve()), str(path)], text=True))
            description = ('Recorded muted thumb thumps, two actual alternates.' if technique == 'thump' else
                f'Recorded {technique} {suffix} notes, six concert-pitch roots, one dynamic/performance each; '
                'nearest-root transposition outside the recorded octave, no invented velocity/RR layers.')
            report = dict(id=ident, path=path.relative_to(output).as_posix(), bytes=path.stat().st_size,
                sha256=hashlib.file_digest(path.open('rb'), 'sha256').hexdigest(), native=native,
                license='CC-BY-3.0', source=SOURCE, attribution=ATTRIBUTION,
                archive=ARCHIVE, archiveSha256=SHA256, description=description,
                conversion='Mono 48-kHz PCM16; 3-ms onset preroll at -65 dB, original natural tail (4-s cap), 10-ms end fade. Original levels.',
                zones=zones)
            (output/'manifests'/f'{ident}.json').write_text(json.dumps(report, indent=2)+'\n')
            (output/'licenses'/f'{ident}-CC-BY-3.0.txt').write_text(ATTRIBUTION)
            reports.append(report)
            print(json.dumps({k: report[k] for k in ('id', 'bytes', 'native')}), flush=True)
    ids = {r['id'] for r in reports}
    catalog['banks'] = [r for r in catalog['banks'] if r['id'] not in ids]
    catalog['banks'] += [{k: r[k] for k in ('id', 'path', 'bytes', 'sha256', 'native', 'source', 'description', 'license')}
                         for r in reports]
    catalog['decodedBytesAllBanks'] = sum(r['native']['pcmBytes'] for r in catalog['banks'])
    catalog['license'] = 'Per-bank: CC0-1.0, CC-BY-3.0 or CC-BY-4.0'
    (output/'catalog.json').write_text(json.dumps(catalog, indent=2)+'\n')


if __name__ == '__main__': main()
