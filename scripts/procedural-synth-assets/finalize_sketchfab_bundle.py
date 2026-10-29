"""Retain upstream notices, publish model credits and refresh package hashes."""
import argparse,hashlib,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--assets',type=Path,required=True);args=p.parse_args();assets=args.assets.resolve()
models=json.loads((assets/'sketchfab-models.json').read_text())
models=[m for m in models if m['id']!='chair']
(assets/'sketchfab-models.json').write_bytes((json.dumps(models,indent=2,ensure_ascii=False)+'\n').encode())
assert {m['id'] for m in models}=={'violin','cello','trumpet','flute','clarinet','timpani','music-stand','stool','sheet-music','mallet'}
lines=['Fetcher Studio third-party performance and stage models','======================================================','Models provided by Sketchfab. Preserve these credits when redistributing.','']
for m in models:
    assert m['license']=='CC BY 4.0' and m['source'].startswith('https://sketchfab.com/3d-models/')
    lines += [m['title']+' — '+m['author'],'Source: '+m['source'],'Author: '+m['authorUrl'],'License: Creative Commons Attribution 4.0 International','https://creativecommons.org/licenses/by/4.0/','Changes: '+m['changes'],'']
    if m.get('upstream'):
        u=m['upstream'];lines += ['Upstream model: '+u['title']+' — '+u['author'],'Source: '+u['source'],'Author: '+u['authorUrl'],'License: '+u['licenseUrl'],'']
lines += ['Additional adaptations: violin/bow separated into independent animated props; one wooden percussion stick selected from the downloaded pair. The percussion beater is wooden, not a felt-headed timpani mallet.','Seated keyboard: existing credited Yamaha/X-stand adaptation lowered by 24 game units; stand legs shortened while key/body thickness is retained. Original keyboard/stand authors and licenses remain applicable.','Sheet booklet joined into standing/seated Yamaha music rests and the separate music stand. Yamaha pages centered on the offset rest, scaled to 86 percent and tilted 76 degrees to sit behind its retaining lip. Existing geometry and collision are retained. Compact stool scaled to a 44-unit seat height.','No endorsement by any model author or instrument manufacturer is implied. Code licenses are separate from these asset licenses.','']
notice='\n'.join(lines)
(assets/'ORCHESTRA-ASSETS.txt').write_bytes(notice.encode('utf-8'))
old=(assets/'ATTRIBUTION.txt').read_text(encoding='utf-8')
for marker in ('Original Fetcher Studio orchestral props','Fetcher Studio third-party performance and stage models'):
    old=old.split(marker,1)[0]
(assets/'ATTRIBUTION.txt').write_bytes((old.rstrip()+'\n\n'+notice).encode('utf-8'))
report=json.loads((assets/'sketchfab-orchestra-report.json').read_text())
report.pop('chair',None)
holders=json.loads((assets/'music-holder-report.json').read_text()) if (assets/'music-holder-report.json').exists() else {}
if 'music-stand' in report and 'meshes/fetcher/orchestra/music-stand.nif' in holders:
    item=holders['meshes/fetcher/orchestra/music-stand.nif']
    for key in ['triangles','sha256','bounds']:report['music-stand'][key]=item[key]
(assets/'sketchfab-orchestra-report.json').write_bytes((json.dumps(report,indent=2)+'\n').encode())
(assets/'orchestra-report.json').write_bytes((json.dumps(report,indent=2)+'\n').encode())
manifest=json.loads((assets/'asset-manifest.json').read_text())
manifest['files']=[{'path':p.relative_to(assets).as_posix(),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(assets.rglob('*')) if p.is_file() and p.name!='asset-manifest.json']
(assets/'asset-manifest.json').write_bytes((json.dumps(manifest,indent=2)+'\n').encode())
print(json.dumps({'files':len(manifest['files']),'bytes':sum(f['bytes'] for f in manifest['files']),'creditedSources':len(models)}))
