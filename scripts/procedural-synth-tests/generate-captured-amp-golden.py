"""Regenerate original synthetic-input checkpoints with the pinned NAM renderer.

Usage: python generate-captured-amp-golden.py /path/to/NAM/render.exe output-dir
The reference core revision and model identity are in amp-model provenance.json.
This developer utility does not run at build time or in the game.
"""
from pathlib import Path
import hashlib
import math
import struct
import subprocess
import sys

root=Path(__file__).resolve().parents[2]
model=root/'files/server/optional-assets/fetcher.studio-amp-models/riffsmith-distorted.nam'
assert hashlib.sha256(model.read_bytes()).hexdigest()=='ec52e16296f96fdd1d07d1aaa699e33e17df253f026f785f135080e1eb5db007'
out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
count=16384;state=12345;pcm=[]
for n in range(count):
    state=(state*1664525+1013904223)&0xffffffff
    x=((state>>16)/65535-.5)*.04+.06*math.sin(n*.071)+.02*math.sin(n*.193)
    if n<64 or 4096<=n<6144 or n>=12300:x=0
    if 8192<=n<12288:x*=3
    pcm.append(x)
data=struct.pack('<'+str(count)+'f',*pcm)
header=b'RIFF'+struct.pack('<I',36+len(data))+b'WAVEfmt '+struct.pack('<IHHIIHH',16,3,1,48000,192000,4,32)+b'data'+struct.pack('<I',len(data))
(out/'input.wav').write_bytes(header+data)
run=subprocess.run([sys.argv[1],'--slim','1',str(model),str(out/'input.wav'),str(out/'output.wav')],capture_output=True,check=True)
data=(out/'output.wav').read_bytes();assert data[:4]==b'RIFF' and data[36:40]==b'data'
values=struct.unpack('<'+str(count)+'f',data[44:])
indices=sorted(set(range(128))|set(range(128,count,97))|{count-1})
text='// Original synthetic stimulus; NAM Core reference renderer, pinned in amp provenance.\n'
text+=f'static constexpr std::array<std::pair<unsigned,float>,{len(indices)}> CaptureGolden{{{{\n'
for i in indices:text+='    {'+str(i)+','+format(values[i],'.9e')+'f},\n'
text+='}};\n'
(Path(__file__).parent/'captured-amp-golden.hpp').write_text(text)
print('Generated',len(indices),'original-signal reference checkpoints')
