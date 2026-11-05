"""Curate pinned CC0 Green Guitar picked/hammer/release recordings.

The source folder is produced by acquire-green-guitar-gestures.py. It contains
only original WAV/SFZ/license files and a verified selection manifest, not code
to execute. Flag 4 is a real upward connected attack; pull-offs/slides retain
native source-continuation/touch behavior rather than claiming a new capture.
"""
import argparse
import array
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import wave


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',type=Path);p.add_argument('tool',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();manifest=a.source/'source-manifest.json'
    pin=next(s for s in json.loads(Path(__file__).with_name('sample-sources.json').read_text())['sources']
        if s['asset']=='black-green-gesture-subset')
    assert hashlib.sha256(manifest.read_bytes()).hexdigest()==pin['sourceManifestSha256']
    source=json.loads(manifest.read_text());assert source['commit']==pin['commit'] and source['license']=='CC0-1.0'
    assert hashlib.sha256((a.source/'LICENSE').read_bytes()).hexdigest()==pin['licenseSha256']
    inputs={};metadata={}
    for info in source['files']:
        raw=(a.source/info['path']).read_bytes();assert hashlib.sha256(raw).hexdigest()==info['sha256']
        assert hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()==info['gitBlob']
        inputs[info['path']]=raw;metadata[info['path']]=info
    roots=source['roots'];bounds={r:(35 if i==0 else (roots[i-1]+r)//2+1,86 if i==len(roots)-1 else (r+roots[i+1])//2) for i,r in enumerate(roots)}
    frames=0;records=[];chunks=[];zones=[]
    for row in source['zones']:
        raw=inputs[row['sample']]
        with wave.open(io.BytesIO(raw),'rb') as w:
            width,channels,rate=w.getsampwidth(),w.getnchannels(),w.getframerate()
            assert width in (2,3,4) and w.getcomptype()=='NONE'
            payload=w.readframes(w.getnframes())
        pcm=array.array('h',subprocess.check_output(['ffmpeg','-v','error','-f',f's{width*8}le','-ar',str(rate),'-ac',str(channels),'-i','pipe:0',
            '-ar','48000','-ac','1','-f','s16le','-'],input=payload))
        if sys.byteorder!='little':pcm.byteswap()
        onset=next(i for i,value in enumerate(pcm) if abs(value)>=19);start=max(0,onset-144)
        cap=31200 if row['flags']==2 else 96000 if row['flags']==4 else 144000
        pcm=pcm[start:start+cap]
        for i in range(min(144,len(pcm))):pcm[i]=round(pcm[i]*i/144)
        for i in range(max(0,len(pcm)-960),len(pcm)):pcm[i]=round(pcm[i]*(len(pcm)-1-i)/960)
        if sys.byteorder!='little':pcm.byteswap()
        encoded=pcm.tobytes();length=len(pcm);low,high=bounds[row['root']];extra=round(row['heldDecay']*256)
        records.append(struct.pack('<12BII',low,high,row['vl'],row['vh'],row['root'],row['alternate'],2,row['flags'],0,0,extra&255,extra>>8,frames,length))
        zones.append(dict(row,low=low,high=high,sourceStartFrame=start,sourceOnsetFrame=onset,frames=length,
            sourceSha256=metadata[row['sample']]['sha256'],pcmSha256=hashlib.sha256(encoded).hexdigest()))
        frames+=length;chunks.append(encoded)
    ident='guitar-green-gestures-v1';path='sound/fetcher/banks/'+ident+'.fsb';target=a.output/path
    target.write_bytes(struct.pack('<8s4I',b'FSBANK2\0',len(records),frames,48000,0)+b''.join(records)+b''.join(chunks))
    native=json.loads(subprocess.check_output([str(a.tool.resolve()),str(target)]))
    report=dict(id=ident,path=path,bytes=target.stat().st_size,sha256=hashlib.sha256(target.read_bytes()).hexdigest(),native=native,
        license='CC0-1.0',source=source['repository'],commit=source['commit'],creators=source['creators'],
        sourceManifestSha256=pin['sourceManifestSha256'],licenseSha256=pin['licenseSha256'],sources=source['files'],maps=source['maps'],requiredNativeApi=37,
        description='Green Gretsch guitar: two real picked dynamics/takes, captured upward hammer attacks and physical release samples. Keys 35-39 transpose recorded low E.',
        conversion='Original mono/stereo PCM averaged at 48 kHz PCM16, -65-dB onset with 3-ms preroll/fade-in; body <=3 s, hammer <=2 s, release <=.65 s, 20-ms end fade. No normalization or invented takes; source release decay 5 dB/s. No SFZ runtime.',zones=zones)
    (a.output/'manifests'/f'{ident}.json').write_text(json.dumps(report,indent=2)+'\n')
    catalog=json.loads((a.output/'catalog.json').read_text());catalog['banks']=[b for b in catalog['banks'] if b['id']!=ident]
    catalog['banks'].append({k:v for k,v in report.items() if k not in ('sources','maps','zones','conversion')})
    catalog['decodedBytesAllBanks']=sum(b['native']['pcmBytes'] for b in catalog['banks'])
    (a.output/'catalog.json').write_text(json.dumps(catalog,indent=2)+'\n');print(ident,native,flush=True)


if __name__=='__main__':main()
