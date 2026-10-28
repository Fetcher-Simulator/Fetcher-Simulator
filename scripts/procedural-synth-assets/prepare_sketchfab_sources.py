"""Record official CC BY metadata and hashes for manually downloaded GLBs."""
import argparse,concurrent.futures,hashlib,json,re,shutil,urllib.request
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--downloads',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
args=p.parse_args();plan=json.loads(Path(__file__).with_name('sketchfab-sources.json').read_text())
def prepare(pair):
    name,item=pair;assert re.fullmatch('[a-f0-9]{32}',item['uid']) and Path(item['file']).name==item['file']
    source=args.downloads/item['file'];assert source.is_file() and source.stat().st_size<256*1024*1024,source
    data=source.read_bytes();assert data[:4]==b'glTF'
    with urllib.request.urlopen('https://api.sketchfab.com/v3/models/'+item['uid'],timeout=30) as response:meta=json.load(response)
    assert meta['isDownloadable'] and meta['license']['url'].replace('http://','https://').rstrip('/')=='https://creativecommons.org/licenses/by/4.0'
    folder=args.output/name;folder.mkdir(parents=True,exist_ok=True);shutil.copy2(source,folder/item['file'])
    (folder/'sketchfab-metadata.json').write_text(json.dumps(meta,indent=2),encoding='utf-8')
    (folder/'download.json').write_text(json.dumps({'uid':item['uid'],'file':item['file'],'sha256':hashlib.sha256(data).hexdigest()},indent=2),encoding='utf-8')
    return name
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    for name in pool.map(prepare,plan.items()):print('Verified',name)
