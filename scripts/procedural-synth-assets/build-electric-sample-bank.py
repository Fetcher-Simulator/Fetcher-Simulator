"""Build the deliberately small, audited SFZ subset used by FreePats DI guitar.

No SFZ is evaluated at runtime. Two recorded alternatives per note/velocity
zone become a deterministic round-robin pair; the full source archive is kept.
"""
from pathlib import Path
import argparse,collections,concurrent.futures,hashlib,json,re,struct,subprocess

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('source',type=Path,help='Extracted FreePats DI source directory')
    parser.add_argument('tool',type=Path,help='Built sample-bank-tool executable')
    parser.add_argument('output',type=Path,help='New/managed optional sample-assets folder')
    args=parser.parse_args();source=args.source.resolve();output=args.output.resolve()
    sfzs=list(source.rglob('*.sfz'));assert len(sfzs)==1,'Expected exactly the audited DI map'
    sfz=sfzs[0];text=re.sub(r'//[^\n]*','',sfz.read_text())
    assert not re.search(r'^\s*#',text,re.M),'Includes/macros are outside this converter subset'
    global_values={};group={};regions=[]
    allowed={'ampeg_release','lokey','hikey','key','lovel','hivel','pitch_keycenter','lorand','hirand','sample'}
    for tag,body in re.findall(r'<(global|group|region)>([^<]*)',text):
        values=dict(re.findall(r'([a-z_0-9]+)\s*=\s*([^\s]+)',body))
        assert set(values)<=allowed,values
        if tag=='global':global_values=values
        elif tag=='group':group=values
        else:
            region={**global_values,**group,**values}
            assert 'sample' in region
            sample=(sfz.parent/region['sample'].replace('\\','/')).resolve()
            assert sample.is_relative_to(source) and sample.is_file()
            key=int(region.get('key',60));lo=int(region.get('lokey',key));hi=int(region.get('hikey',key))
            root=int(region.get('pitch_keycenter',key));vl=int(region.get('lovel',1));vh=int(region.get('hivel',127))
            regions.append({'source':sample,'range':(lo,hi,vl,vh,root),'random':float(region.get('lorand',0))})
    groups=collections.defaultdict(list)
    for region in regions:groups[region['range']].append(region)
    selected=[]
    for key,variants in sorted(groups.items()):
        for index,region in enumerate(sorted(variants,key=lambda r:r['random'])[:2]):
            selected.append({**region,'alternate':index,'alternates':min(2,len(variants))})
    assert len(regions)==120 and len(selected)==60,'Upstream map changed; re-audit before conversion'
    def convert(region):
        # Align the start before the pick transient, retain six seconds and a
        # short terminal fade. Do not normalize away recorded velocity layers.
        command=['ffmpeg','-v','error','-nostdin','-i',str(region['source']),'-af',
            'silenceremove=start_periods=1:start_threshold=-65dB:start_silence=0.003,atrim=duration=6,afade=t=out:st=5.98:d=0.02',
            '-ar','48000','-ac','1','-f','s16le','-']
        pcm=subprocess.check_output(command);assert 4<=len(pcm)<=6*48000*2 and len(pcm)%2==0
        return region,pcm
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:converted=list(pool.map(convert,selected))
    frames=0;records=[];chunks=[];provenance=[]
    for region,pcm in converted:
        lo,hi,vl,vh,root=region['range'];length=len(pcm)//2
        records.append(struct.pack('<8BII',lo,hi,vl,vh,root,region['alternate'],region['alternates'],0,frames,length))
        chunks.append(pcm);frames+=length
        provenance.append({'source':region['source'].relative_to(source).as_posix(),'sourceSha256':hashlib.sha256(region['source'].read_bytes()).hexdigest(),
            'keys':[lo,hi],'velocity':[vl,vh],'root':root,'alternate':region['alternate'],'frames':length,'pcmSha256':hashlib.sha256(pcm).hexdigest()})
    bank=output/'sound/fetcher/banks/electric-di-v1.fsb';bank.parent.mkdir(parents=True,exist_ok=True)
    with bank.open('wb') as f:
        f.write(struct.pack('<8s4I',b'FSBANK1\0',len(records),frames,48000,0))
        for record in records:f.write(record)
        for pcm in chunks:f.write(pcm)
    assert bank.stat().st_size<64*1024*1024
    native=json.loads(subprocess.check_output([str(args.tool.resolve()),str(bank)],text=True))
    assert native['hash']=='969e921873764a7fb7a112d9574d3dfe','Bank bytes changed: audit conversion and update version/identity explicitly'
    report={'id':'electric-di-v1','creator':'FreePats project / FSBS Electric Guitar Direct','license':'CC0-1.0',
        'source':'https://freepats.zenvoid.org/ElectricGuitar/clean-electric-guitar.html','release':'2022-09-11',
        'archiveSha256':'5aedb789d108f85d2c76f0b74eae100439eac939a863f6fe870fee11cf5ab467',
        'sfzSha256':hashlib.sha256(sfz.read_bytes()).hexdigest(),'path':bank.relative_to(output).as_posix(),
        'sha256':hashlib.sha256(bank.read_bytes()).hexdigest(),'bytes':bank.stat().st_size,'native':native,
        'conversion':'mono 48 kHz PCM16; remove leading silence below -65 dB with 3 ms retained; cap at 6 s; 20 ms terminal fade; first two source alternates, round robin; no level normalization',
        'zones':provenance}
    (output/'bank-manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    license_file=next(source.rglob('LICENSE.txt'));licenses=output/'licenses';licenses.mkdir(exist_ok=True)
    (licenses/'freepats-electric-di-CC0.txt').write_bytes(license_file.read_bytes())
    (licenses/'freepats-electric-di-README.txt').write_bytes(next(source.rglob('README.txt')).read_bytes())
    print(json.dumps({'bank':str(bank),'bytes':bank.stat().st_size,**native}))

if __name__=='__main__':main()
