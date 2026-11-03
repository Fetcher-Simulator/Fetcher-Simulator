"""End-to-end renderer configuration checks; uses the built native executable."""
from pathlib import Path
import array
import json
import math
import struct
import subprocess
import sys
import tempfile
import wave

root = Path(__file__).resolve().parents[2]
exe = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'build/synth-core-tests/Release/score-audition.exe'
with tempfile.TemporaryDirectory(prefix='fetcher-audition-') as tmp:
    folder = Path(tmp)
    score = folder / 'note.bin'
    score.write_bytes(struct.pack('<IIBBB', 0, 6000, 1, 60, 100))

    def render(extra='', gate='1', valid=True, master=.5):
        config = folder / 'config.tsv'
        config.write_text(f'length=48000\nmaster={master}\neffects.reverbEnabled=0\neffects.delayEnabled=0\n'
                          f'@1\t1\tguitar\t{gate}\nattack=.002\ndecay=.002\nsustain=1\nrelease=.005\nvolume=.2\n' + extra)
        output = folder / 'output.wav'
        run = subprocess.run([str(exe), str(score), str(config), str(folder), str(output), '0', '1', 'all'],
                             capture_output=True, text=True)
        if not valid:
            assert run.returncode != 0, 'Malformed configuration must reject'
            return
        assert run.returncode == 0, run.stderr
        info = json.loads(run.stdout)
        assert info['strikes'] == 1 and 0 <= info['peak'] <= 1
        with wave.open(str(output), 'rb') as wav:
            assert wav.getframerate() == 48000 and wav.getnchannels() == 1 and wav.getnframes() == 48000
            samples = array.array('h', wav.readframes(wav.getnframes()))
        return info, samples

    dry, _ = render()
    louder, _ = render(master=1)
    assert 1.98 < louder['rms'] / dry['rms'] < 2.01, 'Renderer must apply the saved master'
    muted, _ = render(gate='0')
    assert muted['peak'] == 0, 'Renderer must apply separate audibility gate'
    compressed, _ = render('mix.compEnabled=1\nmix.threshold=-60\nmix.ratio=20\nmix.attack=.1\n')
    assert compressed['rms'] < dry['rms'] * .6, f'Renderer must apply channel-strip state: dry={dry}, compressed={compressed}'
    wet, pcm = render('effects.delayEnabled=1\neffects.delayTime=40\neffects.delayReturn=1\nmix.delaySend=1\n')
    assert math.fsum(v*v for v in pcm[12000:24000]) > 0, 'Renderer must route the authored send into its return'
    render('mix.notAControl=1\n', valid=False)
    render('mix.delaySend=2\n', valid=False)
    render('effects.notAControl=1\n', valid=False)
    render(gate='2', valid=False)
    print('PASS native audition master, channel strip, sends/returns, audibility, exact clock and malformed-config rejection')
