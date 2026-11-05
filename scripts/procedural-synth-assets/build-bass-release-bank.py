"""Append actual CC0 Darkblack release samples to a byte-preserved bass body.

FSBANK2 flag 2 is an API-37 note-off zone, not an ordinary attack. The source
specifies 4 dB/s held decay; native release ownership applies that at note-off.
"""
import argparse
import array
import hashlib
import importlib.util
import json
import io
from pathlib import Path
import struct
import subprocess
import sys
import zipfile
import wave


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('sources',type=Path);p.add_argument('tool',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();out=a.output
    spec=importlib.util.spec_from_file_location('helpers',Path(__file__).with_name('build-band-sample-banks.py'))
    helpers=importlib.util.module_from_spec(spec);spec.loader.exec_module(helpers)
    pins=json.loads(Path(__file__).with_name('sample-sources.json').read_text())['sources']
    pin=next(p for p in pins if p['asset']=='Black_And_Blue_Basses_1002.zip')
    archive=a.sources/pin['asset']
    with archive.open('rb') as stream:assert hashlib.file_digest(stream,'sha256').hexdigest()==pin['sha256']
    catalog=json.loads((out/'catalog.json').read_text());base=next(b for b in catalog['banks'] if b['id']=='bass-finger-v1')
    data=(out/base['path']).read_bytes();assert hashlib.sha256(data).hexdigest()==base['sha256']
    magic,count,frames,rate,reserved=struct.unpack_from('<8s4I',data);assert (magic,rate,reserved)==(b'FSBANK2\0',48000,0)
    records=[data[24+n*20:44+n*20] for n in range(count)];pcm=[data[24+count*20:]]
    original=json.loads((out/'manifests/bass-finger-v1.json').read_text());zones=[dict(z,trigger='attack') for z in original['zones']]
    roots=sorted({z['root'] for z in original['zones']});bounds=helpers.spans(roots,23,64)
    mapping=a.sources/'metadata/Black_And_Blue_Basses_1002/Programs/maps/darkblack_rel_map.sfz'
    mapped=list(helpers.flat_map(mapping.read_text()))
    with zipfile.ZipFile(archive) as source:
        for root in roots:
            for alternate in range(2):
                r=next(r for r in mapped if int(r.get('pitch_keycenter',60))-12==root and int(r.get('seq_position',1))==alternate+1)
                sample=r['sample'].replace('\\','/').removeprefix('../');raw=source.read(sample)
                # Some original WAV data chunks count a trailing padding byte
                # as PCM. Decode complete frames, recording that source quirk
                # rather than asking ffmpeg to accept an invalid last packet.
                with wave.open(io.BytesIO(raw),'rb') as w:
                    channels,width,rate=w.getnchannels(),w.getsampwidth(),w.getframerate()
                    assert w.getcomptype()=='NONE' and width in (2,3,4)
                    payload=w.readframes(w.getnframes());assert len(payload)%(channels*width)==0
                decoded=array.array('h',subprocess.check_output(['ffmpeg','-v','error','-f',f's{width*8}le','-ar',str(rate),'-ac',str(channels),
                    '-i','pipe:0','-ar','48000','-ac','1','-f','s16le','-'],input=payload))
                if sys.byteorder!='little':decoded.byteswap()
                onset=next(i for i,value in enumerate(decoded) if abs(value)>=19);start=max(0,onset-144)
                cut=decoded[start:start+31200]
                for i in range(min(144,len(cut))):cut[i]=round(cut[i]*i/144)
                for i in range(max(0,len(cut)-960),len(cut)):cut[i]=round(cut[i]*(len(cut)-1-i)/960)
                length=len(cut);assert 2<=length<=31200
                if sys.byteorder!='little':cut.byteswap()
                encoded=cut.tobytes();low,high=bounds[root]
                records.append(struct.pack('<12BII',low,high,1,127,root,alternate,2,2,0,0,0,4,frames,length))
                zones.append(dict(trigger='release',sample=sample,root=root,low=low,high=high,vl=1,vh=127,alternate=alternate,flags=2,
                    sourceSha256=hashlib.sha256(raw).hexdigest(),sourceCompleteFrames=len(payload)//(channels*width),
                    sourceBlockAlign=channels*width,onsetFrame=onset,trimStartFrame=start,frames=length,
                    pcmSha256=hashlib.sha256(encoded).hexdigest()))
                pcm.append(encoded);frames+=length
    ident='bass-finger-release-v1';path='sound/fetcher/banks/'+ident+'.fsb';target=out/path
    target.write_bytes(struct.pack('<8s4I',b'FSBANK2\0',len(records),frames,48000,0)+b''.join(records)+b''.join(pcm))
    native=json.loads(subprocess.check_output([str(a.tool.resolve()),str(target)]))
    report=dict(id=ident,path=path,bytes=target.stat().st_size,sha256=hashlib.sha256(target.read_bytes()).hexdigest(),native=native,
        license='CC0-1.0',source='https://github.com/sfzinstruments/karoryfer.black-and-blue-basses',
        archive=archive.name,archiveSha256=pin['sha256'],baseBank=base,baseZones=count,basePcmSha256=hashlib.sha256(pcm[0]).hexdigest(),
        mapSha256=hashlib.sha256(mapping.read_bytes()).hexdigest(),requiredNativeApi=37,
        description='Original Darkblack finger body plus two real captured physical note endings per sparse root; separate native note-off zones.',
        conversion='Existing bass-finger-v1 records/PCM retained byte-for-byte. Releases: mono 48-kHz PCM16, -65-dB silence trim with 3-ms preroll/fade-in; <=.65 seconds, 20-ms end fade; no normalization. Concert roots -12 from source, as in body. Native held decay 4 dB/s.',zones=zones)
    (out/'manifests'/f'{ident}.json').write_text(json.dumps(report,indent=2)+'\n')
    catalog['banks']=[b for b in catalog['banks'] if b['id']!=ident]
    catalog['banks'].append({k:v for k,v in report.items() if k not in ('baseBank','zones','conversion','basePcmSha256','mapSha256','baseZones')})
    catalog['decodedBytesAllBanks']=sum(b['native']['pcmBytes'] for b in catalog['banks'])
    (out/'catalog.json').write_text(json.dumps(catalog,indent=2)+'\n');print(ident,native,flush=True)


if __name__=='__main__':main()
