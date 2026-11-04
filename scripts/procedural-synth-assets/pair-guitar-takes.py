"""Combine the audited v2 bridge/neck recordings as two real alternate takes.

Run build-guitar-techs-banks.py --edition v2 for both players into a scratch
asset directory, then pass that directory, the native bank tool and the target
asset directory here. Source manifests/cuts are retained inside each pair.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import subprocess


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    parser.add_argument('tool',type=Path)
    parser.add_argument('output',type=Path)
    args=parser.parse_args();catalog=json.loads((args.output/'catalog.json').read_text())
    for technique in ('open','palm'):
        sources=[];records=[];chunks=[];zones=[];frames=0
        for alternate,prefix in enumerate(('guitar-techs-bridge-','guitar-techs-')):
            manifest=json.loads((args.source/'manifests'/f'{prefix}{technique}-v2.json').read_text())
            assert manifest['license']=='CC-BY-4.0'
            data=(args.source/manifest['path']).read_bytes()
            assert hashlib.sha256(data).hexdigest()==manifest['sha256']
            magic,count,_,rate,reserved=struct.unpack_from('<8s4I',data)
            assert (magic,count,rate,reserved)==(b'FSBANK2\0',13,48000,0)
            sources.append(manifest)
            for n,info in enumerate(manifest['zones']):
                record=list(struct.unpack_from('<12BII',data,24+20*n))
                start,length=record[-2:];pcm=data[24+count*20+start*2:24+count*20+(start+length)*2]
                assert hashlib.sha256(pcm).hexdigest()==info['pcmSha256']
                record[5:7]=[alternate,2];record[-2]=frames
                records.append(struct.pack('<12BII',*record));chunks.append(pcm)
                zones.append(dict(info,alternate=alternate,sourceBank=manifest['id']))
                frames+=length
        ident=f'guitar-techs-paired-{technique}-v2';path=f'sound/fetcher/banks/{ident}.fsb';target=args.output/path
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(struct.pack('<8s4I',b'FSBANK2\0',26,frames,48000,0)+b''.join(records)+b''.join(chunks))
        native=json.loads(subprocess.check_output([str(args.tool.resolve()),str(target)]))
        report=dict(id=ident,path=path,bytes=target.stat().st_size,sha256=hashlib.sha256(target.read_bytes()).hexdigest(),
            native=native,license='CC-BY-4.0',source='https://zenodo.org/records/14963133',
            description=f'Two real Guitar-TECHS {technique} takes per sparse root: P1 bridge/P2 neck, named-string v2 selection. No synthetic alternates or harmonic reconstruction.',
            sources=sources,zones=zones)
        (args.output/'manifests'/f'{ident}.json').write_text(json.dumps(report,indent=2)+'\n')
        catalog['banks']=[b for b in catalog['banks'] if b['id']!=ident]
        catalog['banks'].append({k:v for k,v in report.items() if k not in ('sources','zones')})
        print(ident,native)
    catalog['decodedBytesAllBanks']=sum(b['native']['pcmBytes'] for b in catalog['banks'])
    (args.output/'catalog.json').write_text(json.dumps(catalog,indent=2)+'\n')


if __name__=='__main__':main()
