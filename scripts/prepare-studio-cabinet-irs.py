"""Reproduce embedded CC0 cabinet kernels from the pinned original archive.
Requires numpy/scipy; no network access or generated audio is needed at runtime.
Run from any directory; optional first argument is the original archive path.
"""
from pathlib import Path
import numpy as np,json,hashlib,shutil,sys,zipfile,tempfile
from scipy.io import wavfile
archive=Path(sys.argv[1] if len(sys.argv)>1 else 'build/studio-cabinet-ir/JestersBrutalPack_1.0.zip')
assert hashlib.sha256(archive.read_bytes()).hexdigest()=='299dc053f01ebd1e980459adc48f9c6b8a8c7af91917b4f946512eefdbb311ea','Unexpected source archive'
workspace=Path(__file__).resolve().parent.parent
temp=tempfile.TemporaryDirectory();root=Path(temp.name)/'Jesters_Brutal_Pack_1.0'
with zipfile.ZipFile(archive) as z:
 for name in ['1_Cookie_Monster.wav','2_Darth_Genocider.wav','3_Kitten_Slayer.wav']:
  target=root/'Impulses/48kHz'/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(z.read('Jesters_Brutal_Pack_1.0/Impulses/48kHz/'+name))
 (root/'Jesters Brutal IR Pack Handbook.pdf').write_bytes(z.read('Jesters_Brutal_Pack_1.0/Jesters Brutal IR Pack Handbook.pdf'))
out=workspace/'files/server/optional-assets/fetcher.studio-cabinet-irs';out.mkdir(parents=True,exist_ok=True)
items=[('v30-sm57','1_Cookie_Monster.wav','Vintage 30 / SM57'),('dv77-sm57','2_Darth_Genocider.wav','DV-77 / SM57'),('rockdriver-e606','3_Kitten_Slayer.wav','Rockdriver Jr / e606')]
arrays=[];records=[]
for key,name,label in items:
 p=root/'Impulses/48kHz'/name;rate,pcm=wavfile.read(p);assert rate==48000 and pcm.ndim==1
 x=pcm.astype(np.float64)/(2**31 if pcm.dtype==np.int32 else 32768);assert np.isfinite(x).all()
 start=int(np.flatnonzero(np.abs(x)>np.max(np.abs(x))*1e-4)[0]);x=x[start:]
 h=x[:2048].copy();h[-128:]*=np.linspace(1,0,128);freq=np.fft.rfftfreq(16384,1/rate);response=np.abs(np.fft.rfft(h,16384));band=response[(freq>=120)&(freq<=6000)];gain=1/np.sqrt(np.mean(band**2));h*=gain
 arrays.append(h.astype(np.float32));shutil.copy2(p,out/(key+'.wav'))
 records.append({'id':key,'label':label,'sourceFile':name,'sourceSha256':hashlib.sha256(p.read_bytes()).hexdigest(),'sampleRate':rate,'onsetRemovedFrames':start,'embeddedFrames':2048,'tailFadeFrames':128,'gain':float(gain),'retainedEnergyFraction':float(np.sum(x[:2048]**2)/np.sum(x**2))})
text='#ifndef OPENMW_MWSOUND_CABINET_IR_DATA_H\n#define OPENMW_MWSOUND_CABINET_IR_DATA_H\n#include <array>\nnamespace MWSound::CabinetIRData {\n// Jester Dyne / Bastian Karschewski, CC0. See fetcher.studio-cabinet-irs credits.\ninline constexpr std::array<std::array<float,2048>,3> Responses{{\n'
for h in arrays:
 text+='{{\n'+''.join('    '+','.join(format(float(v),'.9e')+'f' for v in h[i:i+8])+',\n' for i in range(0,len(h),8))+'}},\n'
text+='}};\n}\n#endif\n';(workspace/'apps/openmw/mwsound/cabinetirdata.hpp').write_text(text)
manifest={'author':'Bastian Karschewski / Jester Dyne Productions','license':'CC0-1.0','source':'https://www.jester-dyne-productions.com/brutal-ir-pack/','archiveSha256':'299dc053f01ebd1e980459adc48f9c6b8a8c7af91917b4f946512eefdbb311ea','responses':records};(out/'catalog.json').write_text(json.dumps(manifest,indent=2))
shutil.copy2(root/'Jesters Brutal IR Pack Handbook.pdf',out/'Original-Handbook.pdf')
print(json.dumps(records,indent=2))
