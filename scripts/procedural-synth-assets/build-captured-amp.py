"""Compile the pinned, reviewed MIT capture into fixed native inference weights.

No remote models or runtime model parser. Re-run after an explicitly audited
capture change; the source hash is deliberately checked before any output.
"""
from pathlib import Path
import json, hashlib
root=Path(__file__).resolve().parents[2]
source=root/'files/server/optional-assets/fetcher.studio-amp-models/riffsmith-distorted.nam'
assert hashlib.sha256(source.read_bytes()).hexdigest()=='ec52e16296f96fdd1d07d1aaa699e33e17df253f026f785f135080e1eb5db007'
r=root
model=json.loads(source.read_text())['config']['submodels'][1]['model'];weights=model['weights'];cfg=model['config']['layers'][0];it=iter(weights);take=lambda:float(next(it));flat=[take() for _ in range(8)];offsets=[]
for k in cfg['kernel_sizes']:
 offsets.append(len(flat));w=[[[take() for tap in range(k)] for j in range(8)] for i in range(8)]
 flat += [w[i][j][tap] for tap in range(k) for j in range(8) for i in range(8)]
 flat += [take() for _ in range(16)]
 w=[[take() for j in range(8)] for i in range(8)];flat += [w[i][j] for j in range(8) for i in range(8)]
 flat += [take() for _ in range(8)]
head=len(flat);w=[[take() for k in range(16)] for j in range(8)];flat += [w[j][k] for k in range(16) for j in range(8)];flat += [take(),take()]
assert len(flat)==12146
try:next(it);raise AssertionError('trailing weights')
except StopIteration:pass
text='''// Generated from the pinned MIT-licensed RiffSmith Distorted full capture.
// Matt Hartman, 2026. See files/server/optional-assets/fetcher.studio-amp-models.
// Do not hand edit: scripts/procedural-synth-assets/build-captured-amp.py.
#ifndef OPENMW_MWSOUND_CAPTUREDAMPDATA_H
#define OPENMW_MWSOUND_CAPTUREDAMPDATA_H
#include <array>
namespace MWSound::CapturedAmpData
{
'''
text+='inline constexpr std::array<unsigned,23> Kernels{'+','.join(map(str,cfg['kernel_sizes']))+'};\n'
text+='inline constexpr std::array<unsigned,23> Dilations{'+','.join(map(str,cfg['dilations']))+'};\n'
text+='inline constexpr std::array<unsigned,23> Offsets{'+','.join(map(str,offsets))+'};\n'
text+=f'inline constexpr unsigned HeadOffset={head};\ninline constexpr std::array<float,{len(flat)}> Weights{{\n'
for i in range(0,len(flat),8):text+='    '+', '.join(format(v,'.9e')+'f' for v in flat[i:i+8])+',\n'
text+='};\n}\n#endif\n';(r/'apps/openmw/mwsound/capturedampdata.hpp').write_text(text)
print('generated',len(text),'chars')
