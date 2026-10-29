"""Curate pinned CC0 sources into bounded Fetcher v2 banks, not a general SFZ importer."""
from pathlib import Path
import argparse,array,collections,concurrent.futures,hashlib,json,re,struct,subprocess,sys,zipfile

def flat_map(text):
    """Read explicit SFZ tag scopes; includes/macros are never evaluated."""
    global_,master,group={},{},{}
    for tag,body in re.findall(r'<(global|master|group|region)>([^<]*)',re.sub(r'//[^\n]*','',text)):
        values=dict(re.findall(r'([a-z_0-9]+)\s*=\s*([^\s]+)',body))
        if tag=='global':global_,master,group=values,{},{}
        elif tag=='master':master,group=values,{}
        elif tag=='group':group=values
        elif 'sample' in values:yield {**global_,**master,**group,**values}

def spans(roots,low,high):
    return {root:(low if n==0 else (roots[n-1]+root)//2+1,
        high if n==len(roots)-1 else (root+roots[n+1])//2) for n,root in enumerate(roots)}

def main():
    p=argparse.ArgumentParser();p.add_argument('sources',type=Path);p.add_argument('tool',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
    base=a.sources.resolve();out=a.output.resolve();tool=a.tool.resolve();metadata=base/'metadata'
    acquired=json.loads((Path(__file__).parent/'sample-sources.json').read_text())['sources'];archives={row['asset']:row for row in acquired}
    specifications=[]
    def spec(id,archive,source,description,rows):specifications.append(dict(id=id,archive=archive,source=source,description=description,rows=rows))
    bass_archive='Black_And_Blue_Basses_1002.zip';bass_maps=metadata/'Black_And_Blue_Basses_1002/Programs/maps'
    for family,id,layers in [('darkblack','bass-finger-v1',[('mp',1,63),('f',64,127)]),('babyblue','bass-picked-v1',[('f',1,63),('ff',64,127)])]:
        parsed={layer:list(flat_map((bass_maps/f'{family}_reg_{layer}_map.sfz').read_text())) for layer,_,_ in layers}
        available=sorted(set(int(r.get('pitch_keycenter',60))-12 for r in parsed[layers[0][0]]))
        roots=available[::3];roots+=[] if roots[-1]==available[-1] else [available[-1]];bounds=spans(roots,available[0],available[-1]);rows=[]
        for layer,vl,vh in layers:
            for root in roots:
                variants={r['sample'].replace('\\','/').removeprefix('../'):r for r in parsed[layer] if int(r.get('pitch_keycenter',60))-12==root}
                for sample in sorted(variants,key=lambda s:int(re.search(r'_rr(\d+)',s)[1]))[:2]:
                    rows.append(dict(sample=sample,low=bounds[root][0],high=bounds[root][1],vl=vl,vh=vh,root=root,seconds=3.,flags=0,group=0,off=0))
        spec(id,bass_archive,'https://shop.karoryfer.com/pages/free-black-and-blue-basses',
            'Regular finger/pick plucks, two velocity layers/two real alternates, roots shifted down 12 to match recorded concert pitch; no release/ghost/keyswitch layers.',rows)
    for kind,high in [('nylon',83),('steel',84)]:
        archive=f'meta-open-sound-meta-{kind}-guitar-wav.zip';folder=metadata/Path(archive).stem
        regions=list(flat_map((folder/f'meta-{kind}-guitar.sfz').read_text()));maps={}
        for r in regions:
            if int(r.get('lochan',1))!=1 or int(r.get('hichan',1))!=1 or ('sw_last' in r and int(r['sw_last'])!=24):continue
            layer=int(re.search(r'_Vel(\d+)',r['sample'])[1]);maps[(int(r['key']),layer)]=r['sample']
        roots=list(range(40,high+1,3));roots+=[] if roots[-1]==high else [high];bounds=spans(roots,40,high);rows=[]
        velocities=[(1,42),(43,84),(85,127)] if kind=='nylon' else [(1,48),(49,96),(97,127)]
        for root in roots:
            for layer,(vl,vh) in enumerate(velocities,1):rows.append(dict(sample=maps[root,layer],low=bounds[root][0],high=bounds[root][1],vl=vl,vh=vh,root=root,seconds=3.5,flags=0,group=0,off=0))
        spec(f'guitar-{kind}-v1',archive,'https://github.com/facebookincubator/Meta-open-sound',
            'Channel-1 first-position string selection, three recorded velocity layers; hard layer splits, gated MIDI notes and keyboard polyphony. Stereo averaged to mono. No invented round robins or SFZ keyswitch/string choking.',rows)
    drums_archive='Big_Rusty_Drums_1100.zip';drums=metadata/'Big_Rusty_Drums_1100/Programs/mappings';rows=[]
    mappings=[([35,36],'kick_24/k_kick.sfz',1.2,0,0),([37],'snare_14/sn_ss_top.sfz',.8,0,0),
        ([38,40],'snare_14/sn_center_top.sfz',1.5,0,0),([41,43],'tom_18/tom_18_cl.sfz',1.8,0,0),
        ([45,47],'tom_15/tom_15_cl.sfz',1.5,0,0),([48,50],'tom_14/tom_14_cl.sfz',1.3,0,0),
        ([42],'hihat_14/ht_cl_cl.sfz',.6,0,1),([44],'hihat_14/ht_chik_cl.sfz',.8,0,1),
        ([46],'hihat_14/ht_open_cl.sfz',2.5,1,1),([49],'crash_17/cr_cl.sfz',5.,0,0),
        ([51],'ride_22/rd_cl.sfz',4.,0,0),([53],'ride_22/rd_bl_cl.sfz',3.,0,0),
        ([52],'china_18/cn_cl.sfz',4.,0,0),([57],'crash_sizzle_17/crs_cr_cl.sfz',4.,0,0),([59],'ride_sizzle_19/rds_cl.sfz',4.,0,0)]
    for keys,map_path,seconds,group,off in mappings:
        regions=list(flat_map((drums/map_path).read_text()));by_velocity=collections.defaultdict(dict)
        for r in regions:by_velocity[int(r.get('lovel',1)),int(r.get('hivel',127))][r['sample'].replace('\\','/').removeprefix('../')]=r
        for vl,vh,target in [(1,42,24),(43,84,72),(85,127,112)]:
            chosen=min(by_velocity,key=lambda span:abs((span[0]+span[1])/2-target))
            variants=sorted(by_velocity[chosen],key=lambda s:int(re.search(r'_rr(\d+)',s)[1]))[:2]
            for key in keys:
                for sample in variants:rows.append(dict(sample=sample,low=key,high=key,vl=vl,vh=vh,root=key,seconds=seconds,flags=1,group=group,off=off))
    spec('drum-kit-v1',drums_archive,'https://shop.karoryfer.com/pages/free-big-rusty-drums',
        'Three selected velocity layers/two recorded alternates; close mics only, snare top mic. GM aliases share recordings. Open hats choke within/across lanes; closed/pedal hats stop open hats. One-shot tails ignore note gates.',rows)
    out.mkdir(parents=True,exist_ok=True);(out/'manifests').mkdir(exist_ok=True);(out/'licenses').mkdir(exist_ok=True)
    reports=[]
    for spec_ in specifications:
        archive=base/spec_['archive'];expected=archives[spec_['archive']]['sha256']
        assert hashlib.sha256(archive.read_bytes()).hexdigest()==expected,'Source archive identity changed'
        with zipfile.ZipFile(archive) as z:
            all_names=set(z.namelist());rows=spec_['rows'];groups=collections.defaultdict(list)
            for row in rows:assert row['sample'] in all_names,row['sample'];groups[(row['low'],row['high'],row['vl'],row['vh'])].append(row)
            unique={(row['sample'],row['seconds']) for row in rows}
            sources={sample:z.read(sample) for sample,_ in sorted(unique)}
            def convert(key):
                sample,seconds=key
                command=['ffmpeg','-v','error','-i','pipe:0','-ar','48000','-ac','1','-f','s16le','-']
                source=sources[sample];decoded=array.array('h',subprocess.check_output(command,input=source))
                if sys.byteorder!='little':decoded.byteswap()
                onset=next((n for n,v in enumerate(decoded) if abs(v)>=19),None)
                assert onset is not None,('Silent source',sample)
                start=max(0,onset-144);decoded=decoded[start:start+int(seconds*48000)]
                for n in range(max(0,len(decoded)-960),len(decoded)):decoded[n]=round(decoded[n]*min(1,(len(decoded)-1-n)/960))
                if sys.byteorder!='little':decoded.byteswap()
                pcm=decoded.tobytes()
                assert 4<=len(pcm)<=int(seconds*48000)*2,(sample,len(pcm),seconds)
                return key,(pcm,hashlib.sha256(source).hexdigest())
            with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:converted=dict(pool.map(convert,sorted(unique)))
            records=[];chunks=[];positions={};frame=0;provenance=[]
            for key in sorted(converted):
                pcm,_=converted[key];positions[key]=(frame,len(pcm)//2);chunks.append(pcm);frame+=len(pcm)//2
            for key,variants in sorted(groups.items()):
                for alternate,row in enumerate(variants):
                    start,length=positions[row['sample'],row['seconds']]
                    records.append(struct.pack('<12BII',row['low'],row['high'],row['vl'],row['vh'],row['root'],alternate,len(variants),row['flags'],row['group'],row['off'],0,0,start,length))
                    provenance.append({**row,'sourceSha256':converted[row['sample'],row['seconds']][1],'alternate':alternate,'frames':length})
            bank=out/f"sound/fetcher/banks/{spec_['id']}.fsb";bank.parent.mkdir(parents=True,exist_ok=True)
            with bank.open('wb') as f:
                f.write(struct.pack('<8s4I',b'FSBANK2\0',len(records),frame,48000,0))
                for record in records:f.write(record)
                for pcm in chunks:f.write(pcm)
            assert bank.stat().st_size<64*1024*1024
            native=json.loads(subprocess.check_output([str(tool),str(bank)],text=True))
            report=dict(id=spec_['id'],source=spec_['source'],archive=spec_['archive'],archiveSha256=expected,
                license='CC0-1.0',description=spec_['description'],path=bank.relative_to(out).as_posix(),bytes=bank.stat().st_size,
                sha256=hashlib.sha256(bank.read_bytes()).hexdigest(),native=native,conversion='Mono 48-kHz PCM16, 3-ms onset preroll at -65 dB threshold, per-family tail caps and 20-ms cap fades. Original layer levels retained.',zones=provenance)
            (out/f"manifests/{spec_['id']}.json").write_text(json.dumps(report,indent=2)+'\n');reports.append(report)
            license_path=next(name for name in z.namelist() if Path(name).name.lower()=='license')
            (out/f"licenses/{spec_['id']}-CC0.txt").write_bytes(z.read(license_path))
            print(json.dumps({'id':spec_['id'],**native,'bytes':bank.stat().st_size}),flush=True)
    original=json.loads((out/'bank-manifest.json').read_text())
    original.setdefault('description','FreePats electric DI: two real alternates and original velocity layers, six-second tails.')
    total=original['native']['pcmBytes']+sum(r['native']['pcmBytes'] for r in reports)
    assert total<240*1024*1024,'Re-curate before exceeding the shared bank budget'
    catalog={'license':'CC0-1.0','nativeApi':19,'decodedBytesAllBanks':total,'banks':[{k:r[k] for k in ['id','path','bytes','sha256','native','source','description']} for r in [original,*reports]]}
    (out/'catalog.json').write_text(json.dumps(catalog,indent=2)+'\n');print(json.dumps({'allBanksDecodedBytes':total,'banks':len(catalog['banks'])}))

if __name__=='__main__':main()
