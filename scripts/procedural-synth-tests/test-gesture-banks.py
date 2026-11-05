"""Audit trigger classes, original PCM, real takes, bounds and held-decay metadata."""
from pathlib import Path
import hashlib
import json
import struct
import subprocess
import sys

root=Path(__file__).resolve().parents[2]
assets=root/'files/server/optional-assets/fetcher.studio-samples'
catalog=json.loads((assets/'catalog.json').read_text())
for ident,limits in [('bass-finger-release-v1',(23,64)),('guitar-green-gestures-v1',(35,86))]:
    entry=next(b for b in catalog['banks'] if b['id']==ident)
    report=json.loads((assets/'manifests'/f'{ident}.json').read_text());data=(assets/entry['path']).read_bytes()
    assert hashlib.sha256(data).hexdigest()==entry['sha256']==report['sha256']
    assert json.loads(subprocess.check_output([sys.argv[1],str(assets/entry['path'])]))==entry['native']
    assert entry['license']=='CC0-1.0' and report['requiredNativeApi']==37
    magic,count,frames,rate,pad=struct.unpack_from('<8s4I',data)
    assert (magic,rate,pad)==(b'FSBANK2\0',48000,0) and len(data)==24+20*count+2*frames
    records=[struct.unpack_from('<12BII',data,24+20*i) for i in range(count)]
    kinds={r[7] for r in records};assert kinds==({0,2} if ident.startswith('bass') else {0,2,4})
    for key in range(128):
        for velocity in (1,63,64,127):
            for kind in kinds:
                matches=[r for r in records if r[7]==kind and r[0]<=key<=r[1] and r[2]<=velocity<=r[3]]
                assert len(matches)==(2 if limits[0]<=key<=limits[1] else 0),(ident,key,velocity,kind)
                if matches:assert sorted(r[5] for r in matches)==[0,1]
    for record,zone in zip(records,report['zones']):
        low,high,vl,vh,note,alternate,alternates,flags,group,off,d0,d1,start,length=record
        assert (low,high,vl,vh,note,alternate)==tuple(zone[k] for k in ('low','high','vl','vh','root','alternate'))
        assert flags==zone['flags'] and group==off==0 and alternates==2
        if flags==2:assert (d0+256*d1)/256==(4 if ident.startswith('bass') else 5) and length<=31200
        else:assert d0==d1==0
        if flags==4:assert '/hammer/' in zone['sample'] and length<=96000
        if flags==2:assert '/rel/' in zone['sample']
        pcm=data[24+20*count+2*start:24+20*count+2*(start+length)]
        assert len(pcm)==2*length
        if 'pcmSha256' in zone:assert hashlib.sha256(pcm).hexdigest()==zone['pcmSha256']
    if ident.startswith('bass'):
        original=(assets/report['baseBank']['path']).read_bytes();original_count=struct.unpack_from('<I',original,8)[0]
        assert data[24:24+20*original_count]==original[24:24+20*original_count]
        assert data[24+20*count:24+20*count+len(original)-24-20*original_count]==original[24+20*original_count:]
        assert hashlib.sha256(original[24+20*original_count:]).hexdigest()==report['basePcmSha256']
    else:
        assert report['commit']=='b3b3249d37dc977a1a297bd2dc053e6d9b6b805c'
        assert len(report['sources'])==104 and len({s['sha256'] for s in report['sources']})==104
    print('PASS',ident,'bounded trigger maps, genuine recordings, independent alternates, physical endings and provenance')
