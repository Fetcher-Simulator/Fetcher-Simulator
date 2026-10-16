"""Install a verified combined NIF into the credited optional runtime bundle."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import zipfile

p=argparse.ArgumentParser()
p.add_argument('--source',type=Path,required=True)
p.add_argument('--converted',type=Path,required=True)
p.add_argument('--working',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--zip',type=Path,required=True)
args=p.parse_args()
def write(path,value):
    path.write_bytes(value.encode('utf-8'))
report=json.loads((args.working/'stage-report.json').read_text())
assert report['roundTripImport']=='PASS' and report['serializedTriangles']==12273
model=report['model']
assert (args.converted/model).read_bytes().startswith(b'NetImmerse File Format, Version 4.0.0.2')
for path in args.converted.rglob('*'):
    if not path.is_file():continue
    if path.suffix=='.dds':
        data=path.read_bytes()
        assert data[:4]==b'DDS ' and data[84:88] in (b'DXT1',b'DXT5')
        assert struct.unpack_from('<I',data,28)[0]>=10
    target=args.output/path.relative_to(args.converted)
    target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(path,target)
credits=[]
sources=[]
for name in ('yamaha-keyboard','keyboard-x-stand'):
    meta=json.loads((args.source/name/'provenance.json').read_text())
    uid=meta['uid']
    source={'uid':uid,'name':meta['name'],'author':{key:meta['user'][key]
        for key in ('username','displayName','profileUrl')},'license':meta['license'],
        'sourceArchiveSha256':meta['sourceArchiveSha256'],'verifiedDate':meta['verifiedDate'],
        'sourceUrl':'https://sketchfab.com/3d-models/'+name+'-'+uid,
        'metadataUrl':'https://api.sketchfab.com/v3/models/'+uid}
    sources.append(source)
    credits.append(f"{source['name']} by {source['author']['displayName']}\n"
        f"Author: {source['author']['profileUrl']}\nSource: {source['sourceUrl']}\n"
        "License: Creative Commons Attribution 4.0 International\n"
        "https://creativecommons.org/licenses/by/4.0/\n")
notice='''Combined Yamaha keyboard and X-stand adaptation for OpenMW
=========================================================
'''+ '\n'.join(credits)+'''
Changes: combined the keyboard OBJ and the stand's static ASCII FBX into one
Morrowind NIF 4.0.0.2; converted Y-up geometry, centered at the stand's feet;
keyboard width 104 game units, stand top height 84. Preserved the original UVs,
adapted materials to legacy NIF, resized textures to 1K/2K mipmapped DDS, and
added seven simple collision boxes. Export removed one zero-area triangle;
total visual surface area was checked unchanged. Keys remain static geometry.
No endorsement by either author or Yamaha is implied.
'''
old=(args.output/'ATTRIBUTION.txt').read_text(encoding='utf-8').split('Combined Yamaha keyboard and X-stand adaptation for OpenMW')[0].rstrip()
write(args.output/'ATTRIBUTION.txt',old+'\n\n'+notice)
write(args.output/'LICENSE-CC-BY-4.0.txt','''The keyboard models, stand and derived textures in this bundle are licensed
under Creative Commons Attribution 4.0 International (CC BY 4.0).
See ATTRIBUTION.txt for the original authors, source links and changes.
License: https://creativecommons.org/licenses/by/4.0/
Full legal terms: https://creativecommons.org/licenses/by/4.0/legalcode.en
This notice supplies the license URI; it is not a reproduction of the legal text.
''')
write(args.output/'stage-provenance.json',json.dumps(sources,indent=2)+'\n')
write(args.output/'stage-report.json',json.dumps(report,indent=2)+'\n')
manifest={'version':1,'packageId':'fetcher.procedural-synth.assets','model':model,'files':[]}
for path in sorted(args.output.rglob('*')):
    if path.is_file() and path.name!='asset-manifest.json':
        data=path.read_bytes()
        manifest['files'].append({'path':path.relative_to(args.output).as_posix(),
            'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()})
write(args.output/'asset-manifest.json',json.dumps(manifest,indent=2)+'\n')
args.zip.parent.mkdir(parents=True,exist_ok=True)
with zipfile.ZipFile(args.zip,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
    for path in sorted(args.output.rglob('*')):
        if path.is_file():archive.write(path,path.relative_to(args.output).as_posix())
print('COMBINED_BUNDLE_PASS',len(manifest['files']),'files;',args.zip,args.zip.stat().st_size,'bytes')
