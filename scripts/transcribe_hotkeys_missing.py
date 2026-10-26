#!/usr/bin/env python3
"""Optional offline caption fallback. Audio stays local; results need review.

Requires faster-whisper, numpy and FFmpeg. Run in a developer environment, not
the game/server. The importer excludes failed/uncertain and isolated-word output.
"""
from __future__ import annotations
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import re
import subprocess
import time

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-data',type=Path,required=True)
    parser.add_argument('--candidates',type=Path,required=True,help='JSON array of sound/... VFS paths')
    parser.add_argument('--output',type=Path,required=True,help='Append/resume JSONL results')
    parser.add_argument('--model',default='small.en')
    parser.add_argument('--model-cache',type=Path,required=True)
    parser.add_argument('--ffmpeg',default='ffmpeg')
    parser.add_argument('--workers',type=int,default=4)
    parser.add_argument('--threads',type=int,default=4)
    args=parser.parse_args()
    if not 1<=args.workers<=16 or not 1<=args.threads<=32:parser.error('Invalid worker/thread count')
    os.environ['HF_HUB_DISABLE_XET']='1'
    os.environ['TOKENIZERS_PARALLELISM']='false'
    import numpy as np
    from faster_whisper import WhisperModel
    from import_hotkeys_voice_transcripts import audio_path
    done={}
    if args.output.exists():
        for line in args.output.read_text(encoding='utf-8').splitlines():
            entry=json.loads(line)
            if entry['status']!='error':done[entry['path']]=entry
    paths=[]
    for raw in json.loads(args.candidates.read_text(encoding='utf-8')):
        path=audio_path(raw)
        if not path:raise ValueError('Unsafe candidate audio path')
        target=(args.game_data/path).resolve()
        if not target.is_relative_to(args.game_data.resolve()):raise ValueError('Candidate escapes game data')
        if path not in done:paths.append(path)
    model=WhisperModel(args.model,device='cpu',compute_type='int8',cpu_threads=args.threads,
        num_workers=args.workers,download_root=str(args.model_cache))
    def transcribe(path):
        record={'path':path,'model':'faster-whisper-'+args.model,'status':'missing'}
        if not (args.game_data/path).is_file():return record
        try:
            # Explicit PCM input avoids coupling to PyAV metadata API changes.
            decoded=subprocess.run([args.ffmpeg,'-v','error','-i',str(args.game_data/path),
                '-f','f32le','-ar','16000','-ac','1','pipe:1'],capture_output=True,check=True)
            audio=np.frombuffer(decoded.stdout,dtype=np.float32)
            stream,info=model.transcribe(audio,language='en',beam_size=3,temperature=0,vad_filter=True,
                vad_parameters={'min_silence_duration_ms':300},condition_on_previous_text=False)
            segments=list(stream);text=' '.join(s.text.strip() for s in segments).strip()
            logprob=sum(s.avg_logprob for s in segments)/max(1,len(segments))
            no_speech=max((s.no_speech_prob for s in segments),default=1)
            compression=max((s.compression_ratio for s in segments),default=0)
            acceptable=bool(re.search('[a-zA-Z]',text)) and logprob>=-1 and no_speech<.6 and compression<2.4
            if any(v in text.lower() for v in ['thanks for watching','thank you for watching','subtitles by','amara.org']):acceptable=False
            record.update(text=text,status='automatic' if acceptable else 'needs-review',avgLogprob=round(logprob,4),
                noSpeech=round(no_speech,4),duration=round(info.duration,3))
        except Exception as error:record.update(status='error',error=str(error))
        return record
    args.output.parent.mkdir(parents=True,exist_ok=True)
    start=time.monotonic()
    with args.output.open('a',encoding='utf-8') as output,concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        for i,record in enumerate(pool.map(transcribe,paths),1):
            output.write(json.dumps(record,ensure_ascii=False)+'\n');output.flush();done[record['path']]=record
            if i%20==0 or i==len(paths):
                counts={s:sum(r['status']==s for r in done.values()) for s in ['automatic','needs-review','missing','error']}
                print(json.dumps({'completed':len(done),'elapsedSeconds':round(time.monotonic()-start),'counts':counts}),flush=True)

if __name__=='__main__':main()
