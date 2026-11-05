"""Build opt-in recorded harmonic banks from pinned CC BY Guitar-TECHS DI.

Both techniques use sounding-pitch keys; original fretted notes and recorded
overtones remain in provenance. Pitch tracking ghosts and weak
or misaligned attacks are rejected, not repaired by synthesized audio.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import zipfile

import numpy as np


def candidates(pcm, events, technique, player):
    result = []
    for time, note, string, name, velocity in events:
        if not (52 <= note <= 83 if technique == 'natural' else 48 <= note <= 76):
            continue
        left = max(0, round((time - .15) * 48000))
        window = pcm[left:round((time + .2) * 48000)]
        rms = np.sqrt(np.convolve(window.astype(float) ** 2, np.ones(96) / 96, 'valid'))
        floor = float(np.median(rms[:1200]))
        hits = np.flatnonzero(rms >= max(.0001, floor * 2.5, float(rms.max()) * .12))
        if not len(hits):
            continue
        onset = left + int(hits[0])
        if abs(onset / 48000 - time) >= .1:
            continue
        probe = pcm[onset + 4800:onset + 4800 + 16384].astype(float)
        probe -= probe.mean()
        spectrum = abs(np.fft.rfft(probe * np.hanning(len(probe))))
        spectrum[:14] = 0  # Exclude DC/handling noise below 40 Hz.
        peak = int(spectrum.argmax());hz = peak * 48000 / len(probe)
        sounding = 69 + 12 * np.log2(hz / 440)
        overtone = 1 if technique == 'natural' else round(2 ** ((sounding - note) / 12))
        expected = note + 12 * np.log2(max(1, overtone))
        if not (1 <= overtone <= 8 and (technique != 'pinch' or overtone >= 2)) or abs(sounding - expected) > .45:
            continue
        root = note if technique == 'natural' else round(sounding)
        if technique == 'pinch' and not 60 <= root <= 96:
            continue
        # Events within 250 ms often describe one tracking pickup/transient.
        # Stop before the next distinct written attack to avoid another note.
        next_time = min((e[0] for e in events if e[0] > time + .25), default=len(pcm) / 48000)
        start = max(0, onset - 144)
        end = min(len(pcm), start + 96000, round((next_time - .06) * 48000))
        if end - start < 24000:
            continue
        cut = pcm[start:end].astype(float).copy()
        strength = float(np.sqrt(np.mean(cut[:14400] ** 2)))
        if strength < .001:
            continue
        gain = min(.10 / strength, .7 / max(float(abs(cut).max()), 1e-9))
        cut *= gain
        cut[:144] *= np.linspace(0, 1, 144)
        cut[-960:] *= np.linspace(1, 0, 960)
        encoded = np.rint(np.clip(cut, -1, 1) * 32767).astype('<i2').tobytes()
        result.append(dict(root=root, annotationNote=note, player=player, sourceString=name, sourceVelocity=velocity,
            annotationTime=time, sourceOnsetFrame=onset, sourceStartFrame=start, frames=len(cut),
            gain=gain, measuredPeakMidi=float(sounding), overtone=overtone,
            pitchErrorSemitones=float(sounding - expected), strength=strength, pcm=encoded,
            pcmSha256=hashlib.sha256(encoded).hexdigest()))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('tool', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    source = Path(__file__).with_name('build-guitar-techs-banks.py')
    spec = importlib.util.spec_from_file_location('guitar_techs', source)
    module = importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    pins = json.loads(Path(__file__).with_name('sample-sources.json').read_text())['sources']
    sources = []; all_candidates = {'natural': [], 'pinch': []}
    for player in ('P1', 'P2'):
        archive = args.source / (player + '_techniques.zip')
        pin = next(p for p in pins if p['asset'] == archive.name)
        with archive.open('rb') as stream:
            assert 'md5:' + hashlib.file_digest(stream, 'md5').hexdigest() == pin['upstreamMd5']
        with archive.open('rb') as stream:
            archive_sha = hashlib.file_digest(stream, 'sha256').hexdigest()
        with zipfile.ZipFile(archive) as z:
            for technique, name in (('natural', 'Harmonics'), ('pinch', 'PinchHarmonics')):
                audio = next(n for n in z.namelist() if n.endswith('/directinput_' + name + '.wav') and not n.startswith('__'))
                annotation = next(n for n in z.namelist() if n.endswith('/midi_' + name + '.mid') and not n.startswith('__'))
                raw = z.read(audio);midi = z.read(annotation)
                pcm = np.frombuffer(subprocess.check_output(['ffmpeg', '-v', 'error', '-i', 'pipe:0',
                    '-af', 'highpass=f=25', '-ar', '48000', '-ac', '1', '-f', 'f32le', '-'], input=raw), dtype='<f4')
                events = module.string_events(midi, 30)
                choices = candidates(pcm, events, technique, player)
                ident = player + '/' + technique
                sources.append(dict(id=ident, archive=archive.name, archiveMd5=pin['upstreamMd5'], archiveSha256=archive_sha,
                    audio=audio, audioSha256=hashlib.sha256(raw).hexdigest(), annotation=annotation,
                    annotationSha256=hashlib.sha256(midi).hexdigest(), license='CC-BY-4.0'))
                for c in choices:
                    c['sourceId'] = ident
                all_candidates[technique] += choices
    catalog_path = args.output / 'catalog.json'
    catalog = json.loads(catalog_path.read_text())
    for technique, choices in all_candidates.items():
        requested = sorted({c['root'] for c in choices})
        selected = []
        for root in requested:
            root_choices = []
            for player in ('P1', 'P2'):
                available = [c for c in choices if c['root'] == root and c['player'] == player]
                if available:
                    root_choices.append(max(available, key=lambda c: (c['strength'], c['sourceVelocity'])))
            if root_choices:
                selected.append((root, root_choices))
        assert len(selected) >= 7, ('Too few audited harmonic roots', technique)
        chunks = [];records = [];zones = [];frames = 0
        for i, (root, takes) in enumerate(selected):
            low = root if i == 0 else (selected[i - 1][0] + root) // 2 + 1
            high = root if i == len(selected) - 1 else (root + selected[i + 1][0]) // 2
            for alternate, take in enumerate(takes):
                info = {k: v for k, v in take.items() if k != 'pcm'}
                info.update(low=low, high=high, alternate=alternate, alternates=len(takes))
                records.append(struct.pack('<12BII', low, high, 1, 127, root, alternate, len(takes), 0, 0, 0, 0, 0, frames, take['frames']))
                chunks.append(take['pcm']);zones.append(info);frames += take['frames']
        ident = 'guitar-harmonics-' + technique + '-v1'
        path = 'sound/fetcher/banks/' + ident + '.fsb';target = args.output / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(struct.pack('<8s4I', b'FSBANK2\0', len(records), frames, 48000, 0) + b''.join(records) + b''.join(chunks))
        native = json.loads(subprocess.check_output([str(args.tool.resolve()), str(target)]))
        report = dict(id=ident, path=path, bytes=target.stat().st_size, sha256=hashlib.sha256(target.read_bytes()).hexdigest(),
            native=native, license='CC-BY-4.0', source='https://zenodo.org/records/14963133',
            description='Recorded Guitar-TECHS ' + technique + ' harmonics; sounding-pitch keys, with original annotation/fretted notes and overtones retained in provenance.',
            conversion='Dual-mono DI averaged at 48 kHz, 25-Hz high-pass to remove DC/handling drift; MIDI/RMS aligned 3-ms preroll and fade-in, <=2-s cuts, 20-ms end fade. Strength normalization bounded at .7 peak; no pitch reconstruction or invented alternates.',
            sources=[s for s in sources if s['id'].endswith('/' + technique)], zones=zones)
        (args.output / 'manifests' / (ident + '.json')).write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        catalog['banks'] = [b for b in catalog['banks'] if b['id'] != ident]
        catalog['banks'].append({k: v for k, v in report.items() if k not in ('sources', 'zones', 'conversion')})
        print(ident, 'roots', [r for r, _ in selected], native, flush=True)
    catalog['decodedBytesAllBanks'] = sum(b['native']['pcmBytes'] for b in catalog['banks'])
    catalog_path.write_text(json.dumps(catalog, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
