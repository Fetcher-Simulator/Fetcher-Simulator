"""Build small CC BY 4.0 open/palm banks from original Guitar-TECHS P1/P2 DI.

Requires numpy, mido and ffmpeg. No imported player/plugin or SFZ runtime.
Archives/annotation identity, exact cuts and gain changes stay in provenance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import zipfile
import mido
import numpy as np


def string_events(annotations, minimum_velocity=1):
    """Track names, not channels, identify strings in these source MIDIs.

    Player 1 writes channel zero on all six tracks. Its low-velocity pickup
    ghosts must not outrank the intended recorded notes. Timing uses the full
    global tempo map, even when an event belongs to a separate string track.
    """
    import io
    midi = mido.MidiFile(file=io.BytesIO(annotations))
    tuning = {'E': (1, 40), 'A': (2, 45), 'D': (3, 50), 'G': (4, 55), 'B': (5, 59), 'e': (6, 64)}
    tempos = [(0, 500000)]; notes = []; found = set()
    for track in midi.tracks:
        name = next((e.name for e in track if e.type == 'track_name'), '')
        tick = 0
        for event in track:
            tick += event.time
            if event.type == 'set_tempo': tempos.append((tick, event.tempo))
            if name in tuning and event.type == 'note_on' and event.velocity >= minimum_velocity:
                string, open_note = tuning[name]
                if open_note <= event.note <= open_note + 22:
                    notes.append((tick, event.note, string, name, event.velocity)); found.add(name)
    assert found == set(tuning), 'Expected all six named source strings'
    tempos.sort(key=lambda entry: entry[0])
    def seconds(tick):
        clock = 0; previous = 0; tempo = 500000
        for at, value in tempos:
            if at > tick: break
            clock += mido.tick2second(at-previous, midi.ticks_per_beat, tempo)
            previous = at; tempo = value
        return clock + mido.tick2second(tick-previous, midi.ticks_per_beat, tempo)
    return [(seconds(tick), note, string, name, velocity) for tick, note, string, name, velocity in notes]


def closest_fret_event(events, root):
    # String 1 is low E, string 6 is high e. Higher open pitch gives the
    # smallest fret at an unchanged concert pitch. Stronger duplicate labels
    # outrank pickup ghosts on the same string.
    return max((e for e in events if e[1] == root), key=lambda e: (e[2], e[4]))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--player',choices=['P1','P2'],default='P2')
    parser.add_argument('--edition',choices=['v1','v2'],default='v1',help='v2 selects actual named string tracks; v1 preserves existing saved bank identities')
    parser.add_argument('source',type=Path);parser.add_argument('tool',type=Path);parser.add_argument('output',type=Path)
    args=parser.parse_args();out=args.output.resolve();reports=[]
    definitions=[('guitar-techs-open-v1','P2_singlenotes.zip','40fbf03d8b04bb2cf42df20f36dc2254','allsinglenotes',3.),
                 ('guitar-techs-palm-v1','P2_techniques.zip','f4189251ce50be25f06a173b2c2bba00','PalmMute',.7)]
    if args.player=='P1':
        pinned=json.loads((Path(__file__).parent/'sample-sources.json').read_text())['sources']
        definitions=[(ident.replace('guitar-techs-','guitar-techs-bridge-'),archive.replace('P2_','P1_'),
            next(r['upstreamMd5'].split(':')[1] for r in pinned if r['asset']==archive.replace('P2_','P1_')),name,cap)
            for ident,archive,md5,name,cap in definitions]
    roots=list(range(40,85,4))+[86]
    for ident,archive,md5,name,cap in definitions:
        if args.edition=='v2': ident=ident.removesuffix('-v1')+'-v2'
        path=args.source/archive
        with path.open('rb') as stream:assert hashlib.file_digest(stream,'md5').hexdigest()==md5,'Re-audit changed upstream archive'
        with path.open('rb') as stream:archive_sha=hashlib.file_digest(stream,'sha256').hexdigest()
        with zipfile.ZipFile(path) as z:
            audio=next(n for n in z.namelist() if n.endswith('/directinput_'+name+'.wav') and not n.startswith('__'))
            midi=next(n for n in z.namelist() if n.endswith('/midi_'+name+'.mid'))
            raw=z.read(audio);annotations=z.read(midi)
        # Decode the actual dual-mono DI capture; do not use amp/room channels.
        decoded=np.frombuffer(subprocess.check_output(['ffmpeg','-v','error','-i','pipe:0','-ar','48000','-ac','1','-f','f32le','-'],input=raw),dtype='<f4')
        import io
        clock=0;events=[]
        if args.edition=='v2': events=string_events(annotations,11 if args.player=='P1' else 1)
        else:
            for event in mido.MidiFile(file=io.BytesIO(annotations)):
                clock+=event.time
                if event.type=='note_on' and event.velocity:events.append((clock,event.note,event.channel+1))
        records=[];chunks=[];zones=[];frames=0
        for i,root in enumerate(roots):
            chosen=closest_fret_event(events,root) if args.edition=='v2' else min((e for e in events if e[1]==root),key=lambda e:e[2])
            time,_,string=chosen[:3]
            left=max(0,round((time-.15)*48000));right=min(len(decoded),round((time+.2)*48000))
            window=decoded[left:right];rms=np.sqrt(np.convolve(window.astype(float)**2,np.ones(96)/96,'valid'))
            # A previous ringing note may remain above a simple silence gate.
            # Estimate its floor well before the MIDI attack, then require an
            # actual rise above it. The strongest rise can be a later vibration
            # or fret noise, so it must not replace the first picked transient.
            floor=0 if left==0 else float(np.median(rms[:1200]))
            threshold=max(.0001,floor*2.5,float(rms.max())*.08)
            hits=np.flatnonzero(rms>=threshold);assert len(hits),('Missing attack',root)
            onset=left+int(hits[0]);assert abs(onset/48000-time)<.10,('Re-audit annotation alignment',root)
            start=max(0,onset-144);end=min(len(decoded),start+round(cap*48000))
            pcm=decoded[start:end].astype(float).copy()
            # One source performance per pitch: equalize recording strength,
            # never invent velocity layers or alternate takes.
            reference=pcm[:min(len(pcm),round((.3 if name=='allsinglenotes' else .12)*48000))]
            gain=min(.10/max(float(np.sqrt(np.mean(reference**2))),1e-9),.75/max(float(abs(pcm).max()),1e-9))
            pcm*=gain;fade=min(960,len(pcm));pcm[-fade:]*=np.linspace(1,0,fade)
            encoded=np.rint(np.clip(pcm,-1,1)*32767).astype('<i2').tobytes();length=len(encoded)//2
            low=35 if i==0 else (roots[i-1]+root)//2+1;high=86 if i==len(roots)-1 else (root+roots[i+1])//2
            records.append(struct.pack('<12BII',low,high,1,127,root,0,1,0,0,0,0,0,frames,length));chunks.append(encoded)
            zones.append(dict(root=root,low=low,high=high,string=string,annotationTime=time,sourceOnsetFrame=onset,
                sourceStartFrame=start,frames=length,gain=gain,pcmSha256=hashlib.sha256(encoded).hexdigest()))
            if args.edition=='v2':zones[-1].update(sourceString=chosen[3],sourceVelocity=chosen[4],selection='highest available named string / smallest fret')
            frames+=length
        target=out/f'sound/fetcher/banks/{ident}.fsb';target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(struct.pack('<8s4I',b'FSBANK2\0',len(records),frames,48000,0)+b''.join(records)+b''.join(chunks))
        native=json.loads(subprocess.check_output([str(args.tool.resolve()),str(target)],text=True))
        report=dict(id=ident,path=target.relative_to(out).as_posix(),bytes=target.stat().st_size,sha256=hashlib.sha256(target.read_bytes()).hexdigest(),
            native=native,license='CC-BY-4.0',source='https://zenodo.org/records/14963133',archive=archive,archiveMd5=md5,archiveSha256=archive_sha,
            sourceAudio=audio,sourceAudioSha256=hashlib.sha256(raw).hexdigest(),annotation=midi,annotationSha256=hashlib.sha256(annotations).hexdigest(),
            description=('Guitar-TECHS player 1 Ibanez PF300 bridge-pickup DI; ' if args.player=='P1' else 'Guitar-TECHS player 2 EVH Wolfgang neck-pickup DI; ')+('recorded open notes' if name=='allsinglenotes' else 'recorded pitched palm mutes')+
                '; one recorded performance per sparse root, no invented alternates/layers. Keys 35-39 transpose the low-E capture.',
            conversion=f'Original dual-mono recording averaged, 48 kHz PCM16. MIDI-guided RMS attack alignment with 3-ms preroll; cap {cap} s; 20-ms end fade. Per-root strength gain (recorded in zones) capped at .75 peak; no harmonic reconstruction.',zones=zones)
        (out/'manifests'/f'{ident}.json').write_text(json.dumps(report,indent=2)+'\n');reports.append(report)
        if args.edition=='v2':
            report['description'] += ' Corrected v2: named physical string tracks, smallest available fret; no channel-to-string assumption.'
            (out/'manifests'/f'{ident}.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps({k:report[k] for k in ['id','bytes','native']}),flush=True)
    catalog_path=out/'catalog.json';catalog=json.loads(catalog_path.read_text());ids={r['id'] for r in reports}
    catalog['banks']=[r for r in catalog['banks'] if r['id'] not in ids]
    for entry in catalog['banks']:entry.setdefault('license','CC0-1.0')
    catalog['banks'] += [{k:r[k] for k in ['id','path','bytes','sha256','native','source','description','license']} for r in reports]
    catalog['license']='Per-bank: CC0-1.0, CC-BY-3.0 or CC-BY-4.0';catalog['decodedBytesAllBanks']=sum(r['native']['pcmBytes'] for r in catalog['banks'])
    # This is the on-disk library, not a request to make every bank resident.
    # The production reader retains its shared 256-MiB budget and evicts unpinned
    # banks. Regression tests check each factory set/project's actual references.
    catalog_path.write_text(json.dumps(catalog,indent=2)+'\n')
    print('Total PCM:',catalog['decodedBytesAllBanks'])


if __name__=='__main__':main()
