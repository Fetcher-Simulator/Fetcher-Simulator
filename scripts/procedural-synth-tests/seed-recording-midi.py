"""Generate two disposable selection-regression phrases in an isolated fixture."""
import sys
from pathlib import Path

fixture = Path(sys.argv[1]).resolve()
if fixture.name != 'procedural-synth':
    raise ValueError('Expected the isolated procedural-synth fixture')
directory = fixture / 'midi-data' / 'midi' / 'qa'
directory.mkdir(parents=True, exist_ok=True)
for name, pitch in [('recording-a.mid', 60), ('recording-b.mid', 72)]:
    track = b''.join(bytes([0, 144, pitch, 100, 96, 128, pitch, 0]) for _ in range(8)) + bytes([0, 255, 47, 0])
    data = b'MThd' + (6).to_bytes(4, 'big') + bytes([0, 0, 0, 1, 0, 96]) + b'MTrk' + len(track).to_bytes(4, 'big') + track
    path = directory / name
    if path.exists() and path.read_bytes() != data:
        raise ValueError(f'Refusing to replace a different MIDI file: {path}')
    path.write_bytes(data)
