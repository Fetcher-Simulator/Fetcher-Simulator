"""Fetch only the pinned CC0 picked/hammer/release subset used by the bank.

No login, executables, plugins or source scripts. Every original file is checked
against the pinned Git tree, and the resulting selection manifest is pinned too.
"""
import argparse
import concurrent.futures
import hashlib
import importlib.util
import json
from pathlib import Path
import urllib.request


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('output',type=Path);a=parser.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    entry=next(s for s in json.loads(Path(__file__).with_name('sample-sources.json').read_text())['sources']
        if s['asset']=='black-green-gesture-subset')
    repo='https://github.com/sfzinstruments/karoryfer.black-and-green-guitars'
    api='https://api.github.com/repos/sfzinstruments/karoryfer.black-and-green-guitars/'
    tree=json.load(urllib.request.urlopen(api+'git/trees/'+entry['tree']+'?recursive=1',timeout=30))
    assert tree['sha']==entry['tree'] and not tree.get('truncated')
    index={x['path']:x for x in tree['tree']}
    base='https://raw.githubusercontent.com/sfzinstruments/karoryfer.black-and-green-guitars/'+entry['commit']+'/'
    def get(name):
        target=out/name;assert target.resolve().is_relative_to(out)
        raw=target.read_bytes() if target.exists() else urllib.request.urlopen(base+name,timeout=45).read()
        assert hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()==index[name]['sha'],name
        if not target.exists():target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(raw)
        return dict(path=name,sha256=hashlib.sha256(raw).hexdigest(),gitBlob=index[name]['sha'],bytes=len(raw))
    for name in ['LICENSE','readme.txt','Programs/modules/maps_green/ord.sfz','Programs/modules/maps_green/hammer.sfz','Programs/modules/maps_green/rel_tp.sfz']:get(name)
    assert hashlib.sha256((out/'LICENSE').read_bytes()).hexdigest()==entry['licenseSha256']
    spec=importlib.util.spec_from_file_location('helpers',Path(__file__).with_name('build-band-sample-banks.py'))
    helpers=importlib.util.module_from_spec(spec);spec.loader.exec_module(helpers)
    roots=list(range(40,87,4))+[86];selected=[];maps=[]
    for name,flags,vels in [('ord',0,[(1,63,42),(64,127,127)]),('hammer',4,[(1,127,127)]),('rel_tp',2,[(1,127,127)])]:
        path=out/'Programs/modules/maps_green'/f'{name}.sfz';regions=list(helpers.flat_map(path.read_text()))
        maps.append(dict(path=path.relative_to(out).as_posix(),sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
        for root in roots:
            for low,high,velocity in vels:
                available=[r for r in regions if int(r.get('pitch_keycenter',60))==root
                    and int(r.get('lovel',1))<=velocity<=int(r.get('hivel',127))]
                samples=[]
                for r in available:
                    sample=r['sample'].replace('\\','/').removeprefix('../')
                    if sample not in samples:samples.append(sample)
                    if len(samples)==2:break
                assert len(samples)==2,(name,root)
                for alternate,sample in enumerate(samples):selected.append(dict(sample=sample,root=root,vl=low,vh=high,alternate=alternate,flags=flags,heldDecay=5 if flags==2 else 0))
    paths=sorted({r['sample'] for r in selected})
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:files=list(pool.map(get,paths))
    manifest=dict(commit=entry['commit'],tree=entry['tree'],license='CC0-1.0',repository=repo,
        roots=roots,zones=selected,files=files,maps=maps,licenseSha256=entry['licenseSha256'],creators=entry['creators'])
    encoded=json.dumps(manifest,indent=2).encode()
    assert hashlib.sha256(encoded).hexdigest()==entry['sourceManifestSha256'],'Selection changed; re-audit before rebuilding'
    (out/'source-manifest.json').write_bytes(encoded)
    print('Verified',len(files),'CC0 recordings;',sum(x['bytes'] for x in files),'bytes')


if __name__=='__main__':main()
